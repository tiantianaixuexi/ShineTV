#include "gallery/ThumbnailService.h"

#include "core/Async.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "gallery/Image.h"
#include "gallery/ImageLoader.h"
#include "gallery/Resize.h"
#include "gallery/cache/CpuThumbCache.h"
#include "gallery/cache/DiskThumbCache.h"
#include "gpu/GpuDevice.h"
#include "gpu/GpuTextureCache.h"
#include "gpu/GpuTextureManager.h"
#include "util/Encoding.h"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace shine::gallery {
namespace {

constexpr int kMaxConcurrent = 4;
constexpr std::size_t kMaxQueue = 4096;
// 与媒体预览的 TextureCache key 隔离（媒体用原始 hash，图库用 id | 高位）
constexpr std::uint64_t kGalleryGpuKeyBit = 1ull << 63;

[[nodiscard]] constexpr std::uint64_t GpuKey(ImageId id) noexcept { return id | kGalleryGpuKeyBit; }

[[nodiscard]] double MsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

[[nodiscard]] std::size_t BudgetBytes(int mb, std::size_t fallbackMb) noexcept {
    const auto v = static_cast<std::size_t>(std::max(mb, static_cast<int>(fallbackMb / 4)));
    return v << 20;
}

struct ThumbEntry {
    ImageId id = 0;
    std::filesystem::path path;
    std::uint32_t targetSize = 0;
    int priority = static_cast<int>(ThumbPriority::Normal);
    LoadState state = LoadState::NotLoaded;
    int retries = 0;
};

struct ThumbCompleted {
    ImageId id = 0;
    Image thumb;
    DecodeError error = DecodeError::DecodeFailed;
    bool ok = false;
    bool fromDisk = false; // G-S14
    std::string name;
    std::uint32_t srcW = 0;
    std::uint32_t srcH = 0;
    std::uint32_t dstSide = 0;
    std::uint32_t targetSize = 0;
    double decodeMs = 0.0;
    double resizeMs = 0.0;
};

} // namespace

struct ThumbnailService::Impl {
    std::unordered_map<ImageId, ThumbEntry> entries;
    std::vector<ImageId> queue;
    std::vector<ThumbCompleted> completed;
    int active = 0;
    bool shutdown = false;
    std::size_t dropped = 0;
    std::size_t workerStarts = 0; // G-S9：派工次数（验收用）
    std::size_t requestCalls = 0; // G-S9：Request 调用次数
};

ThumbnailService::Impl& ThumbnailService::Get() noexcept {
    static Impl impl;
    return impl;
}

ThumbnailService& ThumbnailService::Instance() noexcept {
    static ThumbnailService service;
    return service;
}

ThumbnailService& Thumbs() noexcept { return ThumbnailService::Instance(); }

void ThumbnailService::ApplyBudgetsFromSettings() {
    CpuThumbCache::Instance().SetBudget(BudgetBytes(Settings().galleryCpuBudgetMB, 256));
    gpu::TextureCache().SetBudget(BudgetBytes(Settings().galleryGpuBudgetMB, 512));
}

void ThumbnailService::Init() {
    Impl& g = Get();
    g.shutdown = false;
    g.dropped = 0;
    ApplyBudgetsFromSettings();
    log::Info("图库：缩略图管线就绪（并发 {}，队列上限 {}；CPU {:.0f}MB / GPU {:.0f}MB）", kMaxConcurrent, kMaxQueue,
              static_cast<double>(CpuThumbCache::Instance().Budget()) / (1024.0 * 1024.0),
              static_cast<double>(gpu::TextureCache().Budget()) / (1024.0 * 1024.0));
}

void ThumbnailService::Shutdown() {
    Impl& g = Get();
    g.shutdown = true;
    // App 已先 ReleaseAll + TextureCache.Clear：这里只清映射
    g.entries.clear();
    g.queue.clear();
    g.completed.clear();
}

void ThumbnailService::Reset() {
    Impl& g = Get();
    for (const auto& [id, e] : g.entries) {
        gpu::TextureCache().Erase(GpuKey(id));
    }
    // CPU 缓存保留（换尺寸档时 targetSize 不同自然 miss；换目录由调用方 Clear）
    g.entries.clear();
    g.queue.clear();
    g.completed.clear();
}

