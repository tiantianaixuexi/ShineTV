// 项目中心的**新建项目向导**（4 步：模板 / 命名 / 创意 / 确认）
//
// 四步的 body 各自独立，但**页脚与状态提示是共用的**：每步都要能「取消 / 上一步 /
// 下一步」，都要把本次操作的真实结果摆在页脚左侧。所以那部分留在函数末尾而不是
// 塞进某个 step 分支 —— 塞进去的话，第四步就会成为唯一有「取消」的那一步。
#include "ui/imgui/pages/Hub.h"

#include "project/ProjectTemplate.h"
#include "ui/imgui/pages/PageCommon.h"
#include "ui/imgui/pages/PageModal.h"
#include "util/Encoding.h"
#include "util/Strings.h"

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
    const ModalBox box = DrawModal(draw, area, "新建项目", "sparkles", 600.0f, 420.0f,
                                   hub.dismissArmed ? &dismissWizard : nullptr);
    if (dismissWizard) {
        hub.wizard = false;
    }
    const float stepsW = StepsWidth(stepNames);
    Steps(draw, RectAt(box.header.max.x - 20.0f - stepsW, box.header.center().y - 10.0f, stepsW, 20.0f),
          stepNames, hub.step);

    const Rect body{box.body.min.x + 20.0f, box.body.min.y + 16.0f, box.body.max.x - 20.0f,
                    box.body.max.y - 8.0f};
    if (hub.step == 0) {
        float y = body.min.y;
        for (const project::ProjectTemplate& tpl : project::AllTemplates()) {
            const Rect row{body.min.x, y, body.max.x, y + 62.0f};
            // 模板行走 kit::ListCard：hover 投影档 / 选中 accent 边 / 双行块的
            // 垂直居中全在里面。原来这一段自己排 `row.min.y + 14` 与 `+ 34`，
            // 两行文字的**块**中心偏上 0.875px —— 两行各自都不居中，靠硬凑。
            kit::ListCardSpec spec;
            spec.id = "hub-tpl-" + std::string(tpl.id);
            spec.icon = TemplateIcon(tpl.id);
            spec.iconSize = 20.0f;
            spec.title = tpl.name;
            spec.description = tpl.description;
            spec.titleSize = 13.0f;
            spec.descSize = 11.5f;
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
            draw, Rect{body.min.x, nameField.max.y + 18.0f, body.max.x, nameField.max.y + 66.0f},
            "位置", "项目目录将创建在该路径下");
        constexpr const char* browseLabel = "浏览…";
        const float browseW =
            ButtonWidth(ButtonSize::Medium, 15.0f, LabelWidth(FontAt(13.0f), 13.0f, browseLabel));
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
        draw->AddText(FontAt(11.0f), 11.0f, ImVec2(preview.min.x + 14.0f, preview.min.y + 10.0f),
                      ColorTextMuted(), previewLabel, previewLabel + std::strlen(previewLabel));
        const std::filesystem::path target = TargetDir(hub);
        const std::string targetText = target.empty() ? "<项目名>" : util::PathToUtf8(target);
        DrawTextClipped(draw, MonoAt(12.0f), 12.0f, ImVec2(preview.min.x + 14.0f, preview.min.y + 28.0f),
                        preview.width() - 28.0f, ColorText(), targetText);
        const std::vector<std::string> tree = project::PreviewTree(hub.tpl);
        float ty = preview.min.y + 50.0f;
        for (std::size_t i = 0; i < tree.size() && i < 6; ++i) {
            DrawTextClipped(draw, MonoAt(11.0f), 11.0f,
                            ImVec2(preview.min.x + 14.0f, ty), preview.width() - 28.0f,
                            ColorTextMuted(), tree[i]);
            ty += 15.0f;
        }
        if (tree.size() > 6) {
            const std::string more = "… 共 " + std::to_string(tree.size()) + " 项";
            draw->AddText(MonoAt(11.0f), 11.0f, ImVec2(preview.min.x + 14.0f, ty), ColorTextMuted(),
                          more.data(), more.data() + more.size());
        }
    } else if (hub.step == 2) {
        const std::string label = "一句话创意（" + std::to_string(CharCount(hub.idea)) + "/200）";
        const Rect ideaField = Field(draw, Rect{body.min.x, body.min.y, body.max.x, body.max.y},
                                     label, "将写入 project.json，供 LLM 初始化参考；留空可跳过");
        TextArea(draw,
                 Rect{ideaField.min.x, ideaField.min.y, ideaField.max.x, ideaField.min.y + 120.0f},
                 hub.idea, 5, "hub-wiz-idea");
    } else {
        const Rect card{body.min.x, body.min.y, body.max.x, body.min.y + 96.0f};
        DrawShadowed(draw, card.min, card.max, 10.0f, ColorPanel(), ColorLineSubtle(), 1.0f);
        Art(draw, Rect{card.min.x + 16.0f, card.min.y + 16.0f, card.min.x + 112.0f, card.min.y + 80.0f},
            7, false);
        const std::string title = util::Trim(hub.name).empty() ? "未命名项目"
                                                                    : std::string(util::Trim(hub.name));
        draw->AddText(FontBoldAt(17.0f), 17.0f, ImVec2(card.min.x + 128.0f, card.min.y + 16.0f),
                      ColorText(), title.data(), title.data() + title.size());
        const std::filesystem::path target = TargetDir(hub);
        const std::string pathText = target.empty() ? std::string{} : util::PathToUtf8(target);
        DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(card.min.x + 128.0f, card.min.y + 40.0f),
                        card.width() - 144.0f, ColorTextMuted(), pathText);
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
        DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                        ImVec2(body.min.x, card.max.y + 14.0f), body.width(), ColorTextMuted(), note);
    }

    // 页脚左侧：本次操作的实际结果（错误时用 danger 色），没有就摆事实提示
    if (hub.status.empty()) {
        ModalFooterHint(draw, box.footer, "项目骨架只写入本机目录");
    } else {
        DrawTextClipped(draw, FontAt(11.0f), 11.0f,
                        ImVec2(box.footer.min.x + 20.0f, box.footer.center().y - 5.5f), 300.0f,
                        hub.statusError ? ToneColor(theme::Tone::Danger) : ColorTextMuted(),
                        hub.status, true);
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
            hub.status = "项目名称不能为空（ValidateSpec 也会拦，这里先提示一次）";
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
    Refresh(hub);
}

} // namespace hub
} // namespace shine::pages
