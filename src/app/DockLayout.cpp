#include "app/DockLayout.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace shine::app::dock {
namespace {
bool g_needsRebuild = true;
bool g_layoutBuilt = false;
} // namespace

bool NeedsRebuild() { return g_needsRebuild; }

void RequestRebuild() {
    g_needsRebuild = true;
    g_layoutBuilt = false;
}

void BuildDefaultLayout(ImGuiID dockspaceId, ImVec2 dockSize) {
    if (g_layoutBuilt && !g_needsRebuild) {
        return;
    }

    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, dockSize);

    // 六区中的「工作区」四分：左 / 中 / 右 / 下
    ImGuiID center = dockspaceId;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.26f, nullptr, &center);

    ImGui::DockBuilderDockWindow("侧栏", left);
    ImGui::DockBuilderDockWindow("图", center);
    ImGui::DockBuilderDockWindow("分镜", center); // P5.3：与「图」同区互相切页
    ImGui::DockBuilderDockWindow("图库", center); // G-S5：图库列表（与「图」「分镜」同区互相切页）
    ImGui::DockBuilderDockWindow("小说", center); // 小说工程（新建 + 列表）
    ImGui::DockBuilderDockWindow("属性", right);
    ImGui::DockBuilderDockWindow("预览", right);
    ImGui::DockBuilderDockWindow("底栏", bottom); // 内含 Tab：队列/日志/输出（外层不再叫「队列」，避免双标题）

    ImGui::DockBuilderFinish(dockspaceId);
    g_layoutBuilt = true;
    g_needsRebuild = false;
}

} // namespace shine::app::dock
