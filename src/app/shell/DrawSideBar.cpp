// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉了参数默认值，声明在头里）。
#include "app/shell/DrawSideBar.h"
#include "app/AppIncludes.h"
#include "app/ui/Widgets.h" // R-S2：面板标题统一走 ui::PanelHeader
#include "app/gallery/GalleryView.h" // G-S5：侧栏「图库」
#include "app/novel/NovelView.h"
#include "app/paint/PaintCanvasView.h" // P6.2

namespace shine::app {

void DrawSideBar() {
    // 标题随活动栏切换
    ui::PanelHeader(SideViewTitle(State().sideView)); // R-S2：原 TextDisabled+Separator 改组件
    // Local scroll only inside the side panel; outer dock/host must not wheel-scroll.
    ImGui::BeginChild("##side_content", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    switch (State().sideView) {
    case SideView::Assets: DrawAssetsPanel(); break;
    case SideView::Nodes: DrawNodesPanel(); break;
    case SideView::Workflows: DrawWorkflowsPanel(); break;
    case SideView::Comfy: DrawComfySidePanel(); break;
    case SideView::Shots: shots::DrawVideoSidePanel(); break; // P5.3：分镜工程面板
    case SideView::Gallery: gallery::DrawGallerySidePanel(); break; // G-S5：图库（来源/目录/缩略图/缓存）
    case SideView::Novel: novel::DrawNovelSidePanel(); break;
    case SideView::Paint: paint::DrawPaintSidePanel(); break; // P6.2
    }
    ImGui::EndChild();
}

} // namespace shine::app
