#include "ui/imgui/pages/Gallery.h"

#include "ui/imgui/kit/Views.h"

#include <string>
#include <vector>

namespace shine::pages {
namespace {

using namespace shine::kit;

constexpr float kVwPadX = 24.0f;
constexpr float kVwPadY = 20.0f;

void SectionLabel(ImDrawList* draw, Rect area, std::string_view text) {
    ImFont* font = FontBoldAt(11.5f);
    draw->AddText(font, 11.5f, area.min, ColorTextMuted(), text.data(), text.data() + text.size());
}

// 每个画廊卡：标题 + 内容区。
Rect GalleryCard(ImDrawList* draw, Rect bounds, std::string_view title) {
    DrawShadowed(draw, bounds.min, bounds.max, 10.0f, ColorPanel(), ColorLineSubtle(), 1.0f);
    ImFont* font = FontBoldAt(12.5f);
    draw->AddText(font, 12.5f, ImVec2(bounds.min.x + 14.0f, bounds.min.y + 12.0f), ColorTextSecondary(),
                  title.data(), title.data() + title.size());
    return Rect{bounds.min.x + 14.0f, bounds.min.y + 34.0f, bounds.max.x - 14.0f, bounds.max.y - 12.0f};
}

void Card_Buttons(ImDrawList* draw, Rect area) {
    ButtonSpec spec;
    const float y0 = area.min.y;
    struct Variant {
        ButtonVariant variant;
        const char* label;
    };
    const Variant variants[] = {
        {ButtonVariant::Primary, "主按钮"}, {ButtonVariant::Secondary, "次按钮"},
        {ButtonVariant::Ghost, "幽灵"}, {ButtonVariant::Danger, "危险"}};
    float x = area.min.x;
    for (int i = 0; i < 4; ++i) {
        spec.variant = variants[i].variant;
        const std::string label = variants[i].label;
        const float w = ButtonWidth(ButtonSize::Medium, 0.0f,
                                    FontBoldAt(13.0f)->CalcTextSizeA(13.0f, 1e9f, 0.0f, label.data(),
                                                                       label.data() + label.size())
                                        .x);
        Button(draw, RectAt(x, y0, w, 30.0f), label, spec, "gal-btn-" + std::to_string(i));
        x += w + 8.0f;
    }
    // 尺寸档
    x = area.min.x;
    const float y1 = y0 + 38.0f;
    for (ButtonSize size : {ButtonSize::Small, ButtonSize::Medium, ButtonSize::Large}) {
        const std::string label = "尺寸";
        const float w = ButtonWidth(size, 0.0f,
                                    FontBoldAt(13.0f)->CalcTextSizeA(13.0f, 1e9f, 0.0f, label.data(),
                                                                       label.data() + label.size())
                                        .x);
        ButtonSpec s2 = spec;
        s2.variant = ButtonVariant::Secondary;
        s2.size = size;
        Button(draw, RectAt(x, y1, w, ButtonHeight(size)), label, s2,
               "gal-size-" + std::to_string(static_cast<int>(size)));
        x += w + 8.0f;
    }
    // loading / disabled
    x = area.min.x;
    const float y2 = y1 + 44.0f;
    ButtonSpec loading = spec;
    loading.variant = ButtonVariant::Primary;
    loading.loading = true;
    Button(draw, RectAt(x, y2, 96.0f, 30.0f), "加载中", loading, "gal-load");
    x += 104.0f;
    ButtonSpec disabled = spec;
    disabled.variant = ButtonVariant::Secondary;
    disabled.disabled = true;
    Button(draw, RectAt(x, y2, 96.0f, 30.0f), "已禁用", disabled, "gal-dis");
    x += 104.0f;
    for (std::string_view icon : {"settings", "download", "refresh", "eye"}) {
        IconButton(draw, RectAt(x, y2, 28.0f, 28.0f), icon, false, false,
                   "gal-ib-" + std::string(icon), std::string(icon));
        x += 32.0f;
    }
}

void Card_Tags(ImDrawList* draw, Rect area) {
    float x = area.min.x;
    float y = area.min.y;
    const theme::Tone tones[] = {theme::Tone::Accent, theme::Tone::Info,  theme::Tone::Ok,
                                  theme::Tone::Warn,  theme::Tone::Danger, theme::Tone::Busy,
                                  theme::Tone::Idle};
    const char* names[] = {"accent", "info", "ok", "warn", "danger", "busy", "idle"};
    for (int i = 0; i < 7; ++i) {
        const std::string label = names[i];
        const float w = TagWidth(label, false, i % 2 == 0);
        if (x + w > area.max.x) {
            x = area.min.x;
            y += 26.0f;
        }
        Tag(draw, RectAt(x, y, w, 20.0f), label, tones[i], false, i % 2 == 0);
        x += w + 8.0f;
    }
    y += 28.0f;
    x = area.min.x;
    for (int i = 0; i < 7; ++i) {
        const std::string label = names[i];
        const float w = TagWidth(label, true, false);
        if (x + w > area.max.x) {
            x = area.min.x;
            y += 24.0f;
        }
        Tag(draw, RectAt(x, y, w, 17.0f), label, tones[i], true, false, i == 5);
        x += w + 6.0f;
    }
    y += 30.0f;
    for (int i = 0; i < 7; ++i) {
        StatusDot(draw, ImVec2(area.min.x + 4.0f + static_cast<float>(i) * 18.0f, y + 6.0f),
                  tones[i], i == 5);
    }
    y += 20.0f;
    const char* kbds[] = {"Ctrl", "K", "Shift", "↵"};
    x = area.min.x;
    for (const char* k : kbds) {
        const float w = KbdWidth(k);
        Kbd(draw, RectAt(x, y, w, 18.0f), k);
        x += w + 6.0f;
    }
}

void Card_SegmentedTabs(ImDrawList* draw, Rect area) {
    // ⚠️ 早先这里是两条**裸语句**：
    //     Segmented(draw, ..., options, "a",  "gal-seg");
    //     Tabs(draw, ..., tabs,     "1",  "gal-tabs");
    //   两个 value 都是**字面量**，返回值也直接丢弃。而 kit::Segmented / Tabs 的
    //   实现（Widgets.cpp:451 / 568）是 `picked` 初值 = 传进去的 value，**只在**
    //   `hit.clicked` 时被改 —— 返回值**必须由调用方存回**，否则下一帧又从字面量
    //   重新开始。实际表现：点任意一项都弹回原项，控件完全点不动。
    //
    //   同一个文件 Card_Inputs 里 Switch / Checkbox 的同款问题已经判为
    //   「组件画廊里按不动的开关比在业务页更不能接受」并修好了（见那里 154-164 的
    //   注释）—— 这里是**同一张画布上漏改的另一半**：一页之内，开关能点、分段器
    //   不能点，看起来像「这套控件有的能用有的不能用」，比全都不能用更容易误导。
    //
    // 修法照抄那边：用函数内 static 存跨帧状态（画廊卡是无实例的自由函数，
    // 状态没地方挂，static 是本文件既有的手法，见 Card_Inputs）。
    // 存回时**先拷成 std::string 再赋值**（不是 `segValue = segPicked`）：没点击时
    // `Segmented` 返回的就是传进去的 value 本身，返回的 string_view 会**指向
    // segValue 自己**，直接赋值等于自引用赋值。走临时对象这一步是本仓库既有口径
    // （Page_ImageFlow.cpp 的 `std::to_string(panelTab_)` → `picked` → 存回）。
    static std::string segValue = "a";
    static std::string tabValue = "1";
    const std::vector<SegmentOption> options{{"a", "详情"}, {"b", "总览"}};
    const float w = SegmentedWidth(options);
    const std::string_view segPicked =
        Segmented(draw, RectAt(area.min.x, area.min.y, w, 32.0f), options, segValue, "gal-seg");
    if (!segPicked.empty()) {
        segValue = std::string(segPicked);
    }
    const std::vector<SegmentOption> tabs{{"1", "章节"}, {"2", "设定"}, {"3", "评审"}};
    const std::string_view tabPicked = Tabs(
        draw, RectAt(area.min.x, area.min.y + 42.0f, 260.0f, 30.0f), tabs, tabValue, "gal-tabs");
    if (!tabPicked.empty()) {
        tabValue = std::string(tabPicked);
    }
}

void Card_Inputs(ImDrawList* draw, Rect area) {
    static std::string text = "分镜图_v3";
    static std::string area_text = "{\n  \"code\": \"S012\",\n  \"duration\": 6.0\n}";
    static int selectIndex = 1;
    static bool on = true;
    static bool checked = true;
    float y = area.min.y;
    Rect field = Field(draw, RectAt(area.min.x, y, area.width(), 60.0f), "名称", "首行缩进两字");
    Input(draw, RectAt(field.min.x, field.min.y, area.width(), 30.0f), text, "占位文字", "gal-in");
    y = field.min.y + 42.0f;
    Rect field2 = Field(draw, RectAt(area.min.x, y, area.width(), 30.0f), "Beat JSON", "");
    TextArea(draw, RectAt(field2.min.x, field2.min.y, area.width(), 74.0f), area_text, 3, "gal-ta");
    y = field2.min.y + 84.0f;
    const std::vector<std::string> options{"S011 · 远景", "S012 · 中景", "S013 · 近景"};
    Select(draw, RectAt(area.min.x, y, 200.0f, 30.0f), options, selectIndex, "gal-sel");
    // ⚠️ 早先这两行是 `Switch(draw, ..., on, "gal-sw");` / `Checkbox(draw, ..., checked, ...)`，
    //    **返回值直接丢弃**。kit::Switch / Checkbox 的 `bool on` 是**按值**传进去的
    //    （Widgets.h），它们返回的是「是否被翻转」这个信号。所以点一百次开关都停在 on。
    //    这是组件画廊页：它存在的意义就是**演示这些控件可用**，一个按不动的开关
    //    在这里比在业务页更不能接受 —— 画廊是给人看「这套控件做得对不对」的。
    if (Switch(draw, RectAt(area.min.x + 220.0f, y + 5.0f, 34.0f, 19.0f), on, "gal-sw")) {
        on = !on;
    }
    if (Checkbox(draw, RectAt(area.min.x + 270.0f, y, 140.0f, 20.0f), checked, "启用", "gal-cb")) {
        checked = !checked;
    }
}

void Card_Progress(ImDrawList* draw, Rect area) {
    float y = area.min.y;
    Progress(draw, RectAt(area.min.x, y, area.width(), 6.0f), 64.0f, false, false);
    y += 18.0f;
    Progress(draw, RectAt(area.min.x, y, area.width(), 4.0f), 28.0f, true, true);
    y += 20.0f;
    Progress(draw, RectAt(area.min.x, y, area.width(), 6.0f), 100.0f, false, false);
}

void Card_Cards(ImDrawList* draw, Rect area) {
    const float w = (area.width() - 10.0f) * 0.5f;
    Rect body = Card(draw, RectAt(area.min.x, area.min.y, w, area.height()), "普通卡片", "layers",
                     false, false);
    const std::string_view text = "body";
    draw->AddText(FontAt(12.5f), 12.5f, body.min, ColorTextMuted(), text.data(),
                  text.data() + text.size());
    Rect body2 = Card(draw, RectAt(area.min.x + w + 10.0f, area.min.y, w, area.height()), "辉光",
                      "zap", true, true);
    const std::string_view text2 = "glow";
    draw->AddText(FontAt(12.5f), 12.5f, body2.min, ColorTextMuted(), text2.data(),
                  text2.data() + text2.size());
}

void Card_KV(ImDrawList* draw, Rect area) {
    KeyValues(draw, area, {{"类别", "角色"}, {"别名", "沈砚 / 老沈"}, {"出处", "第 3 章"}, {"降级策略", "保留上一版"}});
}

void Card_Empty(ImDrawList* draw, Rect area) {
    Empty(draw, area, "sparkles", "还没有项目", "点右上角「新建项目」，或用 ⌘O 打开一个已有项目。");
}

void Card_Steps(ImDrawList* draw, Rect area) {
    const std::vector<std::string> steps{"生成", "评审", "落库"};
    Steps(draw, area, steps, 1);
    float y = area.min.y + 46.0f;
    for (int i = 0; i < 5; ++i) {
        const std::vector<kit::StageNode> nodes = {
            {"T1", "读取项目", kit::StageState::Done},   {"T2", "扩写章节", kit::StageState::Done},
            {"T3", "生成图", i == 0 ? kit::StageState::Running : kit::StageState::Todo},
            {"T4", "评审", kit::StageState::Todo},      {"T5", "落库", kit::StageState::Skipped}};
        StageFlow(draw, RectAt(area.min.x, y, area.width(), 30.0f), nodes);
        y += 36.0f;
    }
}

void Card_Art(ImDrawList* draw, Rect area) {
    const float w = (area.width() - 10.0f) * 0.5f;
    Art(draw, RectAt(area.min.x, area.min.y, w, 100.0f), 3, true);
    Art(draw, RectAt(area.min.x + w + 10.0f, area.min.y, w, 100.0f), 7, true);
    ArtInk(draw, RectAt(area.min.x, area.min.y + 108.0f, area.width(), 80.0f), 2, 0.8f);
}

// ListRow / ListCard / DataTable / Menu —— 2026-09-30 才补上的四个。
//
// 为什么这张卡重要：这一页存在的意义就是**证明 kit 控件真的可用**（上一轮
// 「组件画廊的开关与复选框点不动」就是在这里暴露的）。而这四个组件当时
// 「已实现、算得对、全树零调用」—— 零调用意味着**没有任何一处证明它们能用**，
// 于是页面层各写了一份还写错。所以它们必须有演示。
void Card_Rows(ImDrawList* draw, Rect area) {
    float y = area.min.y;
    // ListRow：选中 / 普通 / 禁用 三态。id 给不同值，鼠标点得动。
    {
        ListRowSpec spec;
        spec.id = "gal-row-0";
        spec.icon = "folder";
        spec.title = "open/output";
        spec.trailing = "目录";
        spec.trailingMono = true;
        ListRow(draw, RectAt(area.min.x, y, area.width(), 26.0f), spec);
        y += 30.0f;
    }
    {
        ListRowSpec spec;
        spec.id = "gal-row-1";
        spec.icon = "file";
        spec.title = "ch012.json";
        spec.trailing = "T12";
        spec.trailingMono = true;
        spec.selected = true;
        ListRow(draw, RectAt(area.min.x, y, area.width(), 26.0f), spec);
        y += 30.0f;
    }
    {
        ListRowSpec spec;
        spec.id = "gal-row-2";
        spec.icon = "file";
        spec.title = "ch013.json";
        spec.trailing = "降级";
        spec.disabled = true;
        ListRow(draw, RectAt(area.min.x, y, area.width(), 26.0f), spec);
        y += 34.0f;
    }
    // ListCard：双行（标题 + 描述）+ 选中态。
    {
        ListCardSpec spec;
        spec.id = "gal-card-0";
        spec.icon = "sparkles";
        spec.title = "小说长篇";
        spec.description = "章节 → 分镜 → 出图 → 出片，含连续性校验";
        spec.selected = true;
        ListCard(draw, RectAt(area.min.x, y, area.width(), 54.0f), spec);
        y += 60.0f;
    }
    {
        ListCardSpec spec;
        spec.id = "gal-card-1";
        spec.title = "影视短片";
        spec.description = "剧本 → 镜表 → 连贯性 → 渲染";
        ListCard(draw, RectAt(area.min.x, y, area.width(), 54.0f), spec);
        y += 60.0f;
    }
}

void Card_TableMenu(ImDrawList* draw, Rect area) {
    // DataTable：4 列 + tag 列 + mono 列 + 排序。
    static kit::TableSort sort;
    const std::vector<kit::TableColumn> columns{
        {"检查", 56.0f, false, false, false, true, false},
        {"级别", 72.0f, false, false, false, false, true},
        {"结论", 72.0f, false, false, false, false, true},
        {"详情", 0.0f, false, false, /*sortable=*/true, false, false},
    };
    std::vector<kit::TableRow> rows;
    for (int i = 0; i < 4; ++i) {
        kit::TableRow row;
        row.cells = {"C" + std::to_string(i + 1), i < 2 ? "high" : "low", i < 2 ? "未过" : "提示",
                     "第 " + std::to_string(i + 1) + " 项校验的说明文字"};
        row.tones = {theme::Tone::Idle, i < 2 ? theme::Tone::Danger : theme::Tone::Warn,
                     i < 2 ? theme::Tone::Danger : theme::Tone::Warn, theme::Tone::Idle};
        rows.push_back(row);
    }
    DataTable(draw, RectAt(area.min.x, area.min.y, area.width(), 200.0f), columns, rows, sort,
              /*compact=*/true, "gal-table");
    // Menu：贴锚点右下展开（点它看展开）。
    std::vector<MenuRow> menu;
    MenuRow label;
    label.kind = MenuRowKind::Label;
    label.label = "视图";
    menu.push_back(label);
    MenuRow a;
    a.kind = MenuRowKind::Item;
    a.label = "紧凑列表";
    a.icon = "list";
    a.selected = true;
    menu.push_back(a);
    MenuRow b;
    b.kind = MenuRowKind::Item;
    b.label = "网格";
    b.icon = "grid";
    menu.push_back(b);
    MenuRow sep;
    sep.kind = MenuRowKind::Separator;
    menu.push_back(sep);
    MenuRow c;
    c.kind = MenuRowKind::Item;
    c.label = "归档（暂不可用）";
    c.disabled = true;
    menu.push_back(c);
    const Rect anchor{area.max.x - 130.0f, area.min.y + 210.0f, area.max.x, area.min.y + 240.0f};
    DrawRoundRect(draw, anchor.min, anchor.max, 6.0f, ColorElevated(), ColorLineNormal(), 1.0f);
    DrawTextClipped(draw, FontAt(12.0f), 12.0f,
                    ImVec2(anchor.min.x, kit::CenterTextY(FontAt(12.0f), 12.0f, anchor.center().y)),
                    anchor.width() - 20.0f, ColorTextSecondary(), "点我展开菜单");
    Menu(draw, anchor, menu, "gal-menu");
}

} // namespace

void GalleryPage::Draw(Rect area, ImDrawList* draw) {
    const Rect content{area.min.x + kVwPadX, area.min.y + kVwPadY, area.max.x - kVwPadX,
                       area.max.y - kVwPadY};

    // 头
    DrawIcon(draw, "grid", content.min, 20.0f, ColorAccent());
    ImFont* title = FontBoldAt(18.0f);
    const std::string_view titleText = "组件画廊";
    draw->AddText(title, 18.0f, ImVec2(content.min.x + 28.0f, content.min.y - 2.0f), ColorText(),
                  titleText.data(), titleText.data() + titleText.size());
    const std::string sub = "全部组件 × 全部状态 · 主题：" +
                            std::string(theme::ThemeDisplayName(theme::CurrentThemeId()));
    draw->AddText(FontAt(12.5f), 12.5f, ImVec2(content.min.x + 28.0f, content.min.y + 22.0f),
                  ColorTextMuted(), sub.data(), sub.data() + sub.size());

    const Rect grid{content.min.x, content.min.y + 48.0f, content.max.x, content.max.y};

    // repeat(auto-fit, minmax(340px,1fr)) gap 14
    const int columns = AutoGridCols(grid.width(), 340.0f, 14.0f);
    const float columnW = (grid.width() - 14.0f * static_cast<float>(columns - 1)) /
                         static_cast<float>(columns);

    struct Entry {
        const char* title;
        void (*draw)(ImDrawList*, Rect);
        float height;
    };
    static const Entry entries[] = {
        {"Button 4 变体 × 3 尺寸", Card_Buttons, 148.0f},
        {"Tag / StatusDot / Kbd", Card_Tags, 130.0f},
        {"Segmented / Tabs", Card_SegmentedTabs, 84.0f},
        {"Field / Input / TextArea / Select / Switch / Checkbox", Card_Inputs, 240.0f},
        {"Progress（渐变 + run 微光）", Card_Progress, 48.0f},
        {"Card 普通 / hover / glow", Card_Cards, 96.0f},
        {"KV 键值对", Card_KV, 92.0f},
        {"Empty 空态", Card_Empty, 190.0f},
        {"Steps / StageFlow 5 态", Card_Steps, 200.0f},
        {"ListRow / ListCard", Card_Rows, 226.0f},
        {"DataTable / Menu", Card_TableMenu, 340.0f},
        {"Art / ArtInk 程序化占位画", Card_Art, 200.0f},
    };

    float y = grid.min.y;
    for (std::size_t i = 0; i < std::size(entries); ++i) {
        const std::size_t column = i % static_cast<std::size_t>(columns);
        const std::size_t row = i / static_cast<std::size_t>(columns);
        if (row == 0) {
            y = grid.min.y;
        }
        const float x = grid.min.x + (columnW + 14.0f) * static_cast<float>(column);
        const float h = entries[i].height;
        const Rect card = RectAt(x, y, columnW, h);
        Rect body = GalleryCard(draw, card, entries[i].title);
        entries[i].draw(draw, body);
        if (column + 1 == static_cast<std::size_t>(columns) || i + 1 == std::size(entries)) {
            y += h + 14.0f;
        }
    }
}

} // namespace shine::pages
