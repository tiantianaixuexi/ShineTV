// 项目中心的**项目卡**（栅格里的每一格）
//
// 这一块的全部难点是**命中顺序**，不是绘制。整卡热区与页脚四个按钮压在**同一批
// 像素**上，而 ImGui 同一窗口内是「先注册者独占 HoveredId / ActiveId」——
// 所以顺序反了不会编译报错、不会崩、截图也全绿，只是「点『…』变成打开项目」。
// 那段注释因此留在函数开头，不是注释洁癖，是这份代码的全部内容。
#include "ui/imgui/pages/Hub.h"

#include "ui/imgui/kit/Text.h"
#include "ui/imgui/pages/PageCommon.h"
#include "util/Encoding.h"
#include "util/Shell.h"

#include <cstring>
#include <string>

namespace shine::pages {
namespace hub {

using namespace shine::kit;

bool DrawCard(ImDrawList* draw, HubState& hub, const HubCard& card, Rect bounds) {
    const float y = bounds.center().y;
    constexpr float actionWidth = 38.0f;
    constexpr float openWidth = 52.0f;
    const float actionsX = bounds.max.x - openWidth - actionWidth * 3.0f - 16.0f;
    const Rect body{bounds.min, ImVec2(actionsX - 8.0f, bounds.max.y)};
    const bool hovered = !hub.dismissArmed && bounds.contains(ImGui::GetIO().MousePos);
    DrawRoundRect(draw, bounds.min, bounds.max, 0.0f, hovered ? ColorFillHover() : ColorPanel());
    draw->AddLine(ImVec2(bounds.min.x, bounds.max.y - 1.0f),
                  ImVec2(bounds.max.x, bounds.max.y - 1.0f), ColorLineNormal());
    const float x = bounds.min.x + 16.0f;
    const TextStyle title{.size = theme::font::kTitle, .bold = true, .color = ColorText()};
    const TextStyle details{.size = theme::font::kTiny, .color = ColorTextSecondary()};
    const std::string path = util::PathToUtf8(card.entry.rootDir);
    TextClipped(draw, ImVec2(x, y - 18.0f), body.max.x - x, card.entry.name, title);
    TextClipped(draw, ImVec2(x, y + 4.0f), body.max.x - x, path, details);

    const bool open = HitTest(body, "hub-card-" + card.entry.id).clicked;
    const bool openPressed = Button(draw, RectAt(actionsX, y - 14.0f, openWidth, 28.0f),
                                    "打开", ButtonSpec{.variant = ButtonVariant::Secondary},
                                    "hub-open-" + card.entry.id);
    const float fx = actionsX + openWidth + 4.0f;
    const bool revealPressed = IconButton(draw, RectAt(fx, y - 14.0f, 32.0f, 28.0f),
                                          "folder", false, false, "hub-reveal-" + card.entry.id,
                                          "在资源管理器中显示");
    const bool morePressed = IconButton(draw, RectAt(fx + actionWidth, y - 14.0f, 32.0f, 28.0f),
                                        "dots", false, false, "hub-more-" + card.entry.id, "更多");
    const bool themePressed = IconButton(draw, RectAt(fx + actionWidth * 2.0f, y - 14.0f,
                                                      32.0f, 28.0f),
                                         "palette", false, false, "hub-theme-" + card.entry.id,
                                         "切换主题");

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
