#pragma once
// shine::kit::Icon —— 46 个设计稿图标的 ImGui 实现
//
// 对应 refactor/phases.md P3.6。权威是 webui/src/components/Icon.jsx：
// 24 网格 / 1.6 描边 / 圆头圆角 / fill=none / stroke=currentColor。
//
// 落法说明（design-spec §3 的「方案 2」）：不栅格化成图标字体，直接把
// SVG `d` 解析成路径段，用 ImDrawList::PathStroke2D 画。这样
// `currentColor` 语义天然成立（换主题不用重建图集），代价是每次绘制
// 有一次点变换 —— 对 24 网格的短线段来说可以忽略。
//
// 路径只解析一次（首次取用时惰性解析并缓存），每帧只做仿射变换 + 描边。
//
// 拆分后本文件是**门面**：导出面与拆分前逐字一致，下游（kit/Widgets.h、
// pages/*、kit/Widget_Button.cpp、kit/Widget_Card.cpp）不需要改 include。
// 四族实现在：
//   * Icon_Glyph.h      —— 字形数据结构（IconSegment / IconSubPath / IconGlyph）
//   * Icon_Path.h/.cpp  —— SVG `d` 属性解析器（圆弧按附录 F.6 升阶成三次贝塞尔）
//   * Icon_Registry.h/.cpp —— 48 条路径表 + 惰性注册表 + 名字查找 + info 兜底
//   * Icon_Draw.h/.cpp  —— 每帧的仿射变换 + 描边
#pragma once

#include "ui/imgui/kit/Icon_Draw.h"
#include "ui/imgui/kit/Icon_Glyph.h"
#include "ui/imgui/kit/Icon_Registry.h"

#include <imgui.h>

#include <string_view>
#include <vector>

namespace shine::kit {

// 取图标 / 图标名清单见 Icon_Registry.h；
// 绘制入口 DrawIcon / DrawIconCentered 见 Icon_Draw.h。

} // namespace shine::kit
