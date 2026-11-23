// 项目中心的**新建项目向导**（4 步：模板 / 命名 / 创意 / 确认）
//
// 四步的 body 各自独立，但**页脚与状态提示是共用的**：每步都要能「取消 / 上一步 /
// 下一步」，都要把本次操作的真实结果摆在页脚左侧。所以那部分留在函数末尾而不是
// 塞进某个 step 分支 —— 塞进去的话，第四步就会成为唯一有「取消」的那一步。
#include "ui/imgui/pages/Hub.h"

#include "project/ProjectTemplate.h"
#include "ui/imgui/kit/Text.h"
#include "ui/imgui/pages/PageCommon.h"
#include "ui/imgui/pages/PageModal.h"
#include "util/Encoding.h"
#include "util/Strings.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace shine::pages {
namespace hub {

using namespace shine::kit;

void DrawWizard(HubState& hub, Rect area, ImDrawList* draw) {
    static const std::vector<std::string> stepNames{"模板", "命名", "创意", "确认"};
    bool dismissWizard = false;
    // dismissArmed 为假（= 本帧才被打开）时传 nullptr：这一下打开它的点击
    // 不能顺手把它关掉。理由见 ProjectHub.cpp 里 dismissArmed 的说明。
    const ModalBox box = DrawModal(draw, area, "新建项目", "sparkles", 824.0f, 572.0f,
                                   hub.dismissArmed ? &dismissWizard : nullptr);
    if (dismissWizard) {
        hub.wizard = false;
    }
    const TextStyle stepLabel{.size = theme::font::kBody, .bold = true, .color = ColorText()};
    const float dividerX = box.body.min.x + 164.0f;
    const Rect stepsPanel{box.body.min, ImVec2(dividerX, box.body.max.y)};
    DrawRoundRect(draw, stepsPanel.min, stepsPanel.max, 0.0f, ColorSurface());
    Stack steps{Rect{ImVec2(stepsPanel.min.x + 14.0f, stepsPanel.min.y + 20.0f),
                     ImVec2(stepsPanel.max.x - 14.0f, stepsPanel.max.y)}};
    for (int step = 0; step < static_cast<int>(stepNames.size()); ++step) {
        const Rect row = steps.Next(38.0f, 8.0f);
        if (step == hub.step) {
            DrawRoundRect(draw, row.min, row.max, 6.0f, ColorFillSelected(), ColorLineNormal(), 1.0f);
        }
        const std::string label = kit::Fmt("{}. {}", step + 1, stepNames[static_cast<std::size_t>(step)]);
        Text(draw, ImVec2(row.min.x + 12.0f, CenterY(row.center().y, stepLabel)), label, stepLabel);
    }
    static constexpr const char* headings[] = {
        "为项目选择起点", "命名并选择位置", "写下你的创意", "确认并创建项目",
    };
    const Rect body{ImVec2(dividerX + 20.0f, box.body.min.y + 54.0f),
                    ImVec2(box.body.max.x - 20.0f, box.body.max.y - 12.0f)};
    Text(draw, ImVec2(body.min.x, body.min.y - 32.0f), headings[hub.step],
         TextStyle{.size = theme::font::kChapter, .bold = true, .color = ColorText()});
    if (hub.step == 0) {
        float y = body.min.y;
        for (const project::ProjectTemplate& tpl : project::AllTemplates()) {
            const Rect row{body.min.x, y, body.max.x, y + 62.0f};
            // 模板行走 kit::ListCard：hover 投影档 / 选中 accent 边 / 双行块的
            // 垂直居中全在里面。原来这一段自己排 `row.min.y + 14` 与 `+ 34`，
            // 两行文字的**块**中心偏上 0.875px —— 两行各自都不居中，靠硬凑。
            kit::ListCardSpec spec;
            // id 在栈上拼（第二十轮波 3）。原写法把临时 string 的 view 存进
            // spec.id(string_view) —— 临时量在语句结束就析构，view 悬垂
            //（clang -Wdangling-assignment-gsl 点名的地方）。
            char tplIdBuf[32];
            const int tplIdN =
                std::snprintf(tplIdBuf, sizeof(tplIdBuf), "hub-tpl-%s", tpl.id.c_str());
            spec.id = std::string_view(tplIdBuf, tplIdN > 0 ? static_cast<std::size_t>(tplIdN) : 0u);
            spec.icon = TemplateIcon(tpl.id);
            spec.iconSize = 20.0f;
            spec.title = tpl.name;
            spec.description = tpl.description;
            spec.titleSize = theme::font::kTitle;
            spec.descSize = theme::font::kBody;
            spec.textGap = 4.0f;
            spec.paddingX = 14.0f;
            // 文字原从 +60 起：图标 20 + 8 间距 + 14 padding = 42，再让 18 给
            // 右侧的「选中勾」⇒ 60。这里 paddingX 已经是 14，textInset 补到 60。
            spec.textInset = 60.0f - 14.0f - (20.0f + 8.0f);
            spec.selected = hub.tpl == tpl.id;
            if (kit::ListCard(draw, row, spec).clicked) {
                hub.tpl = tpl.id;
            }
            y += 70.0f;
        }
    } else if (hub.step == 1) {
        const Rect nameField =
            Field(draw, Rect{body.min.x, body.min.y, body.max.x, body.min.y + 48.0f}, "项目名称", {});
        Input(draw, nameField, hub.name, "例如：灯语回声", "hub-wiz-name");
        const Rect dirField = Field(
            draw, Rect{body.min.x, nameField.max.y + 12.0f, body.max.x, nameField.max.y + 86.0f},
            "位置", "项目将创建在所选目录下");
        constexpr const char* browseLabel = "浏览…";
        const float browseW =
            ButtonWidth(ButtonSize::Medium, 15.0f,
                        kit::TextWidth(browseLabel, TextStyle{.size = theme::font::kBody}));
        Input(draw, Rect{dirField.min.x, dirField.min.y, dirField.max.x - browseW - 8.0f,
                         dirField.max.y},
              hub.dir, "E:\\projects", "hub-wiz-dir");
        ButtonSpec browseSpec;
        browseSpec.variant = ButtonVariant::Secondary;
        browseSpec.icon = "folder";
        if (Button(draw, RectAt(dirField.max.x - browseW, dirField.min.y, browseW, 30.0f),
                   browseLabel, browseSpec, "hub-wiz-browse")) {
            const std::string picked = PickFolder();
            if (!picked.empty()) {
                hub.dir = picked;
            }
        }
        // 将创建目录：目标路径 + PreviewTree 给出的真实骨架清单
        const Rect preview{body.min.x, dirField.max.y + 16.0f, body.max.x, body.max.y};
        DrawRoundRect(draw, preview.min, preview.max, 10.0f, ColorFillMuted(), ColorLineSubtle(),
                      1.0f);
        const char* previewLabel = "将创建目录";
        kit::Text(draw, ImVec2(preview.min.x + 14.0f, preview.min.y + 10.0f), previewLabel,
                  TextStyle{.size = theme::font::kMicro, .color = ColorTextMuted()});
        const std::filesystem::path target = TargetDir(hub);
        const std::string targetText = target.empty() ? "<项目名>" : util::PathToUtf8(target);
        kit::TextClipped(draw, ImVec2(preview.min.x + 14.0f, preview.min.y + 28.0f),
                         preview.width() - 28.0f, targetText,
                         TextStyle{.size = theme::font::kTiny, .mono = true, .color = ColorText()});
        // 预览树只随模板变：模板没切就复用（第二十轮波 3；向导单实例、UI 单线程）。
        static std::string cachedTpl;
        static std::vector<std::string> tree;
        if (cachedTpl != hub.tpl) {
            cachedTpl = hub.tpl;
            tree = project::PreviewTree(hub.tpl);
        }
        float ty = preview.min.y + 50.0f;
        const int lines = std::clamp(static_cast<int>((preview.max.y - ty - 13.0f) / 15.0f), 0, 6);
        for (int i = 0; i < lines && i < static_cast<int>(tree.size()); ++i) {
            kit::TextClipped(draw, ImVec2(preview.min.x + 14.0f, ty), preview.width() - 28.0f,
                             tree[i],
                             TextStyle{.size = theme::font::kMicro, .mono = true,
                                       .color = ColorTextMuted()});
            ty += 15.0f;
        }
        if (lines > 0 && tree.size() > static_cast<std::size_t>(lines)) {
            // 「共 N 项」只随条数变（第二十轮波 3）。
            static int cachedTreeSize = -1;
            static std::string more;
            if (cachedTreeSize != static_cast<int>(tree.size())) {
                cachedTreeSize = static_cast<int>(tree.size());
                more = kit::Fmt("… 共 {} 项", tree.size());
            }
            kit::Text(draw, ImVec2(preview.min.x + 14.0f, ty), more,
                      TextStyle{.size = theme::font::kMicro, .mono = true, .color = ColorTextMuted()});
        }
    } else if (hub.step == 2) {
        // 字数标签只随字数变（打字帧之外不再重建，第二十轮波 3）。
        static int cachedIdeaCount = -1;
        static std::string label;
        const int ideaCount = CharCount(hub.idea);
        if (cachedIdeaCount != ideaCount) {
            cachedIdeaCount = ideaCount;
            label = kit::Fmt("一句话创意（{}/200）", ideaCount);
        }
        const Rect ideaField = Field(draw, Rect{body.min.x, body.min.y, body.max.x, body.max.y},
                                     label, "将写入 project.json，供 LLM 初始化参考；留空可跳过");
        TextArea(draw,
                 Rect{ideaField.min.x, ideaField.min.y, ideaField.max.x, ideaField.min.y + 120.0f},
                 hub.idea, 5, "hub-wiz-idea");
    } else {
        const Rect card{body.min.x, body.min.y, body.max.x, body.min.y + 96.0f};
        DrawShadowed(draw, card.min, card.max, theme::radius::kMd, ColorPanel(), ColorAccentGlow(),
                     1.0f, theme::ShadowTier::Accent);
        Art(draw, Rect{card.min.x + 16.0f, card.min.y + 16.0f, card.min.x + 112.0f, card.min.y + 80.0f},
            7, false);
        const std::string title = util::Trim(hub.name).empty() ? "未命名项目"
                                                                    : std::string(util::Trim(hub.name));
        // `.proj-card .pname { font-size: 14.5px }`（views.css:97-99）。原来这里是 17.0 ——
        // 栅格里的真项目卡（HubCard.cpp）早就改成 14.5 了，这张向导汇总卡漏改，
        // 于是「同一个项目名」在列表里和确认页上**字号不一样**。
        kit::TextClipped(draw, ImVec2(card.min.x + 128.0f, card.min.y + 16.0f), card.width() - 144.0f, title,
                  TextStyle{.size = theme::font::kCardTitle, .bold = true, .color = ColorText()});
        const std::filesystem::path target = TargetDir(hub);
        const std::string pathText = target.empty() ? std::string{} : util::PathToUtf8(target);
        kit::TextClipped(draw, ImVec2(card.min.x + 128.0f, card.min.y + 40.0f),
                         card.width() - 144.0f, pathText,
                         TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()});
        const project::ProjectTemplate* tpl = project::FindTemplate(hub.tpl);
        const std::string tplName = tpl != nullptr ? tpl->name : std::string{};
        float tagX = card.min.x + 128.0f;
        if (!tplName.empty()) {
            const float tagW = TagWidth(tplName, true, false);
            Tag(draw, RectAt(tagX, card.min.y + 62.0f, tagW, TagHeight(true)), tplName,
                theme::Tone::Accent, true);
            tagX += tagW + 6.0f;
        }
        const char* ideaTag = util::Trim(hub.idea).empty() ? "无创意" : "含一句话创意";
        const float ideaW = TagWidth(ideaTag, true, false);
        Tag(draw, RectAt(tagX, card.min.y + 62.0f, ideaW, TagHeight(true)), ideaTag,
            theme::Tone::Idle, true);
        const char* note = "创建后将打开工作坊：总控 / 小说 / 资产 / 分镜 / 出图 / 出片 六个工作区可用。";
        kit::TextClipped(draw, ImVec2(body.min.x, card.max.y + 14.0f), body.width(), note,
                         TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()});
    }

    // 页脚左侧：本次操作的实际结果（错误时用 danger 色），没有就摆事实提示
    if (!hub.status.empty()) {
        const TextStyle statusStyle{.size = theme::font::kMicro,
                                    .color = hub.statusError ? ToneColor(theme::Tone::Danger)
                                                             : ColorTextMuted()};
        kit::TextClipped(draw, ImVec2(box.footer.min.x + 20.0f, box.footer.center().y - 10.0f),
                         box.footer.width() - 260.0f, hub.status, statusStyle, true);
    }

    ButtonSpec ghostSpec;
    ghostSpec.variant = ButtonVariant::Ghost;
    ButtonSpec secondarySpec;
    secondarySpec.variant = ButtonVariant::Secondary;
    ButtonSpec primarySpec;
    primarySpec.variant = ButtonVariant::Primary;
    if (hub.step == 3) {
        primarySpec.icon = "sparkles";
    }
    std::vector<ModalAction> actions;
    actions.push_back({"取消", ghostSpec, "hub-wiz-cancel"});
    if (hub.step > 0) {
        actions.push_back({"上一步", secondarySpec, "hub-wiz-prev"});
    }
    actions.push_back({hub.step < 3 ? "下一步" : "创建", primarySpec,
                       hub.step < 3 ? "hub-wiz-next" : "hub-wiz-create"});
    const std::vector<int> hit = DrawModalFooter(draw, box.footer, actions);

    if (hit[0] != 0) {
        hub.wizard = false;
        hub.status.clear();
        hub.statusError = false;
        return;
    }
    if (hub.step > 0 && hit[1] != 0) {
        hub.step -= 1;
        hub.status.clear();
        hub.statusError = false;
        return;
    }
    if (hit.back() == 0) {
        return;
    }
    if (hub.step < 3) {
        // canNext：只有第 1 步（命名）会挡人，其余步都能过（设计稿 ProjectHub.jsx:22）
        if (hub.step == 1 && util::Trim(hub.name).empty()) {
            hub.status = "请输入项目名称后再继续";
            hub.statusError = true;
            return;
        }
        if (hub.step == 2 && CharCount(hub.idea) > 200) {
            hub.status = "一句话创意超过 200 字，请删减后再继续";
            hub.statusError = true;
            return;
        }
        hub.step += 1;
        hub.status.clear();
        hub.statusError = false;
        return;
    }

    // 第 4 步：真建。ValidateSpec / Materialize 的中文错误直接摆给用户。
    project::ProjectSpec spec;
    spec.name = std::string(util::Trim(hub.name));
    spec.rootDir = TargetDir(hub);
    spec.templateId = hub.tpl;
    spec.premise = std::string(util::Trim(hub.idea));
    const std::expected<project::ProjectRef, project::Error> created = hub.service.Create(spec);
    if (!created) {
        hub.status = "创建失败：" + created.error().message;
        hub.statusError = true;
        return;
    }
    hub.wizard = false;
    hub.name.clear();
    hub.idea.clear();
    hub.step = 0;
    hub.status = "已创建并打开项目「" + created->name + "」· " + util::PathToUtf8(created->rootDir);
    hub.statusError = false;
    hub.leaveToWorkspace = true;
    Refresh(hub);
}

} // namespace hub
} // namespace shine::pages
