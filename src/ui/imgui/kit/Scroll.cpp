#include "ui/imgui/kit/Scroll.h"

#include <algorithm>
#include <cmath>

namespace shine::kit {

ScrollRegion::ScrollRegion(std::string_view id, Rect bounds, bool borders, bool horizontalScroll) {
    ImGui::SetCursorScreenPos(bounds.min);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ColorPanel());
    ImGui::PushStyleColor(ImGuiCol_Border, ColorLineSubtle());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGuiChildFlags childFlags = borders ? ImGuiChildFlags_Borders : ImGuiChildFlags_None;
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                   ImGuiWindowFlags_NoBackground;
    // NoScrollWithMouse 关掉是为了让滚轮交给外层区域处理；这里反其道而行：
    // 页面内容需要滚轮，所以不关。
    windowFlags &= ~ImGuiWindowFlags_NoScrollWithMouse;
    if (horizontalScroll) {
        windowFlags &= ~ImGuiWindowFlags_NoScrollbar;
        windowFlags |= ImGuiWindowFlags_HorizontalScrollbar;
    }

    const ImVec2 size(bounds.width(), bounds.height() > 0.0f ? bounds.height() : 0.0f);
    visible_ = ImGui::BeginChild(std::string(id).c_str(), size, childFlags, windowFlags);

    // 原点已含滚动偏移：页面照常按绝对矩形画，超出部分由 child 裁剪。
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    content_ = Rect{origin, ImVec2(origin.x + available.x, origin.y + std::max(available.y, 1.0f))};

    scrollY_ = ImGui::GetScrollY();
    maxScrollY_ = std::max(0.0f, ImGui::GetScrollMaxY());
    bottomHeight_ = bounds.height();
}

ScrollRegion::~ScrollRegion() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void ScrollRegion::scrollToBottom() {
    ImGui::SetScrollHereY(1.0f);
}

} // namespace shine::kit
