// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉了参数默认值，声明在头里）。
#include "app/shell/DrawDockedPanels.h"
#include "app/AppIncludes.h"
#include "app/gallery/GalleryView.h" // G-S5：中央「图库」窗口 + 「查看器」浮窗
#include "app/novel/NovelView.h"
#include "app/paint/PaintCanvasView.h" // P6.2
#include "app/Shortcuts.h"             // P8.2

namespace shine::app {

void DrawDockedPanels(ImVec2 dockSize) {
    const ImGuiID dockspaceId = ImGui::GetID("ShineTVDockSpace");
    dock::BuildDefaultLayout(dockspaceId, dockSize);
    ImGui::DockSpace(dockspaceId, dockSize, ImGuiDockNodeFlags_None);

    // Docked shells must not scroll with the wheel; content that needs scroll
    // uses an inner BeginChild / table ScrollY.
    constexpr ImGuiWindowFlags kDockFlags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    // 侧栏内容随活动栏切换
    ImGui::SetNextWindowSizeConstraints(ImVec2(160, 0), ImVec2(520, FLT_MAX));
    ImGui::Begin("侧栏", nullptr, kDockFlags);
    if (State().sideOpen) {
        DrawSideBar();
    } else {
        ImGui::TextDisabled("侧栏已隐藏");
    }
    ImGui::End();

    ImGui::Begin("图", nullptr, kDockFlags);
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
    ImGui::Begin("分镜", nullptr, kDockFlags); // P5.3：默认与「图」同区（见 DockLayout.cpp）
    shots::DrawShotTable();
    ImGui::End();

    focusTarget(FocusWindow::Gallery);
    ImGui::Begin("图库", nullptr, kDockFlags); // G-S5：与「图」「分镜」同区（见 DockLayout.cpp）
    gallery::DrawGalleryWindow();
    ImGui::End();

    // 活动栏点「小说」时，把中央切到本页
    if (State().sideView == SideView::Novel && State().focusFrames > 0 && State().focusWindow == FocusWindow::Novel) {
        --State().focusFrames;
        ImGui::SetNextWindowFocus();
    }
    ImGui::Begin("小说", nullptr, kDockFlags);
    novel::DrawNovelWindow();
    ImGui::End();

    focusTarget(FocusWindow::Paint);
    ImGui::Begin("画布", nullptr, kDockFlags); // P6.2
    paint::DrawPaintCanvasWindow();
    ImGui::End();

    ImGui::Begin("属性", nullptr, kDockFlags);
    DrawInspectorPanel();
    ImGui::End();

    ImGui::Begin("预览", nullptr, kDockFlags);
    DrawPreviewPanel();
    ImGui::End();

    ImGui::Begin("底栏", nullptr, kDockFlags); // 窗口标题 ≠ 内部 Tab「队列」，避免出现两层「队列」
    DrawBottomPanel();
    ImGui::End();

    if (Settings().showDemoWindow) {
        ImGui::ShowDemoWindow(&Settings().showDemoWindow);
    }
    if (State().showStyleEditor) {
        ImGui::Begin("样式编辑器", &State().showStyleEditor, kDockFlags);
        ImGui::ShowStyleEditor();
        ImGui::End();
    }
    if (State().showAbout) {
        ImGui::Begin("关于", &State().showAbout, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextUnformatted("ShineTV Studio");
        ImGui::TextUnformatted("独立 C++26 ComfyUI 工作站");
        ImGui::TextUnformatted("版本 0.2.0");
        ImGui::TextUnformatted("重写自 UE Plugins/Shine + ShineMCP");
        ImGui::Separator();
        ImGui::TextDisabled("P3–P5/G 完成 · P6 画布 inpaint · P7 MCP");
        ImGui::TextDisabled("布局：活动栏 | 侧栏 | 图/分镜/图库/画布 | 属性/预览 | 底栏 | 状态栏");
        ImGui::End();
    }
    DrawSettingsWindow();
    DrawTemplateWindow();   // P3.7：工作流模板浏览器（独立浮窗）
    DrawNodeWindow();       // P3.7b：节点浏览器（独立浮窗）
    if (State().showViewer) { // G-S6：「查看器」浮窗（适应窗口；完整实现在 G-S10）
        ImGui::Begin("查看器", &State().showViewer);
        gallery::DrawGalleryViewerWindow();
        ImGui::End();
    }
    if (State().showShortcuts) { // P8.2
        shortcuts::DrawHelpWindow(&State().showShortcuts);
    }
    shortcuts::Poll(); // P8.2：全局快捷键（文本框聚焦时不触发）
    // 活动栏在「画布」时把中央 dock 页切过来（imgui.ini 的 Selected= 代码改不动，每帧纠偏）
    if (State().sideView == SideView::Paint) {
        ImGui::SetWindowFocus("画布");
    }
}

} // namespace shine::app
