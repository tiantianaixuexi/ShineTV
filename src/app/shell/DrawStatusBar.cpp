// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉了参数默认值，声明在头里）。
#include "app/shell/DrawStatusBar.h"
#include "app/AppIncludes.h"

#include <string>
#include "gallery/Gallery.h" // G-S5：图库统计（前缀「图片」）

namespace shine::app {

void DrawStatusBar(float width, float height) {
    const auto& tc = theme::Current();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 bg = ImGui::ColorConvertFloat4ToU32(ImVec4(tc.menuBar[0], tc.menuBar[1], tc.menuBar[2], 1.f));
    dl->AddRectFilled(p0, ImVec2(p0.x + width, p0.y + height), bg);

    ImGui::BeginChild("##status_bar", ImVec2(width, height), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                          ImGuiWindowFlags_NoBackground);
    ImGui::SetCursorPosY(2.f);
    ImGui::SetCursorPosX(8.f);

    auto& session = comfy::ComfySession::Instance();
    const ImVec4 scol = StateColor(session.State());
    const ImVec2 sp = ImGui::GetCursorScreenPos();
    dl->AddCircleFilled(ImVec2(sp.x + 5, sp.y + 8), 4.f, ImGui::ColorConvertFloat4ToU32(scol));
    ImGui::Dummy(ImVec2(14, 14));
    ImGui::SameLine();
    const std::string_view health = session.HealthSummary();
    ImGui::Text("%.*s", static_cast<int>(health.size()), health.data());

    ImGui::SameLine();
    ImGui::TextDisabled("·");
    ImGui::SameLine();
    ImGui::Text("队列 %d", session.Queue().QueueRemaining());

    ImGui::SameLine();
    ImGui::TextDisabled("·");
    ImGui::SameLine();
    const std::string_view busy = session.BusyDetail();
    ImGui::Text("%.*s", static_cast<int>(busy.size()), busy.data());

    ImGui::SameLine();
    ImGui::TextDisabled("·");
    ImGui::SameLine();
    ImGui::Text("图 %zu/%zu", graph::NodeCount(), graph::ConnectionCount());

    ImGui::SameLine();
    ImGui::TextDisabled("·");
    ImGui::SameLine();
    ImGui::Text("缩放 %.0f%%", graph::Zoom() * 100.f);

    const size_t sel = graph::SelectedCount();
    ImGui::SameLine();
    ImGui::TextDisabled("·");
    ImGui::SameLine();
    if (sel == 0) {
        ImGui::TextDisabled("未选中");
    } else {
        const auto nodes = graph::SelectedNodes();
        if (sel == 1 && !nodes.empty()) {
            ImGui::Text("选中 %s", nodes[0].name.c_str());
        } else {
            ImGui::Text("选中 %zu", sel);
        }
    }

    // P3.5：最近提交的 promptId + 运行拦截/提交错误
    const std::string lastPrompt = graph::LastPromptId();
    if (!lastPrompt.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("·");
        ImGui::SameLine();
        ImGui::Text("prompt %s", lastPrompt.substr(0, std::min<std::size_t>(8, lastPrompt.size())).c_str());
    }
    const auto& errors = graph::LastCompileErrors();
    if (!errors.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("·");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.40f, 1.0f), "错误 %zu", errors.size());
    }

    // G-S5：图库统计（前缀用「图片」，与上面那个「图」= 节点图区分）
    ImGui::SameLine();
    ImGui::TextDisabled("·");
    ImGui::SameLine();
    ImGui::Text("图片 %zu", ::shine::gallery::ItemCount());
    if (::shine::gallery::State().scanning) {
        ImGui::SameLine();
        ImGui::TextDisabled("（扫描中）");
    }

    // 右侧：主题 + 版本（单一来源 Settings.appVersion / SHINE_VERSION）
    const std::string ver = std::string("ShineTV ") + Settings().appVersion;
    const theme::ThemePreset* preset = theme::FindPreset(Settings().themeId);
    const char* themeName = preset ? preset->name.c_str() : Settings().themeId.c_str();
    const float rightW = ImGui::CalcTextSize(ver.c_str()).x + ImGui::CalcTextSize(themeName).x + 48.f;
    ImGui::SameLine(std::max(80.f, width - rightW));
    ImGui::TextDisabled("%s", themeName);
    ImGui::SameLine();
    ImGui::TextDisabled("·");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ver.c_str());

    ImGui::EndChild();
}

} // namespace shine::app
