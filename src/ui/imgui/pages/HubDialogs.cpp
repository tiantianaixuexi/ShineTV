// 项目中心的**「打开…」对话框**与**移除确认框**
//
// 两个放一起，因为它们是同一族「一次性的确认/选择」浮层，共用同一套外壳
// （PageModal）与同一条命中顺序规矩（整卡命中在 footer 按钮**之后**）。
// 分成两个 30 行的文件只是把「同一族」这件事藏起来。
#include "ui/imgui/pages/Hub.h"

#include "project/ProjectIndex.h"
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
    bool dismissOpen = false;
    const ModalBox box = DrawModal(draw, area, "打开项目", "folder", 520.0f, 440.0f,
                                   hub.dismissArmed ? &dismissOpen : nullptr);
    if (dismissOpen) {
        hub.openDlg = false;
    }
    // 右上角标出列表的真实来源（索引文件所在目录）
    const std::string indexDir = util::PathToUtf8(project::DefaultIndexFile().parent_path());
    const float indexW = LabelWidth(FontAt(11.5f), 11.5f, indexDir.c_str());
    DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                    ImVec2(box.header.max.x - 20.0f - indexW, box.header.center().y - 5.75f),
                    indexW + 1.0f, ColorTextMuted(), indexDir);

    if (hub.cards.empty()) {
        Empty(draw, Rect{box.body.min.x, box.body.min.y, box.body.max.x, box.body.max.y}, "folder",
              "还没有可打开的项目", "先新建一个项目，或把已有项目目录登记进来");
    } else {
        float y = box.body.min.y + 6.0f;
        for (const HubCard& card : hub.cards) {
            const Rect row{box.body.min.x + 12.0f, y, box.body.max.x - 12.0f, y + 62.0f};
            // 「打开…」列表行走 kit::ListCard（`.card.hoverable`，ProjectHub.jsx:226）。
            // 两行文字块原来自己排 `row.min.y + 12` 与 `+ 34`，块中心偏上 1.875px。
            //
            // ⚠️ 「打开」按钮必须由 ListCard 的 footer 回调画 —— 原代码把它排在
            // 整行 HitTest **之后**，而 ImGui 同窗口内先注册者独占 HoveredId
            //（imgui.cpp:5161），于是那个按钮永远 clicked=false。它当时没暴露，
            // 只因为外面还写了 `|| hit.clicked` 兜底，整卡点击把功能兜住了 ——
            // 按钮画得出来、按下去没反应。footer 在整卡命中**之后**调用，顺序对了。
            const std::string meta =
                card.tplName.empty() ? ("最近 " + card.when) : (card.tplName + " · " + card.when);
            bool openPressed = false;
            kit::ListCardSpec spec;
            spec.id = "hub-open-" + std::string(card.entry.id);
            spec.icon = {}; // 缩略图走 Art，不是 kit 图标
            spec.title = card.entry.name;
            spec.description = meta;
            spec.titleSize = 13.0f;
            spec.descSize = 11.5f;
            spec.paddingX = 12.0f;
            // 缩略图 64 宽 + 10 左内边距 + 12 间距 ⇒ 文字从 +86 起（原代码的 nameX）。
            spec.textInset = 64.0f + 10.0f + 12.0f;
            const Rect rowFinal = row;
            // 缩略图 + 类型 Tag + 「打开」按钮都排在 footer 里，而 footer 由
            // ListCard 在整卡命中**之后**调用（顺序反了按钮永远点不动）。
            const kit::Hit cardHit = kit::ListCard(
                draw, row, spec, [&](ImDrawList* d, Rect footer) {
                // 封面：程序化插画（无外部资源），64×42。
                Art(d, Rect{rowFinal.min.x + 10.0f, rowFinal.min.y + 10.0f, rowFinal.min.x + 74.0f,
                            rowFinal.min.y + 52.0f},
                    card.artSeed, false);
                // 类型 Tag 贴在标题右侧（标题行，不是卡心）。
                if (!card.tplLabel.empty()) {
                    const theme::Tone tone = card.tplLabel == "小说"  ? theme::Tone::Accent
                                              : card.tplLabel == "影视" ? theme::Tone::Info
                                                                        : theme::Tone::Idle;
                    const float tagW = TagWidth(card.tplLabel, true, false);
                    const float nameW = LabelWidth(FontBoldAt(13.0f), 13.0f, card.entry.name.c_str());
                    Tag(d, RectAt(rowFinal.min.x + 86.0f + nameW + 8.0f,
                                 rowFinal.min.y + 10.0f, tagW, TagHeight(true)),
                        card.tplLabel, tone, true);
                }
                ButtonSpec openSpec;
                openSpec.variant = ButtonVariant::Primary;
                openSpec.size = ButtonSize::Small;
                const float openW =
                    ButtonWidth(ButtonSize::Small, 0.0f, LabelWidth(FontBoldAt(12.0f), 12.0f, "打开"));
                const Rect openRect{rowFinal.max.x - 14.0f - openW, rowFinal.center().y - 12.0f,
                                    rowFinal.max.x - 14.0f, rowFinal.center().y + 12.0f};
                openPressed = Button(d, openRect, "打开", openSpec,
                                     "hub-open-btn-" + std::string(card.entry.id));
            });
            // 整卡点击走 ListCard **返回的同一个 Hit**，不能再补一次 HitTest：
            // 同窗口内先注册者独占 HoveredId，第二次永远 clicked=false。
            if (openPressed || cardHit.clicked) {
                // 先按值取出来：Open 会刷新列表，hub.cards 随即重建
                const HubCard target = card;
                hub.openDlg = false;
                Open(hub, target);
                return;
            }
            y += 70.0f;
        }
    }

    ModalFooterHint(draw, box.footer, "单击卡片直接打开");
    ButtonSpec ghostSpec;
    ghostSpec.variant = ButtonVariant::Ghost;
    ButtonSpec folderSpec;
    folderSpec.variant = ButtonVariant::Ghost;
    folderSpec.icon = "folder";
    folderSpec.disabled = hub.cards.empty();
    const std::vector<ModalAction> actions{
        {"打开所在文件夹", folderSpec, "hub-open-reveal"},
        {"关闭", ghostSpec, "hub-open-close"}};
    const std::vector<int> hit = DrawModalFooter(draw, box.footer, actions);
    if (hit[0] != 0) {
        const std::string error = util::ShellReveal(hub.cards.front().entry.rootDir);
        hub.status = error.empty() ? "已在资源管理器中显示项目目录" : error;
        hub.statusError = !error.empty();
    }
    if (hit[1] != 0) {
        hub.openDlg = false;
    }
}

void DrawConfirmDialog(HubState& hub, Rect area, ImDrawList* draw) {
    bool dismissConfirm = false;
    const ModalBox box = DrawModal(draw, area, hub.confirmTitle, "info", 460.0f, 210.0f,
                                   hub.dismissArmed ? &dismissConfirm : nullptr);
    if (dismissConfirm) {
        hub.confirm = false;
    }
    DrawTextClipped(draw, FontAt(12.5f), 12.5f,
                    ImVec2(box.body.min.x + 18.0f, box.body.min.y + 18.0f),
                    box.body.width() - 36.0f, ColorTextSecondary(), hub.confirmBody, true);
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
