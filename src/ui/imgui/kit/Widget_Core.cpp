#include "ui/imgui/kit/Widget_Core.h"

#include "ui/imgui/kit/Draw.h"

namespace shine::kit {

namespace detail {

// 命中测试：在绝对位置放一个不可见 item，让 ImGui 维护 ID 栈与焦点。
// 返回 hovered / active / clicked / doubleClicked。
Hit HitTestItem(Rect bounds, std::string_view id) {
    Hit hit;
    // 命中测试同样吃「反向矩形」这一套：InvisibleButton 的尺寸取自
    // width()/height()，反向时是负数，ItemAdd 出来的区域是反的 —— 不崩、不报，
    // 但这个控件**点不中**。所以和 DrawRoundRect 共用同一个计数。
    if (bounds.max.x < bounds.min.x || bounds.max.y < bounds.min.y) {
        NoteInvertedRect(bounds.min.x, bounds.min.y, bounds.max.x, bounds.max.y, "HitTest");
    }
    ImGui::SetCursorScreenPos(bounds.min);
    const std::string idStr(id);
    // 同一帧内重叠热区自检：ImGui 先注册者独占，第二个 InvisibleButton
    // 永远 clicked=false，而它上面的控件外观画得好好的。调用点在 kit 组件
    // 之外补第二次命中是最常见的触发方式。
    // ⚠️ 传 idStr.c_str() 而不是 id.data()：string_view 的 data() **不保证**
    //    以 '\0' 结尾，而 NoteDuplicateHit 内部按 C 字符串写进定长缓冲，
    //    拿 string_view 的裸指针会越界读。
    NoteDuplicateHit(bounds.min.x, bounds.min.y, bounds.max.x, bounds.max.y, idStr.c_str());
    ImGui::InvisibleButton(idStr.c_str(), ImVec2(bounds.width(), bounds.height()));
    hit.hovered = ImGui::IsItemHovered();
    hit.held = ImGui::IsItemActive();
    hit.clicked = ImGui::IsItemClicked();
    hit.doubleClicked = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    // 命中自检：取证要能分清「鼠标没落进热区」与「命中了却什么都不画」。
    // 记在 InvisibleButton **之后**、返回之前 —— 早先返回前漏了这一句，
    // 记的是上一帧的 hovered，探针的判据就成了延迟一帧的假信号。
    if (hit.hovered) {
        NoteHoveredItem(idStr.c_str());
    }
    return hit;
}

std::string UniqueId(std::string_view id, int index) {
    return std::string(id) + "##" + std::to_string(index);
}

} // namespace detail

// 对外的命中测试（外壳/页面层用）。内部实现叫 detail::HitTestItem，
// 名字不与这里重。
Hit HitTest(Rect bounds, std::string_view id) { return detail::HitTestItem(bounds, id); }
bool Clicked(Rect bounds, std::string_view id) { return detail::HitTestItem(bounds, id).clicked; }
bool Hovered(Rect bounds, std::string_view id) { return detail::HitTestItem(bounds, id).hovered; }

} // namespace shine::kit
