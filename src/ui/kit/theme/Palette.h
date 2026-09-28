#pragma once
// shine::theme::PaletteFor —— ColorToken → QPalette（换肤的另一半）
//
// 为什么必须有（源码事实，不推断）：
//  * 全局 QSS 只画被选择器命中的东西；QSS 之外的绘制路径读的是 QPalette ——
//    QSS 里的 palette(...) 关键字、未被规则命中的控件、原生子绘制（QComboBox 弹层、
//    QCalendarWidget、QLineEdit 清空按钮、QMessageBox/QFileDialog 内置按钮…）。
//  * 应用此前**只** setStyleSheet、从不 setPalette，这些路径拿的是系统浅色调色板：
//    深色主题下表现为「深底黑字」（首启向导第③步 Select 摘要 = `color: palette(text)`）。
//
// 换肤契约：QSS 与 QPalette 同源于一组 Token（ThemeService 同一次切换一起下发），
// 新增控件**不要**用内联 palette(...) 绕开这条路径 —— 颜色一律走 QSS %N 占位符。
#include "ui/kit/controls/WidgetCommon.h"
#include "ui/kit/theme/Token.h"

#include <QPalette>

#include <cstdint>

namespace shine::theme {

// Token 组 → 应用调色板（调色板只做兜底，不覆盖 QSS 已命中的颜色）
[[nodiscard]] inline QPalette PaletteFor(const ColorToken& c) {
    const auto col = [&c](std::uint32_t token) { return widgets::TokenQColor(token); };

    QPalette p;
    // 底 / 前景：与 QSS 的 %2 bg.surface、%9 text.primary 同一对取值
    p.setColor(QPalette::Window, col(c.bgSurface));
    p.setColor(QPalette::WindowText, col(c.textPrimary));
    p.setColor(QPalette::Base, col(c.bgPanel));       // 输入类底 = QSS %3
    p.setColor(QPalette::AlternateBase, col(c.bgElevated));
    p.setColor(QPalette::Button, col(c.bgPanel));
    p.setColor(QPalette::ButtonText, col(c.textPrimary));
    p.setColor(QPalette::Text, col(c.textPrimary));
    p.setColor(QPalette::ToolTipBase, col(c.bgElevated));
    p.setColor(QPalette::ToolTipText, col(c.textPrimary));
    p.setColor(QPalette::PlaceholderText, col(c.textMuted));
    p.setColor(QPalette::BrightText, col(c.accentPrimaryFg));
    // 选中态：选中背景与选中文字成对取，避免「亮底浅字」
    p.setColor(QPalette::Highlight, col(c.accentPrimary));
    p.setColor(QPalette::HighlightedText, col(c.accentPrimaryFg));
    p.setColor(QPalette::Link, col(c.accentPrimary));
    p.setColor(QPalette::LinkVisited, col(c.accentSecondary));
    // 3D/边框角色：原生控件画 bevel 时不再冒出系统灰
    p.setColor(QPalette::Light, col(c.lineNormal));
    p.setColor(QPalette::Midlight, col(c.lineNormal));
    p.setColor(QPalette::Mid, col(c.lineNormal));
    p.setColor(QPalette::Dark, col(c.lineStrong));
    p.setColor(QPalette::Shadow, col(c.bgVoid));
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    p.setColor(QPalette::Accent, col(c.accentPrimary));
#endif
    // 禁用组：QSS 的 QWidget:disabled 已给 %11，这里同源，保证原生控件也不回亮色
    p.setColor(QPalette::Disabled, QPalette::WindowText, col(c.textMuted));
    p.setColor(QPalette::Disabled, QPalette::Text, col(c.textMuted));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, col(c.textMuted));
    p.setColor(QPalette::Disabled, QPalette::PlaceholderText, col(c.textMuted));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, col(c.textMuted));
    return p;
}

} // namespace shine::theme
