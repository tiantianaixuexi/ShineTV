#include "gallery/Viewer.h"

#include "core/Async.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "gallery/Gallery.h"
#include "gallery/Image.h"
#include "gallery/ImageLoader.h"
#include "gallery/ThumbnailService.h"
#include "gpu/GpuDevice.h"
#include "gpu/GpuTextureManager.h"
#include "util/Encoding.h"

#include <span>
#include <algorithm>
#include <deque>
#include <ranges>
#include <unordered_map>
#include <utility>

namespace shine::gallery::viewer {
namespace {

constexpr std::size_t kMaxFullCache = 4;
constexpr float kZoomMin = 0.1f;
constexpr float kZoomMax = 16.f;

State g_state;
std::unordered_map<ImageId, gpu::GpuTextureHandle> g_full;
std::deque<ImageId> g_fullOrder;
std::size_t g_fullLoads = 0;
std::size_t g_discarded = 0;

void ReleaseFull(ImageId id) {
    const auto it = g_full.find(id);
    if (it == g_full.end()) {
        return;
    }
    gpu::Textures().Release(it->second);
    g_full.erase(it);
    if (const auto o = std::ranges::find(g_fullOrder, id); o != g_fullOrder.end()) {
        g_fullOrder.erase(o);
    }
}

void InsertFull(ImageId id, gpu::GpuTextureHandle handle) {
    ReleaseFull(id);
    while (g_fullOrder.size() >= kMaxFullCache) {
        ReleaseFull(g_fullOrder.front());
    }
    g_full.emplace(id, handle);
    g_fullOrder.push_back(id);
}

void SyncIndex() {
    g_state.total = Model().Count();
    g_state.index = g_state.id != 0 ? Model().IndexOf(g_state.id) : 0;
    if (g_state.index >= g_state.total) {
        g_state.index = g_state.total == 0 ? 0 : g_state.total - 1;
    }
}

void StartLoad(const ImageInfo& info) {
    g_state.id = info.id;
    g_state.name = util::FileNameToUtf8(info.path);
    g_state.imgW = info.width;
    g_state.imgH = info.height;
    SyncIndex();

    if (FullTexture(info.id) != gpu::GpuTextureHandle{}) {
        g_state.loading = false;
        return;
    }
    if (!gpu::Ready() || info.path.empty()) {
        g_state.loading = false;
        return;
    }

    const std::uint64_t gen = ++g_state.loadGen;
    g_state.loading = true;
    const auto path = info.path;
    const ImageId id = info.id;
    const std::string name = g_state.name;

    async::RunOnWorker([id, path, name, gen]() {
        auto decoded = ImageLoader::Instance().Load(path);
        Image img;
        const bool ok = decoded.has_value() && decoded->valid();
        if (ok) {
            img = std::move(*decoded);
        }
        async::PostToUi([id, name, gen, ok, img = std::move(img)]() mutable {
            if (!g_state.open || gen != g_state.loadGen) {
                ++g_discarded;
                return;
            }
            g_state.loading = false;
            if (!ok) {
                log::Warn("查看器：原图加载失败：{}", name);
                return;
            }
            if (!gpu::Ready()) {
                log::Warn("查看器：GPU 未就绪，无法上传原图：{}", name);
                return;
            }
            auto up = gpu::Textures().Upload(img.width, img.height,
                                             std::span<const std::byte>(img.data, img.bytes));
            if (!up) {
                log::Warn("查看器：原图上传失败：{} —— {}", name, gpu::GpuErrorText(up.error()));
                return;
            }
            InsertFull(id, *up);
            ++g_fullLoads;
            g_state.imgW = img.width;
            g_state.imgH = img.height;
            log::Info("viewer full ready {} {}x{} (loads={} cache={})", name, img.width, img.height, g_fullLoads,
                      g_full.size());
        });
    });
}

} // namespace

void Open(ImageId id) {
    const ImageInfo* info = Model().Find(id);
    if (info == nullptr) {
        return;
    }
    g_state.open = true;
    g_state.id = id;
    g_state.zoom = 1.f;
    g_state.panX = 0.f;
    g_state.panY = 0.f;
    SyncIndex();
    StartLoad(*info);
    log::Info("查看器打开：{}（{} / {}）", g_state.name, g_state.index + 1, g_state.total);
}

void Close() noexcept {
    g_state.open = false;
    g_state.loading = false;
    ++g_state.loadGen; // 作废在飞请求
}

bool IsOpen() noexcept { return g_state.open; }

const State& Get() noexcept { return g_state; }

void Tick() {
    if (!g_state.open) {
        return;
    }
    SyncIndex();
}

bool Step(int dir) {
    GalleryModel& model = Model();
    const auto& items = model.Items();
    if (items.empty() || dir == 0) {
        return false;
    }
    std::size_t i = g_state.id != 0 ? model.IndexOf(g_state.id) : 0;
    if (i >= items.size()) {
        i = 0;
    }
    for (std::size_t n = 0; n < items.size(); ++n) {
        i = dir > 0 ? (i + 1) % items.size() : (i + items.size() - 1) % items.size();
        const ImageInfo& info = items[i];
        if (Thumbs().State(info.id) == LoadState::Failed) {
            continue; // 跳过失败项
        }
        const bool keepZoom = !Settings().viewerFitOnOpen;
        const float keepZ = g_state.zoom;
        g_state.id = info.id;
        if (keepZoom) {
            g_state.zoom = keepZ;
            g_state.panX = 0.f;
            g_state.panY = 0.f;
        } else {
            Fit();
        }
        StartLoad(info);
        return true;
    }
    return false;
}

void Fit() noexcept {
    g_state.zoom = 1.f;
    g_state.panX = 0.f;
    g_state.panY = 0.f;
}

void ActualPixels(float fitScale) noexcept {
    if (fitScale <= 1e-6f) {
        return;
    }
    // 1:1：显示尺寸 = 图像像素 → absScale=1 → zoom = 1/fitScale
    g_state.zoom = std::clamp(1.f / fitScale, kZoomMin / fitScale, kZoomMax / fitScale);
    g_state.zoom = 1.f / fitScale;
    const float absScale = fitScale * g_state.zoom;
    if (absScale < kZoomMin || absScale > kZoomMax) {
        g_state.zoom = (absScale < kZoomMin ? kZoomMin : kZoomMax) / fitScale;
    }
    g_state.panX = 0.f;
    g_state.panY = 0.f;
}

void OnWheel(float wheelSteps, float mouseInContentX, float mouseInContentY, float contentW, float contentH,
             float fitScale) noexcept {
    if (fitScale <= 1e-6f || contentW <= 1.f || contentH <= 1.f || wheelSteps == 0.f) {
        return;
    }
    const float step = 1.12f;
    const float factor = wheelSteps > 0.f ? step : (1.f / step);
    const float oldAbs = std::max(1e-6f, fitScale * g_state.zoom);
    const float newAbs = std::clamp(oldAbs * factor, kZoomMin, kZoomMax);
    if (std::abs(newAbs - oldAbs) < 1e-6f) {
        return;
    }
    const float newZoom = newAbs / fitScale;

    const float cx = contentW * 0.5f + g_state.panX;
    const float cy = contentH * 0.5f + g_state.panY;
    const float oldW = static_cast<float>(g_state.imgW) * oldAbs;
    const float oldH = static_cast<float>(g_state.imgH) * oldAbs;
    const float u = oldW > 0.f ? (mouseInContentX - (cx - oldW * 0.5f)) / oldW : 0.5f;
    const float v = oldH > 0.f ? (mouseInContentY - (cy - oldH * 0.5f)) / oldH : 0.5f;

    const float newW = static_cast<float>(g_state.imgW) * newAbs;
    const float newH = static_cast<float>(g_state.imgH) * newAbs;
    const float newCx = mouseInContentX - (u - 0.5f) * newW;
    const float newCy = mouseInContentY - (v - 0.5f) * newH;
    g_state.zoom = newZoom;
    g_state.panX = newCx - contentW * 0.5f;
    g_state.panY = newCy - contentH * 0.5f;
    ClampPan(contentW, contentH, newW, newH);
}

void OnDrag(float dx, float dy) noexcept {
    g_state.panX += dx;
    g_state.panY += dy;
}

void ClampPan(float contentW, float contentH, float drawW, float drawH) noexcept {
    const float margin = 24.f;
    float maxPanX = 0.f;
    float maxPanY = 0.f;
    if (drawW < contentW) {
        maxPanX = (contentW - drawW) * 0.5f + margin;
    } else {
        maxPanX = (drawW - contentW) * 0.5f + contentW * 0.25f;
    }
    if (drawH < contentH) {
        maxPanY = (contentH - drawH) * 0.5f + margin;
    } else {
        maxPanY = (drawH - contentH) * 0.5f + contentH * 0.25f;
    }
    g_state.panX = std::clamp(g_state.panX, -maxPanX, maxPanX);
    g_state.panY = std::clamp(g_state.panY, -maxPanY, maxPanY);
}

gpu::GpuTextureHandle FullTexture(ImageId id) noexcept {
    const auto it = g_full.find(id);
    if (it == g_full.end()) {
        return {};
    }
    if (gpu::Textures().Get(it->second) == nullptr) {
        return {};
    }
    return it->second;
}

std::size_t FullCacheCount() noexcept { return g_full.size(); }
std::size_t FullLoads() noexcept { return g_fullLoads; }
std::size_t DiscardedLoads() noexcept { return g_discarded; }

} // namespace shine::gallery::viewer
