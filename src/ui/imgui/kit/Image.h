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
//    每一帧都在换档与重解码。所以 `RequestImage` **不暴露 size 参数**，全 UI 统一一档；
//    要更大的尺寸就换**另一个 key 号段位**再开一档（`RequestImageFull` 就是这么做的，
//    见它上面的说明）—— 同 key 换尺寸是禁止的，换 key 是允许的。
// 3. **key 号段**：`ImageScanner` 的 id 从 1 自增，`ThumbnailService` 的 GPU 键再占
//    最高位（`ImageId | 1ull<<63`）。本层取 **bit 62**（缩略图档）与 **bit 61**
//    （大图档）。
//
//    ⚠️ 两档的 key 必须是 `(FNV(path) & ~两档位) | 自己那一位` —— **先清后置**。
//    写成 `FNV(path) | 自己那一位` 看起来"各占一位、互不干扰"，实际不成立：FNV 的
//    每一位都是任意的 ⇒ 约 1/4 的路径它的 bit61 与 bit62 本来就是 1，两档撞成同一个
//    key。后果见 `Image.cpp` 顶部那段（一句话：大图档登记不进登记表 ⇒
//    `RequestedImageFullCount()` 恒 0 ⇒ 取真假红，而**换个输出目录就不再撞**）。
//    这条不变量由 `ImageKeyCollisionCount()` 自检，并进取证的 overall。
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
// 两档各一个：`ImageKey` = 缩略图档（bit 62），`ImageKeyFull` = 大图档（bit 61）。
// ⚠️ 二者**必须互不相同**（先清号段位再置位），见上面约束 3。
[[nodiscard]] std::uint64_t ImageKey(const std::filesystem::path& path);
[[nodiscard]] std::uint64_t ImageKeyFull(const std::filesystem::path& path);

// 号段自检用的探针条数（`ImageKeyCollisionCount()` 遍历这么多条就地的路径字符串）。
inline constexpr int kImageKeySelfTestProbes = 256;

// 号段自检：返回「两档 key 撞成同一个」的探针条数，**0 = 号段真的分裂了**。
//
// 为什么要它、并且为什么要用**就地生成**的探针：这条不变量的失败模式极像产品缺陷
// —— 大图档登记不进登记表 ⇒ `RequestedImageFullCount()==0` ⇒ 取证报「查看器里
// 一张大图都没请求过」，而那张大图**明明在屏上**。而它撞不撞取决于整条路径字符串的
// FNV，路径里又含取证输出目录（`SHINE_IMGUI_REVIEW=<dir>`）⇒ 同一份代码换个 OutDir
// 就从 PASS 变 FAIL。拿工程里的真路径当探针，等于让这条判据跟着那个目录转。
[[nodiscard]] int ImageKeyCollisionCount();

// 请求并查询一张图。**每帧调**（见头文件第 1 条约束）。
// 第一次调用会在 worker 上排队解码；就绪后同路径的调用直接返回纹理。
// 路径为空 / 文件不存在 → `Failed`（调用方据此回退到 `kit::Art()` 占位）。
[[nodiscard]] ImageSlot RequestImage(const std::filesystem::path& path);

// ---- 第二档尺寸：查看器大图（`pages/MediaViewer.cpp` 唯一的消费者）----
//
// 缩略图那一档是 512，在查看器里会被放大到 2–3 倍，边缘肉眼可见地糊。所以再开一档
// 2048 给「大图」用。
//
// ⚠️ **必须换一个 key 号段位（bit 61）**，不能复用同一把 key 只换尺寸 —— 那会直接
//    撞上约束 2：服务侧按 key 存 `targetSize`，同 key 换档 = `Erase` + 重新解码，
//    于是「网格里画缩略图、查看器里画大图」两个调用点**每帧互相换档**、每帧重解码。
//    换 key 之后这是两条互不干扰的条目，各自只解一次。bit 61 与缩略图档（bit 62）、
//    服务侧的 GPU 键（bit 63）都不撞 —— ⚠️ 前提是**先清后置**（约束 3）：直接
//    `FNV(path) | bit61` 会与缩略图档的 key 撞上约 1/4 的路径，那时这一档就完全失效。
//
// 代价说清楚：同一张图被两档都请求过时会有**两份**纹理（512 与 2048 各一份），
// 这是刻意的 —— 用一份就得每帧重解码，比多占一点显存贵得多。
[[nodiscard]] ImageSlot RequestImageFull(const std::filesystem::path& path);

