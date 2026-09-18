#include "app/UiState.h"

namespace shine::app {
namespace {

const std::array<ActivityItem, 7> kActivities = {{
    {SideView::Assets, "资", "资源"},
    {SideView::Nodes, "节", "节点"},
    {SideView::Workflows, "流", "工作流"},
    {SideView::Comfy, "C", "Comfy"},
    {SideView::Shots, "分", "分镜"},    // P5.3
    {SideView::Gallery, "图", "图库"},  // G-S5
    {SideView::Novel, "说", "小说"},
}};

} // namespace

const std::array<ActivityItem, 7>& Activities() noexcept { return kActivities; }

UiState& State() noexcept {
    static UiState state;
    return state;
}

} // namespace shine::app
