// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉了参数默认值，声明在头里）。
// P3.7：从 ComfyUI 拉模板的**列表**已抽到独立窗口 `app/dialogs/DrawTemplateWindow.*`
//（用户要求：模板不塞侧栏，工具栏给按钮弹窗、左分类右条目）。这里只留文件级导入 / 导出 + 一个入口按钮。
#include "app/panels/workflows/DrawWorkflowsPanel.h"
#include "app/AppIncludes.h"

namespace shine::app {

void DrawWorkflowsPanel() {
    ImGui::TextDisabled("工作流 / 图文件");
    ImGui::Separator();
    if (ImGui::Button("保存当前图（VNS）", ImVec2(-1, 0))) {
        graph::SaveGraph();
    }
    if (ImGui::Button("加载 graph.json（VNS）", ImVec2(-1, 0))) {
        graph::LoadGraph();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("路径");
    ImGui::TextWrapped("%s", graph::GraphPath().c_str());
    ImGui::Spacing();
    ImGui::SeparatorText("导入 / 导出（按内容识别格式）");
    if (ImGui::Button("导入文件...", ImVec2(-1, 0))) {
        const std::string path =
            OpenFileDialog("导入图 / 工作流 / API JSON", {{"JSON / 图文件", "*.json;*.txt"}, {"全部文件", "*.*"}});
        if (!path.empty()) {
            graph::ImportGraphFile(path);
        }
    }
    if (ImGui::Button("导出 API JSON...", ImVec2(-1, 0))) {
        const std::string path = SaveFileDialog("导出 API JSON", "shine_api.json", {{"JSON", "*.json"}});
        if (!path.empty()) {
            graph::ExportApiJson(path);
        }
    }
    if (ImGui::Button("导出工作流 v1.0...", ImVec2(-1, 0))) {
        const std::string path =
            SaveFileDialog("导出 ComfyUI 工作流 v1.0", "shine_workflow.json", {{"JSON", "*.json"}});
        if (!path.empty()) {
            graph::ExportWorkflowV1(path);
        }
    }
    ImGui::Spacing();
    ImGui::TextDisabled("当前：%zu 节点 / %zu 连线", graph::NodeCount(), graph::ConnectionCount());

    ImGui::Spacing();
    ImGui::SeparatorText("从 ComfyUI 新建（模板）");
    if (ImGui::Button("打开模板浏览器...", ImVec2(-1, 0))) {
        State().showTemplateWindow = true;
    }
    ImGui::TextDisabled("左边分类、右边工作流，单击即创建（会替换当前画布）。");
    ImGui::TextDisabled("入口也在图工具条的「模板」按钮与「文件」菜单里。");
}

} // namespace shine::app
