// shine::pages::Gallery —— 组件画廊（**原生 ImGui 试点页**）
//
// 这一页是「去掉 1:1 复刻约束、改用 ImGui 原生控件」的唯一试点判据：
//   * 布局 / 裁剪 / 滚动 / 焦点 / 文本编辑 / 排序 / 弹出层 全部由 ImGui 负责；
//   * 卡片高度由内容决定（BeginChild + AutoResizeY），不再手写「148 / 130 / 240」；
//   * 颜色仍来自 theme（ApplyTheme 已把 31 个 token 铺进 ImGuiStyle），
//     页面层**不再自己推主题色**。
//
// 与旧版的差异是有意的（约束已去掉）：
//   * 不再是 repeat(auto-fit, minmax(340px,1fr)) 两列网格，改单列卡片流；
//     可用宽度由 ImGui 给，窗口变窄时自动回流。
//   * 控件尺寸由 ImGuiStyle（FramePadding / ItemSpacing / Rounding）决定，
//     不再逐控件写死 30 / 28 / 26 这些 CSS 像素。
//   * Steps / StageFlow / Art 仍自绘 —— 它们是**图**不是控件（没有命中/焦点语义），
//     塞进 Button 只会得到一个假的可点区域。
//
// 验收点（下一轮铺开到其余页面时按这四条复查）：
//   1. 卡片高度自适应：内容多高，卡就多高，不靠常量表；
//   2. 窗口变窄 / 变高：自动换行与滚动，不需要 setContentHeight；
//   3. 键盘可达：Tab 能遍历控件，输入框能打字，弹出层能开合；
//   4. 切主题：颜色跟着变（不需要页面参与）。

#include "ui/imgui/pages/Gallery.h"

#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Icon.h"
#include "ui/imgui/kit/Text.h"
#include "ui/imgui/kit/UiBridge.h"
#include "ui/imgui/kit/Views.h"
#include "ui/imgui/pages/PageCommon.h"
#include "ui/imgui/theme/Theme.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shine::pages {
namespace {

using namespace shine::kit;

// ---------------------------------------------------------------- 小工具

// 卡片标题：13px / 700 / text.secondary（设计稿 .card-h）。
void CardTitle(std::string_view text) {
    PushUiFont(theme::font::kHint, /*bold=*/true);
    ImGui::TextColored(theme::Rgba(theme::Current().textSecondary), "%.*s",
                       static_cast<int>(text.size()), text.data());
    PopUiFont();
    ImGui::Spacing();
}

void CardSpacing() { ImGui::Dummy(ImVec2(0.0f, 6.0f)); }

// 每张卡：BeginUiCard + 14px 内边距。End 必须在 EndUiCard 之前。
void CardBegin(const char* id) {
    BeginUiCard(id);
    BeginUiCardBody();
}
void CardEnd() {
    EndUiCardBody();
    EndUiCard();
}

// ---- Tag（非交互标签）----
// 用自绘而不是 Selectable：它没有选中语义，套一个可选控件只会凭空多出 hover 态。
ImU32 TagBg(theme::Tone tone) {
    return ColorOf(theme::CurrentDerived().tagBg[static_cast<std::size_t>(tone)]);
}
ImU32 TagBorder(theme::Tone tone) {
    return ColorOf(theme::CurrentDerived().tagBorder[static_cast<std::size_t>(tone)]);
}
ImU32 TagFg(theme::Tone tone) {
    return tone == theme::Tone::Idle ? ColorTextMuted() : ToneColor(tone);
}

// 原 PushUiFont 将 kSmall 取整到 kMicro，CalcTextSize 将宽度向上取整。
constexpr TextStyle kTagTextStyle{.size = theme::font::kMicro, .bold = true};

ImVec2 TagSize(std::string_view label, float fontHeight) {
    const float textW = std::ceil(kit::TextWidth(label, kTagTextStyle));
    return ImVec2(textW + 16.0f, fontHeight + 8.0f);
}

void DrawTagAt(ImVec2 p, std::string_view label, theme::Tone tone) {
    const float h = theme::font::kSmall + 8.0f;
    const ImVec2 sz = TagSize(label, h);
    const ImVec2 p2(p.x + sz.x, p.y + sz.y);
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, p2, TagBg(tone), 999.0f);
    d->AddRect(p, p2, TagBorder(tone), 999.0f, 0, 1.0f);
    TextStyle style = kTagTextStyle;
    style.color = TagFg(tone);
    kit::Text(d, ImVec2(p.x + 8.0f, p.y + 0.5f * (sz.y - style.size)), label, style);
}

