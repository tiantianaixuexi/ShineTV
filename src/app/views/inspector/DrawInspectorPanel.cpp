// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉了参数默认值，声明在头里）。
#include "app/views/inspector/DrawInspectorPanel.h"
#include "app/AppIncludes.h"

namespace shine::app {

void DrawInspectorPanel() {
    const auto selected = graph::SelectedNodes();
    if (selected.empty()) {
        ImGui::TextDisabled("未选中节点");
        ImGui::Spacing();
        ImGui::TextWrapped("在中央画布点击节点后，此处显示名称、类型与插口。");
    } else {
        ImGui::Text("已选中 %zu 个节点", selected.size());
        ImGui::Separator();
        if (selected.size() == 1) {
            const auto& n = selected[0];
            ImGui::Text("名称：%s", n.name.c_str());
            ImGui::TextDisabled("类型：%s", n.type.c_str());
            ImGui::TextDisabled("ID：%s", n.id.c_str());
            ImGui::Spacing();
            ImGui::Text("位置：(%.0f, %.0f)", n.x, n.y);
            ImGui::Text("尺寸：%.0f × %.0f", n.w, n.h);
            ImGui::Text("输入 %d  ·  输出 %d", n.inputs, n.outputs);
            ImGui::Spacing();
            if (ImGui::Button("居中到节点", ImVec2(-1, 0))) {
                graph::CenterView();
            }
            if (ImGui::Button("删除此节点", ImVec2(-1, 0))) {
                graph::DeleteSelected();
            }
        } else {
            ImGui::BeginChild("##sel_list", ImVec2(0, 160), true);
            for (const auto& n : selected) {
                ImGui::BulletText("%s (%s)", n.name.c_str(), n.type.c_str());
            }
            ImGui::EndChild();
            if (ImGui::Button("删除全部选中", ImVec2(-1, 0))) {
                graph::DeleteSelected();
            }
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Comfy 连接");
    auto& session = comfy::ComfySession::Instance();
    ImGui::TextWrapped("%s", session.BaseUrl().c_str());
    DrawConnectionStatus("状态：");
    ImGui::TextDisabled("clientId：%s", session.ClientId().c_str());
    ImGui::TextDisabled("队列剩余：%d", session.Queue().QueueRemaining());
    ImGui::TextDisabled("节点类：%d", session.ObjectInfoNodeCount());
    ImGui::TextDisabled("图：%zu 节点 / %zu 连线", graph::NodeCount(), graph::ConnectionCount());
    if (ImGui::SmallButton("重连")) {
        session.Connect();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("刷新队列")) {
        session.RefreshQueue();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("object_info")) {
        session.RefreshObjectInfo();
    }
}

} // namespace shine::app
