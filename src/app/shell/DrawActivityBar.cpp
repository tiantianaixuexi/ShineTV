// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉了参数默认值，声明在头里）。
#include "app/shell/DrawActivityBar.h"
#include "app/AppIncludes.h"

namespace shine::app {

void DrawActivityBar(float height) {
    const float w = kActivityBarWidth;
    const float h = height;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 活动栏背景：比侧栏更深一档
    const auto& tc = theme::Current();
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h),
                      ImGui::ColorConvertFloat4ToU32(ImVec4(tc.titleBar[0], tc.titleBar[1],
                                                             tc.titleBar[2], 1.f)));

    ImGui::BeginChild("##activity_bar", ImVec2(w, h), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                          ImGuiWindowFlags_NoBackground);

    // 顶部预留一点，图标垂直排布
    ImGui::Dummy(ImVec2(0, 8));

    const float btn = 40.f;
    const float pad = (w - btn) * 0.5f;

    for (const auto& item : Activities()) {
        ImGui::PushID(static_cast<int>(item.id));
        ImGui::SetCursorPosX(pad);
        const bool active = (State().sideView == item.id) && State().sideOpen;
        const auto& themeColors = theme::Current();
        const ImVec4 accent(themeColors.accent[0], themeColors.accent[1],
                            themeColors.accent[2], 1.f);
        const ImVec4 textCol = active ? accent : ImVec4(0.62f, 0.66f, 0.72f, 1.f);

        // 左侧指示条（VS Code 风格）
        if (active) {
            const ImVec2 ip = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(ImVec2(p0.x, ip.y), ImVec2(p0.x + 2.5f, ip.y + btn),
                              ImGui::ColorConvertFloat4ToU32(accent));
        }

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.12f));
        ImGui::PushStyleColor(ImGuiCol_Text, textCol);
        if (ImGui::Button(item.icon, ImVec2(btn, btn))) {
            if (State().sideView == item.id) {
                State().sideOpen = !State().sideOpen; // 再点一次收起/展开侧栏
            } else {
                State().sideView = item.id;
                State().sideOpen = true;
            }
            // 点「小说」时把中央区切到「小说」页（连点也刷新焦点）
            if (item.id == SideView::Novel) {
                State().focusWindow = FocusWindow::Novel;
                State().focusFrames = 8;
            }
        }
        ImGui::PopStyleColor(4);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("%s", item.title);
        }
        ImGui::PopID();
    }

    // 底部：设置
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - btn - 10.f);
    ImGui::SetCursorPosX(pad);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.12f));
    if (ImGui::Button("⚙", ImVec2(btn, btn))) {
        State().showSettings = true;
    }
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("设置");
    }

    ImGui::EndChild();
}

} // namespace shine::app
