#pragma once
// shine::pages —— 媒体查看器（webui `components/FileViewer.jsx` 的 1:1 复刻）
//
// 设计稿出处：`FileViewer.jsx:129-210`（外壳 720 × min(78vh,700) / `.modal-h` 头 /
// `.modal-b` 身 / `.modal-f` 脚）、`ui.css:566-636`（`.fv-stage` / `.fv-lens` /
// `.fv-tools` / `.sheet*`）；调用点：`Assets.jsx` 的 `setViewer({...})`
// （image 见 :190-199 与 :215，sheet 见 :177-181）。
//
// ## 设计决策 1：原图从哪来（PROMPT 要求把这一条写进源码）
//
// `gallery::viewer::*`（`src/media/Viewer.h`）那套「异步解全尺寸 + 只留 4 张」的
// 模型层**只能按 `ImageId` 打开**：`viewer::Open(id)` 第一句就是 `Model().Find(id)`，
// 找不到就静默 return。而 `Model` 的条目只有在**图库扫描**（Local 目录 / Comfy
// output|input 三种来源）命中过的文件才有 id —— 资产设定图、派生链那几层
// （`visual_assets.sheet_rel_path` / `visual_artifacts.rel_path`）都是**工程内文件**，
// 不在扫描来源里，拿不到 `ImageId`。
//
// 所以本查看器走 PROMPT 给的第二条路：**路径 → 纹理**，复用界面侧已有的
// `kit::Image`，并为它开**第二档尺寸**（`kit::RequestImageFull`，bit 61 / 2048px）。
// 不选第一条路（把路径注册进图库 Model）的理由很硬：那要改 `shine_core`
// （`src/media/**`），而本次移植的硬约束是业务层一行不改。
//
// ⚠️ 与「模型层齐全，别重写」不冲突：模型层那条链要的是**图库条目**，而本查看器
//    的用户可见入口（资产页 / 派生链 / 导出整版）**全是工程内文件**，两者不可
//    互换。等图库网格页回来时，那一页应当直接调 `gallery::OpenViewer(id)`；
//    本文件不替代它，只补工程内文件这一支。
//
// ## 设计决策 2：它是浮层
//
// 浮层的 item 提交在**根窗口**，而工作区 / 侧栏是 `BeginChild`（child 会成为鼠标
// 所在处的 `HoveredWindow`）⇒ 浮层按钮恒 `hovered=false`。所以：
//   * `Shell::OverlayBlocksMouse()` 与 `chromeInteractive_` 都必须把本查看器算进去
//     （否则工作区 child 与 chrome 会把这次点击先吃掉）；
//   * 遮罩**只画不注册**（`kit::ScrimPaint`），「点外面关闭」手算 `contains`；
//   * 画在**前景 draw list**、且排在一帧所有浮层之后（`Shell::DrawFrame` 尾部）。
// 判据是 `verify/ReviewOverlayProbe.cpp` 里那一组「查看器 ×／点遮罩关闭／底下点不动」
// —— 静息截图对「浮层里按钮点不动」是零覆盖。

#include "ui/imgui/kit/Widgets.h"

#include <filesystem>
#include <string>
#include <vector>

