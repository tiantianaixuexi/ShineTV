#pragma once
// theme token → CSS 色串。页面侧统一走这里，禁止各 View 复制 CssRgb。

#include "widget/controls/WidgetCommon.h"
#include "widget/theme/Token.h"

#include <QString>

#include <cstdint>

namespace shine::widget {

[[nodiscard]] inline QString CssRgb(std::uint32_t token) {
    return widgets::TokenQColor(token).name();
}

} // namespace shine::widget
