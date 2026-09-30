#pragma once
// shine::kit::Views —— 过程视图门面（P3.3 / P3.5）
//
// 从 kit/Views.cpp 拆出后，本文件是**门面**：导出面与拆分前逐字一致，
// 下游（pages/Gallery.cpp、pages/WorkspacePages.h）不需要改 include。
// 三个族的实现在：
//   * View_Stage.h/.cpp      —— 步骤条 / 阶段流 / 阶段列表
//   * View_FlowCanvas.h/.cpp —— 节点画布（连线 / 拖拽 / 缩放 / fit）
//   * 本文件配套的 Views.cpp  —— 过程艺术占位画（见下面的豁免说明）
//
// ⚠️ Art / ArtInk 为什么留在 Views.cpp 而不是再拆一个 View_Art.cpp：
//    tools\check-colors.ps1 的 $Zones 是**按文件**列豁免的，其中一条指名
//    'kit/Views.cpp'（理由：这里存的是设计稿 UI.jsx:184 的 12 组插画调色板**数据表**，
//    数据表本来就得写死色值）。而那份脚本 227-232 行会在**豁免区文件不存在时 exit 1** ——
//    也就是说把 kArtPalettes 搬走会让门禁直接红，且修门禁要动 tools\（不是本轮的文件范围）。
//    连带影响：pages/PageCommon.h:24 的注释指名的也是 kit/Views.cpp。改动美术时改这一个文件。
//
// Art / ArtInk 在 webui 里是**程序化 SVG**（UI.jsx:198/223），无外部资源。
// 它们本身就是纯几何绘制，所以 ImDrawList 能 1:1 复刻 —— 不需要任何图片资源。

#include "ui/imgui/kit/View_FlowCanvas.h"
#include "ui/imgui/kit/View_Stage.h"
#include "ui/imgui/kit/Widgets.h"

#include <string>
#include <string_view>
#include <vector>

namespace shine::kit {

// ---- 18. Art（UI.jsx:198）----
// 12 组调色板按 |seed| % 12；viewBox 160×100；竖直渐变天空 + 日轮 r13（+r20 光晕）
// + 3 层山形。cover=true 时按 cover 规则铺满（见 P5.5 的锚点注意事项）。
void Art(ImDrawList* draw, Rect bounds, int seed, bool cover);

// ---- 19. ArtInk（UI.jsx:223）----
// 800×260；4 层水墨山形用 text-primary 透明度 .08/.13/.22/.42，
// 远两层高斯模糊 7（多层半透明叠近似）；小船 + 26×26 印泥印章
void ArtInk(ImDrawList* draw, Rect bounds, int seed, float opacity = 1.0f);

// 步骤条 / 阶段流 / 阶段列表见 View_Stage.h：
//   Steps / StepsWidth、StageState / StageNode、StageFlow / StageFlowWidth、StageList
// 节点画布见 View_FlowCanvas.h：
//   FlowState / FlowNode / FlowLink / FlowView、kFlowNodeW/H、
//   FlowLayoutNodes / FlowFit / FlowCanvas

} // namespace shine::kit
