#include "ui/imgui/kit/Widget_Core.h"

#include "ui/imgui/kit/Draw.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace shine::kit {

namespace detail {

namespace {

// 命中 id 全是「短字面量 + 序号」，128 字节栈缓冲装得下；装不下才退回堆分配
//（语义不变，只是那一处多了 malloc）。这样 Draw 每帧对每个热区**零堆分配**。
constexpr std::size_t kHitIdStack = 128;

// 把 string_view 落成 NUL 结尾的 C 串：优先栈缓冲，溢出退回 overflow（堆）。
const char* IntoStackBuf(char (&buf)[kHitIdStack], std::string_view id, std::string& overflow) {
    if (id.size() < sizeof(buf)) {
        if (!id.empty()) {
            std::memcpy(buf, id.data(), id.size());
        }
        buf[id.size()] = '\0';
        return buf;
    }
    overflow.assign(id);
    return overflow.c_str();
}

} // namespace

// 命中测试：在绝对位置放一个不可见 item，让 ImGui 维护 ID 栈与焦点。
// 返回 hovered / active / clicked / doubleClicked。
// 本体只吃 NUL 结尾的 C 串；string_view / (prefix, index) 两个重载负责把它
// 落进栈缓冲再进来 —— 这是「Draw 每帧零分配」的唯一入口约束。
Hit HitTestItemId(Rect bounds, const char* idStr, bool keyboard) {
    Hit hit;
    // 命中测试同样吃「反向矩形」这一套：InvisibleButton 的尺寸取自
    // width()/height()，反向时是负数，ItemAdd 出来的区域是反的 —— 不崩、不报，
    // 但这个控件**点不中**。所以和 DrawRoundRect 共用同一个计数。
    if (bounds.max.x < bounds.min.x || bounds.max.y < bounds.min.y) {
        NoteInvertedRect(bounds.min.x, bounds.min.y, bounds.max.x, bounds.max.y, "HitTest");
    }
    ImGui::SetCursorScreenPos(bounds.min);
    // 同一帧内重叠热区自检：ImGui 先注册者独占，第二个 InvisibleButton
    // 永远 clicked=false，而它上面的控件外观画得好好的。调用点在 kit 组件
    // 之外补第二次命中是最常见的触发方式。
    // ⚠️ 传 idStr 本体：它是 NUL 结尾的，NoteDuplicateHit 内部按 C 字符串写进
    //    定长缓冲（早先传 string_view 的裸 data() 会越界读，已用本约束根治）。
    // 补间 id 必须在 InvisibleButton **之前**、同一 ID 栈上取：InvisibleButton 内部
    // 就是 window->GetID(str_id)，两条路径同串同栈 ⇒ 同一 ImGuiID。
    hit.tid = ImGui::GetID(idStr);
    const auto flags = keyboard ? ImGuiButtonFlags_EnableNav : ImGuiButtonFlags_None;
    const bool activated = ImGui::InvisibleButton(idStr, ImVec2(bounds.width(), bounds.height()), flags);
    if ((ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) == 0) {
        NoteDuplicateHit(bounds.min.x, bounds.min.y, bounds.max.x, bounds.max.y, idStr);
    }
    hit.hovered = ImGui::IsItemHovered();
    hit.held = ImGui::IsItemActive();
    hit.clicked = keyboard ? activated : ImGui::IsItemClicked();
    hit.doubleClicked = hit.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    // 命中自检：取证要能分清「鼠标没落进热区」与「命中了却什么都不画」。
    // 记在 InvisibleButton **之后**、返回之前 —— 早先返回前漏了这一句，
    // 记的是上一帧的 hovered，探针的判据就成了延迟一帧的假信号。
    if (hit.hovered) {
        NoteHoveredItem(idStr);
    }
    return hit;
}

Hit HitTestItem(Rect bounds, std::string_view id, bool keyboard) {
    char buf[kHitIdStack];
    std::string overflow;
    return HitTestItemId(bounds, IntoStackBuf(buf, id, overflow), keyboard);
}

Hit HitTestItem(Rect bounds, std::string_view prefix, int index) {
    char buf[kHitIdStack];
    std::string overflow;
    const int n =
        std::snprintf(buf, sizeof(buf), "%.*s%d", static_cast<int>(prefix.size()), prefix.data(),
                      index);
    if (n < 0 || static_cast<std::size_t>(n) >= sizeof(buf)) {
        // 极端长前缀：退回堆拼接，id 语义与栈上路径逐字节一致。
        overflow.reserve(prefix.size() + 12);
        overflow.assign(prefix);
        overflow += std::to_string(index);
        return HitTestItemId(bounds, overflow.c_str());
    }
    return HitTestItemId(bounds, buf);
}

std::string UniqueId(std::string_view id, int index) {
    return std::string(id) + "##" + std::to_string(index);
}

std::string_view FormatUniqueId(char* buf, std::size_t cap, std::string_view id,
                                std::string_view middle, int index, std::string* overflow) {
    const int n = std::snprintf(buf, cap, "%.*s%.*s##%d", static_cast<int>(id.size()), id.data(),
                                static_cast<int>(middle.size()), middle.data(), index);
    if (n >= 0 && static_cast<std::size_t>(n) < cap) {
        return std::string_view(buf, static_cast<std::size_t>(n));
    }
    // 溢出兜底：退回堆版本，产物与栈上路径逐字节一致。
    *overflow = UniqueId(std::string(id) + std::string(middle), index);
    return *overflow;
}

} // namespace detail

IconTextLayout PlaceIconText(Rect box, float iconSize, float gap, float textWidth,
                             IconTextAlign align, float padLeft) {
    IconTextLayout out;
    if (align == IconTextAlign::Center) {
        // 与 `CenterTextX` 同一条算式：把「图标 + gap + 文字」当成一整段来居中。
        out.iconX = CenterTextX(box.min.x, box.max.x, iconSize + gap + textWidth);
    } else {
        out.iconX = box.min.x + padLeft;
    }
    out.textX = out.iconX + iconSize + gap;
    return out;
}

// 对外的命中测试（外壳/页面层用）。内部实现叫 detail::HitTestItem，
// 名字不与这里重。
Hit HitTest(Rect bounds, std::string_view id) { return detail::HitTestItem(bounds, id); }
bool Clicked(Rect bounds, std::string_view id) { return detail::HitTestItem(bounds, id).clicked; }
bool Hovered(Rect bounds, std::string_view id) { return detail::HitTestItem(bounds, id).hovered; }

// (前缀, 序号) 重载：循环里的热区 id 由 kit 在栈上拼，调用点不再
// `"prefix" + std::to_string(i)` 地每帧造串。
Hit HitTest(Rect bounds, std::string_view prefix, int index) {
    return detail::HitTestItem(bounds, prefix, index);
}
bool Clicked(Rect bounds, std::string_view prefix, int index) {
    return detail::HitTestItem(bounds, prefix, index).clicked;
}
bool Hovered(Rect bounds, std::string_view prefix, int index) {
    return detail::HitTestItem(bounds, prefix, index).hovered;
}

} // namespace shine::kit