bool ThumbnailService::TryServeFromCache(ImageId id, std::uint32_t targetSize) {
    if (id == 0 || targetSize == 0 || !gpu::Ready()) {
        return false;
    }
    // ① GPU 命中（刷新 LRU）
    if (const gpu::GpuTextureHandle hit = gpu::TextureCache().Find(GpuKey(id)); hit) {
        return true;
    }
    // ② CPU 命中 → UI 线程立刻重传
    if (const Image* img = CpuThumbCache::Instance().Find(id, targetSize); img != nullptr) {
        auto up = gpu::Textures().Upload(img->width, img->height,
                                         std::span<const std::byte>(img->data, img->bytes));
        if (up) {
            gpu::TextureCache().Insert(GpuKey(id), *up);
            return true;
        }
        log::Warn("图库：CPU 缓存重传 GPU 失败 id={} —— {}", id, gpu::GpuErrorText(up.error()));
    }
    return false;
}

void ThumbnailService::EnqueueWithCap(ImageId id, int priority) {
    Impl& g = Get();
    if (g.queue.size() < kMaxQueue) {
        g.queue.push_back(id);
        return;
    }
    std::size_t dropIdx = g.queue.size();
    int dropPri = priority + 1;
    for (std::size_t i = 0; i < g.queue.size(); ++i) {
        const auto it = g.entries.find(g.queue[i]);
        if (it == g.entries.end()) {
            dropIdx = i;
            break;
        }
        if (it->second.priority <= priority && it->second.priority < dropPri) {
            dropIdx = i;
            dropPri = it->second.priority;
            if (dropPri == priority) {
                break;
            }
        }
    }
    if (dropIdx >= g.queue.size()) {
        if (auto it = g.entries.find(id); it != g.entries.end()) {
            it->second.state = LoadState::NotLoaded;
        }
        ++g.dropped;
        if (g.dropped <= 16 || g.dropped % 256 == 0) {
            log::Warn("图库缩略图队列已满（{}），新请求被丢弃 id={}（累计丢弃 {}）", kMaxQueue, id, g.dropped);
        }
        return;
    }
    const ImageId droppedId = g.queue[dropIdx];
    if (auto it = g.entries.find(droppedId); it != g.entries.end() && it->second.state == LoadState::Queued) {
        it->second.state = LoadState::NotLoaded;
    }
    g.queue.erase(g.queue.begin() + static_cast<std::ptrdiff_t>(dropIdx));
    g.queue.push_back(id);
    ++g.dropped;
    if (g.dropped <= 16 || g.dropped % 256 == 0) {
        log::Warn("图库缩略图队列已满（{}），丢弃低优先级任务 id={}（累计丢弃 {}）", kMaxQueue, droppedId, g.dropped);
    }
}

