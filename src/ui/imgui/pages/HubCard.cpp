// 项目中心的**项目卡**（栅格里的每一格）
//
// 这一块的全部难点是**命中顺序**，不是绘制。整卡热区与页脚四个按钮压在**同一批
// 像素**上，而 ImGui 同一窗口内是「先注册者独占 HoveredId / ActiveId」——
// 所以顺序反了不会编译报错、不会崩、截图也全绿，只是「点『…』变成打开项目」。
// 那段注释因此留在函数开头，不是注释洁癖，是这份代码的全部内容。
#include "ui/imgui/pages/Hub.h"

#include "ui/imgui/pages/PageCommon.h"
#include "util/Encoding.h"
#include "util/Shell.h"

#include <cstring>
#include <string>

namespace shine::pages {
namespace hub {

using namespace shine::kit;

bool DrawCard(ImDrawList* draw, HubState& hub, const HubCard& card, Rect bounds, float bodyWidth) {
    // ⚠️ 整卡热区在**页脚四个按钮之后**才注册，方向不能反。
    //
    //    ImGui 同一窗口内是「**先注册者独占** HoveredId / ActiveId」：
    //    `ItemHoverable` 里 `if (g.HoveredId != 0 && g.HoveredId != id && !AllowOverlap)
    //    return false;`（imgui.cpp:5161），随后 `SetHoveredID(id)`。所以先注册整卡
    //    ⇒ 页脚四个按钮**永远 hovered=false / clicked=false**，而整卡在它们的像素上
    //    照样 clicked=true —— 症状是「点『…』不是弹移除确认，而是直接把项目打开」，
    //    另外三个键连 hover 底色都不出。**这里原来的注释断言「后命中的 item 优先」，
    //    与 ImGui 的规则正好相反**，所以那个 bug 一直没人看出来。
    //
    //    先注册按钮之后，落在页脚像素上的那次点击归按钮，落在卡片正文的归整卡 ——
    //    效果等价于 webui 里 pfoot 上的 e.stopPropagation()，而且是 ImGui 自己做的，
    //    不依赖下面那串 `!openPressed && …` 兜底（兜底仍保留，双保险）。
    //
    //    代价：边框的高亮要手算鼠标位置，不能用 hit.hovered（那时还没注册）。
    //    ⚠️ 只能用 bounds.contains() 手算，**不要**用 ImGui::IsMouseHoveringRect ——
    //    本工程调它会 0xC0000005（fault offset 0x8b8597），已改成手算比较。
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool cardHovered = bounds.contains(mouse);
    constexpr float radius = 10.0f;
    // `.card.proj-card.hoverable.glow`（ProjectHub.jsx:167）。命中的是
    // `.card.glow:hover`（ui.css:201-203 → border accent-glow + shadow-accent）；
    // `views.css:117` 的 `.proj-card:hover`（shadow-2）在**本元素上是死规则** ——
    // 特异度 0,2,0 输给 glow 的 0,3,0，与 CSS 引入顺序无关。
    DrawShadowed(draw, bounds.min, bounds.max, radius, ColorPanel(),
                 cardHovered ? ColorAccentGlow() : ColorLineSubtle(), 1.0f,
                 cardHovered ? theme::ShadowTier::Accent : theme::ShadowTier::None);

    // 封面满幅（views.css:82 .cover 无内缩）。这版 ImGui 没有 PushClipPath，
    // 卡片顶部的两个圆角用同色三角补掉。
    draw->PushClipRect(bounds.min, bounds.max, true);
    Art(draw, Rect{bounds.min.x, bounds.min.y, bounds.max.x, bounds.min.y + 120.0f}, card.artSeed,
        true);
    draw->PopClipRect();
    draw->PathClear();
    draw->PathLineTo(ImVec2(bounds.min.x, bounds.min.y));
    draw->PathLineTo(ImVec2(bounds.min.x + radius, bounds.min.y));
    draw->PathLineTo(ImVec2(bounds.min.x, bounds.min.y + radius));
    draw->PathFillConvex(ColorPanel());
    draw->PathClear();
    draw->PathLineTo(ImVec2(bounds.max.x, bounds.min.y));
    draw->PathLineTo(ImVec2(bounds.max.x - radius, bounds.min.y));
    draw->PathLineTo(ImVec2(bounds.max.x, bounds.min.y + radius));
    draw->PathFillConvex(ColorPanel());
    if (!card.tplLabel.empty()) {
        Tag(draw, RectAt(bounds.min.x + 10.0f, bounds.min.y + 10.0f, TagWidth(card.tplLabel, true, false),
                         TagHeight(true)),
            card.tplLabel, theme::Tone::Accent, true);
    }

    const float bodyX = bounds.min.x + 14.0f;
    float y = bounds.min.y + 120.0f + 12.0f;
    // 项目名：索引里的 name（与 project.json 同源）
    DrawTextClipped(draw, FontBoldAt(14.5f), 14.5f, ImVec2(bodyX, y), bodyWidth, ColorText(),
                    card.entry.name);
    y += 21.0f;
    // 副行：一句话创意。没有就整行留空 —— 不拿模板名之类的字段顶替
    if (!card.premise.empty()) {
        DrawTextClipped(draw, FontAt(12.0f), 12.0f, ImVec2(bodyX, y), bodyWidth, ColorTextMuted(),
                        card.premise);
    }
    y += 21.0f;
    // meta：模板名 · lastOpened 相对时间
    DrawIcon(draw, "book", ImVec2(bodyX, y + 2.0f), 12.0f, ColorTextMuted());
    const std::string meta =
        card.tplName.empty() ? card.when : (card.tplName + " · " + card.when);
    DrawTextClipped(draw, FontAt(12.0f), 12.0f, ImVec2(bodyX + 20.0f, y), bodyWidth - 20.0f,
                    ColorTextMuted(), meta);
    y += 20.0f;
    DrawRoundRect(draw, ImVec2(bodyX, y), ImVec2(bounds.max.x - 14.0f, y + 1.0f), 0.0f,
                  ColorLineSubtle());
    y += 11.0f;

    bool open = false;
    ButtonSpec openSpec;
    openSpec.variant = ButtonVariant::Primary;
    openSpec.size = ButtonSize::Small;
    const float openW = ButtonWidth(ButtonSize::Small, 0.0f, LabelWidth(FontBoldAt(12.0f), 12.0f, "打开"));
    const Rect openRect{bodyX, y, bodyX + openW, y + ButtonHeight(ButtonSize::Small)};
    const bool openPressed =
        Button(draw, openRect, "打开", openSpec, "hub-open-" + card.entry.id);

    float fx = openRect.max.x + 6.0f;
    const float iconSize = 22.0f;
    const float iconH = ButtonHeight(ButtonSize::Small);
    const bool revealPressed =
        IconButton(draw, RectAt(fx, y, iconSize, iconH), "folder", false, false,
                   "hub-reveal-" + card.entry.id, "在资源管理器中显示");
    fx += iconSize + 6.0f;
    const bool morePressed = IconButton(draw, RectAt(fx, y, iconSize, iconH), "dots", false, false,
                                        "hub-more-" + card.entry.id, "更多");
    const bool themePressed = IconButton(
        draw, RectAt(bounds.max.x - 14.0f - iconSize, y, iconSize, iconH), "palette", false, false,
        "hub-theme-" + card.entry.id, "切换主题");

    // ⚠️ 整卡热区**必须排在页脚四个按钮之后**（理由见函数开头那段）。
    const Hit hit = HitTest(bounds, "hub-card-" + card.entry.id);

    // 整卡点击要扣掉页脚按钮：ImGui 的 IsItemClicked 只看「光标在本 item 矩形内」，
    // 页脚按钮压在卡片上，不扣掉的话点「移除」会顺手把项目也打开
    // （等价 webui 里 pfoot 上的 e.stopPropagation）。
    // 改成「按钮先注册」之后 ImGui 自己就把这两次点击分开了，这串判定是双保险。
    open = hit.clicked && !openPressed && !revealPressed && !morePressed && !themePressed;

    if (revealPressed) {
        const std::string error = util::ShellReveal(card.entry.rootDir);
        hub.status = error.empty() ? ("已在资源管理器中显示「" + card.entry.name + "」") : error;
        hub.statusError = !error.empty();
    }
    if (morePressed) {
        hub.confirm = true;
        hub.confirmTitle = "从列表移除「" + card.entry.name + "」？";
        hub.confirmBody = "仅从最近项目列表移除，不会删除项目文件；之后可通过「打开…」重新加入。";
        hub.confirmOk = "移除";
        hub.pendingId = card.entry.id;
    }
    if (themePressed) {
        CycleTheme(hub);
    }
    return open || openPressed;
}

} // namespace hub
} // namespace shine::pages
