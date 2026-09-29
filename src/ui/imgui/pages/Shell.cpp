#include "ui/imgui/pages/Shell.h"

#include "ui/imgui/theme/Theme.h"

#include <imgui.h>

namespace shine::pages {

void Shell::DrawFrame(float dt) {
    lastDelta_ = dt;

    // 外壳自绘，ImGui 根窗口只做全屏画布：不加标题栏、不加 padding、不参与布局。
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    constexpr int kNoDecoration = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##shine-root", nullptr, kNoDecoration);

    // P0.4 判据：出窗口 + 显示一个 ImGui 窗口。外壳在 P4 接管这一块。
    ImGui::SetNextWindowSize(ImVec2(360, 140), ImGuiCond_Always);
    ImGui::SetNextWindowPos(
        ImVec2((ImGui::GetIO().DisplaySize.x - 360.0f) * 0.5f,
               (ImGui::GetIO().DisplaySize.y - 140.0f) * 0.5f),
        ImGuiCond_Always);
    if (ImGui::Begin("ShineTV Studio", nullptr,
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        const auto& c = theme::Current();
        ImGui::TextUnformatted(std::string(theme::ThemeDisplayName(theme::CurrentThemeId())).c_str());
        ImGui::TextUnformatted("ImGui 1.93 / D3D11");
        ImGui::Separator();
        ImGui::TextColored(theme::Rgba(c.statusOk), "P0.4 host OK");
        ImGui::End();
    }

    ImGui::End();
}

} // namespace shine::pages
