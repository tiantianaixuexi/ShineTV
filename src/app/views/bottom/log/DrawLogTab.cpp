// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉了参数默认值，声明在头里）。
#include "app/views/bottom/log/DrawLogTab.h"
#include "app/AppIncludes.h"

namespace shine::app {

void DrawLogTab() {
    if (ImGui::SmallButton("清空")) {
        log::Clear();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu 条", log::LineCount());
    ImGui::BeginChild("##logscroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    static std::vector<log::Line> linesCache;
    static uint64_t linesVersion = 0;
    if (const uint64_t ver = log::Version(); ver != linesVersion) {
        linesCache = log::LinesSnapshot();
        linesVersion = ver;
    }
    const auto& lines = linesCache;
    const bool stickToBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.f;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(lines.size()));
    while (clipper.Step()) {
        for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n) {
            const auto& line = lines[static_cast<size_t>(n)];
            ImVec4 col(0.75f, 0.78f, 0.82f, 1.f);
            if (line.level == 1) col = ImVec4(1.f, 0.75f, 0.3f, 1.f);
            if (line.level == 2) col = ImVec4(1.f, 0.4f, 0.4f, 1.f);
            ImGui::TextColored(col, "%s", line.text.c_str());
        }
    }
    if (stickToBottom) {
        ImGui::SetScrollHereY(1.f);
    }
    ImGui::EndChild();
}

} // namespace shine::app
