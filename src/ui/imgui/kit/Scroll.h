#pragma once
// shine::kit::ScrollRegion —— 滚动区域（直接用 ImGui::BeginChild）
//
// 为什么必须用 ImGui 自带的 BeginChild 而不是继续手写：
// 自绘控件只产出 draw call，**不产出裁剪、不产出滚动、不接管命中**。内容超出
// 区域时既不会裁也不会滚，鼠标事件还会穿透到下层。ImGui 的 child 窗口把这三件事
// 一次给全（裁剪矩形 + 滚动偏移 + 输入归属），这正是 demo 与官方示例的通用做法。
// 代价是我们拿不到 CSS 式自由定位 —— 但外壳的固定栅格本来就是绝对定位，
// child 只用来当「内容区」，不参与外壳布局。
//
    // 用法：
    //     {
    //         ScrollRegion region("workspace", area);
    //         if (region) {
    //             page.Draw(region.content(), draw);
    //             region.setContentHeight(pageContentHeight);
    //         }
    //     }  // 析构自动 EndChild
//
// ⚠️ **必须调 setContentHeight**，否则这个区域永远不会滚（不是「滚不顺」，是滚不动）。
//    自绘控件只往 ImDrawList 上加 draw call，ImGui 那边**一个 item 都没有**，
//    于是 `ContentSize` 恒为 0 → `ScrollMaxY` 恒为 0 → 滚轮怎么转都停在原地，
//    视口以下的内容被裁掉且**无法到达**。这不是理论问题：本仓库三个 ScrollRegion
//    （workspace-scroll / hub-scroll / ov-right）一度全都中招，其中工作区那层的
//    注释还写着「撑到 2400」，但 2400 从没告诉过 ImGui，只是个自说自话的局部变量。
//    `setContentHeight` 在析构前补一个不可见 item（ImGui::Dummy），ContentSize 这才准。
#pragma once

#include "ui/imgui/kit/Widgets.h"

#include <algorithm>
#include <string>
#include <string_view>

namespace shine::kit {

class ScrollRegion {
public:
    // bounds 是区域的外框。height <= 0 时用 ImGui 的「撑满剩余」语义。
    //
    // `noMouseInputs` = 给这个 child 加 `ImGuiWindowFlags_NoMouseInputs`，让它**退出
    // 鼠标命中测试**。这不是「优化」，是本工程浮层能不能用的前提：
    //
    //   ImGui 先定 `g.HoveredWindow`（imgui.cpp:6573 从 `g.Windows` **末尾往前**扫、
    //   6607 取第一个命中 ⇒ 最靠上的窗口赢），再在 `ItemHoverable` 第一句判
    //   `if (g.HoveredWindow != window) return false;`（:5156）。child 是在根窗口之后
    //   建的，所以只要鼠标在工作区范围内，HoveredWindow 恒是**工作区 child**，
    //   根窗口里画的所有浮层 item（设置模态的 ×、报告模态的按钮、命令面板输入框、
    //   主题菜单行）恒 hovered=false / clicked=false —— **按钮画得出来、点不动**。
    //   而静息截图完全看不出来（不点击），80 张绿图对它零覆盖。
    //
    //   浮层打开时把 child 关掉，根窗口才拿得到 HoveredWindow；顺带这也实现了模态
    //   语义：底下的侧栏 / 顶栏 / 工作区**点不动**，点击由浮层自己的遮罩接走。
    //   ⚠️ 遮罩那边必须**真的注册一个全屏 HitTest** —— 只画不命中的话，点击既不
    //   关闭浮层也不被任何人接住，就是「点了没反应」。
    ScrollRegion(std::string_view id, Rect bounds, bool borders = false,
                 bool horizontalScroll = false, bool noMouseInputs = false);
    ~ScrollRegion();

    ScrollRegion(const ScrollRegion&) = delete;
    ScrollRegion& operator=(const ScrollRegion&) = delete;

    // BeginChild 返回 false = 完全被裁掉（可跳过内容，但**必须**配对 EndChild，
    // 所以析构里无条件 End）。
    explicit operator bool() const { return visible_; }

    // 内容区：原点已含滚动偏移。y 给一个足够大的值（调用方按内容高度自定），
    // x 给满宽 —— 这样页面照常按绝对矩形画，超出部分由 child 负责裁与滚。
    [[nodiscard]] Rect content() const { return content_; }