// 画一张图。`slot` 没就绪时**什么都不画**（占位由调用方自己画 —— 我们不在这里
// 偷偷画 `Art`：那会让"有没有真图"这件事在代码里看不出来）。
//
// `tint` 默认 `IM_COL32_WHITE` = **不改色**。这是 ImGui 自己的约定，也是唯一正确的
// 「恒等值」：纹理染色在管线上是**相乘**，白 × 纹理 = 纹理。它**不是**主题色，
// 所以没有、也不该有 `kit::ColorXxx()` 出口 —— 别为了「走门禁」去拿 `ColorText()`
// 之类当默认值，那在浅色主题下会把每张图都染成深色。
void DrawImage(ImDrawList* draw, Rect bounds, const ImageSlot& slot, float rounding,
               ImageFit fit = ImageFit::Cover, ImU32 tint = IM_COL32_WHITE);

// 某个 `fit` 规则下、把这张图画进 `bounds` 时该用的 UV 区间（`DrawImage` 内部就是用它）。
//
// 为什么要有这个出口：查看器的 3× 放大镜要从**屏幕坐标反推 UV**（把光标附近那一小块
// 画进 160px 的圆里）。没有它就得把 cover 的裁切规则抄第二份 —— 抄两份的下场是改一处
// 忘另一处，而那两处一旦分叉，「圆里放大出来的位置」与「方框里画出来的位置」就对不上，
// 这种错在截图上看不出来（都是同一张图），只有肉眼盯着放大镜才知道偏了。
void ImageUvForFit(const ImageSlot& slot, Rect bounds, ImageFit fit, ImVec2& uv0, ImVec2& uv1);

// ---- 诊断 / 取证用 ----
//
// 还**画不出来**的图有几张。判据是「这一帧能不能画出来」，**不是**「服务侧状态是几」：
//   · `NotLoaded` / `Queued` / `Loading`          → 没落地；
//   · `Ready` **但 `Texture()` 是空句柄**          → 也没落地（纹理被 GPU LRU 淘汰 /
//                                                    设备丢失，GPU 侧补不回来）；
//   · `Ready` 且有纹理                            → 落地；
//   · `Failed`                                     → 落地（解不出来的诚实空态，
//                                                    不能让等待被它一直拖到超时）。
// 所以这个数只在「还在飞」或「纹理缺了」时大于 0，不会因为某个文件坏了而永远不归零。
// ⚠️ 「`Ready` 但没纹理」那一条是后补的：漏了它的时候，一个「纹理没了但状态是 Ready」
//    的图会被判成 settled，而截图上什么都没有 —— 判据说绿、图上是空的。
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

// 上面那条**分档**的版本：大图档（bit 61）请求过几张。
//
// 为什么非要单开一条：`PendingImageCount() == 0` 单独看有二义 —— 「全落地了」与
// 「一张都没被请求过」同形。缩略图那一族靠 `RequestedImageCount() > 0` 补上另一半；
// 而查看器是**按需**请求大图的（只有开着那一帧才请求），抓图那一刻它要是压根没请求，
// 「pending=0」就什么都没证明。判据必须能分开这两件事。
//
// ⚠️ 档位是**登记时记下来的**，不是「按 key 的第 61 位事后判」—— key 是
//    `FNV(path) | 号段位`，FNV 的每一位都是任意的，缩略图档的 key 有一半天然带着
//    bit 61。按位判会把 16 张缩略图数成 8 张大图，而那个数正好进判据的分母。
[[nodiscard]] int RequestedImageFullCount();

// 只数**大图档**里还没落地的几张。
//
// 为什么不能用总的 `PendingImageCount()` 代替：它按「注册过的全部 key」遍历，
// 而注册表是**进程级**的、只增不减 —— 某张图一旦不再被任何可见位点绘制（切了视图 /
// 网格被换成详情页），它被 GPU 淘汰后就永远停在「Ready 但没有纹理」= pending。
// 实测：`gpu::TextureCache().Clear()`（淘汰试验）+ 从总览切回详情之后，那批网格缩略图
// 全部变成永久 pending，于是「等 pending 归零」在任何后续位置都等不到 ——
// 判据会以「链断了」的样子报红，而链上其实什么都没坏。
[[nodiscard]] int PendingImageFullCount();

} // namespace shine::kit