void ThumbnailService::Pump() {
    Impl& g = Get();
    if (g.shutdown) {
        return;
    }
    while (g.active < kMaxConcurrent && !g.queue.empty()) {
        std::size_t bestIdx = g.queue.size();
        int bestPri = -1;
        for (std::size_t i = 0; i < g.queue.size(); ++i) {
            const auto it = g.entries.find(g.queue[i]);
            if (it == g.entries.end()) {
                continue;
            }
            const LoadState st = it->second.state;
            if (st == LoadState::Ready || st == LoadState::Loading || st == LoadState::Failed) {
                continue;
            }
            if (it->second.priority > bestPri) {
                bestPri = it->second.priority;
                bestIdx = i;
            }
        }
        if (bestIdx >= g.queue.size()) {
            g.queue.clear();
            return;
        }
        const ImageId id = g.queue[bestIdx];
        g.queue.erase(g.queue.begin() + static_cast<std::ptrdiff_t>(bestIdx));

        const auto it = g.entries.find(id);
        if (it == g.entries.end()) {
            continue;
        }
        if (it->second.state == LoadState::Ready || it->second.state == LoadState::Loading) {
            continue;
        }

        // 派工前再查一次缓存（滚动期间可能刚被别的路径填上）
        if (TryServeFromCache(id, it->second.targetSize)) {
            it->second.state = LoadState::Ready;
            continue;
        }

        ThumbEntry& e = it->second;
        e.state = LoadState::Loading;
        ++g.active;
        ++g.workerStarts;
        if (g.workerStarts <= 8 || g.workerStarts % 64 == 0) {
            log::Info("thumb worker start #{} id={} {}", g.workerStarts, id, util::FileNameToUtf8(e.path));
        }

        const ImageId idCopy = id;
        const std::filesystem::path path = e.path;
        const std::uint32_t target = e.targetSize;

        async::RunOnWorker([idCopy, path, target]() {
            ThumbCompleted done;
            done.id = idCopy;
            done.name = util::FileNameToUtf8(path);
            done.targetSize = target;

            // G-S14：GPU → CPU 之后，先查磁盘，再解码
            if (auto disk = disk_cache::Load(path, target); disk && disk->valid()) {
                done.ok = true;
                done.thumb = std::move(*disk);
                done.srcW = done.thumb.width;
                done.srcH = done.thumb.height;
                done.dstSide = std::max(done.thumb.width, done.thumb.height);
                done.fromDisk = true;
                done.decodeMs = 0;
                done.resizeMs = 0;
                async::PostToUi([done = std::move(done)]() mutable {
                    Impl& g2 = ThumbnailService::Get();
                    --g2.active;
                    if (g2.shutdown) {
                        return;
                    }
                    g2.completed.push_back(std::move(done));
                });
                return;
            }

            const auto t0 = std::chrono::steady_clock::now();
            auto decoded = ImageLoader::Instance().Load(path);
            const auto t1 = std::chrono::steady_clock::now();
            done.decodeMs = MsSince(t0);

            if (!decoded) {
                done.ok = false;
                done.error = decoded.error();
            } else if (!decoded->valid()) {
                done.ok = false;
                done.error = DecodeError::DecodeFailed;
            } else {
                done.srcW = decoded->width;
                done.srcH = decoded->height;
                auto thumb = ResizeBox(*decoded, target);
                done.resizeMs = MsSince(t1);
                if (!thumb.valid()) {
                    done.ok = false;
                    done.error = DecodeError::DecodeFailed;
                } else {
                    done.ok = true;
                    done.dstSide = std::max(thumb.width, thumb.height);
                    // worker 侧写盘，UI 不等待
                    if (!disk_cache::Store(path, target, thumb)) {
                        log::Warn("磁盘缩略图写失败（不影响显示）：{}", done.name);
                    }
                    done.thumb = std::move(thumb);
                }
            }

            async::PostToUi([done = std::move(done)]() mutable {
                Impl& g2 = ThumbnailService::Get();
                --g2.active;
                if (g2.shutdown) {
                    return;
                }
                g2.completed.push_back(std::move(done));
            });
        });
    }
}

void ThumbnailService::Request(ImageId id, std::filesystem::path path, std::uint32_t targetSize,
                               ThumbPriority priority) {
    Impl& g = Get();
    if (g.shutdown || id == 0 || targetSize == 0) {
        return;
    }
    ++g.requestCalls;
    const int pri = static_cast<int>(priority);

    // Ready：确认 GPU 仍在；被 LRU/设备丢失清掉则回源
    const auto it = g.entries.find(id);
    if (it != g.entries.end()) {
        ThumbEntry& e = it->second;
        if (e.targetSize != 0 && e.targetSize != targetSize) {
            // 换尺寸档
            gpu::TextureCache().Erase(GpuKey(id));
            e.state = LoadState::NotLoaded;
            e.targetSize = targetSize;
            e.retries = 0;
        }
        switch (e.state) {
        case LoadState::Ready:
            if (gpu::TextureCache().Find(GpuKey(id))) {
                return;
            }
            if (TryServeFromCache(id, e.targetSize != 0 ? e.targetSize : targetSize)) {
                return;
            }
            e.state = LoadState::NotLoaded;
            e.path = std::move(path);
            e.targetSize = targetSize;
            e.priority = pri;
            EnqueueWithCap(id, e.priority);
            Pump();
            return;
        case LoadState::Loading:
            return;
        case LoadState::Queued:
            if (pri > e.priority) {
                e.priority = pri;
            }
            return;
        case LoadState::Failed:
            return;
        case LoadState::NotLoaded:
            e.path = std::move(path);
            e.targetSize = targetSize;
            e.priority = std::max(e.priority, pri);
            if (TryServeFromCache(id, targetSize)) {
                e.state = LoadState::Ready;
                return;
            }
            e.state = LoadState::Queued;
            EnqueueWithCap(id, e.priority);
            Pump();
            return;
        }
        return;
    }

    ThumbEntry e;
    e.id = id;
    e.path = std::move(path);
    e.targetSize = targetSize;
    e.priority = pri;
    if (TryServeFromCache(id, targetSize)) {
        e.state = LoadState::Ready;
        g.entries.emplace(id, std::move(e));
        return;
    }
    e.state = LoadState::Queued;
    g.entries.emplace(id, std::move(e));
    EnqueueWithCap(id, pri);
    Pump();
}

