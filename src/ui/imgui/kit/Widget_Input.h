#pragma once
// shine::kit::Widget_Input —— 输入系（Field / Input / TextArea / Select / Switch / Checkbox）
//
// 从 kit/Widgets.cpp 拆出。与其它族最大的不同：Input / TextArea / Select 背后是
// **真正的 ImGui 控件**（要维护 ID 栈与焦点），Switch / Checkbox 才是纯自绘。

#include "ui/imgui/kit/Widget_Core.h"

#include <string>
#include <string_view>
#include <vector>

namespace shine::kit {

// ---- 9. Field（UI.jsx:103）----
// 竖排 gap6；标签 12px/600 secondary；help 12px muted。返回控件该放的位置。
Rect Field(ImDrawList* draw, Rect bounds, std::string_view label, std::string_view help);

// ---- 10-12. Input / TextArea / Select ----
// Input h30 pad 0 10 r6 fill-muted 1px line-normal；
// focus = line-focus 边 + 0 0 0 3px accent-dim 外环
bool Input(ImDrawList* draw, Rect bounds, std::string& value, std::string_view placeholder,
           std::string_view id);
bool TextArea(ImDrawList* draw, Rect bounds, std::string& value, int lines, std::string_view id);
bool Select(ImDrawList* draw, Rect bounds, const std::vector<std::string>& options,
            int& index, std::string_view id);

// ---- 13. Switch（UI.jsx:131）----
// 34×19 r-pill；旋钮 13×13 位移 15px
bool Switch(ImDrawList* draw, Rect bounds, bool on, std::string_view id);

// ---- 14. Checkbox（UI.jsx:133）----
// 盒 15×15 r4 1.5px 边 + 内联对勾（accent-fg）
bool Checkbox(ImDrawList* draw, Rect bounds, bool on, std::string_view label, std::string_view id);

} // namespace shine::kit
