#include "media/MediaLibrary.h"
#include "util/File.h"
#include "util/Encoding.h"

#include "comfy/ComfyClient.h"
#include "comfy/ComfyHttp.h"
#include "comfy/ComfySession.h"
#include "comfy/ComfySocket.h"
#include "core/Async.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "gallery/decoders/PngDecoder.h"
#include "gpu/GpuTextureCache.h"
#include "gpu/GpuTextureManager.h"
#include "media/VideoThumb.h"
#include "util/Strings.h"
#include "util/Time.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

namespace shine::media {
namespace {

constexpr std::uint64_t kPreviewFetchKey = 0;      // 预览走单独纹理，不参与 LRU
constexpr std::int64_t kWsPreviewThrottleMs = 500; // ≤ 2 次/秒（P4.5 S2）
constexpr std::int64_t kFallbackThrottleMs = 2000;

[[nodiscard]] std::string JoinKey(std::string_view fileName, std::string_view subfolder, std::string_view type) {
    std::string joined;
    joined.reserve(fileName.size() + subfolder.size() + type.size() + 2);
    joined.append(fileName).push_back('|');
    joined.append(subfolder).push_back('|');
    joined.append(type);
    return joined;
}

[[nodiscard]] std::uint64_t HashKey(std::string_view text) noexcept {
    std::uint64_t hash = 1469598103934665603ull; // FNV-1a 64
    for (const unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash == 0 ? 1ull : hash;
}

} // namespace

MediaLibrary& MediaLibrary::Instance() {
    static MediaLibrary library;
    return library;
}

std::filesystem::path MediaLibrary::CacheDir() const {
    if (!Settings().mediaCacheDir.empty()) {
        return std::filesystem::path{Settings().mediaCacheDir};
    }
    wchar_t buffer[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
    const std::wstring base = (n > 0 && n < MAX_PATH) ? std::wstring(buffer, n) : L".";
    return std::filesystem::path{base + L"\\ShineTVStudio\\cache\\media"};
}

void MediaLibrary::Init() {
    if (inited_) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(CacheDir(), ec);
    log::Info("媒体缓存目录：{}", util::PathToUtf8(CacheDir()));
    // WS 线程回调：只做节流 + 投递，重活在 worker
    comfy::ComfySocket::Instance().AddBinaryListener([this](const comfy::BinaryFrame& frame) { OnBinaryFrame(frame); });
    comfy::ComfySocket::Instance().AddPromptListener(
        [this](const comfy::PromptEvent& ev) { OnPromptEventForPreview(ev); });
    inited_ = true;
}

void MediaLibrary::Shutdown() {
    ClearPreview();
    states_.clear();
    errors_.clear();
    videoThumbTried_.clear();
    selectLatestWhenMerged_ = false;
    pendingSelectName_.clear();
    items_.clear();
    inited_ = false;
}

const MediaItem* MediaLibrary::Latest() const noexcept {
    return items_.empty() ? nullptr : &items_.front();
}

const MediaItem* MediaLibrary::Selected() const noexcept {
    if (selectedKey_ == 0) {
        return nullptr;
    }
    for (const MediaItem& item : items_) {
        if (item.key == selectedKey_) {
            return &item;
        }
    }
    return nullptr;
}

void MediaLibrary::MergeHistory(const comfy::HistoryResult& result) {
    std::vector<std::uint64_t> known;
    known.reserve(items_.size());
    for (const MediaItem& item : items_) {
        known.push_back(item.key);
    }
    int added = 0;
    for (const comfy::HistoryEntry& entry : result.entries) {
        for (const comfy::HistoryMedia& media : entry.media) {
            const std::uint64_t key = HashKey(JoinKey(media.fileName, media.subfolder, media.type));
            if (std::ranges::find(known, key) != known.end()) {
                continue;
            }
            known.push_back(key);
            MediaItem item;
            item.key = key;
            item.fileName = media.fileName;
            item.subfolder = media.subfolder;
            item.type = media.type.empty() ? "output" : media.type;
            item.kind = comfy::ClassifyMediaKind(media.fileName);
            item.promptId = entry.promptId;
            item.createdAtMillis = entry.completedAtMillis != 0 ? entry.completedAtMillis : entry.createdAtMillis;
            // 本地缓存命中则直接记住路径（P4.2 S3：命中不再发网络请求）
            const std::filesystem::path cached = CacheDir() / item.fileName;
            std::error_code ec;
            if (std::filesystem::exists(cached, ec)) {
                item.localPath = cached;
            }
            items_.push_back(std::move(item));
            ++added;
        }
    }
    std::ranges::sort(items_, [](const MediaItem& a, const MediaItem& b) {
        if (a.createdAtMillis != b.createdAtMillis) {
            return a.createdAtMillis > b.createdAtMillis;
        }
        return a.key < b.key;
    });
    log::Info("媒体历史：{} 项（新增 {}）", items_.size(), added);

    // P5.6 S3：生成完成后自动选中"最近输出"（优先按刚落盘的文件名匹配）
    if (selectLatestWhenMerged_) {
        selectLatestWhenMerged_ = false;
        const MediaItem* pick = nullptr;
        if (!pendingSelectName_.empty()) {
            for (const MediaItem& item : items_) {
                if (item.fileName == pendingSelectName_) {
                    pick = &item;
                    break;
                }
            }
        }
        if (pick == nullptr && !items_.empty()) {
            pick = &items_.front();
        }
        if (pick != nullptr) {
            selectedKey_ = pick->key;
            log::Info("自动选中最近输出：{}（{}）", pick->fileName, pick->kind);
        } else {
            log::Info("历史里还没有任何产物，未改变选中项");
        }
        pendingSelectName_.clear();
    }
}

void MediaLibrary::Refresh(int maxItems) {
    // 刷新历史 = 允许重新尝试取视频首帧（首次可能因为还没缓存而失败）
    videoThumbTried_.clear();
    comfy::ComfySession::Instance().FetchHistory(maxItems, [this](comfy::HistoryResult result) {
        if (!result.ok) {
            log::Warn("历史拉取失败：{}", result.error);
            return;
        }
        MergeHistory(result);
    });
}

void MediaLibrary::RefreshAndSelectLatest(std::string_view preferFileName) {
    selectLatestWhenMerged_ = true;
    pendingSelectName_ = std::string{preferFileName};
    Refresh(200);
}

bool MediaLibrary::StartFetch(const MediaItem& item, std::uint64_t key, bool asPreview) {
    FetchedCb cb = [this, asPreview](FetchedImage img) { ApplyFetched(std::move(img), asPreview); };
    if (!item.localPath.empty()) {
        LoadLocalAndDecodeAsync(item.localPath, key, std::move(cb));
        return true;
    }
    FetchAndDecodeAsync(comfy::ComfySession::Instance().BaseUrl(), item.fileName, item.subfolder, item.type, key,
                        std::move(cb));
    return true;
}

gpu::GpuTextureHandle MediaLibrary::TextureFor(const MediaItem& item) {
    if (const gpu::GpuTextureHandle hit = gpu::TextureCache().Find(item.key); hit) {
        return hit;
    }
    if (item.kind == "video") {
        return TextureForVideo(item); // P5.6 S2：视频走 Shell 首帧缩略图，不喂 PngDecoder
    }
    const auto state = states_[item.key];
    if (state == TextureState::Loading || state == TextureState::Failed) {
        return {};
    }
    states_[item.key] = TextureState::Loading;
    StartFetch(item, item.key, /*asPreview=*/false);
    return {};
}

// —— P5.6 S2：视频首帧缩略图（Windows Shell）——
//
// 服务端没有"给视频抽首帧"的接口，所以：
//   ① 未落本地缓存 → 拿不到首帧（**不自动下载**：视频可能很大），标 `Unavailable` + 中文原因；
//   ② 已有本地文件 → worker 里用 `IShellItemImageFactory` 取首帧 + Shell 属性取分辨率 → 上屏。
gpu::GpuTextureHandle MediaLibrary::TextureForVideo(const MediaItem& item) {
    const auto state = states_[item.key];
    if (state == TextureState::Loading) {
        return {};
    }
    if (item.localPath.empty()) {
        if (state != TextureState::Unavailable) {
            states_[item.key] = TextureState::Unavailable;
            errors_[item.key] = "视频还没下载到本地：先下载（双击条目或右键「下载到本地缓存」）才能显示首帧缩略图";
        }
        return {};
    }
    // 同一个本地路径只试一次（避免每帧重发 worker 任务）；换过文件（重新下载）才会再试
    const auto tried = videoThumbTried_.find(item.key);
    if (tried != videoThumbTried_.end() && tried->second == item.localPath) {
        return {};
    }
    videoThumbTried_[item.key] = item.localPath;
    states_[item.key] = TextureState::Loading;
    errors_[item.key].clear();
    const std::uint64_t key = item.key;
    VideoThumbnailAsync(item.localPath, [this, key](VideoThumbResult result) { ApplyVideoThumb(key, std::move(result)); });
    return {};
}

void MediaLibrary::ApplyVideoThumb(std::uint64_t key, VideoThumbResult result) {
    std::string name;
    for (MediaItem& item : items_) {
        if (item.key != key) {
            continue;
        }
        name = item.fileName;
        item.width = result.width;
        item.height = result.height;
        break;
    }
    if (!result.image.valid()) {
        states_[key] = TextureState::Unavailable;
        errors_[key] = result.error.empty() ? std::string{"没有首帧缩略图"} : result.error;
        log::Warn("视频首帧缩略图不可用：{}（{}）", name, errors_[key]);
        return;
    }
    const std::span<const std::byte> bytes{result.image.data, result.image.bytes};
    auto handle = gpu::Textures().Upload(result.image.width, result.image.height, bytes);
    if (!handle.has_value()) {
        states_[key] = TextureState::Failed;
        errors_[key] = gpu::GpuErrorText(handle.error());
        log::Warn("视频首帧纹理上传失败：{}（{}）", name, errors_[key]);
        return;
    }
    gpu::TextureCache().Insert(key, *handle);
    states_[key] = TextureState::Ready;
    errors_[key].clear();
    log::Info("video thumb ready: {} 首帧 {}x{}（视频 {}x{}，{:.0f}ms）", name, result.image.width,
              result.image.height, result.width, result.height, result.ms);
}

TextureState MediaLibrary::StateOf(std::uint64_t key) const {
    const auto it = states_.find(key);
    return it == states_.end() ? TextureState::Idle : it->second;
}

std::string MediaLibrary::ErrorOf(std::uint64_t key) const {
    const auto it = errors_.find(key);
    return it == errors_.end() ? std::string{} : it->second;
}

void MediaLibrary::EnsureLocal(const MediaItem& item, std::function<void(bool, std::filesystem::path)> onDone) {
    const std::filesystem::path target = CacheDir() / item.fileName;
    std::error_code ec;
    if (std::filesystem::exists(target, ec)) {
        log::Info("cache hit: {}", item.fileName); // 验收 2 的判据行
        async::PostToUi([onDone = std::move(onDone), target]() { onDone(true, target); });
        return;
    }
    const std::string url = comfy::BuildViewUrl(comfy::ComfySession::Instance().BaseUrl(), item.fileName,
                                                item.subfolder, item.type);
    async::RunOnWorker([this, url, target, item, onDone = std::move(onDone)]() mutable {
        const auto bytes = comfy::HttpDownloadBinary(url, std::chrono::seconds{120});
        if (!bytes.has_value()) {
            const std::string error = bytes.error().message;
            async::PostToUi([onDone = std::move(onDone), error]() { onDone(false, std::filesystem::path{error}); });
            return;
        }
        std::filesystem::create_directories(target.parent_path());
        std::ofstream out = util::OpenOutput(target);
        out.write(bytes->data(), static_cast<std::streamsize>(bytes->size()));
        const bool ok = out.good();
        out.close();
        async::PostToUi([this, ok, target, key = item.key, name = item.fileName, onDone = std::move(onDone)]() mutable {
            if (ok) {
                for (MediaItem& it : items_) { // 记录路径 → 下次命中
                    if (it.key == key) {
                        it.localPath = target;
                    }
                }
                log::Info("已缓存 {}（{} 字节）", name, std::filesystem::file_size(target));
            } else {
                log::Warn("写缓存失败 {}", util::PathToUtf8(target));
            }
            onDone(ok, target);
        });
    });
}

std::size_t MediaLibrary::CachedBytes() const {
    std::size_t total = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(CacheDir(), ec)) {
        if (entry.is_regular_file(ec)) {
            total += static_cast<std::size_t>(entry.file_size(ec));
        }
    }
    return total;
}

void MediaLibrary::ClearCache() {
    std::error_code ec;
    std::size_t removed = 0;
    for (const auto& entry : std::filesystem::directory_iterator(CacheDir(), ec)) {
        if (entry.is_regular_file(ec) && std::filesystem::remove(entry.path(), ec)) {
            ++removed;
        }
    }
    for (MediaItem& item : items_) {
        item.localPath.clear();
    }
    log::Info("已清空媒体缓存（{} 个文件）", removed);
}

void MediaLibrary::MarkUncached(std::uint64_t key) {
    for (MediaItem& item : items_) {
        if (item.key == key) {
            item.localPath.clear();
            return;
        }
    }
}

// *********************** 纹理与预览 ***********************

void MediaLibrary::ApplyFetched(FetchedImage&& img, bool asPreview) {
    if (asPreview) {
        if (!img.image.valid()) {
            log::Warn("preview: fallback 失败：{}", img.error);
            return;
        }
        UploadPreview(std::move(img.image), /*fromWs=*/false);
        return;
    }
    if (!img.image.valid()) {
        states_[img.key] = TextureState::Failed;
        errors_[img.key] = img.error.empty() ? std::string{"解码失败"} : img.error;
        log::Warn("取图失败 {}：{}", img.fileName, errors_[img.key]);
        return;
    }
    const std::span<const std::byte> bytes{img.image.data, img.image.bytes};
    auto handle = gpu::Textures().Upload(img.image.width, img.image.height, bytes);
    if (!handle.has_value()) {
        states_[img.key] = TextureState::Failed;
        errors_[img.key] = gpu::GpuErrorText(handle.error());
        log::Warn("纹理上传失败 {}：{}", img.fileName, errors_[img.key]);
        return;
    }
    gpu::TextureCache().Insert(img.key, *handle);
    states_[img.key] = TextureState::Ready;
    errors_[img.key].clear();
    log::Info("texture ready: {} {}x{}（fetch {:.0f}ms / decode {:.0f}ms，纹理缓存 {:.1f} MB / {} 项）",
              img.fileName, img.image.width, img.image.height, img.fetchMs, img.decodeMs,
              static_cast<double>(gpu::TextureCache().Bytes()) / (1024.0 * 1024.0), gpu::TextureCache().Count());
}

void MediaLibrary::UploadPreview(gallery::Image&& image, bool fromWs) {
    if (previewTexture_) {
        gpu::Textures().Release(previewTexture_);
        previewTexture_ = {};
    }
    const std::span<const std::byte> bytes{image.data, image.bytes};
    auto handle = gpu::Textures().Upload(image.width, image.height, bytes);
    if (!handle.has_value()) {
        log::Warn("预览纹理上传失败：{}", gpu::GpuErrorText(handle.error()));
        return;
    }
    previewTexture_ = *handle;
    previewActive_ = true;
    previewFromWs_ = fromWs;
    lastPreviewUiMs_ = util::MonotonicMillis();
    log::Info("preview: {} 已上屏 {}x{}", fromWs ? "ws-frame" : "fallback", image.width, image.height);
}

void MediaLibrary::ClearPreview() {
    if (previewTexture_) {
        gpu::Textures().Release(previewTexture_);
        previewTexture_ = {};
    }
    previewActive_ = false;
}

void MediaLibrary::OnBinaryFrame(const comfy::BinaryFrame& frame) {
    // **WS 线程**：节流（≤2 次/秒）→ 拷贝字节 → worker 解码（不在这里碰 ImGui/DX11）
    const std::int64_t now = util::MonotonicMillis();
    const std::int64_t last = lastWsPreviewMs_.load(std::memory_order_relaxed);
    if (now - last < kWsPreviewThrottleMs) {
        return;
    }
    lastWsPreviewMs_.store(now, std::memory_order_relaxed);
    if (frame.format != "png") {
        return; // 目前只解 PNG（JPEG 需要额外解码器；坏帧不参与兜底判定）
    }
    auto payload = std::make_shared<std::vector<std::byte>>(frame.payload);
    async::RunOnWorker([this, payload]() {
        auto image = gallery::decoders::PngDecoder::DecodeMemory(
            std::span<const std::byte>(payload->data(), payload->size()));
        if (!image.has_value()) {
            log::Warn("preview: ws-frame 解码失败：{}", gallery::DecodeErrorText(image.error()));
            return;
        }
        async::PostToUi([this, image = std::move(*image)]() mutable { UploadPreview(std::move(image), true); });
    });
}

void MediaLibrary::OnPromptEventForPreview(const comfy::PromptEvent& ev) {
    // **WS 线程**：只有在"服务器没发二进制预览帧"时才走 /view 兜底（P4.5 S3）
    // 注意：实测 ComfyUI v0.36.0 执行期间发的是 `progress_state`（未必发 `executing`），
    // 所以三类"正在跑"的事件都作为兜底触发点（更宽的条件，但被 2s 节流压住频率）。
    const bool runningEvent = (ev.type == "executing" || ev.type == "progress_state" || ev.type == "progress");
    if (!runningEvent || ev.nodeId.empty()) {
        return;
    }
    async::PostToUi([this]() {
        const std::int64_t now = util::MonotonicMillis();
        if (now - lastFallbackMs_ < kFallbackThrottleMs) {
            return;
        }
        if (comfy::ComfySocket::Instance().LastBinaryFrameMs() != 0) {
            return; // 服务器确实在发预览帧 → 不需要兜底
        }
        const MediaItem* latest = Latest();
        if (latest == nullptr) {
            return;
        }
        lastFallbackMs_ = now;
        log::Info("preview: fallback（服务器未发二进制预览帧，用 /view 拉最近输出）");
        StartFetch(*latest, kPreviewFetchKey, /*asPreview=*/true);
    });
}

} // namespace shine::media