void ThumbnailService::Demote(ImageId id) {
    Impl& g = Get();
    const auto it = g.entries.find(id);
    if (it == g.entries.end()) {
        return;
    }
    if (it->second.state == LoadState::Queued) {
        it->second.priority = static_cast<int>(ThumbPriority::Low);
    }
}

void ThumbnailService::Retry(ImageId id) {
    Impl& g = Get();
    if (g.shutdown) {
        return;
    }
    const auto it = g.entries.find(id);
    if (it == g.entries.end()) {
        return;
    }
    if (it->second.state != LoadState::Failed && it->second.state != LoadState::NotLoaded) {
        return;
    }
    ThumbEntry& e = it->second;
    e.retries = 0;
    e.state = LoadState::NotLoaded;
    e.priority = static_cast<int>(ThumbPriority::High);
    if (TryServeFromCache(id, e.targetSize)) {
        e.state = LoadState::Ready;
        return;
    }
    e.state = LoadState::Queued;
    if (std::ranges::find(g.queue, id) == g.queue.end()) {
        EnqueueWithCap(id, e.priority);
    }
    Pump();
}

void ThumbnailService::DrainCompleted() {
    Impl& g = Get();
    ApplyBudgetsFromSettings();

    if (!g.completed.empty()) {
        std::vector<ThumbCompleted> local;
        local.swap(g.completed);

        for (ThumbCompleted& done : local) {
            const auto it = g.entries.find(done.id);
            if (it == g.entries.end()) {
                continue;
            }
            ThumbEntry& e = it->second;

            if (!done.ok) {
                if (e.retries < 1) {
                    ++e.retries;
                    e.state = LoadState::NotLoaded;
                    log::Warn("图库缩略图失败（将重试）：{} —— {}", done.name, DecodeErrorText(done.error));
                    if (std::ranges::find(g.queue, done.id) == g.queue.end()) {
                        EnqueueWithCap(done.id, e.priority);
                    }
                    continue;
                }
                e.state = LoadState::Failed;
                log::Warn("图库缩略图失败：{} —— {}", done.name, DecodeErrorText(done.error));
                continue;
            }

            if (!gpu::Ready()) {
                e.state = LoadState::Failed;
                log::Warn("图库：GPU 未就绪，无法上传缩略图：{}", done.name);
                continue;
            }

            auto handle = gpu::Textures().Upload(done.thumb.width, done.thumb.height,
                                                 std::span<const std::byte>(done.thumb.data, done.thumb.bytes));
            if (!handle) {
                if (e.retries < 1) {
                    ++e.retries;
                    e.state = LoadState::NotLoaded;
                    if (std::ranges::find(g.queue, done.id) == g.queue.end()) {
                        EnqueueWithCap(done.id, e.priority);
                    }
                    log::Warn("图库缩略图上传失败（将重试）：{} —— {}", done.name,
                              gpu::GpuErrorText(handle.error()));
                    continue;
                }
                e.state = LoadState::Failed;
                log::Warn("图库缩略图上传失败：{} —— {}", done.name, gpu::GpuErrorText(handle.error()));
                continue;
            }

            // 先写 GPU 缓存（接管纹理所有权），再把 CPU 副本移入 CPU 缓存
            gpu::TextureCache().Insert(GpuKey(done.id), *handle);
            CpuThumbCache::Instance().Insert(done.id, done.targetSize != 0 ? done.targetSize : e.targetSize,
                                             std::move(done.thumb));
            e.state = LoadState::Ready;
            e.retries = 0;
            log::Info("loaded {} {}x{} -> {} in {:.0f} ms (decode {:.0f} / resize {:.0f}{})", done.name, done.srcW,
                      done.srcH, done.dstSide, done.decodeMs + done.resizeMs, done.decodeMs, done.resizeMs,
                      done.fromDisk ? ", disk-hit" : "");
        }
    }
    Pump();
}

void ThumbnailService::EvictCaches() {
    if (Get().shutdown) {
        return;
    }
    // 必须在本帧 ImGui 绘制之前：`Image()` 只把 SRV 记进 draw list
    gpu::TextureCache().EvictIfNeeded();
}