    // ⚠️ 画这个区域的内容**必须**用这里返回的 draw list，不能用外层那个。
    //    BeginChild 的裁剪矩形只写进**它自己那条** draw list 的 CmdBuffer；画到外层
    //    list 上，child 照样 Begin/End，但裁剪矩形对那些 draw call 完全无效 ——
    //    内容会一路溢出，盖住上层的 KPI 行和页头按钮，而且**看不出是哪里错了**
    //    （卡片都在、只是位置往上飘了）。Shell.cpp 传页面 draw list 时有同一条规矩。
    [[nodiscard]] ImDrawList* drawList() const { return ImGui::GetWindowDrawList(); }

    [[nodiscard]] float scrollY() const { return scrollY_; }
    [[nodiscard]] float maxScrollY() const { return maxScrollY_; }
    [[nodiscard]] bool atBottom() const { return scrollY_ >= maxScrollY_ - 1.0f; }
    void scrollToBottom();

    // 声明本区域的内容高度（**相对可视区顶**，不是屏幕绝对坐标）。
    // 画完内容后调一次即可；析构时补一个 Dummy 把它报给 ImGui。
    void setContentHeight(float height);
    [[nodiscard]] float contentHeight() const { return contentHeight_; }

    // ---- 粘性滚动请求（取证用）----
    //
    // 为什么要「粘性」：`scrollToBottom()` 只能在**该区域正在绘制的那一帧**调用，
    // 而取证驱动（Review.cpp）在 DrawFrame 的**外面** —— 它没法拿到这个局部对象。
    // 早先的做法是把滚动状态做成 Shell 的成员，等于给每个要滚的区域都加一份状态。
    //
    // 这里改成：调用方登记一个 id 与目标位置，下一帧该 id 的 ScrollRegion 构造时自己
    // 消费。通用能力，任何「需要滚到某张卡才能拍到」的取证都能用，不必再往 Shell 里
    // 加成员。**顶与底必须共用这一个入口** —— 分成两个请求时，同一帧里先后登记
    // 「滚到底」再「滚到顶」会互相覆盖，结果是停在中途，两次都不到位。
    //
    // ⚠️ 一次性：构造时消费并清空，所以不会在每一帧都把用户拽回某个位置。
    enum class ScrollTarget { Top, Bottom };
    static void RequestScroll(std::string_view id, ScrollTarget target);
    static void RequestScrollBottom(std::string_view id) {
        RequestScroll(id, ScrollTarget::Bottom);
    }
    static void RequestScrollTop(std::string_view id) { RequestScroll(id, ScrollTarget::Top); }

    // ---- 请求是否真的生效（取证判据用）----
    //
    // 为什么要这一层：**「两张图不一样」证明不了「滚动生效了」**。第一次跑
    // `overview-vstages` 时取证明明是绿的（identical-driven-pairs = 0），但两张图
    // 只差 326 个像素、位置在左上角一块无关区域，右栏压根没滚 —— 差异来自别处，
    // 与滚动无关。这与悬停探针的「无差异有两种原因」是同一族：信号有二义性时，
    // 必须再加一个正交信号。
    //
    // 这个结构体就是那个正交信号：它读的是**产品自己的滚动读数**（scrollY /
    // maxScrollY），不是从像素反推。maxScrollY > 0 而 scrollY 没到底 = 滚不动；
    // maxScrollY == 0 = 内容压根没超出区域（这时候「没变化」是正确结果）。
    struct ScrollApplied {
        std::string id;
        float scrollY = 0.0f;
        // 期望的滚动上限 = 自报内容高度 - 可视高度（自己算，确定性）。
        float maxScrollY = 0.0f;
        // ImGui 自己算出来的上限。**它比 maxScrollY 小说明 Dummy 那一招没生效**
        // （自绘内容没被报上去），滚轮依然滚不动 —— 两个数一起打出来才能把
        // 「粘性请求没送到」和「内容高度没上报」这两种原因分开。
        float imGuiMaxScrollY = 0.0f;
        // 自报内容高度与可视高度。**这两个数一起打出来才能定位问题**：
        // 期望上限 = contentHeight - viewHeight，是 0 还是因为「内容没超出」
        // 还是因为「viewHeight 读成了 0」，光看上限分不出来。
        float contentHeight = 0.0f;
        float viewHeight = 0.0f;
        bool ok = false;
    };
    // 最近一次**已核销**的请求。核销发生在发出请求的下一帧同一 id 的构造期 ——
    // 发出当帧读到的 scrollY 是生效前的值，拿它当结果就是自己给自己作证。
    static ScrollApplied LastApplied();

private:
    bool visible_ = false;
    Rect content_;
    float scrollY_ = 0.0f;
    float maxScrollY_ = 0.0f;
    float bottomHeight_ = 0.0f;
    float contentHeight_ = 0.0f;
    float expectedMaxScrollY_ = 0.0f;
    std::string pendingKey_;
};

} // namespace shine::kit
