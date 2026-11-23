#include "ui/imgui/kit/Widget_Input.h"

#include "ui/imgui/kit/Anim.h"
#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Icon.h"
#include "ui/imgui/kit/Widget_Color.h"
#include "ui/imgui/kit/Widget_Core.h"

#include <misc/cpp/imgui_stdlib.h>
#include <string>

namespace shine::kit {

// ------------------------------------------------------------------ 9 Field
Rect Field(ImDrawList* draw, Rect bounds, std::string_view label, std::string_view help) {
    float y = bounds.min.y;
    if (!label.empty()) {
        ImFont* font = FontBoldAt(12.0f);
        draw->AddText(font, 12.0f, ImVec2(bounds.min.x, y), ColorTextSecondary(), label.data(),
                      label.data() + label.size());
        y += 18.0f;
    }
    if (!help.empty()) {
        y += 6.0f;
        ImFont* font = FontAt(12.0f);
        draw->AddText(font, 12.0f, ImVec2(bounds.min.x, y), ColorTextMuted(), help.data(),
                      help.data() + help.size());
        y += 18.0f;
    }
    return Rect{ImVec2(bounds.min.x, y), bounds.max};
}

// ------------------------------------------------------------------ 10-12 Input
bool Input(ImDrawList* draw, Rect bounds, std::string& value, std::string_view placeholder,
           std::string_view id, ImGuiInputTextFlags flags) {
    ImGui::SetCursorScreenPos(bounds.min);
    ImGui::PushID(std::string(id).c_str());
    ImGui::SetNextItemWidth(bounds.width());
    // ⚠️ 必须 PushFont：ImGui 的 InputText 用 io.FontDefault，不推的话输入框里的
    // 文字会用默认字体（不是我们的 13px 雅黑），中文与行高都会偏。
    ImGui::PushFont(FontAt(theme::font::kBody), theme::font::kBody);
    // h30 = 13px 字高 + 上下各 7.5 padding + 上下各 1px 边框
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 7.5f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ColorFillMuted());
    ImGui::PushStyleColor(ImGuiCol_Border, ColorLineNormal());
    ImGui::PushStyleColor(ImGuiCol_Text, ColorText());
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ColorAccentDim());
    const std::string hint(placeholder);
    const bool changed = ImGui::InputTextWithHint("##value", hint.c_str(), &value,
                                                 flags | ImGuiInputTextFlags_EnterReturnsTrue);
    const bool focused = ImGui::IsItemFocused();
    const bool hovered = ImGui::IsItemHovered();
    // 补间 id 用 InputText 自己的 item id：与 PushID(id) + "##value" 同串同栈 ⇒
    // 每帧稳定，且与各 Hit 的 tid 同一 id 空间、不同串不撞车（Input 没有 Hit）。
    const ImGuiID animId = ImGui::GetItemID();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    ImGui::PopFont();
    ImGui::PopID();

    // 焦点环 = CSS 的 0 0 0 3px accent-dim 外环
    // 补间版：聚焦环（ch0 主色）与 hover 描边（ch2 描边）的硬切换各自走色补间
    // （kDurFast，含 alpha）。目标式与原 if/else-if 逐值相同：settle 在「不画」的
    // 一侧目标是全透明，DrawRoundRect 对 alpha=0 的 border 自己就跳过 ⇒ 与原样
    // 逐像素相同。聚焦/悬停互斥的两态在切换过程中短暂交叠淡入淡出，属补间过程。
    const ImU32 ringNow = TransitionColorTo(
        animId, 0, focused ? ColorOf(theme::CurrentDerived().inputFocusRing) : ImU32(0), kDurFast);
    const ImU32 hoverBorderNow = TransitionColorTo(
        animId, 2, (!focused && hovered) ? ColorLineStrong() : ImU32(0), kDurFast);
    DrawRoundRect(draw, bounds.min - ImVec2(2.0f, 2.0f), bounds.max + ImVec2(2.0f, 2.0f), 8.0f, 0,
                  ringNow, 3.0f);
    DrawRoundRect(draw, bounds.min, bounds.max, 6.0f, 0, hoverBorderNow, 1.0f);
    return changed;
}

bool TextArea(ImDrawList* draw, Rect bounds, std::string& value, int lines, std::string_view id) {
    ImGui::SetCursorScreenPos(bounds.min);
    ImGui::PushID(std::string(id).c_str());
    ImGui::PushFont(FontAt(theme::font::kBody), theme::font::kBody);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ColorFillMuted());
    ImGui::PushStyleColor(ImGuiCol_Border, ColorLineNormal());
    ImGui::PushStyleColor(ImGuiCol_Text, ColorText());
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ColorAccentDim());
    // 行高 1.6：ImGui 的 multiline 用 FontSize + FramePadding*2，12.5*1.6 ≈ 20
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 6.0f));
    const bool changed =
        ImGui::InputTextMultiline("##value", &value,
                                  ImVec2(bounds.width(), 16.0f + lines * 20.0f),
                                  ImGuiInputTextFlags_AllowTabInput);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::PopFont();
    ImGui::PopID();
    (void)draw;
    return changed;
}