void ThumbnailService::OnDeviceLost() {
    Impl& g = Get();
    log::Warn("图库：模拟设备丢失 —— 释放 GPU 纹理并清空 GPU 缓存（CPU 缩略图保留）");
    gpu::Textures().OnDeviceLost();
    gpu::TextureCache().OnDeviceLost();
    for (auto& [id, e] : g.entries) {
        if (e.state == LoadState::Ready) {
            e.state = LoadState::NotLoaded; // 下帧 Request 走 CPU 重传
        } else if (e.state == LoadState::Loading) {
            // 在飞 worker 仍会 PostToUi；Upload 会重建
        }
    }
}

std::size_t ThumbnailService::ReplayFromCache() {
    Impl& g = Get();
    ApplyBudgetsFromSettings();
    gpu::TextureCache().ResetStats();
    CpuThumbCache::Instance().ResetStats();

    std::size_t cpuHits = 0;
    std::size_t queued = 0;
    for (auto& [id, e] : g.entries) {
        if (e.state == LoadState::Failed || e.path.empty()) {
            continue;
        }
        gpu::TextureCache().Erase(GpuKey(id));
        e.retries = 0;
        if (TryServeFromCache(id, e.targetSize)) {
            e.state = LoadState::Ready;
            ++cpuHits;
            continue;
        }
        e.state = LoadState::Queued;
        if (std::ranges::find(g.queue, id) == g.queue.end()) {
            EnqueueWithCap(id, static_cast<int>(ThumbPriority::High));
        }
        ++queued;
    }
    Pump();
    log::Info("缓存重放：CPU 命中 {}，回源队列 {}（GPU 命中率 {:.0f}% / CPU 命中率 {:.0f}%）", cpuHits, queued,
              GpuHitRate() * 100.0, CpuHitRate() * 100.0);
    return cpuHits;
}

LoadState ThumbnailService::State(ImageId id) const {
    const Impl& g = Get();
    const auto it = g.entries.find(id);
    return it == g.entries.end() ? LoadState::NotLoaded : it->second.state;
}

gpu::GpuTextureHandle ThumbnailService::Texture(ImageId id) const {
    // 一律走 GPU 缓存 Find：刷新 LRU，避免持有已被 Evict 的句柄
    if (const gpu::GpuTextureHandle hit = gpu::TextureCache().Find(GpuKey(id)); hit) {
        return hit;
    }
    return {};
}

std::size_t ThumbnailService::InFlightAndQueued() const {
    const Impl& g = Get();
    return g.queue.size() + static_cast<std::size_t>(g.active);
}

std::size_t ThumbnailService::ReadyCount() const {
    const Impl& g = Get();
    std::size_t n = 0;
    for (const auto& [id, e] : g.entries) {
        if (e.state == LoadState::Ready) {
            ++n;
        }
    }
    return n;
}

std::size_t ThumbnailService::FailedCount() const {
    const Impl& g = Get();
    std::size_t n = 0;
    for (const auto& [id, e] : g.entries) {
        if (e.state == LoadState::Failed) {
            ++n;
        }
    }
    return n;
}

std::size_t ThumbnailService::CpuCacheBytes() const { return CpuThumbCache::Instance().Bytes(); }
std::size_t ThumbnailService::GpuCacheBytes() const { return gpu::TextureCache().Bytes(); }
double ThumbnailService::CpuHitRate() const { return CpuThumbCache::Instance().HitRate(); }
double ThumbnailService::GpuHitRate() const { return gpu::TextureCache().HitRate(); }
double ThumbnailService::DiskHitRate() const { return disk_cache::HitRate(); }

std::string ThumbnailService::CacheSummary() const {
    return fmt::format("CPU {:.1f}MB {:.0f}% · GPU {:.1f}MB {:.0f}% · Disk {:.0f}%", CpuCacheBytes() / (1024.0 * 1024.0),
                       CpuHitRate() * 100.0, GpuCacheBytes() / (1024.0 * 1024.0), GpuHitRate() * 100.0,
                       DiskHitRate() * 100.0);
}

std::size_t ThumbnailService::WorkerStarts() const { return Get().workerStarts; }
std::size_t ThumbnailService::RequestCalls() const { return Get().requestCalls; }
void ThumbnailService::ResetRequestStats() noexcept {
    Impl& g = Get();
    g.workerStarts = 0;
    g.requestCalls = 0;
}

} // namespace shine::gallery
