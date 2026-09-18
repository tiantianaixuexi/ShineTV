// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉了参数默认值，声明在头里）。
#include "app/views/graph/DrawGraphPanel.h"
#include "app/AppIncludes.h"

namespace shine::app {

void DrawGraphPanel() {
    // 工具条
    if (ImGui::SmallButton("▶ 运行")) {
        graph::RunCurrentGraph();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("编译当前图并提交到 ComfyUI（Ctrl+Enter）");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("■ 中断")) {
        graph::InterruptCurrentGraph();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("保存")) {
        graph::SaveGraph();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("加载")) {
        graph::LoadGraph();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("节点")) {
        State().showNodeWindow = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("节点浏览器：左边分类、右边节点，单击建到视口中心（也可拖到画布）");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("模板")) {
        State().showTemplateWindow = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("工作流模板（从 ComfyUI 拉取）：左边分类、右边工作流，单击即新建");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("居中")) {
        graph::CenterView();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("注释")) {
        graph::AddGroupCommentAt(40.f, 40.f, "注释");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("删除选中") && graph::SelectedCount() > 0) {
        graph::DeleteSelected();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu 节点 · %zu 连线 · 缩放 %.0f%% · 选中 %zu",
                        graph::NodeCount(), graph::ConnectionCount(),
                        graph::Zoom() * 100.f, graph::SelectedCount());

    // —— P3.5：最近提交 + 编译/提交错误（节点级中文）——
    const std::string promptId = graph::LastPromptId();
    if (!promptId.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("· prompt %s", promptId.substr(0, std::min<std::size_t>(8, promptId.size())).c_str());
    }
    const auto& compileErrors = graph::LastCompileErrors();
    if (!compileErrors.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.40f, 1.0f));
        ImGui::TextWrapped("运行被拦下：%zu 项问题", compileErrors.size());
        ImGui::PopStyleColor();
        for (const auto& e : compileErrors) {
            if (e.nodeName.empty()) {
                ImGui::BulletText("%s", e.message.c_str());
            } else {
                ImGui::BulletText("%s：%s", e.nodeName.c_str(), e.message.c_str());
            }
        }
    }
    ImGui::Separator();
    ImGui::Spacing();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("##vns_host", avail, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    graph::DrawCanvas();
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SHINE_NODE_TYPE")) {
            const char* type = static_cast<const char*>(payload->Data);
            const ImVec2 m = ImGui::GetMousePos();
            const ImVec2 wpos = ImGui::GetWindowPos();
            graph::SpawnNode(type ? type : "", m.x - wpos.x, m.y - wpos.y);
        }
        // G-S11：图库拖入 —— 本步只记日志 + 反馈；P3 再真正接到 LoadImage / @image
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SHINE_IMAGE_PATH")) {
            const char* text = static_cast<const char*>(payload->Data);
            if (text != nullptr && text[0] != '\0') {
                ::shine::gallery::SetLastGraphDropPath(text);
                log::Info("drop image path {}", text);
            }
        }
        ImGui::EndDragDropTarget();
    }
    // 拖入反馈：最近一次图库路径
    const std::string dropPath = ::shine::gallery::LastGraphDropPath();
    if (!dropPath.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("· 拖入图片 %s", dropPath.c_str());
    }
    ImGui::EndChild();
}

} // namespace shine::app