// 一组 Tag 的自动换行。返回值是**排版用**的终点，不依赖 ImGui 的 item 流。
class TagPen {
public:
    void Begin(float gapX, float gapY) {
        start_ = ImGui::GetCursorScreenPos();
        x_ = start_.x;
        y_ = start_.y;
        right_ = start_.x + std::max(1.0f, ImGui::GetContentRegionAvail().x);
        rowH_ = 0.0f;
        gapX_ = gapX;
        gapY_ = gapY;
        used_ = false;
    }
    void Place(std::string_view label, theme::Tone tone) {
        const ImVec2 sz = TagSize(label, theme::font::kSmall + 8.0f);
        if (used_ && x_ + sz.x > right_) {
            x_ = start_.x;
            y_ += rowH_ + gapY_;
            rowH_ = 0.0f;
        }
        DrawTagAt(ImVec2(x_, y_), label, tone);
        x_ += sz.x + gapX_;
        rowH_ = std::max(rowH_, sz.y);
        used_ = true;
    }
    // 把「画了多少高」提交给 ImGui —— 否则父窗口不知道内容有多高（旧实现的手写
    // setContentHeight 就是干这个的，原生布局下不需要）。
    void End() {
        ImGui::SetCursorScreenPos(start_);
        const float used = (y_ - start_.y) + rowH_;
        ImGui::Dummy(ImVec2(std::max(1.0f, right_ - start_.x), used));
    }

private:
    ImVec2 start_{};
    float x_ = 0.0f, y_ = 0.0f, right_ = 0.0f, rowH_ = 0.0f;
    float gapX_ = 6.0f, gapY_ = 6.0f;
    bool used_ = false;
};

// ---------------------------------------------------------------- 卡片

void Card_Buttons() {
    CardBegin("gal-card-buttons");
    CardTitle("Button 4 变体 × 3 尺寸");

    struct Variant {
        const char* label;
        ImU32 bg, hover, active, fg;
    };
    const Variant variants[] = {
        // 主按钮：accent 底 + accent-fg 字。
        {"主按钮", ColorAccent(), ColorAccentHover(), ColorAccentHover(), ColorAccentFg()},
        // 次按钮：fill.muted → fill.hover → fill.selected。
        {"次按钮", ColorFillMuted(), ColorFillHover(), ColorFillSelected(), ColorText()},
        // 幽灵：透明底，只有 hover/active 才有面。
        {"幽灵", ColorTransparent(), ColorFillHover(), ColorFillSelected(), ColorTextSecondary()},
        // 危险：status.danger 底。
        {"危险", ColorOf(theme::Current().statusDanger), ColorOf(theme::Current().statusDanger),
         ColorOf(theme::Current().statusDanger), ColorTextInverse()},
    };

    for (const Variant& v : variants) {
        ImGui::PushID(v.label);
        ImGui::PushStyleColor(ImGuiCol_Button, v.bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v.hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, v.active);
        ImGui::PushStyleColor(ImGuiCol_Text, v.fg);
        ImGui::Button(v.label, ImVec2(96.0f, 30.0f));
        ImGui::PopStyleColor(4);
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::PopID();
    }
    ImGui::NewLine();

    // 尺寸档：只有 FramePadding 在变 —— 高度与内边距是 ImGui 自己的算法。
    struct SizeCase {
        const char* label;
        ImVec2 pad;
    };
    const SizeCase sizes[] = {
        {"Small", ImVec2(8.0f, 3.0f)},
        {"Medium", ImVec2(12.0f, 7.0f)},
        {"Large", ImVec2(16.0f, 11.0f)},
    };
    for (int i = 0; i < 3; ++i) {
        ImGui::PushID(i);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, sizes[i].pad);
        ImGui::Button(sizes[i].label);
        ImGui::PopStyleVar();
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::PopID();
    }
    ImGui::NewLine();

    ImGui::BeginDisabled();
    ImGui::Button("已禁用");
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::BeginDisabled();
    ImGui::Button("加载中…");
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::TextDisabled("BeginDisabled / EndDisabled 由 ImGui 管命中与灰显");

    CardEnd();
    ImGui::Spacing();
}

