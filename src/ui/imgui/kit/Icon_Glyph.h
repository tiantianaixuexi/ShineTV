#pragma once
// shine::kit::Icon_Glyph —— 图标字形的数据结构
//
// 从 kit/Icon.cpp 拆出。这三个结构是**解析器的产物、绘制器的输入**，两侧都要用，
// 所以单独成一个小头文件，让 Icon_Path.h（写）与 Icon_Draw.h（读）都只依赖它，
// 而不是各自去 include 门面 Icon.h —— 那会成环（Icon.h 又要转发这两个头）。
//
// 结构本身与拆分前逐字一致：IconSegment.op 只有 'M'(起子路径) / 'L' / 'C' / 'Z'
// 四种。'S'/'Q'/'T' 在**解析时**就升阶成 'C' 了（SVG 允许描边平滑连接，但
// ImDrawList 的路径没有「反射上一个控制点」的入口）。
#include <imgui.h>

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

} // namespace shine::kit
