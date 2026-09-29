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
#include <imgui.h>

#include <string_view>
#include <vector>

namespace shine::kit {

struct IconSegment {
    char op;      // 'M' 起子路径 / 'L' 直线 / 'C' 三次贝塞尔 / 'Z' 闭合
    float p[6];   // L: p[0..1]；C: p[0..5]（两个控制点 + 终点）
};

struct IconSubPath {
    ImVec2 start;
    std::vector<IconSegment> segments;
};

struct IconGlyph {
    std::vector<IconSubPath> subPaths;
};

// 取图标。未知名字回落到 info（与 Icon.jsx:52 的兜底一致），但**不**照抄
// Gallery.jsx:140 传未定义 flow 的 bug —— flow 是本实现补的真图标。
[[nodiscard]] const IconGlyph* GetIcon(std::string_view name);

// 图标名清单（46 + minus + flow），供画廊与自检遍历。
[[nodiscard]] const std::vector<std::string_view>& IconNames();

// 在 (pos.x, pos.y) 处画一个 size×size 的图标。
// thickness 是 24 网格下的描边宽度（设计稿 1.6），实际像素按 size/24 缩放。
void DrawIcon(ImDrawList* draw, std::string_view name, ImVec2 pos, float size, ImU32 color,
              float thickness = 1.6f);

// 以 (center.x, center.y) 为中心画。
void DrawIconCentered(ImDrawList* draw, std::string_view name, ImVec2 center, float size,
                      ImU32 color, float thickness = 1.6f);

} // namespace shine::kit