void Card_Tags() {
    CardBegin("gal-card-tags");
    CardTitle("Tag / Kbd");
    const theme::Tone tones[] = {theme::Tone::Accent, theme::Tone::Info, theme::Tone::Ok,
                                 theme::Tone::Warn,   theme::Tone::Danger, theme::Tone::Busy,
                                 theme::Tone::Idle};
    const char* names[] = {"accent", "info", "ok", "warn", "danger", "busy", "idle"};

    TagPen pen;
    pen.Begin(6.0f, 6.0f);
    for (int i = 0; i < 7; ++i) {
        pen.Place(names[i], tones[i]);
    }
    pen.End();

    CardSpacing();
    const char* kbds[] = {"Ctrl", "K", "Shift", "Enter"};
    const TextStyle kbdStyle{.size = theme::font::kMicro, .bold = true, .color = ColorTextMuted()};
    for (const char* k : kbds) {
        const float w = std::ceil(kit::TextWidth(k, kbdStyle)) + 12.0f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            p, ImVec2(p.x + w, p.y + 18.0f), ColorFillMuted(), 4.0f);
        ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + w, p.y + 18.0f), ColorLineNormal(),
                                            4.0f, 0, 1.0f);
        kit::Text(ImGui::GetWindowDrawList(), ImVec2(p.x + 6.0f, p.y + 3.0f), k, kbdStyle);
        ImGui::Dummy(ImVec2(w, 18.0f));
        ImGui::SameLine(0.0f, 6.0f);
    }
    ImGui::NewLine();

    CardEnd();
    ImGui::Spacing();
}

void Card_SegmentedTabs() {
    CardBegin("gal-card-seg");
    CardTitle("Segmented / Tabs");
    static int seg = 0;
    static int tab = 0;

    // Segmented：用 Selectable + 一个容器 child 拼（ImGui 没有原生 segmented）。
    // 收益是**每段的命中/焦点/键盘导航免费**。
    const char* segItems[] = {"详情", "总览"};
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ColorFillMuted());
    ImGui::BeginChild("gal-seg", ImVec2(0.0f, 32.0f), ImGuiChildFlags_Borders);
    ImGui::PopStyleColor();
    for (int i = 0; i < 2; ++i) {
        ImGui::PushID(i);
        if (i > 0) {
            ImGui::SameLine();
        }
        if (ImGui::Selectable(segItems[i], seg == i, ImGuiSelectableFlags_None,
                              ImVec2(ImGui::CalcTextSize(segItems[i]).x + 24.0f, 0.0f))) {
            seg = i;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::BeginTabBar("gal-tabs", ImGuiTabBarFlags_None)) {
        const char* tabs[] = {"章节", "设定", "评审"};
        for (int i = 0; i < 3; ++i) {
            ImGui::PushID(i);
            if (ImGui::BeginTabItem(tabs[i], nullptr, tab == i ? ImGuiTabItemFlags_SetSelected
                                                              : ImGuiTabItemFlags_None)) {
                tab = i;
                ImGui::TextDisabled("第 %d 个页签的内容", i + 1);
                ImGui::EndTabItem();
            }
            ImGui::PopID();
        }
        ImGui::EndTabBar();
    }

    CardEnd();
    ImGui::Spacing();
}

