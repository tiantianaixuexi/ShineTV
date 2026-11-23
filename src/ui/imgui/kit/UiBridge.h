#pragma once
// shine::kit::UiBridge —— 原生 ImGui 控件的**最小**上下文桥
//
// 背景：页面层原先全部走绝对矩形 + 自绘（kit::Widget_*）。改走 ImGui 原生控件后，
// 只剩两件小事需要桥：
//   * 字体档位：theme::font 是 CSS 像素档（多为半点），而原生控件走的是
//     `ImGui::PushFont(font, size)`（1.92+ 动态字号）。见 Fonts.h 顶部第 4 条：
//     上下文路径的 size 会被 IM_ROUND 取整 —— 这是「放弃逐像素」后的可接受偏差。
//   * 卡片容器：`BeginChild` 的自动高度 + 面板底色 + 边框。
//
// ⚠️ **颜色不在这里推**。`theme::ApplyTheme()` 已经把 31 个 ColorToken 铺进
//    ImGuiStyle（theme/Theme.cpp:230 起，含 Button / Header / Tab / Table / Text /
//    Scrollbar 全套）。在这里再推一份就是第二个真相源 —— 切主题时两边会分叉。
//
// ⚠️ 本文件**不定义任何控件**。控件用 ImGui 自己的（Button / Selectable / InputText /
//    BeginTable / TreeNodeEx / BeginTabBar…）。这里只解决「字体与卡片壳」。

#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Widget_Color.h"

#include <imgui.h>

namespace shine::kit {

// 把 theme::font 的档位与字重喂给当前 ImGui 上下文。
// bold = msyhbd.ttc（取不到时退回常规字重，只偏细）；mono = 等宽字面。
inline void PushUiFont(float pixelSize, bool bold = false, bool mono = false) {
    ImFont* font = mono ? MonoAt(pixelSize) : (bold ? FontBoldAt(pixelSize) : FontAt(pixelSize));
    ImGui::PushFont(font, pixelSize);
}

inline void PopUiFont() { ImGui::PopFont(); }

// 卡片容器。**无论 BeginChild 返回什么，都必须调用 EndUiCard()。**
//
// AutoResizeY：高度由内容决定 —— 这是相对旧实现最直接的收益，页面不再手写
// 「这张卡 148 / 130 / 240」这样的高度表（旧 Gallery.cpp 里那张 entries[12] 表）。
inline bool BeginUiCard(const char* id, float width = 0.0f) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ColorPanel());
    ImGui::PushStyleColor(ImGuiCol_Border, ColorLineSubtle());
    const bool visible =
        ImGui::BeginChild(id, ImVec2(width, 0.0f),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::PopStyleColor(2);
    return visible;
}

inline void EndUiCard() { ImGui::EndChild(); }

// 卡片内边距（.card 的 14px）。Begin / End 必须成对，且 End 必须在 EndUiCard 之前。
inline void BeginUiCardBody() { ImGui::Indent(14.0f); }
inline void EndUiCardBody() {
    ImGui::Unindent(14.0f);
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
}

} // namespace shine::kit