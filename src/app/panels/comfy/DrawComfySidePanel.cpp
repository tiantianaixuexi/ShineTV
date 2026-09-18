// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉了参数默认值，声明在头里）。
#include "app/panels/comfy/DrawComfySidePanel.h"
#include "app/AppIncludes.h"

namespace shine::app {

void DrawComfySidePanel() {
    ImGui::TextDisabled("ComfyUI");
    ImGui::Separator();
    auto& session = comfy::ComfySession::Instance();
    ImGui::TextWrapped("%s", session.BaseUrl().c_str());
    DrawConnectionStatus();
    ImGui::Spacing();
    if (ImGui::Button("重连", ImVec2(-1, 0))) {
        session.Connect();
    }
    if (ImGui::Button("刷新 object_info", ImVec2(-1, 0))) {
        session.RefreshObjectInfo();
    }
    if (ImGui::Button("刷新队列", ImVec2(-1, 0))) {
        session.RefreshQueue();
    }
    if (ImGui::Button("中断任务", ImVec2(-1, 0))) {
        session.RequestInterruptCurrent();
    }
    if (ImGui::Button("释放显存", ImVec2(-1, 0))) {
        session.FreeVram([](comfy::OperationResult r) {
            log::Info("FreeVram: {}", r.ok ? "ok" : r.error);
        });
    }
    if (ImGui::Button("系统状态", ImVec2(-1, 0))) {
        session.FetchSystemStats([](comfy::SystemStatsResult r) {
            if (r.ok) {
                log::Info("VRAM free {:.2f} / {:.2f} GB ({})", r.FreeGb(), r.TotalGb(), r.deviceName);
            } else {
                log::Warn("system_stats: {}", r.error);
            }
        });
    }
    ImGui::Spacing();
    ImGui::TextDisabled("节点类：%d", session.ObjectInfoNodeCount());
    ImGui::TextDisabled("队列剩余：%d", session.Queue().QueueRemaining());
    ImGui::TextDisabled("clientId：%s", session.ClientId().c_str());

    // P3.0 S4/S9：忙碌详情 + 距上次事件 + 最近错误（可展开回溯）
    ImGui::Separator();
    const std::string_view busy = session.BusyDetail();
    ImGui::TextWrapped("状态：%.*s", static_cast<int>(busy.size()), busy.data());
    ImGui::TextDisabled("距上次事件：%lld ms", static_cast<long long>(session.SinceLastEvent().count()));
    const auto& err = session.LastErrorDetail();
    if (err.valid && ImGui::CollapsingHeader("最近错误")) {
        if (!err.exceptionType.empty()) {
            ImGui::TextWrapped("类型：%s", err.exceptionType.c_str());
        }
        if (!err.exceptionMessage.empty()) {
            ImGui::TextWrapped("消息：%s", err.exceptionMessage.c_str());
        }
        if (!err.nodeId.empty()) {
            ImGui::TextDisabled("节点：%s %s", err.nodeId.c_str(), err.nodeType.c_str());
        }
        if (!err.hint.empty()) {
            ImGui::TextWrapped("建议：%s", err.hint.c_str());
        }
        const size_t tbLines = std::min<size_t>(err.traceback.size(), 3);
        for (size_t i = 0; i < tbLines; ++i) {
            ImGui::TextDisabled("%s", err.traceback[i].c_str());
        }
        ImGui::TextDisabled("来源：%s", err.source.c_str());
    }
}

} // namespace shine::app