void Card_Inputs() {
    CardBegin("gal-card-inputs");
    CardTitle("Field / Input / TextArea / Select / Switch / Checkbox");
    static char name[64] = "分镜图_v3";
    static char json[256] = "{\n  \"code\": \"S012\",\n  \"duration\": 6.0\n}";
    static int combo = 1;
    static bool on = true;
    static bool checked = true;
    static float slider = 0.64f;
    static int radio = 0;

    PushUiFont(theme::font::kHint);
    ImGui::TextUnformatted("名称");
    PopUiFont();
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputTextWithHint("##gal-name", "占位文字", name, sizeof(name));
    ImGui::Spacing();

    PushUiFont(theme::font::kHint);
    ImGui::TextUnformatted("Beat JSON");
    PopUiFont();
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputTextMultiline("##gal-json", json, sizeof(json), ImVec2(0.0f, 74.0f));
    ImGui::Spacing();

    const char* options[] = {"S011 · 远景", "S012 · 中景", "S013 · 近景"};
    ImGui::SetNextItemWidth(200.0f);
    ImGui::Combo("##gal-combo", &combo, options, IM_ARRAYSIZE(options));
    ImGui::SameLine();
    ImGui::Checkbox("启用", &checked);
    ImGui::SameLine();
    ImGui::RadioButton("A", &radio, 0);
    ImGui::SameLine();
    ImGui::RadioButton("B", &radio, 1);

    ImGui::SetNextItemWidth(240.0f);
    ImGui::SliderFloat("##gal-slider", &slider, 0.0f, 1.0f, "%.2f");
    ImGui::SameLine();
    ImGui::Text("Switch → 用 Checkbox 代替（ImGui 无原生 Switch）");

    CardEnd();
    ImGui::Spacing();
}

void Card_Progress() {
    CardBegin("gal-card-progress");
    CardTitle("Progress");
    ImGui::ProgressBar(0.64f, ImVec2(ImGui::GetContentRegionAvail().x, 8.0f), "");
    ImGui::Spacing();
    ImGui::ProgressBar(0.28f, ImVec2(ImGui::GetContentRegionAvail().x, 6.0f), "");
    ImGui::Spacing();
    ImGui::ProgressBar(1.0f, ImVec2(ImGui::GetContentRegionAvail().x, 8.0f), "");
    CardEnd();
    ImGui::Spacing();
}

void Card_Cards() {
    CardBegin("gal-card-cards");
    CardTitle("Card 普通 / glow");
    const float w = (ImGui::GetContentRegionAvail().x - 10.0f) * 0.5f;
    const char* titles[] = {"普通卡片", "辉光卡片"};
    for (int i = 0; i < 2; ++i) {
        ImGui::PushID(i);
        if (i > 0) {
            ImGui::SameLine();
        }
        BeginUiCard("gal-inner-card", w);
        BeginUiCardBody();
        PushUiFont(theme::font::kHint, /*bold=*/true);
        ImGui::TextUnformatted(titles[i]);
        PopUiFont();
        ImGui::Spacing();
        ImGui::TextDisabled("body");
        EndUiCardBody();
        EndUiCard();
        ImGui::PopID();
    }
    CardEnd();
    ImGui::Spacing();
}

void Card_KV() {
    CardBegin("gal-card-kv");
    CardTitle("KV 键值对");
    struct Row {
        const char* k;
        const char* v;
    };
    static const Row rows[] = {
        {"类别", "角色"}, {"别名", "沈砚 / 老沈"}, {"出处", "第 3 章"}, {"降级策略", "保留上一版"},
    };
    if (ImGui::BeginTable("gal-kv", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("value");
        for (const Row& r : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", r.k);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.v);
        }
        ImGui::EndTable();
    }
    CardEnd();
    ImGui::Spacing();
}

void Card_Empty() {
    CardBegin("gal-card-empty");
    CardTitle("Empty 空态");
    const char* title = "还没有项目";
    const char* body = "点右上角「新建项目」，或用 Ctrl+O 打开一个已有项目。";
    PushUiFont(theme::font::kTitle, /*bold=*/true);
    const float tw = ImGui::CalcTextSize(title).x;
    PopUiFont();
    PushUiFont(theme::font::kHint);
    const float bw = ImGui::CalcTextSize(body).x;
    PopUiFont();
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(1.0f, 10.0f));
    PushUiFont(theme::font::kTitle, /*bold=*/true);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail - tw) * 0.5f));
    ImGui::TextUnformatted(title);
    PopUiFont();
    PushUiFont(theme::font::kHint);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail - bw) * 0.5f));
    ImGui::TextColored(theme::Rgba(theme::Current().textMuted), "%s", body);
    PopUiFont();
    CardEnd();
    ImGui::Spacing();
}

