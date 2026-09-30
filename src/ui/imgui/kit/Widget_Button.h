#pragma once
// shine::kit::Widget_Button —— Button 系（Button / IconButton）
//
// 从 kit/Widgets.cpp 拆出。

#include "ui/imgui/kit/Widget_Core.h"

#include <string_view>

namespace shine::kit {

// ---- 1. Button（UI.jsx:7）----
// md h30 / pad 0 14 / r6 / 13px / 600；sm h24 pad 0 10 12px；lg h36 pad 0 20 14px r10
// 图标 15（sm 13）；:active scale(.97)；disabled opacity .45
enum class ButtonVariant { Primary, Secondary, Ghost, Danger };
enum class ButtonSize { Small, Medium, Large };

struct ButtonSpec {
    ButtonVariant variant = ButtonVariant::Ghost;
    ButtonSize size = ButtonSize::Medium;
    std::string_view icon;      // 空 = 无图标
    bool loading = false;       // 转圈占位 13×13
    bool disabled = false;
};

[[nodiscard]] float ButtonHeight(ButtonSize size);
[[nodiscard]] float ButtonWidth(ButtonSize size, float iconWidth, float textWidth);
[[nodiscard]] float ButtonPadding(ButtonSize size);
// 返回 true = 本帧被点击（disabled / loading 时恒 false）。
bool Button(ImDrawList* draw, Rect bounds, std::string_view label, const ButtonSpec& spec,
            std::string_view id);

// ---- 2. IconBtn（UI.jsx:17）----
// 28×28 r6（sm 22×22），图标 16（sm 13）；hover fill-hover；active fill-selected+accent
bool IconButton(ImDrawList* draw, Rect bounds, std::string_view icon, bool active, bool disabled,
                std::string_view id, std::string_view tip = {});

} // namespace shine::kit
