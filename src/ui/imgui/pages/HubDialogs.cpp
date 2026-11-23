// 项目中心的**「打开…」对话框**与**移除确认框**
//
// 两个放一起，因为它们是同一族「一次性的确认/选择」浮层，共用同一套外壳
// （PageModal）与同一条命中顺序规矩（整卡命中在 footer 按钮**之后**）。
// 分成两个 30 行的文件只是把「同一族」这件事藏起来。
#include "ui/imgui/pages/Hub.h"

#include "project/ProjectIndex.h"
#include "ui/imgui/kit/Text.h"
#include "ui/imgui/kit/ScrollList.h"
#include "ui/imgui/pages/PageCommon.h"
#include "ui/imgui/pages/PageModal.h"
#include "util/Encoding.h"
#include "util/Shell.h"

#include <string>
#include <utility>
#include <vector>

namespace shine::pages {
namespace hub {

using namespace shine::kit;

void DrawOpenDialog(HubState& hub, Rect area, ImDrawList* draw) {
    bool dismiss = false;
    const ModalBox box = DrawModal(draw, area, "打开项目", "folder", 660.0f, 484.0f,
                                   hub.dismissArmed ? &dismiss : nullptr);
    if (dismiss) {
        hub.openDlg = false;
        return;
    }
    // 搜索框（主屏 hub-search 同款 kit::Input）：名称 / 创意 / 路径任一命中即保留。
    // ⚠️ 过滤必须排在输入**之后**：本帧敲进来的字立刻生效（ProjectHub 工具条同款时序）。
    // ⚠️ 返回值必须吃：kit::Input 是 EnterReturnsTrue、回车才为真 —— 这里当
    //    「Enter = 打开第一条命中」用；丢弃返回值 = 回车什么都不发生。
    // openSearchRect 每帧写下：判据点它拿键盘焦点（settingsClose_ 同款「几何只有一份」）。
    const Rect search = RectAt(box.body.min.x + 20.0f, box.body.min.y + 12.0f,
                               box.body.width() - 40.0f, 30.0f);
    hub.openSearchRect = search;
    const bool submit =
        Input(draw, search, hub.openQuery, "搜索项目名称、创意或路径…", "hub-open-search");
    RefilterOpen(hub);

    const Rect list{ImVec2(box.body.min.x + 20.0f, box.body.min.y + 52.0f),
                    ImVec2(box.body.max.x - 20.0f, box.body.max.y - 12.0f)};

    int picked = -1;
    if (hub.cards.empty()) {
        Empty(draw, list, "folder", "没有最近项目", "通过「浏览目录」打开已有项目");
    } else if (hub.openShown.empty()) {
        // 没匹配与没项目是两件事（同 ProjectHub 的空态口径）：搜索词还在框里，
        // 「请换个搜索词」自己解释自己。
        Empty(draw, list, "search", "没有匹配的项目", "请换个搜索词");
    } else {
        constexpr float pitch = 66.0f;
        ScrollList recent("hub-open-recents", list, static_cast<int>(hub.openShown.size()), pitch,
                          58.0f, 0.0f, false);
        if (recent) {
            for (int i = recent.firstRow(); i < recent.lastRow(); ++i) {
                const int cardIndex = hub.openShown[static_cast<std::size_t>(i)];
                const HubCard& card = hub.cards[static_cast<std::size_t>(cardIndex)];
                ListCardSpec spec;
                spec.id = card.entry.id;
                spec.icon = TemplateIcon(card.tplLabel);
                spec.title = card.entry.name;
                const std::string path = util::PathToUtf8(card.entry.rootDir);
                spec.description = path;
                spec.titleSize = theme::font::kTitle;
                spec.descSize = theme::font::kTiny;
                if (ListCard(recent.drawList(), recent.RowRect(i), spec).clicked) {
                    picked = cardIndex;
                }
            }
        }
    }
    if (submit && !hub.openShown.empty()) {
        picked = hub.openShown.front(); // Enter = 打开第一条命中（搜索框的回车语义）
    }
    if (picked >= 0) {
        Open(hub, hub.cards[static_cast<std::size_t>(picked)]);
        hub.openDlg = !hub.leaveToWorkspace;
        return;
    }

    if (hub.statusError && !hub.status.empty()) {
        TextClipped(draw, ImVec2(box.footer.min.x, box.footer.min.y + 4.0f), box.footer.width() - 208.0f,
                    hub.status, TextStyle{.size = theme::font::kTiny, .color = ToneColor(theme::Tone::Danger)},
                    true);
    }
    ButtonSpec browse;
    browse.variant = ButtonVariant::Primary;
    browse.icon = "folder";
    const std::vector<ModalAction> actions{
        {"取消", ButtonSpec{.variant = ButtonVariant::Ghost}, "hub-open-close"},
        {"浏览目录", browse, "hub-open-browse"},
    };
    const auto hit = DrawModalFooter(draw, box.footer, actions);
    if (hit[0]) {
        hub.openDlg = false;
    } else if (hit[1]) {
        const std::string path = PickFolder();
        if (!path.empty()) {
            HubCard card;
            card.entry.rootDir = util::PathFromUtf8(path);
            Open(hub, card);
            hub.openDlg = !hub.leaveToWorkspace;
        }
    }
}

void DrawConfirmDialog(HubState& hub, Rect area, ImDrawList* draw) {
    bool dismissConfirm = false;
    const ModalBox box = DrawModal(draw, area, hub.confirmTitle, "info", 460.0f, 210.0f,
                                   hub.dismissArmed ? &dismissConfirm : nullptr);
    if (dismissConfirm) {
        hub.confirm = false;
    }
    kit::TextClipped(draw, ImVec2(box.body.min.x + 18.0f, box.body.min.y + 18.0f),
                     box.body.width() - 36.0f, hub.confirmBody,
                     TextStyle{.size = theme::font::kHint, .color = ColorTextSecondary()}, true);
    ButtonSpec ghostSpec;
    ghostSpec.variant = ButtonVariant::Ghost;
    ButtonSpec dangerSpec;
    dangerSpec.variant = ButtonVariant::Danger;
    const std::vector<ModalAction> actions{{"取消", ghostSpec, "hub-confirm-cancel"},
                                           {hub.confirmOk, dangerSpec, "hub-confirm-ok"}};
    const std::vector<int> hit = DrawModalFooter(draw, box.footer, actions);
    if (hit[0] != 0) {
        hub.confirm = false;
    }
    if (hit[1] != 0) {
        Remove(hub, hub.pendingId);
        hub.confirm = false;
    }
}

} // namespace hub
} // namespace shine::pages