void Card_Steps(ImDrawList* draw) {
    CardBegin("gal-card-steps");
    CardTitle("Steps / StageFlow");
    const std::vector<std::string> steps{"生成", "评审", "落库"};
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float avail = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const float w = std::min(avail, StepsWidth(steps));
        Steps(draw, Rect{p, ImVec2(p.x + w, p.y + 44.0f)}, steps, 1);
        ImGui::Dummy(ImVec2(avail, 44.0f));
    }
    CardSpacing();
    {
        const std::vector<StageNode> nodes = {
            {"T1", "读取", StageState::Done},
            {"T2", "扩写", StageState::Done},
            {"T3", "出图", StageState::Running},
            {"T4", "评审", StageState::Failed},
            {"T5", "落库", StageState::Skipped},
        };
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float avail = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const float w = std::min(avail, StageFlowWidth(nodes));
        StageFlow(draw, Rect{p, ImVec2(p.x + w, p.y + 30.0f)}, nodes);
        ImGui::Dummy(ImVec2(avail, 30.0f));
    }
    CardEnd();
    ImGui::Spacing();
}

void Card_Rows(ImDrawList* draw) {
    CardBegin("gal-card-rows");
    CardTitle("ListRow / ListCard（Selectable 版）");
    struct Row {
        const char* icon;
        const char* title;
        const char* trailing;
        bool selected;
    };
    static bool picked0 = false;
    static bool picked1 = true;
    static bool picked2 = false;
    bool* picked[] = {&picked0, &picked1, &picked2};
    const Row rows[] = {
        {"folder", "open/output", "目录", false},
        {"file", "ch012.json", "T12", true},
        {"file", "ch013.json", "降级", false},
    };
    // 原 PushUiFont 将 kHint 取整到 kBody。
    const TextStyle rowTitleStyle{.size = theme::font::kBody, .color = ColorText()};
    const TextStyle rowTrailingStyle{.size = rowTitleStyle.size, .color = ColorTextMuted()};
    for (int i = 0; i < 3; ++i) {
        ImGui::PushID(i);
        const float avail = ImGui::GetContentRegionAvail().x;
        // Selectable 自带 hover / 选中 / 键盘焦点 —— 这一行不再需要 HitTest。
        if (ImGui::Selectable("##row", *picked[i], ImGuiSelectableFlags_None,
                              ImVec2(avail, 26.0f))) {
            *picked[i] = !*picked[i];
        }
        const ImVec2 rowMin = ImGui::GetItemRectMin();
        const ImVec2 rowMax = ImGui::GetItemRectMax();
        const float midY = (rowMin.y + rowMax.y) * 0.5f;
        if (rows[i].icon != nullptr && rows[i].icon[0] != '\0') {
            DrawIcon(draw, rows[i].icon, ImVec2(rowMin.x + 8.0f, midY - 6.0f), 12.0f,
                     ColorTextMuted());
        }
        const float textY = kit::CenterY(midY, rowTitleStyle);
        kit::Text(draw, ImVec2(rowMin.x + 26.0f, textY), rows[i].title, rowTitleStyle);
        const float tw = std::ceil(kit::TextWidth(rows[i].trailing, rowTrailingStyle));
        kit::Text(draw, ImVec2(rowMax.x - 8.0f - tw, textY), rows[i].trailing, rowTrailingStyle);
        ImGui::PopID();
    }

    CardSpacing();
    PushUiFont(theme::font::kHint, /*bold=*/true);
    ImGui::TextUnformatted("双行卡（ListCard）");
    PopUiFont();
    static bool card0 = true;
    static bool card1 = false;
    const char* cardTitles[] = {"小说长篇", "影视短片"};
    const char* cardDescs[] = {"章节 → 分镜 → 出图 → 出片，含连续性校验", "剧本 → 镜表 → 连贯性 → 渲染"};
    bool* cardPicked[] = {&card0, &card1};
    // 标题沿用 kHint 取整后的 kBody，描述沿用 kLabel 取整后的 kTiny。
    const TextStyle cardTitleStyle{.size = theme::font::kBody, .bold = true, .color = ColorText()};
    const TextStyle cardDescStyle{.size = theme::font::kTiny, .color = ColorTextMuted()};
    for (int i = 0; i < 2; ++i) {
        ImGui::PushID(100 + i);
        const float avail = ImGui::GetContentRegionAvail().x;
        if (ImGui::Selectable("##card", *cardPicked[i], ImGuiSelectableFlags_None,
                              ImVec2(avail, 44.0f))) {
            *cardPicked[i] = !*cardPicked[i];
        }
        const ImVec2 rmin = ImGui::GetItemRectMin();
        kit::Text(draw, ImVec2(rmin.x + 10.0f, rmin.y + 7.0f), cardTitles[i], cardTitleStyle);
        kit::Text(draw, ImVec2(rmin.x + 10.0f, rmin.y + 7.0f + cardTitleStyle.size + 4.0f),
                  cardDescs[i], cardDescStyle);
        ImGui::PopID();
    }
    CardEnd();
    ImGui::Spacing();
}