namespace shine::pages {

// V0 派生链的层名与中文标签。层名与顺序来自 `src/novel/NovelAssetPipeline.cpp` 的
// `kChain`（`stage` / `shot` 两层属分镜侧，不在 V0 派生链里）。
//
// ⚠️ **只此一份**：资产页画链、查看器按层取图、联系表排格子都读它。两处各存一份数组
// 的下场是「点第 3 格看的是第 4 层的图」—— 顺序调了以后，画的和点的不再是同一层，
// 而两边都编译得过、各自看着都对。
inline constexpr int kChainLayerCount = 4;
inline constexpr const char* kChainLayerKeys[kChainLayerCount] = {"front", "turnaround",
                                                                 "base_body", "wardrobe"};
inline constexpr const char* kChainLayerLabels[kChainLayerCount] = {"正脸", "四视图", "基础身体",
                                                                   "服装"};

// 查看器里的一页（`type:'image'`）。
struct ViewerImage {
    std::filesystem::path path;  // 绝对路径；空 = 只有说明行，没有图（诚实空态）
    std::string name;            // 头部那行等宽文件名
    std::string meta;            // 页脚说明行（`FileViewer.jsx` 的 `meta`）
    // 图还没解码出来时画 `kit::Art` 占位用的种子（设计稿里是 `seed`）。
    int seed = 5;
};

// 联系表（`type:'sheet'`）里的一格：图 + 底部标签（`.sheet-cell` / `.sheet-label`）。
struct ViewerSheetCell {
    std::filesystem::path path;
    std::string label;
    int seed = 5;
};

struct ViewerRequest {
    std::string name;                     // 标题（等宽显示）
    std::vector<ViewerImage> gallery;     // 非空 = 图片查看；>1 时出左右翻页
    std::vector<ViewerSheetCell> cells;   // 非空 = 联系表（此时忽略 gallery）
    // 联系表页脚那两个按钮（设计稿 Assets.jsx:177-181 的「导出整版」进去的那一屏）。
    bool exportButtons = false;
};

// 打开 / 关闭。`initialIndex` 是落在哪一页（对齐设计稿「点哪一层就看哪一层」）。
void OpenMediaViewer(ViewerRequest request, int initialIndex = 0);
void CloseMediaViewer();

// ---- 产品状态读数（判据只能读产品自己的状态，不能自己复算一份）----
[[nodiscard]] bool MediaViewerOpen();
[[nodiscard]] bool MediaViewerSheet();
[[nodiscard]] int MediaViewerIndex();
[[nodiscard]] int MediaViewerCount();          // 页数（联系表恒为 1）
[[nodiscard]] std::string MediaViewerName();   // 当前页文件名
void MediaViewerStep(int dir);                 // 翻页（判据用；与 ←→ 同一条路径）

// 判据用的**产品自己那份**矩形。不要在外面照 720/min(78vh,700) 重算一遍 ——
// 布局一改就会打在一个已经不存在的位置上，而它报的仍然是「通过」
// （`SettingsCloseRect()` 那次的分叉就是这么来的）。
[[nodiscard]] kit::Rect MediaViewerRect();
[[nodiscard]] kit::Rect MediaViewerCloseRect();
[[nodiscard]] kit::Rect MediaViewerNextRect();
[[nodiscard]] kit::Rect MediaViewerPrevRect();

// 从资产快照构造请求（**页面与取证共用这一份**，两处各拼一遍路径迟早分叉）。
//
// ⚠️ 页序固定在**数据里**，落在哪一页由 `OpenMediaViewer` 的 `initialIndex` 决定：
//    页 0 = 基线设定图，页 1..N = 派生链四层（与 `kChainLayerKeys` 同序）。
//    这里**不接受**一个「初始页」参数 —— 收过一版，而忽略了它的调用点
//    （`OpenMediaViewer(AssetViewerRequest(0, 1))` 的那个 1 落进了被 `(void)` 掉的
//    形参里）会让「点第 2 层」静默打开第 1 页，而两条语句各自看起来都对。
[[nodiscard]] ViewerRequest AssetViewerRequest(int assetIndex);
// 「导出整版」：联系表 —— 基线 + 派生链四层，各一格。
[[nodiscard]] ViewerRequest AssetSheetViewerRequest(int assetIndex);

// 逐镜成图那一支：画廊 = **本章各镜**的成片图，页序与 `BookState::shots` **同序**。
//
// 设计稿出处：`ImageFlow.jsx:144-155` —— 结果页签那张 `.art` 点开后，
// `gallery` 就是 `SHOTS.filter(s => s.chapter === shot.chapter)`（同章的各镜）。
// 落在哪一页由 `OpenMediaViewer` 的第二个实参决定（传同一个下标即可：页序与镜序同序）。
//
// 数据来源与分镜页 `.tthumb` **同一处**：`generated_images` 的
// `source_kind='shot'` + `source_id=shots.id` + `rel_path`。空路径 = 这一镜还没出图
// ⇒ 那一页只有说明行，没有图（诚实空态），不画占位画冒充实拍。
[[nodiscard]] ViewerRequest ShotViewerRequest(int shotIndex);

// 由 `Shell::DrawFrame` 在**帧尾**调（前景 draw list，排在其它浮层之后）。
// 关闭态是空操作。
void DrawMediaViewer(ImDrawList* draw);

} // namespace shine::pages