bool Select(ImDrawList* draw, Rect bounds, const std::vector<std::string>& options, int& index,
            std::string_view id) {
    const bool hasSelection = index >= 0 && static_cast<std::size_t>(index) < options.size();
    const char* preview = hasSelection ? options[static_cast<std::size_t>(index)].c_str() : "请选择";
    // ### 隐藏标签，并让 ImGui 继续用原来的 id 计算控件标识。
    std::string label = "###";
    label.append(id);
    bool changed = false;
    ImGui::SetCursorScreenPos(bounds.min);
    ImGui::SetNextItemWidth(bounds.width());
    ImGui::PushFont(FontAt(theme::font::kBody), theme::font::kBody);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 7.5f));
    ImGui::BeginDisabled(options.empty());
    if (ImGui::BeginCombo(label.c_str(), preview)) {
        for (int i = 0; i < static_cast<int>(options.size()); ++i) {
            const bool selected = (i == index);
            if (ImGui::Selectable(options[static_cast<std::size_t>(i)].c_str(), selected)) {
                index = i;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::PopFont();
    (void)draw;
    return changed;
}

// ------------------------------------------------------------------ 13 Switch
bool Switch(ImDrawList* draw, Rect bounds, bool on, std::string_view id) {
    const Hit hit = detail::HitTestItem(bounds, id);
    const float height = bounds.height();
    const float radius = height * 0.5f;
    // 补间版：开关底色（ch0）/描边（ch2）/滑块色（ch3）的硬切换走色补间（kDurFast）；
    // 三条目标式与原硬切换逐字相同。滑块位移是 ch4 结构滑动（kDurBase，ease-out）。
    DrawRoundRect(
        draw, bounds.min, bounds.max, radius,
        TransitionColorTo(hit.tid, 0,
                          on ? ColorAccent() : (hit.hovered ? ColorFillHover() : ColorFillMuted()),
                          kDurFast),
        TransitionColorTo(hit.tid, 2, on ? ColorAccent() : ColorLineNormal(), kDurFast), 1.0f);
    const float knob = height - 6.0f;
    const float travel = bounds.width() - knob - 6.0f;
    // settle 后 t 恰为 1/0 ⇒ cx 与原算式逐值相同。
    const float t = TransitionFloatTo(hit.tid, 4, on ? 1.0f : 0.0f, kDurBase);
    const float cx = bounds.min.x + 3.0f + travel * t;
    DrawRoundRect(draw, ImVec2(cx, bounds.min.y + 3.0f), ImVec2(cx + knob, bounds.max.y - 3.0f),
                  knob * 0.5f,
                  TransitionColorTo(hit.tid, 3, on ? ColorAccentFg() : ColorTextMuted(), kDurFast), 0,
                  0.0f);
    return hit.clicked;
}

// ------------------------------------------------------------------ 14 Checkbox
bool Checkbox(ImDrawList* draw, Rect bounds, bool on, std::string_view label,
              std::string_view id) {
    const Rect box = RectAt(bounds.min.x, 0.5f * (bounds.min.y + bounds.max.y) - 7.5f, 15.0f, 15.0f);
    const Hit hit = detail::HitTestItem(bounds, id);
    if (on) {
        DrawRoundRect(draw, box.min, box.max, 4.0f, ColorAccent());
        DrawIcon(draw, "check", ImVec2(box.min.x + 2.6f, box.min.y + 2.6f), 10.0f, ColorAccentFg(),
                 3.4f);
    } else {
        DrawRoundRect(draw, box.min, box.max, 4.0f, 0, ColorLineStrong(), 1.5f);
    }
    if (!label.empty()) {
        ImFont* font = FontAt(12.5f);
        // 补间版：标签 hover 文字色硬切换走 ch3 色补间（kDurFast）。勾选框本体是
        // 结构性 if/else（含勾图标），保持离散不动。
        draw->AddText(font, 12.5f, ImVec2(box.max.x + 8.0f, box.min.y + 1.0f),
                      TransitionColorTo(hit.tid, 3,
                                        hit.hovered ? ColorText() : ColorTextSecondary(), kDurFast),
                      label.data(), label.data() + label.size());
    }
    return hit.clicked;
}

} // namespace shine::kit
