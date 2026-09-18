// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉了参数默认值，声明在头里）。
#include "app/views/bottom/queue/DrawQueueTab.h"
#include "app/AppIncludes.h"

namespace shine::app {

void DrawQueueTab() {
    auto& session = comfy::ComfySession::Instance();
    const std::string lastPrompt = graph::LastPromptId();
    if (!lastPrompt.empty()) {
        ImGui::TextDisabled("最近提交 prompt：%s", lastPrompt.c_str());
        ImGui::Separator();
    }
    if (ImGui::SmallButton("刷新")) {
        session.RefreshQueue();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("中断")) {
        session.RequestInterruptCurrent();   // 已中断 ≠ 失败（§12.2）
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("释放显存")) {
        session.FreeVram([](comfy::OperationResult r) {
            log::Info("FreeVram: {}", r.ok ? "ok" : r.error);
        });
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("系统状态")) {
        session.FetchSystemStats([](comfy::SystemStatsResult r) {
            if (r.ok) {
                log::Info("VRAM free {:.2f} / {:.2f} GB ({})", r.FreeGb(), r.TotalGb(), r.deviceName);
            } else {
                log::Warn("system_stats: {}", r.error);
            }
        });
    }
    ImGui::SameLine();
    DrawConnectionStatus();

    const auto rows = session.Queue().Snapshot();
    if (ImGui::BeginTable("queue_table", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 0))) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("工作流/状态");
        ImGui::TableSetupColumn("节点", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("进度", ImGuiTableColumnFlags_WidthFixed, 140);
        ImGui::TableSetupColumn("状态", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();
        if (rows.empty()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("—");
            ImGui::TableNextColumn();
            ImGui::TextDisabled("暂无任务");
            ImGui::TableNextColumn();
            ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            ImGui::ProgressBar(0.f, ImVec2(-1, 0));
            ImGui::TableNextColumn();
            ImGui::TextDisabled("空闲");
        } else {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(rows.size()));
            while (clipper.Step()) {
                for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n) {
                    const auto& row = rows[static_cast<size_t>(n)];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const std::string shortId =
                        row.promptId.size() > 8 ? row.promptId.substr(0, 8) : row.promptId;
                    ImGui::TextUnformatted(shortId.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(row.label.c_str());
                    if ((!row.error.empty() || !row.hint.empty()) && ImGui::IsItemHovered()) {
                        // P3.0 S9：hover 展开"错误 + 中文建议 + 回溯前 3 行"
                        ImGui::BeginTooltip();
                        if (!row.error.empty()) {
                            ImGui::TextWrapped("%s", row.error.c_str());
                        }
                        if (!row.hint.empty()) {
                            ImGui::TextWrapped("建议：%s", row.hint.c_str());
                        }
                        const size_t tbLines = std::min<size_t>(row.traceback.size(), 3);
                        for (size_t i = 0; i < tbLines; ++i) {
                            ImGui::TextDisabled("%s", row.traceback[i].c_str());
                        }
                        ImGui::EndTooltip();
                    }
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(row.nodeId.empty() ? "-" : row.nodeId.c_str());
                    ImGui::TableNextColumn();
                    if (row.state == comfy::TaskState::Running) {
                        ImGui::ProgressBar(row.progress, ImVec2(-1, 0),
                                           row.progressMax > 0 ? std::to_string(row.progressValue).c_str()
                                                               : "");
                    } else {
                        ImGui::ProgressBar(row.state == comfy::TaskState::Done ? 1.f : 0.f, ImVec2(-1, 0));
                    }
                    ImGui::TableNextColumn();
                    const char* st = comfy::TaskStateLabel(row.state);
                    if (row.state == comfy::TaskState::Failed) {
                        ImGui::TextColored(ImVec4(0.90f, 0.22f, 0.27f, 1.f), "%s", st);
                    } else if (row.state == comfy::TaskState::Done) {
                        ImGui::TextColored(ImVec4(0.24f, 0.86f, 0.59f, 1.f), "%s", st);
                    } else if (row.state == comfy::TaskState::Running) {
                        ImGui::TextColored(ImVec4(1.f, 0.62f, 0.11f, 1.f), "%s", st);
                    } else {
                        ImGui::TextUnformatted(st);
                    }
                }
            }
        }
        ImGui::EndTable();
    }
}

} // namespace shine::app
