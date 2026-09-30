#pragma once
// shine::kit::Icon_Draw —— 把字形描到 ImDrawList 上
//
// 从 kit/Icon.cpp 拆出。这一族是**每帧的仿射变换 + 描边**，不碰解析、也不碰
// 图标表：拿到 IconGlyph，按 size/24 缩放，PathStroke 出 currentColor。
//
// 单独成文件的理由：这是热路径（每帧每个图标都调），它的时间/空间口径
// —— 缩放取 size/24、线宽下限 1px、每条子路径一次 PathClear + 一次 PathStroke ——
// 与 Icon_Path.h 的容错口径、Icon_Registry.h 的数据口径都不相干。放一起时
// 调「线宽下限」要去翻解析器，改「解析容错」要担心碰着描边。
#include "ui/imgui/kit/Icon_Glyph.h"

#include <string_view>

namespace shine::kit {

// 在 (pos.x, pos.y) 处画一个 size×size 的图标。
// thickness 是 24 网格下的描边宽度（设计稿 1.6），实际像素按 size/24 缩放。
void DrawIcon(ImDrawList* draw, std::string_view name, ImVec2 pos, float size, ImU32 color,
              float thickness = 1.6f);

// 以 (center.x, center.y) 为中心画。
void DrawIconCentered(ImDrawList* draw, std::string_view name, ImVec2 center, float size,
                      ImU32 color, float thickness = 1.6f);

} // namespace shine::kit
