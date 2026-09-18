// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉了参数默认值，声明在头里）。
#include "app/shell/DrawDockedPanels.h"
#include "app/AppIncludes.h"
#include "app/gallery/GalleryView.h" // G-S5：中央「图库」窗口 + 「查看器」浮窗
#include "app/novel/NovelView.h"

namespace shine::app {

void DrawDockedPanels(ImVec2 dockSize) {
    const ImGuiID dockspaceId = ImGui::GetID("ShineTVDockSpace");
    dock::BuildDefaultLayout(dockspaceId, dockSize);
    ImGui::DockSpace(dockspaceId, dockSize, ImGuiDockNodeFlags_None);

    // 侧栏内容随活动栏切换
    ImGui::SetNextWindowSizeConstraints(ImVec2(160, 0), ImVec2(520, FLT_MAX));
    ImGui::Begin("侧栏");
    if (State().sideOpen) {
        DrawSideBar();
    } else {
        ImGui::TextDisabled("侧栏已隐藏");
    }
    ImGui::End();

    ImGui::Begin("图");
    DrawGraphPanel();
    ImGui::End();

    // 验收（SHINE_WINDOW=shots|gallery）：开局抢几帧焦点，把中央区切到目标页；抢完归零，不干扰用户
    const auto focusTarget = [](FocusWindow target) {
        if (State().focusFrames <= 0 || State().focusWindow != target) {
            return;
        }
        --State().focusFrames;
        ImGui::SetNextWindowFocus();
    };

    focusTarget(FocusWindow::Shots);
    ImGui::Begin("分镜"); // P5.3：默认与「图」同区（见 DockLayout.cpp）
    shots::DrawShotTable();
    ImGui::End();

    focusTarget(FocusWindow::Gallery);
    ImGui::Begin("图库"); // G-S5：与「图」「分镜」同区（见 DockLayout.cpp）
    gallery::DrawGalleryWindow();
    ImGui::End();

    // 活动栏点「小说」时，把中央切到本页
    if (State().sideView == SideView::Novel && State().focusFrames > 0 && State().focusWindow == FocusWindow::Novel) {
        --State().focusFrames;
        ImGui::SetNextWindowFocus();
    }
    ImGui::Begin("小说");
    novel::DrawNovelWindow();
    ImGui::End();

    ImGui::Begin("属性");
    DrawInspectorPanel();
    ImGui::End();

    ImGui::Begin("预览");
    DrawPreviewPanel();
    ImGui::End();

    ImGui::Begin("底栏"); // 窗口标题 ≠ 内部 Tab「队列」，避免出现两层「队列」
    DrawBottomPanel();
    ImGui::End();

    if (Settings().showDemoWindow) {
        ImGui::ShowDemoWindow(&Settings().showDemoWindow);
    }
    if (State().showStyleEditor) {
        ImGui::Begin("样式编辑器", &State().showStyleEditor);
        ImGui::ShowStyleEditor();
        ImGui::End();
    }
    if (State().showAbout) {
        ImGui::Begin("关于", &State().showAbout, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextUnformatted("ShineTV Studio");
        ImGui::TextUnformatted("独立 C++26 ComfyUI 工作站");
        ImGui::TextUnformatted("重写自 UE Plugins/Shine + ShineMCP");
        ImGui::Separator();
        ImGui::TextDisabled("阶段 P2：GraphHost / VNS 节点图");
        ImGui::TextDisabled("布局：活动栏 | 侧栏 | 图 | 属性/预览 | 底栏 | 状态栏");
        ImGui::End();
    }
    DrawSettingsWindow();
    DrawTemplateWindow();   // P3.7：工作流模板浏览器（独立浮窗）
    DrawNodeWindow();       // P3.7b：节点浏览器（独立浮窗）
    if (State().showViewer) { // G-S5：「查看器」浮窗（占位；完整实现在 G-S10）
        ImGui::Begin("查看器", &State().showViewer);
        gallery::DrawGalleryViewerWindow();
        ImGui::End();
    }
    // TEMP-G3 的「纹理自检」浮窗已关闭（G-S5 收尾，用户要求；源码留到 G-S6 一起删）
}

} // namespace shine::app
