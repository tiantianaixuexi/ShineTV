#pragma once
// shine::kit::Image —— 真实图片文件的加载与绘制（消费 shine_core 的异步缩略图流水线）
//
// ## 为什么需要这一层
//
// `shine_core` 里那条链早就建好了：`gallery::ImageScanner`（只探文件头）→
// `gallery::ThumbnailService`（worker 解码 + 缩放 + CPU/GPU 两级 LRU）→
// `gpu::Textures().Upload()`，而纹理注入点 `gpu::AttachDevice(GlApi)` 也在
// `host/Host.cpp` 接上了。**但界面侧一直没有任何一处消费它**：`src/ui/imgui/**` 里
// 找不到一个 `ImGui::Image` / `AddImage`，所有「图」都是 `kit::Art()` 的程序化占位。
//
// 这一层就是那个消费端：**路径 → 纹理 → 画进矩形**。
//
// ## 分工（别在这层里做 IO 或解码）
//
//   worker 线程   解码 + 缩放                ← `gallery::ThumbnailService`
//   UI 线程       上传 GPU + 两级 LRU 淘汰    ← 同上，由 `host/Host.cpp` 的
//                                              `PumpUi()` 每帧 `gallery::Tick()` 驱动
//   本层          路径→key / 查状态 / 绘制     ← 只做 UI 侧簿记与画
//
// ## ⚠️ 三个必须守住的约束
//
// 1. **`RequestImage` 每帧都要调**（对**可见**的那些）。服务侧的 `Request` 是幂等的：
//    条目已就绪时它只"确认 GPU 还在"，被 LRU 淘汰或设备丢失后会重新上屏。
//    只调一次的话，纹理一旦被淘汰这张图就**永远不再出现**（状态还是 Ready，
//    但 `Texture()` 返回空句柄）—— 界面表现为「图闪过一次就没了」。
// 2. **一个路径只用一档尺寸**。条目按 `ImageId` 存 `targetSize`，换档会
//    `Erase(GpuKey)` 并**重新解码**；两个调用点用不同尺寸请求同一个文件 =
//    每一帧都在换档与重解码。所以这里**不暴露 size 参数**，全 UI 统一一档。
// 3. **key 号段**：`ImageScanner` 的 id 从 1 自增，`ThumbnailService` 的 GPU 键再占
//    最高位（`ImageId | 1ull<<63`）。本层取 **bit 62**，两边都不撞。
#include "ui/imgui/kit/Widget_Core.h"

#include <cstdint>
#include <filesystem>

namespace shine::kit {

// `cover` = 按短边裁切填满（等价 CSS `object-fit: cover`，也等价设计稿里
// `Art` 那个 SVG 的 `preserveAspectRatio="xMidYMid slice"`）；
// `contain` = 完整放下、不裁。设计稿里所有缩略图/封面都是 cover。
enum class ImageFit {
    Cover,
    Contain,
};

// 对绘制而言只有三态：还没好 / 好了 / 坏了。服务侧的 `Queued` 与 `Loading`
// 在 UI 侧没有区别（都不画）。
enum class ImageStatus {
    Pending,
    Ready,
    Failed,
};

struct ImageSlot {
    std::uint64_t key = 0;
    ImageStatus status = ImageStatus::Pending;
    ImTextureID texture = 0;
    float width = 0.0f;   // 纹理像素宽 —— cover 裁切要按它算 UV
    float height = 0.0f;  // 纹理像素高
    [[nodiscard]] bool ready() const {
        return status == ImageStatus::Ready && texture != 0 && width > 0.0f && height > 0.0f;
    }
};

// 路径 → UI 侧稳定 key（FNV-1a + 号段位）。**只按路径算**，同一文件的两次请求同 key。
[[nodiscard]] std::uint64_t ImageKey(const std::filesystem::path& path);

// 请求并查询一张图。**每帧调**（见头文件第 1 条约束）。
// 第一次调用会在 worker 上排队解码；就绪后同路径的调用直接返回纹理。
// 路径为空 / 文件不存在 → `Failed`（调用方据此回退到 `kit::Art()` 占位）。
[[nodiscard]] ImageSlot RequestImage(const std::filesystem::path& path);

// 画一张图。`slot` 没就绪时**什么都不画**（占位由调用方自己画 —— 我们不在这里
// 偷偷画 `Art`：那会让"有没有真图"这件事在代码里看不出来）。
//
// `tint` 默认 `IM_COL32_WHITE` = **不改色**。这是 ImGui 自己的约定，也是唯一正确的
// 「恒等值」：纹理染色在管线上是**相乘**，白 × 纹理 = 纹理。它**不是**主题色，
// 所以没有、也不该有 `kit::ColorXxx()` 出口 —— 别为了「走门禁」去拿 `ColorText()`
// 之类当默认值，那在浅色主题下会把每张图都染成深色。
void DrawImage(ImDrawList* draw, Rect bounds, const ImageSlot& slot, float rounding,
               ImageFit fit = ImageFit::Cover, ImU32 tint = IM_COL32_WHITE);

// ---- 诊断 / 取证用 ----
//
// 还**没落地**的图有几张（`Pending`）。`Failed` 与 `Ready` 都算落地 —— 所以这个数
// 只在「解码还在飞」时大于 0，不会因为某个文件坏了而永远不归零。
//
// 为什么需要它：图片是**异步**上屏的（worker 解码 → `PostToUi` → 上传 GPU），
// 而任何「摆好状态就抓图」的地方只泵一两帧 —— 那一两帧里图还没到，抓到的会是
// 占位画。于是「这条链断了」与「拍得太早」在截图上**完全一样**。
// 取证侧按它等到 0 再抓，就能把这两件事分开。
[[nodiscard]] int PendingImageCount();

// 请求过的图有几张。**必须与 `PendingImageCount()` 一起用** ——
// 单独看「待处理 = 0」有二义：它同样成立在「一张都还没被请求过」的时候，
// 而那正是最该报出来的一种情况（调用点没跑到，或筛选后没有可显示的图）。
// 实测踩过：把等待循环放在「筛选后只剩没有设定图的那一类」的那一帧之后，
// 循环会 0 帧就通过，而后面的抓图仍在抢异步时间 —— 同一二进制两轮，
// 一轮数到 33848 像素、另一轮 0 像素。
[[nodiscard]] int RequestedImageCount();

} // namespace shine::kit
