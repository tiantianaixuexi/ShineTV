#include "ui/imgui/kit/Scroll.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shine::kit {

namespace {
// 待消费的滚动请求。一次性：被匹配的区域构造时消费并清空。
// ⚠️ 单线程即可 —— UI 线程驱动 DrawFrame，取证也在 UI 线程。
//
// 用 (id → target) 而不是 `vector<string>`：同一个 id 在同一帧里先后登记不同目标时
// （复位很常见：「滚到底拍一张，再滚回顶」），后者必须**压过**前者，而不是先来先到。
// 存成纯 id 列表会让「滚到底 / 滚回顶」两次登记只生效一次，截图就拍在错误位置。
std::vector<std::pair<std::string, ScrollRegion::ScrollTarget>>& PendingScroll() {
    static std::vector<std::pair<std::string, ScrollRegion::ScrollTarget>> requests;
    return requests;
}

// 每个 id 上一帧自报的内容高度。ScrollRegion 每帧新建，没有跨帧成员可存。
// 之所以要缓存而不能只靠 ImGui 的 `GetScrollMaxY()`：`ScrollMax` 的更新时机
// 跟着 ImGui 版本变，实测在构造期读到的还是 0 —— 于是「滚到底」被算成「滚 0」。
std::map<std::string, float>& LastContentHeight() {
    static std::map<std::string, float> heights;
    return heights;
}

// 已消费、正在等下一帧核销的请求。**必须隔一帧再核对**：请求发出去的那一帧
// `scrollY_` 读的是「滚动生效前」的值，拿它当结果就等于自己给自己作证。
std::string& VerifyingId() {
    static std::string id;
    return id;
}
ScrollRegion::ScrollTarget& VerifyingTarget() {
    static ScrollRegion::ScrollTarget target = ScrollRegion::ScrollTarget::Bottom;
    return target;
}
ScrollRegion::ScrollApplied& AppliedProbe() {
    static ScrollRegion::ScrollApplied probe;
    return probe;
}
} // namespace

void ScrollRegion::RequestScroll(std::string_view id, ScrollTarget target) {
    auto& pending = PendingScroll();
    const std::string key(id);
    const auto it = std::find_if(pending.begin(), pending.end(),
                                 [&key](const auto& e) { return e.first == key; });
    if (it != pending.end()) {
        it->second = target; // 同一 id 后登记者覆盖
        return;
    }
    pending.emplace_back(key, target);
}

ScrollRegion::ScrollApplied ScrollRegion::LastApplied() { return AppliedProbe(); }

void ScrollRegion::setContentHeight(float height) {
    contentHeight_ = std::max(0.0f, height);
    LastContentHeight()[pendingKey_] = contentHeight_;
}

ScrollRegion::ScrollRegion(std::string_view id, Rect bounds, bool borders, bool horizontalScroll) {
    ImGui::SetCursorScreenPos(bounds.min);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ColorPanel());
    ImGui::PushStyleColor(ImGuiCol_Border, ColorLineSubtle());
    // ⚠️ **不设** `ImGuiWindowFlags_NoScrollbar`。
    //    原来一直关着它，于是「还有更多」没有任何视觉提示 —— 内容虽然能滚了（内容
    //    高度已经上报），用户看不出来下面还有东西。ImGui 自己会在
    //    `ScrollMax > 0` 时才显示竖条，所以不用手动判「要不要画」。
    //    直接用它自带的（自带点击拖拽与 hover/active 态），**不要自己手搓滑块** ——
    //    手搓一版没有拖拽的滑块，等于给用户一个看得见摸不着的假控件。
    //    颜色走主题，不用默认灰。
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ColorLineStrong());
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ColorAccent());
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ColorAccent());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGuiChildFlags childFlags = borders ? ImGuiChildFlags_Borders : ImGuiChildFlags_None;
    // 滚轮要留给本区域（页面内容需要它），所以 NoScrollWithMouse 关掉。
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoBackground;
    if (horizontalScroll) {
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

    const std::string key(id);
    pendingKey_ = key;
    // 上一帧自报的内容高度（每帧新建对象，只能走 id 缓存）。
    const auto known = LastContentHeight().find(key);
    contentHeight_ = known != LastContentHeight().end() ? known->second : 0.0f;
    expectedMaxScrollY_ = std::max(0.0f, contentHeight_ - content_.height());

    // 核销上一帧发出去的请求：这一帧读到的 scrollY_ 才是**生效后**的值。
    if (VerifyingId() == key) {
        ScrollApplied& probe = AppliedProbe();
        probe.id = key;
        probe.scrollY = scrollY_;
        probe.maxScrollY = expectedMaxScrollY_;
        probe.imGuiMaxScrollY = maxScrollY_;
        probe.contentHeight = contentHeight_;
        probe.viewHeight = content_.height();
        probe.ok = VerifyingTarget() == ScrollTarget::Bottom
                       ? (expectedMaxScrollY_ > 0.0f && scrollY_ >= expectedMaxScrollY_ - 1.0f)
                       : (scrollY_ <= 1.0f);
        VerifyingId().clear();
    }

    // 消费粘性滚动请求：id 匹配就滚到登记的目标位并清空。
    //
    // ⚠️ 必须在 BeginChild 之后、EndChild 之前 —— SetScrollY 依赖当前的 child 上下文。
    //    消费后**立刻清空**，否则用户在底部时每一帧都会被拽回去，永远滚不上去。
    //
    // ⚠️ 这里用 `SetScrollY(GetScrollMaxY())` 而不是 `SetScrollHereY(1.0f)`。
    //    后者的语义是「滚到**当前帧最后一个 item** 的底边」，而请求是在构造期
    //    （BeginChild 之后、任何内容之前）发出的，此时当前帧还没有 item，于是它
    //    拿到的是上一帧残留的 LastItemData —— 实测在右栏这种「内容由多个绝对
    //    定位 Card 拼成、没有一个统一 item」的场景下**滚不动**。
    //    `GetScrollMaxY()` 来自上一帧已经上报完整的内容高度，是确定性的。
    //    （顺带：正因为它不依赖 item，取证 PumpFrames(1) 就能到位，2 帧只是保险。）
    auto& pending = PendingScroll();
    const auto it = std::find_if(pending.begin(), pending.end(),
                                 [&key](const auto& e) { return e.first == key; });
    if (it != pending.end()) {
        const ScrollTarget target = it->second;
        pending.erase(it);
        if (target == ScrollTarget::Bottom) {
            scrollToBottom();
        } else {
            ImGui::SetScrollY(0.0f);
        }
        VerifyingId() = key;
        VerifyingTarget() = target;
    }
}

ScrollRegion::~ScrollRegion() {
    // ⚠️ 必须在 EndChild **之前**补这个不可见 item —— 自绘控件全程没给 ImGui 提交过
    //    任何 item，ContentSize 一直是 0，ScrollMaxY 也就一直是 0，区域滚不动。
    //    Dummy 不带 ID，不会被 g.HoveredId 抢走，鼠标事件仍然落在真实控件上。
    if (contentHeight_ > 0.0f) {
        ImGui::Dummy(ImVec2(0.0f, contentHeight_));
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(6);
}

void ScrollRegion::scrollToBottom() {
    // 目标值用**我们自己声明的内容高度**减可视高度算，不用 `ImGui::GetScrollMaxY()`：
    // 后者在构造期读到的还是上一帧的旧值（实测恒为 0），会把「滚到底」算成「滚 0」。
    ImGui::SetScrollY(expectedMaxScrollY_);
}

} // namespace shine::kit
