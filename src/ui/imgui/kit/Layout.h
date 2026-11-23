#pragma once
// shine::kit::Layout —— 布局推进原语（R4，refactor/记忆.md（原 记忆.md（原 component-reuse.md）） §4.2 三件套之二）
//
// 收编页面层的手写步进：`y += 30.0f` 这类「元素高 + 间距」各自写一份的算式
// （2026-10-01 实测 11 个文件 49 处）。它们是「静默截断」「热区叠行」两类
// 缺陷的共同源头 —— 高度一改，推进值不会自己跟着动。
//
// 三个推进器**只算矩形、不画任何东西**：画仍由各控件自己完成 —— 布局与绘制
// 分离，几何才有唯一出处。它们也不是 ImGui 线性布局：全部吃绝对矩形，
// 与本仓「外壳自绘栅格」的约定一致（见 Widgets.h 顶部）。
//
// ⚠️ **像素不变契约**：`Next(h, gap)` = 「在 y 处取 h 高整行矩形，然后
//    y += h + gap」—— 与它替换的手写两句逐位等价。收编时把原式
//    `y += X`（前面画了 H 高的元素）分解成 `Next(H, X − H)`，分解错了
//    当帧就偏，取证抓得到。**裸段间距**（`y += 22` 这类不是「元素 + 间距」的）
//    不要硬分解成小数 gap —— 用 `Advance(dy)` 原样保留。
//
// 三件套的第三件（ScrollList，内置 clipper）是 R5，不在这里。
#pragma once

#include "ui/imgui/kit/Widget_Core.h"

namespace shine::kit {

// 竖向堆叠游标：一列元素依次向下排。
struct Stack {
    Rect area;  // 内容区（Next 取整行宽）
    float y;    // 下一个元素的顶
    float gap;  // 缺省间距（逐个覆盖见 Next）

    explicit Stack(Rect a, float gap = 0.0f) : area(a), y(a.min.y), gap(gap) {}

    // 取下一个 h 高的**整行**矩形并推进 y += h + gap。
    // gapOverride >= 0 时用它代替缺省间距（-1 = 用缺省）。
    [[nodiscard]] Rect Next(float h, float gapOverride = -1.0f) {
        const Rect r = RectAt(area.min.x, y, area.width(), h);
        y += h + (gapOverride >= 0.0f ? gapOverride : gap);
        return r;
    }
    // 不放元素、只推进（裸段间距 / padding 专用；不要用它替代 Next）。
    void Advance(float dy) { y += dy; }
    // 剩余高度（到内容区底）：排不定高内容前先问，防「写死高度溢出到下一段」
    // —— 重叠的静默源头之一（溢出的那段会盖住下一个区块，截图正常、肉眼才发现）。
    [[nodiscard]] float Remain() const { return area.max.y - y; }
    // 直接落到绝对 y：只在「另一个游标（Flow 的折行行顶）把后续位置决定了」的
    // 交接处用 —— 常规推进走 Next / Advance。
    void Seek(float absY) { y = absY; }
    [[nodiscard]] float Cur() const { return y; }
};

// 横向排布游标：一行元素依次向右排。
struct Row {
    Rect area;  // 行区（Next 的高从这里取不了 —— 高是每个元素自己的）
    float x;
    float gap;

    explicit Row(Rect a, float gap = 0.0f) : area(a), x(a.min.x), gap(gap) {}

    // 取下一个 w×h 矩形并推进 x += w + gap。
    [[nodiscard]] Rect Next(float w, float h, float gapOverride = -1.0f) {
        const Rect r = RectAt(x, area.min.y, w, h);
        x += w + (gapOverride >= 0.0f ? gapOverride : gap);
        return r;
    }
    // 剩余宽度（与 Stack::Remain 同语义，横向版）。
    [[nodiscard]] float Remain() const { return area.max.x - x; }
    [[nodiscard]] float Cur() const { return x; }
};

// 流式游标（flex-wrap）：放不下就折行。折行判式与原先各页手写的
// `if (x + w > 右界) { x = 左界; y += h + vGap; }` 逐字相同 —— 差异只允许
// 出在「不再抄 N 份」，不允许出在判式上。
//
// 与 Stack/Row 不同，Flow 通常**不**独占推进权：一组标签前后往往还有别的
// 段落，组间距离由调用方用 `flow.y`（当前行顶）接着算 —— 折行次数是运行态，
// 组间距必须挂在折行后的行顶上，不能预 advance（预了就会把折行丢了）。
struct Flow {
    Rect area;
    float x, y;
    float hGap, vGap;

    explicit Flow(Rect a, float hGap, float vGap)
        : area(a), x(a.min.x), y(a.min.y), hGap(hGap), vGap(vGap) {}

    // 放一个 w×h 的元素：右边放不下就折到下一行（行推进 = h + vGap）。
    [[nodiscard]] Rect Place(float w, float h) {
        if (x + w > area.max.x) {
            x = area.min.x;
            y += h + vGap;
        }
        const Rect r = RectAt(x, y, w, h);
        x += w + hGap;
        return r;
    }
    // 折行后最后一行的顶（组间距从它起算；Place 内部的 y 只在折行时变）。
    [[nodiscard]] float Cur() const { return y; }
};

} // namespace shine::kit
