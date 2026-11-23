#include "ui/imgui/kit/Image.h"

#include "ui/imgui/kit/Draw.h" // NoteInvertedRect（Widget_Core.h 只声明 Rect/Hit，不拉 Draw.h）
#include "ui/imgui/kit/Hash.h"

#include "gpu/GpuTexture.h"
#include "gpu/GpuTextureManager.h" // gpu::Textures()
#include "media/ThumbnailService.h"
#include "util/Encoding.h"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace shine::kit {
namespace {

// 缩略图那一档的解码尺寸（见头文件约束 2：一个 key 只允许一档）。
//
// 为什么是 512：设计稿里用图的矩形最大是资产卡的 ~150px 高与出图结果面板的 190px 高，
// 512 在 2× 位图密度（`kRasterizerDensity`）下仍有余量。
constexpr std::uint32_t kTargetPx = 512;

// 查看器大图那一档。舞台最宽 620、放大上限 3×，2048 有富余；再大就只是白烧显存。
constexpr std::uint32_t kFullTargetPx = 2048;

// ⚠️ 号段：见头文件约束 3。两个档位各占一位（同 key 换尺寸会被禁止的原因写在
//    `RequestImageFull` 的注释里）。
//
// ⚠️⚠️ 但「各占一位」**只有先清掉 FNV 的自然位才成立**（`base & ~kBits | bit`）。
//    第一版是 `Fnv1a(path) | bit`，写出来像「两个号段互不干扰」，其实不成立：FNV
//    的每一位都是任意的 ⇒ 约 1/4 的路径它自己的 bit61 与 bit62 **本来就是 1**，
//    于是 `ImageKey(p) == ImageKeyFull(p)`，两档共用一个 key。两层后果：
//      · 登记表用 `try_emplace`（不覆盖已有项）⇒ 路径先被**缩略图档**注册过时，
//        大图档根本登记不进去 ⇒ `RequestedImageFullCount()` 恒 0 ⇒ 取证判据
//        `viewer-full` 报「大图一次都没请求过」，而那张大图**明明在屏上**（假红）；
//      · 服务侧按 key 存 `targetSize` ⇒ 两个调用点每帧互相换档、每帧重解码 ——
//        正是头文件约束 2 要挡的形态。
//    为什么长期没暴露：撞不撞取决于**整条路径字符串**的 FNV，而路径里含取证输出目录
//    （`SHINE_IMGUI_REVIEW=<dir>`）⇒ 同一份代码换个 OutDir 就从 PASS 变 FAIL
//    （实测 `shots-base` 撞、`shots-base2` 不撞，两轮 viewer 截图的像素数一模一样）。
constexpr std::uint64_t kUiImageBit = 1ull << 62;
constexpr std::uint64_t kUiImageFullBit = 1ull << 61;
constexpr std::uint64_t kUiImageBits = kUiImageBit | kUiImageFullBit;

// key → 登记项（谁请求过什么）。用途只有两个：
//   · 诊断：日志/取证里能说出「这个 key 是哪个文件」；
//   · 取证的计数：`PendingImageCount()` / `RequestedImageCount()` 靠它遍历。
// ⚠️ 它**不参与**「要不要再调 `Request`」的判断 —— 现在是**每帧无条件调**（见
//    `RequestImage` 里那段）。早先这里挂着一个「只在 State==NotLoaded 时请求」的
//    gate，那是错的：它让被淘汰的纹理永远回不来。
//
// ⚠️ `full` 这个标志**不能**靠「key 的第 61 位」事后判：key 是
//    `FNV(path) | 号段位`，而 FNV 的每一位都是任意的 —— 缩略图档的 key 有一半
//    天然带着 bit 61（实测把 `RequestedImageFullCount()` 写成按位与之后，
//    16 张缩略图被数成了 8 张大图，而那个数进了判据的分母，整个判定是错的）。
//    档位必须在**登记时**显式记下来。
struct RequestedImage {
    std::string path;
    bool full = false;  // true = 大图档（bit 61 / 2048）
};
std::unordered_map<std::uint64_t, RequestedImage>& RequestedPaths() {
    static std::unordered_map<std::uint64_t, RequestedImage> paths;
    return paths;
}

// FNV 已提到 kit/Hash.h（图片 key 与页面缓存共用一份，不再各留一个实现）。


// 两档共用的「键基」：FNV-1a **清掉两个号段位**。两档的 key 只能由它置自己那一位得到
// —— 这样「号段分裂」是结构性的，不依赖 FNV 恰好没置那两位（见上面那段教训）。
std::uint64_t ImageKeyBase(const std::filesystem::path& path) {
    return Fnv1a(util::PathToUtf8(path)) & ~kUiImageBits;
}

// 两档尺寸共用的请求体。`key` / `targetPx` / `full` 必须**同档**：调用方只能通过
// `RequestImage`（512）与 `RequestImageFull`（2048）进来，不要在本文件之外拼 key。
ImageSlot RequestImageAt(const std::filesystem::path& path, std::uint64_t key,
                         std::uint32_t targetPx, bool full) {
    ImageSlot slot;
    if (path.empty()) {
        slot.status = ImageStatus::Failed;
        return slot;
    }
    slot.key = key;

    using gallery::LoadState;
    // 登记 key → 路径（诊断与取证计数用；只登记一次，不参与任何判断）。
    RequestedPaths().try_emplace(slot.key, RequestedImage{util::PathToUtf8(path), full});

    // ⚠️⚠️ **每帧无条件 `Request`** —— 这是头文件里第 1 条约束，不是可选优化。
    //
    // 早先这里挂着一个 gate：`if (State == NotLoaded) Request(...)`，理由写的是
    // 「顺带给出服务侧知道不知道，比一个本地 bool 更抗 Reset」。**那段理由是错的**，
    // 而且它与紧邻的注释自相矛盾 —— 下面查纹理那段写着「下一帧的 Request 会把 GPU 侧
    // 重新补上（这就是约束 1 要每帧调的原因）」，可这个 gate 让下一帧**根本不会调**：
    //
    //   · 纹理被 GPU LRU 淘汰（或设备丢失）后，服务侧的 State 仍是 `Ready`；
    //   · gate 于是永远不请求 ⇒ GPU 侧永远补不回来 ⇒ `Texture()` 一直空句柄；
    //   · 界面表现为**「图闪过一次就没了」**，而且不报任何错、判据也照样绿
    //     （因为「Ready 算落地」那半边判据当时也缺纹理检查，已一并修）。
    //
    // 无条件调是**幂等且廉价**的，`Request` 的 `Ready` 分支就是为它写的
    // （ThumbnailService.cpp）：GPU 侧还在 → 一次哈希查找直接返回；不在 → 从 CPU
    // 缩略图缓存重传；CPU 也没有 → 重新解码。还白拿一个好处：**命中会刷新 GPU LRU
    // 的 tick**，于是「正在上屏的图」不会被「早已滚走的图」挤掉。
    // `Thumbs().Reset()`（图库换目录 / 换尺寸档）之后也照样自愈 —— 服务侧忘了，
    // 下一次调用就当首次请求处理。
    gallery::Thumbs().Request(slot.key, path, targetPx, gallery::ThumbPriority::Normal);

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

} // namespace

std::uint64_t ImageKey(const std::filesystem::path& path) {
    return ImageKeyBase(path) | kUiImageBit;
}

std::uint64_t ImageKeyFull(const std::filesystem::path& path) {
    return ImageKeyBase(path) | kUiImageFullBit;
}

int ImageKeyCollisionCount() {
    // 探针**就地生成**、与工程路径 / 输出目录无关 —— 因为被验的正是「换一条路径字符串
    // 号段还裂不裂」，拿工程里的真路径当探针等于让判据随 `SHINE_IMGUI_REVIEW` 的真假跑。
    // 256 条足够：号段重叠时约 1/4 的字符串会撞（同一份 FNV，概率不靠运气），
    // 一条都不撞才说明两档 key 的分裂是结构性的。
    int collisions = 0;
    for (int i = 0; i < kImageKeySelfTestProbes; ++i) {
        const std::filesystem::path probe = std::filesystem::path("probe") / ("img_" + std::to_string(i) + ".png");
        if (ImageKey(probe) == ImageKeyFull(probe)) {
            ++collisions;
        }
    }
    return collisions;
}

int RequestedImageCount() { return static_cast<int>(RequestedPaths().size()); }

int RequestedImageFullCount() {
    int full = 0;
    for (const auto& [key, entry] : RequestedPaths()) {
        (void)key;
        if (entry.full) {
            ++full;
        }
    }
    return full;
}

namespace {

int CountPending(bool onlyFull) {
    int pending = 0;
    for (const auto& [key, entry] : RequestedPaths()) {
        if (onlyFull && !entry.full) {
            continue;
        }
        switch (gallery::Thumbs().State(key)) {
        case gallery::LoadState::Ready:
            // ⚠️ `Ready` **不等于**「画得出来」：`Texture()` 一律走 GPU 的 LRU 缓存，
            //    纹理被淘汰 / 设备丢失之后它返回**空句柄**，而服务侧的 State 仍是 Ready。
            //    那一刻界面上画的是占位画 ⇒ 它也是「还没落地」。
            //
            //    这条判据早先漏了它（`Ready` 无条件算落地），于是「纹理没了但状态是 Ready」
            //    会被判成 settled，而截图上什么都没有 —— 正是本轮那个
            //    「只在 State==NotLoaded 时才 Request」缺陷的形态：判据说绿、图上是空的。
            if (!gallery::Thumbs().Texture(key)) {
                ++pending;
            }
            break;
        case gallery::LoadState::Failed:
            break; // 解不出来也是落地（诚实空态），不能让它把等待一直拖到超时
        case gallery::LoadState::NotLoaded:
        case gallery::LoadState::Queued:
        case gallery::LoadState::Loading:
            ++pending;
            break;
        }
    }
    return pending;
}

} // namespace

int PendingImageCount() { return CountPending(/*onlyFull=*/false); }

int PendingImageFullCount() { return CountPending(/*onlyFull=*/true); }

ImageSlot RequestImage(const std::filesystem::path& path) {
    return RequestImageAt(path, ImageKey(path), kTargetPx, /*full=*/false);
}

ImageSlot RequestImageFull(const std::filesystem::path& path) {
    return RequestImageAt(path, ImageKeyFull(path), kFullTargetPx, /*full=*/true);
}

void ImageUvForFit(const ImageSlot& slot, Rect bounds, ImageFit fit, ImVec2& uv0, ImVec2& uv1) {
    uv0 = ImVec2(0.0f, 0.0f);
    uv1 = ImVec2(1.0f, 1.0f);
    if (fit != ImageFit::Cover || bounds.width() <= 0.0f || bounds.height() <= 0.0f ||
        slot.width <= 0.0f || slot.height <= 0.0f) {
        return; // Contain（或几何退化）= 整张图
    }
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

void DrawImage(ImDrawList* draw, Rect bounds, const ImageSlot& slot, float rounding, ImageFit fit,
               ImU32 tint) {
    if (draw == nullptr || !slot.ready()) {
        return;
    }
    if (bounds.max.x <= bounds.min.x || bounds.max.y <= bounds.min.y) {
        NoteInvertedRect(bounds.min.x, bounds.min.y, bounds.max.x, bounds.max.y, "DrawImage");
        return;
    }

    ImVec2 uv0;
    ImVec2 uv1;
    ImageUvForFit(slot, bounds, fit, uv0, uv1);
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
