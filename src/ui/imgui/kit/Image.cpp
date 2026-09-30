#include "ui/imgui/kit/Image.h"

#include "ui/imgui/kit/Draw.h" // NoteInvertedRect（Widget_Core.h 只声明 Rect/Hit，不拉 Draw.h）

#include "gpu/GpuTexture.h"
#include "gpu/GpuTextureManager.h" // gpu::Textures()
#include "media/ThumbnailService.h"
#include "util/Encoding.h"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace shine::kit {
namespace {

// 全 UI 唯一一档解码尺寸（见头文件约束 2：一个路径只允许一档）。
//
// 为什么是 512：设计稿里用图的矩形最大是资产卡的 ~150px 高与出图结果面板的 190px 高，
// 512 在 2× 位图密度（`kRasterizerDensity`）下仍有余量；再大就为「查看器大图」付代价了 ——
// 而查看器原图本来就有另一条路（`gallery::viewer::FullTexture`，按需解全尺寸 + 只留 4 张）。
constexpr std::uint32_t kTargetPx = 512;

// ⚠️ 号段：见头文件约束 3。
constexpr std::uint64_t kUiImageBit = 1ull << 62;

// 已请求过的路径（key → UTF-8 路径）。**只用于诊断**（日志里能看出是哪个文件），
// 不参与「要不要再调 Request」的判断 —— 那个判断只看服务侧的 State，
// 否则 `Thumbs().Reset()`（图库换目录时会调）之后就再也不会重新请求了。
std::unordered_map<std::uint64_t, std::string>& RequestedPaths() {
    static std::unordered_map<std::uint64_t, std::string> paths;
    return paths;
}

std::uint64_t Fnv1a(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ull; // FNV offset basis
    for (const char c : text) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= 1099511628211ull; // FNV prime
    }
    return hash;
}

} // namespace

std::uint64_t ImageKey(const std::filesystem::path& path) {
    return Fnv1a(util::PathToUtf8(path)) | kUiImageBit;
}

int RequestedImageCount() { return static_cast<int>(RequestedPaths().size()); }

int PendingImageCount() {
    int pending = 0;
    for (const auto& [key, path] : RequestedPaths()) {
        switch (gallery::Thumbs().State(key)) {
        case gallery::LoadState::NotLoaded:
        case gallery::LoadState::Queued:
        case gallery::LoadState::Loading:
            ++pending;
            break;
        case gallery::LoadState::Ready:
        case gallery::LoadState::Failed:
            break; // 落地了（成功或失败都算 —— 见头文件）
        }
    }
    return pending;
}

ImageSlot RequestImage(const std::filesystem::path& path) {
    ImageSlot slot;
    if (path.empty()) {
        slot.status = ImageStatus::Failed;
        return slot;
    }
    slot.key = ImageKey(path);

    using gallery::LoadState;
    // **只在服务侧不认识它时才请求**。这一条同时解决两件事：
    //   · 首次请求（State == NotLoaded）；
    //   · `Thumbs().Reset()` 之后（图库换目录 / 换尺寸档）—— 服务侧把它忘了，
    //     State 又回到 NotLoaded，于是这里会**自动重新请求**，界面自愈。
    // 反过来（每帧无条件 Request）会撞上约束 1 里说的那个陷阱吗？不会 ——
    // Request 是幂等的。但这里仍按 State 判，因为它顺带给出了"服务侧知道不知道"，
    // 比一个本地 bool 更抗 Reset。
    if (gallery::Thumbs().State(slot.key) == LoadState::NotLoaded) {
        RequestedPaths().try_emplace(slot.key, util::PathToUtf8(path));
        gallery::Thumbs().Request(slot.key, path, kTargetPx, gallery::ThumbPriority::Normal);
    }

    switch (gallery::Thumbs().State(slot.key)) {
    case LoadState::Failed:
        // 解不出来（文件是桩 / 格式不支持 / 已删）。**不在这里重试** ——
        // 服务侧自己会重试一次（`DrainCompleted` 里的 `retries < 1`）。
        slot.status = ImageStatus::Failed;
        return slot;
    case LoadState::Ready:
        break;
    case LoadState::NotLoaded:
    case LoadState::Queued:
    case LoadState::Loading:
    default:
        return slot; // 还是 Pending
    }

    // 状态 Ready ≠ 纹理在。`Texture()` 一律走 `gpu::TextureCache().Find()`，
    // 所以被 LRU 淘汰 / 设备丢失之后它会返回空句柄 —— 这时**当帧就当作还没好**，
    // 下一帧的 `Request` 会把 GPU 侧重新补上（这就是约束 1 要每帧调的原因）。
    const gpu::GpuTextureHandle handle = gallery::Thumbs().Texture(slot.key);
    const gpu::GpuTexture* texture = handle ? gpu::Textures().Get(handle) : nullptr;
    if (texture == nullptr || !texture->valid()) {
        return slot;
    }
    slot.texture = static_cast<ImTextureID>(texture->texture());
    slot.width = static_cast<float>(texture->width());
    slot.height = static_cast<float>(texture->height());
    slot.status = ImageStatus::Ready;
    return slot;
}

void DrawImage(ImDrawList* draw, Rect bounds, const ImageSlot& slot, float rounding, ImageFit fit,
               ImU32 tint) {
    if (draw == nullptr || !slot.ready()) {
        return;
    }
    if (bounds.max.x <= bounds.min.x || bounds.max.y <= bounds.min.y) {
        NoteInvertedRect(bounds.min.x, bounds.min.y, bounds.max.x, bounds.max.y, "DrawImage");
        return;
    }

    ImVec2 uv0(0.0f, 0.0f);
    ImVec2 uv1(1.0f, 1.0f);
    if (fit == ImageFit::Cover) {
        // 等价 CSS `object-fit: cover` / SVG `xMidYMid slice`：把长出去的那一边**对半裁**。
        // ⚠️ 只改 UV、不改目标矩形 —— 改矩形就成了居中缩放，四条边会留白。
        const float boxAspect = bounds.width() / bounds.height();
        const float imgAspect = slot.width / slot.height;
        if (imgAspect > boxAspect) {
            const float keep = boxAspect / imgAspect; // 横向保留的比例
            const float cut = (1.0f - keep) * 0.5f;
            uv0.x = cut;
            uv1.x = 1.0f - cut;
        } else {
            const float keep = imgAspect / boxAspect; // 纵向保留的比例
            const float cut = (1.0f - keep) * 0.5f;
            uv0.y = cut;
            uv1.y = 1.0f - cut;
        }
    }
    // 圆角由 `AddImageRounded` 自己裁（不必先 PushClipRect）—— 设计稿里
    // `.art { border-radius: var(--r-sm) }`、`.tl-card .tthumb` 的圆角都是这么来的。
    const ImTextureRef ref(slot.texture);
    if (rounding > 0.0f) {
        draw->AddImageRounded(ref, bounds.min, bounds.max, uv0, uv1, tint, rounding);
    } else {
        draw->AddImage(ref, bounds.min, bounds.max, uv0, uv1, tint);
    }
}

} // namespace shine::kit
