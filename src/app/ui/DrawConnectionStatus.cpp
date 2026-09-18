// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉了参数默认值，声明在头里）。
#include "app/ui/DrawConnectionStatus.h"
#include "app/AppIncludes.h"

namespace shine::app {

void DrawConnectionStatus(const char* prefix) {
    auto& session = comfy::ComfySession::Instance();
    const auto st = session.State();
    const ImVec4 col = StateColor(st);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float r = 5.f;
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, p.y + r + 2.f), r,
                                                ImGui::ColorConvertFloat4ToU32(col));
    ImGui::Dummy(ImVec2(r * 2 + 6, r * 2 + 4));
    ImGui::SameLine();
    // P3.0 S9：状态文案统一走 HealthSummary（"已连接 · 执行中 · 节点 X" / "未连接（原因）"）——
    // 任何位置都不得把"执行中"写成"卡死/断开"（`Doc/RULES-COMFY.md` §12.4）
    const std::string_view health = session.HealthSummary();
    ImGui::Text("%s%.*s", prefix, static_cast<int>(health.size()), health.data());
    if (st == comfy::ConnectionState::Connected) {
        if (ImGui::IsItemHovered()) {
            const std::string_view busy = session.BusyDetail();
            ImGui::SetTooltip("%.*s\n距上次事件 %lld ms", static_cast<int>(busy.size()), busy.data(),
                              static_cast<long long>(session.SinceLastEvent().count()));
        }
    } else if (!session.LastError().empty()) {
        ImGui::TextDisabled("%s", session.LastError().c_str());
    }
}

} // namespace shine::app
