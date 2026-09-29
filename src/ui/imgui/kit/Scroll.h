#pragma once
// shine::kit::ScrollRegion —— 滚动区域（直接用 ImGui::BeginChild）
//
// 为什么必须用 ImGui 自带的 BeginChild 而不是继续手写：
// 自绘控件只产出 draw call，**不产出裁剪、不产出滚动、不接管命中**。内容超出
// 区域时既不会裁也不会滚，鼠标事件还会穿透到下层。ImGui 的 child 窗口把这三件事
// 一次给全（裁剪矩形 + 滚动偏移 + 输入归属），这正是 demo 与官方示例的通用做法。
// 代价是我们拿不到 CSS 式自由定位 —— 但外壳的固定栅格本来就是绝对定位，
// child 只用来当「内容区」，不参与外壳布局。
//
// 用法：
//     {
//         ScrollRegion region("workspace", area);
//         if (region) { page.Draw(region.content(), draw); }
//     }  // 析构自动 EndChild
#pragma once

#include "ui/imgui/kit/Widgets.h"

#include <string>
#include <string_view>

namespace shine::kit {

class ScrollRegion {
public:
    // bounds 是区域的外框。height <= 0 时用 ImGui 的「撑满剩余」语义。
    ScrollRegion(std::string_view id, Rect bounds, bool borders = false,
                 bool horizontalScroll = false);
    ~ScrollRegion();

    ScrollRegion(const ScrollRegion&) = delete;
    ScrollRegion& operator=(const ScrollRegion&) = delete;

    // BeginChild 返回 false = 完全被裁掉（可跳过内容，但**必须**配对 EndChild，
    // 所以析构里无条件 End）。
    explicit operator bool() const { return visible_; }

    // 内容区：原点已含滚动偏移。y 给一个足够大的值（调用方按内容高度自定），
    // x 给满宽 —— 这样页面照常按绝对矩形画，超出部分由 child 负责裁与滚。
    [[nodiscard]] Rect content() const { return content_; }
    [[nodiscard]] float scrollY() const { return scrollY_; }
    [[nodiscard]] float maxScrollY() const { return maxScrollY_; }
    [[nodiscard]] bool atBottom() const { return scrollY_ >= maxScrollY_ - 1.0f; }
    void scrollToBottom();

private:
    bool visible_ = false;
    Rect content_;
    float scrollY_ = 0.0f;
    float maxScrollY_ = 0.0f;
    float bottomHeight_ = 0.0f;
};

} // namespace shine::kit