void Card_TableMenu() {
    CardBegin("gal-card-table");
    CardTitle("DataTable（BeginTable，排序由 ImGui 的 SortSpecs 驱动）");

    struct Row {
        std::string cells[4];
        theme::Tone tones[4];
    };
    static std::vector<Row> rows = [] {
        std::vector<Row> v;
        for (int i = 0; i < 5; ++i) {
            Row r;
            r.cells[0] = Fmt("C{}", i + 1);
            r.cells[1] = (i < 2) ? "high" : "low";
            r.cells[2] = (i < 2) ? "未过" : "提示";
            r.cells[3] = Fmt("第 {} 项校验的说明文字", i + 1);
            r.tones[0] = theme::Tone::Idle;
            r.tones[1] = (i < 2) ? theme::Tone::Danger : theme::Tone::Warn;
            r.tones[2] = (i < 2) ? theme::Tone::Danger : theme::Tone::Warn;
            r.tones[3] = theme::Tone::Idle;
            v.push_back(std::move(r));
        }
        return v;
    }();

    if (ImGui::BeginTable("gal-table", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInner |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("检查", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn("级别", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn("结论", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn("详情");
        ImGui::TableHeadersRow();

        for (Row& r : rows) {
            ImGui::TableNextRow();
            for (int c = 0; c < 4; ++c) {
                ImGui::TableNextColumn();
                if (c == 0) {
                    PushUiFont(theme::font::kSmall, /*bold=*/true, /*mono=*/true);
                    ImGui::TextUnformatted(r.cells[c].c_str());
                    PopUiFont();
                } else if (c == 1 || c == 2) {
                    ImGui::TextColored(theme::Rgba(ToneColor(r.tones[c])), "%s", r.cells[c].c_str());
                } else {
                    ImGui::TextColored(theme::Rgba(ColorTextMuted()), "%s", r.cells[c].c_str());
                }
            }
        }

        // 排序：**ImGui 只给 SortSpecs，排序由调用方做**。旧自绘表把 sort 当参数传
        // 进来自己排；这里换成读 SortSpecs，排序方向是 ImGui 的表头点出来的。
        if (ImGuiTableSortSpecs* sort = ImGui::TableGetSortSpecs()) {
            if (sort->SpecsDirty && sort->SpecsCount > 0) {
                const ImGuiTableColumnSortSpecs& s = sort->Specs[0];
                if (s.ColumnIndex >= 0 && s.ColumnIndex < 4) {
                    std::stable_sort(rows.begin(), rows.end(),
                                     [&s](const Row& a, const Row& b) {
                                         const int cmp = a.cells[s.ColumnIndex].compare(
                                             b.cells[s.ColumnIndex]);
                                         const bool ascending = s.SortDirection == ImGuiSortDirection_Ascending;
                                         return ascending ? cmp < 0 : cmp > 0;
                                     });
                }
                sort->SpecsDirty = false;
            }
        }
        ImGui::EndTable();
    }

    CardSpacing();
    // 弹出层：OpenPopup + BeginPopup。命中、遮罩、Esc 关闭都是 ImGui 的。
    if (ImGui::Button("视图 ▾")) {
        ImGui::OpenPopup("gal-menu");
    }
    if (ImGui::BeginPopup("gal-menu")) {
        if (ImGui::MenuItem("紧凑列表", nullptr, true)) {
        }
        if (ImGui::MenuItem("网格")) {
        }
        ImGui::Separator();
        ImGui::BeginDisabled();
        ImGui::MenuItem("归档（暂不可用）");
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    CardEnd();
    ImGui::Spacing();
}

void Card_Art(ImDrawList* draw) {
    CardBegin("gal-card-art");
    CardTitle("Art / ArtInk 程序化占位画（仍是自绘：它是图不是控件）");
    const float avail = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float w = (avail - 10.0f) * 0.5f;
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        Art(draw, Rect{p, ImVec2(p.x + w, p.y + 100.0f)}, 3, true);
        ImGui::Dummy(ImVec2(w, 100.0f));
        ImGui::SameLine();
        const ImVec2 q = ImGui::GetCursorScreenPos();
        Art(draw, Rect{q, ImVec2(q.x + w, q.y + 100.0f)}, 7, true);
        ImGui::Dummy(ImVec2(w, 100.0f));
    }
    ImGui::Spacing();
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ArtInk(draw, Rect{p, ImVec2(p.x + avail, p.y + 80.0f)}, 2, 0.8f);
        ImGui::Dummy(ImVec2(avail, 80.0f));
    }
    CardEnd();
    ImGui::Spacing();
}

} // namespace

void GalleryPage::Draw(Rect area, ImDrawList* draw) {
    // 原生流式布局：直接用 ImGui 的光标推进，**不再**手写 RectAt / Stack / Flow。
    // 外层 Shell 的 workspace ScrollRegion 已就位，页面只管往下排。
    ImGui::SetCursorScreenPos(area.min);

    PushUiFont(theme::font::kViewTitle, /*bold=*/true);
    const char* title = "组件画廊";
    ImGui::TextUnformatted(title);
    PopUiFont();
    ImGui::Spacing();
    PushUiFont(theme::font::kHint);
    ImGui::TextColored(theme::Rgba(theme::Current().textMuted),
                       "原生 ImGui 控件 · 流式布局 · 主题：%s",
                       theme::ThemeDisplayName(theme::CurrentThemeId()));
    PopUiFont();
    ImGui::Separator();
    CardSpacing();

    Card_Buttons();
    Card_Tags();
    Card_SegmentedTabs();
    Card_Inputs();
    Card_Progress();
    Card_Cards();
    Card_KV();
    Card_Empty();
    Card_Steps(draw);
    Card_Rows(draw);
    Card_TableMenu();
    Card_Art(draw);

    // 页面把「实际用到的高度」报给外壳 —— 这个数现在是 ImGui 排版算出来的，
    // 不再是 12 条卡片高度常量表（旧实现：entries[12].height）。
    const float used = ImGui::GetCursorScreenPos().y - area.min.y;
    SetPageContentHeight(used + 8.0f);
}

} // namespace shine::pages
