#pragma once
// shine::kit::ScrollList —— 等行距长列表（R5，refactor/记忆.md（原 记忆.md（原 component-reuse.md）） §4.2 三件套之三）
//
// 收编页面层「自己算内容高度 + 全量画行」的滚动列表。它对应 ImGui demo 里
// `BeginListBox + ImGuiListClipper` 免费送的两件事，而这正是自绘路径丢掉的：
//
//   1. **clipper 等价**：只画与可视区相交的行（`firstRow()..lastRow()` 半开区间），
//      屏外一行都不提交 —— 1 万行也不卡，命中注册也随之只发生在可见行。
//   2. **内容高度自动**：`rowCount × rowPitch + tailH` 在构造期上报给 ScrollRegion，
//      调用方**不用再算、不可能忘调** `setContentHeight` —— 本仓两条「静默截断」
//      记录与 `find-silent-truncation` 扫描器盯的正是「高度没上报 / 上报错」这一类。
//
// ⚠️ **像素不变契约**：可见行的矩形 = `RowRect(i)`，与手写的
//    `Rect{inner.min.x, inner.min.y + i * pitch, ...}` 逐位一致；屏外的行原先
//    也画了、只是被 child 裁掉 —— 不提交它们不改变任何一个像素。
//
// ⚠️ 等行距是前提。行高不一的列表（时间线、树）不走这里 —— 硬套等于换一种
//    方式手算高度，回到老问题。
//
// 日志流**不收**这里：它是刻意保留的尾部窗口（跟随最新 vs 可上翻的取舍，
// `Shell_Dock.cpp` 有 `scan:allow-silent-truncation` 标注），不是漏收。
#pragma once

#include "ui/imgui/kit/Scroll.h"

#include <algorithm>
#include <cmath>

namespace shine::kit {

class ScrollList {
public:
    // bounds = 视口外框；rowCount × rowPitch + tailH = 内容高度（自动上报）。
    // tailH 是行区之后那段（页脚 / 光标行等），用 `TailRect()` 取它的矩形。
    // rowPitch 是行距（下一行顶 − 本行顶），rowH 是行矩形高（两者可以不同：
    // 队列行高 30、行距 32 —— 中间留 2px 空隙）。
    ScrollList(std::string_view id, Rect bounds, int rowCount, float rowPitch, float rowH,
               float tailH = 0.0f, bool noMouseInputs = OverlayMouseGate())
        : region_(id, bounds, /*borders=*/false, /*horizontalScroll=*/false, noMouseInputs),
          rowCount_(rowCount), rowPitch_(rowPitch), rowH_(rowH), tailH_(tailH) {
        if (region_) {
            // 可见窗口：滚动偏移 scrollY（= 视口顶 − content().min.y，后者已含偏移）
            // 到 scrollY + 可视高。行 i 可见 ⇔ [i×pitch, (i+1)×pitch) 与之相交。
            const float scrollY = bounds.min.y - region_.content().min.y;
            const float viewH = region_.content().height();
            firstRow_ = std::clamp(static_cast<int>(scrollY / rowPitch_), 0, rowCount_);
            lastRow_ = std::clamp(
                static_cast<int>(std::ceil((scrollY + viewH) / rowPitch_)), firstRow_, rowCount_);
            region_.setContentHeight(rowCount_ * rowPitch_ + tailH_);
        }
    }
    ~ScrollList() = default;

    ScrollList(const ScrollList&) = delete;
    ScrollList& operator=(const ScrollList&) = delete;

    explicit operator bool() const { return static_cast<bool>(region_); }

    // 可见行区间 [firstRow, lastRow)。**只循环这一段**，屏外行不要画。
    [[nodiscard]] int firstRow() const { return firstRow_; }
    [[nodiscard]] int lastRow() const { return lastRow_; }

    [[nodiscard]] Rect content() const { return region_.content(); }
    // ⚠️ 必须用这条 draw list（child 自己的），否则裁剪无效 —— 见 Scroll.h。
    [[nodiscard]] ImDrawList* drawList() const { return region_.drawList(); }

    // 第 i 行的矩形（内容坐标，原点已含滚动偏移）。
    [[nodiscard]] Rect RowRect(int row) const {
        const Rect inner = region_.content();
        return RectAt(inner.min.x, inner.min.y + static_cast<float>(row) * rowPitch_,
                      inner.width(), rowH_);
    }
    // 行区之后 tailH 高的那块（页脚 / 提示行）。
    [[nodiscard]] Rect TailRect() const {
        const Rect inner = region_.content();
        return RectAt(inner.min.x, inner.min.y + static_cast<float>(rowCount_) * rowPitch_,
                      inner.width(), tailH_);
    }

    // 透传给需要粘性滚动 / 读数的调用方（取证）。
    [[nodiscard]] float scrollY() const { return region_.scrollY(); }
    static void RequestScrollBottom(std::string_view id) {
        ScrollRegion::RequestScrollBottom(id);
    }

private:
    ScrollRegion region_;
    int rowCount_ = 0;
    int firstRow_ = 0;
    int lastRow_ = 0;
    float rowPitch_ = 0.0f;
    float rowH_ = 0.0f;
    float tailH_ = 0.0f;
};

} // namespace shine::kit
