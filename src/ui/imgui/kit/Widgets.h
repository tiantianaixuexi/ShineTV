#pragma once
// shine::kit::Widgets —— 设计稿 20 个组件的 ImGui 实现（P3.2 / P3.3）
//
// 权威是 webui/src/components/UI.jsx（文件头自己写着「对应 Qt 端 kit::widgets
// 语义」）+ webui/src/styles/ui.css 的尺寸。**不是**把 Qt 的 32 个 widget 逐个翻译。
//
// 全部尺寸来自 design-spec.md §5，逐条标了 CSS 出处。这里只写契约，实现见 .cpp。
//
// 统一约定：
//   * 所有控件吃 **绝对矩形**（design-spec 的布局是 CSS 绝对定位式），
//     不走 ImGui 的线性布局 —— 否则外壳的自绘栅格无法控制。
//   * 命中测试统一用 InvisibleButton 占位（保持 ImGui 的 ID 栈与焦点语义）。
//   * 颜色一律从 theme 取，**不许**在调用点写 ImVec4 字面量（check-layers 规则 3）。
#pragma once

#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Icon.h"
#include "ui/imgui/theme/Theme.h"

#include <imgui.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace shine::kit {

struct Rect {
    ImVec2 min;
    ImVec2 max;
    // 显式两套构造：ImVec2 不是聚合类型，四个裸 float 的字面量要能直接初始化。
    Rect() = default;
    Rect(ImVec2 a, ImVec2 b) : min(a), max(b) {}
    // ⚠️ 四参是 **(minX, minY, maxX, maxY)**，不是 (x, y, w, h)。写宽高请用 RectAt()。
    //    写错的**不报编译错**，只是 max < min，DrawRoundRect 的 `max.x <= min.x` 会直接
    //    return —— 控件整个不画、也点不到，界面上是一片空白，看不出是哪儿错了。
    //    已确认踩过的实例：Page_Assets.cpp 资产总览网格的 card / thumb、Shell.cpp 底栏
    //    页签条（2026-09-30 修）。全树扫描见 tools\find-rect-wh-misuse.ps1。
    Rect(float x0, float y0, float x1, float y1) : min(x0, y0), max(x1, y1) {}
    [[nodiscard]] float width() const { return max.x - min.x; }
    [[nodiscard]] float height() const { return max.y - min.y; }
    [[nodiscard]] ImVec2 center() const { return ImVec2(0.5f * (min.x + max.x), 0.5f * (min.y + max.y)); }
    [[nodiscard]] bool contains(ImVec2 p) const { return p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y; }
};

[[nodiscard]] inline Rect RectAt(float x, float y, float w, float h) {
    return Rect{ImVec2(x, y), ImVec2(x + w, y + h)};
}

// ---- 命中测试 ----
// 所有自绘控件都靠它在绝对位置占一个不可见 item，ImGui 才能维护 ID 栈与焦点。
struct Hit {
    bool hovered = false;
    bool held = false;
    bool clicked = false;
    bool doubleClicked = false;
};
[[nodiscard]] Hit HitTest(Rect bounds, std::string_view id);
// 外壳/页面层最常用的一条：本帧是否点了这块矩形。
[[nodiscard]] bool Clicked(Rect bounds, std::string_view id);
[[nodiscard]] bool Hovered(Rect bounds, std::string_view id);

// ---- 同一帧重复命中的自检（2026-09-30 加）----
//
// ImGui 同窗口内是「**先注册者独占** HoveredId」（imgui.cpp:5161
// `ItemHoverable` 里的 `if (g.HoveredId != 0 && g.HoveredId != id && !AllowOverlap) return false;`）。
// 于是**同一帧里在重叠矩形上注册第二个 item**，第二个永远
// hovered=false / clicked=false —— 而它上面已经画好了一整套控件外观：
// 编译过、不崩、截图正常、manifest 记 saved，只是那个控件按不动。
//
// 这个失败模式极难靠读代码发现（两条调用都「看起来对」），所以这里做成
// 运行时兜底：本帧内登记过的矩形，第二次重叠命中时报一次。取证跑完全部
// 工作区后若计数 > 0 即判 FAIL。
//
// 只报**同一帧**的重叠；跨帧相同 id 是正常的（ImGui 按 id 记状态）。
void NoteDuplicateHit(float minX, float minY, float maxX, float maxY, const char* id);
[[nodiscard]] int DuplicateHitCount();
void ResetDuplicateHitCount();
[[nodiscard]] const char* LastDuplicateHit();

// 主题相关的常用取色（控件内部用，页面不要直接拼 ImU32）。
[[nodiscard]] ImU32 ColorOf(std::uint32_t rgba);
[[nodiscard]] ImU32 ColorText();
[[nodiscard]] ImU32 ColorTextSecondary();
[[nodiscard]] ImU32 ColorTextMuted();
[[nodiscard]] ImU32 ColorTextInverse();
[[nodiscard]] ImU32 ColorSurface();
[[nodiscard]] ImU32 ColorPanel();
[[nodiscard]] ImU32 ColorElevated();
[[nodiscard]] ImU32 ColorOverlay();
[[nodiscard]] ImU32 ColorVoid();
[[nodiscard]] ImU32 ColorFillHover();
[[nodiscard]] ImU32 ColorFillSelected();
[[nodiscard]] ImU32 ColorFillMuted();
[[nodiscard]] ImU32 ColorLineSubtle();
[[nodiscard]] ImU32 ColorLineNormal();
[[nodiscard]] ImU32 ColorLineStrong();
[[nodiscard]] ImU32 ColorAccent();
[[nodiscard]] ImU32 ColorAccentHover();
[[nodiscard]] ImU32 ColorAccentFg();
[[nodiscard]] ImU32 ColorAccentDim();
[[nodiscard]] ImU32 ColorAccentGlow();
[[nodiscard]] ImU32 ColorFocusRing();
[[nodiscard]] ImU32 ColorScrim();

// 全透明。**不是主题色，是「不画」** —— 之所以要有这个出口：`IM_COL32(0,0,0,0)`
// 是源码里最常见的硬编码颜色字面量（透明滚动条底、hover 时「不画底」），而
// `tools/check-colors.ps1` 按规则禁止硬编码颜色。没有这个函数，唯一的合规写法
// 就是给每处加 `// theme-ok` 豁免 —— 豁免一旦多了就等于没有门禁。
[[nodiscard]] ImU32 ColorTransparent();

// 半透明**调制**：结果 alpha = 原 alpha × alpha。传 1.0 保持原样。
// 这是给「已经在派生色里带了 alpha」的场景用的（tagBg 12% / gateBg 10% / accentGlow 30%），
// 参数只用来做动画调制（脉冲、hover 渐变），不要用它把 12% 的淡底拉成 100% 实心。
[[nodiscard]] ImU32 WithAlpha(ImU32 color, float alpha);

// 半透明**覆盖**：直接把 alpha 设成给定值。用于实色补透明度（阴影、scrim、叠层）。
[[nodiscard]] ImU32 WithAlphaSet(ImU32 color, float alpha);
[[nodiscard]] ImU32 LerpColorTo(ImU32 from, ImU32 to, float t);

// ---- 1. Button（UI.jsx:7）----
// md h30 / pad 0 14 / r6 / 13px / 600；sm h24 pad 0 10 12px；lg h36 pad 0 20 14px r10
// 图标 15（sm 13）；:active scale(.97)；disabled opacity .45
enum class ButtonVariant { Primary, Secondary, Ghost, Danger };
enum class ButtonSize { Small, Medium, Large };

struct ButtonSpec {
    ButtonVariant variant = ButtonVariant::Ghost;
    ButtonSize size = ButtonSize::Medium;
    std::string_view icon;      // 空 = 无图标
    bool loading = false;       // 转圈占位 13×13
    bool disabled = false;
};

[[nodiscard]] float ButtonHeight(ButtonSize size);
[[nodiscard]] float ButtonWidth(ButtonSize size, float iconWidth, float textWidth);
[[nodiscard]] float ButtonPadding(ButtonSize size);
// 返回 true = 本帧被点击（disabled / loading 时恒 false）。
bool Button(ImDrawList* draw, Rect bounds, std::string_view label, const ButtonSpec& spec,
            std::string_view id);

// ---- 2. IconBtn（UI.jsx:17）----
// 28×28 r6（sm 22×22），图标 16（sm 13）；hover fill-hover；active fill-selected+accent
bool IconButton(ImDrawList* draw, Rect bounds, std::string_view icon, bool active, bool disabled,
                std::string_view id, std::string_view tip = {});

// ---- 3. Tag（UI.jsx:32）----
// h20 pad 0 8 r-pill 11.5/600 1px 边；sm h17 pad 0 6 10.5；7 色调；可选 7px 圆点
void Tag(ImDrawList* draw, Rect bounds, std::string_view label, theme::Tone tone, bool small = false,
         bool dot = false, bool busyPulse = false);
[[nodiscard]] float TagWidth(std::string_view label, bool small, bool dot);
[[nodiscard]] float TagHeight(bool small);

// ---- 4. StatusDot（UI.jsx:42）----
// 7×7 圆；7 色调；run 时 1.6s 脉冲环
void StatusDot(ImDrawList* draw, ImVec2 center, theme::Tone tone, bool run);

// ---- 5. Kbd（UI.jsx:47）----
// min-w18 h18 pad 0 5 r4，1px 边 + 下边 2px，10.5/600
void Kbd(ImDrawList* draw, Rect bounds, std::string_view label);
[[nodiscard]] float KbdWidth(std::string_view label);

// ---- 6. Card（UI.jsx:52）----
// bg-panel + 1px line-subtle + r10；头 pad 12/16 + 下边框；体 pad 16；
// hover = line-strong + 上浮 2px；glow = accent 边 + accent 辉光
// 返回内容区（头之下、体之内）。
Rect Card(ImDrawList* draw, Rect bounds, std::string_view title, std::string_view icon,
          bool hoverable, bool glow);
// 头部右侧的自绘控件区（extra 按钮放这里）。
[[nodiscard]] Rect CardHeaderRow(Rect card, std::string_view title, std::string_view icon);

// ---- 7. Segmented（UI.jsx:69）----
// 容器 pad3 gap2 fill-muted r6 1px；项 h26 pad 0 13 r4 12.5/600；
// 选中 bg-elevated + shadow + 4px accent 圆点
struct SegmentOption {
    std::string_view value;
    std::string_view label;
};
// 返回被选中的 value（点击时更新 *value）。
std::string_view Segmented(ImDrawList* draw, Rect bounds,
                           const std::vector<SegmentOption>& options, std::string_view value,
                           std::string_view id);
[[nodiscard]] float SegmentedWidth(const std::vector<SegmentOption>& options);

// ---- 7b. Chip（views.css:670-706）----
// 默认 h26 pad 0 11 r-pill 1px line-normal 12/600 secondary；hover line-strong + primary；
// 选中 accent-dim 底 + accent-glow 边 + accent 字。count 非空时右侧画 .cnt
// （10.5px / pad 0 6 / r-pill / fill-muted 底 muted 字；选中态底换成 accent 18%）。
// compact 是资产侧栏的内联覆写（h22 pad 0 8 11px，Shell.jsx:540）——
// 设计稿那处是写死的 style，不是另一个变体，所以做成参数而不是第二份规格。
struct ChipSpec {
    bool selected = false;
    bool compact = false;
    std::string_view count;  // 空 = 不画计数
};
// 返回 true = 本帧被点击。
bool Chip(ImDrawList* draw, Rect bounds, std::string_view label, const ChipSpec& spec,
          std::string_view id);
[[nodiscard]] float ChipHeight(bool compact);
// 量宽：pad×2 + 字宽 + (count ? gap + count 宽 : 0)。
[[nodiscard]] float ChipWidth(std::string_view label, const ChipSpec& spec);

// ---- 8. Tabs（UI.jsx:86）----
// 项 pad 8/12 13/600；选中 accent + 2px 下划线（左右各内缩 10px）
std::string_view Tabs(ImDrawList* draw, Rect bounds, const std::vector<SegmentOption>& tabs,
                      std::string_view value, std::string_view id);

// ---- 9. Field（UI.jsx:103）----
// 竖排 gap6；标签 12px/600 secondary；help 12px muted。返回控件该放的位置。
Rect Field(ImDrawList* draw, Rect bounds, std::string_view label, std::string_view help);

// ---- 10-12. Input / TextArea / Select ----
// Input h30 pad 0 10 r6 fill-muted 1px line-normal；
// focus = line-focus 边 + 0 0 0 3px accent-dim 外环
bool Input(ImDrawList* draw, Rect bounds, std::string& value, std::string_view placeholder,
           std::string_view id);
bool TextArea(ImDrawList* draw, Rect bounds, std::string& value, int lines, std::string_view id);
bool Select(ImDrawList* draw, Rect bounds, const std::vector<std::string>& options,
            int& index, std::string_view id);

// ---- 13. Switch（UI.jsx:131）----
// 34×19 r-pill；旋钮 13×13 位移 15px
bool Switch(ImDrawList* draw, Rect bounds, bool on, std::string_view id);

// ---- 14. Checkbox（UI.jsx:133）----
// 盒 15×15 r4 1.5px 边 + 内联对勾（accent-fg）
bool Checkbox(ImDrawList* draw, Rect bounds, bool on, std::string_view label, std::string_view id);

// ---- 15. Progress（UI.jsx:145）----
// 轨 h6（thin 4）r-pill；填充 grad-accent；run 时叠 100° 白 35% 微光扫过
void Progress(ImDrawList* draw, Rect bounds, float value, bool run, bool thin);
[[nodiscard]] float ProgressHeight(bool thin);

// ---- 16. Empty（UI.jsx:154）----
// 居中 gap10 pad 40/20；字形 52×52 r14 虚线边 + 24px 图标 + 4s 上下浮动；
// 标题 13/600；正文 max-w 320
void Empty(ImDrawList* draw, Rect bounds, std::string_view icon, std::string_view title,
           std::string_view body);

// ---- 17. KV（UI.jsx:165）----
// 网格 auto 1fr gap 6px 14px，12.5；键 muted，值 primary/500
void KeyValues(ImDrawList* draw, Rect bounds,
               const std::vector<std::pair<std::string, std::string>>& rows);

// ---- 18. Spinner（ui.css:457）----
// 14×14 圆环 2px 边（line-normal），顶边 accent，0.7s/圈；sm 11×11 边宽 1.5。
// 组件画廊（Gallery.jsx:96-97）与 StageFlow/Overlays 的「运行中」都用这一档。
// 画在中心点：外接圆 = bounds（直径即 size），描边向内吃 thickness。
void Spinner(ImDrawList* draw, ImVec2 center, bool small);
// 按 .spin / .spin.sm 的直径返回占位边长（布局用它预留空间）。
[[nodiscard]] float SpinnerSize(bool small);

// ---- 19. Tooltip（ui.css:472-507，[data-tip]）----
// bg-overlay + 1px line-normal + 11.5/500 pad 4/9 r6 + shadow-1；
// 默认贴锚点**右侧 10px 垂直居中**（left: calc(100% + 10px)），below=true 则贴下方 8px 居中。
// 只画不命中（CSS 的 pointer-events: none），且自带 200ms 出现延迟（--dur-2）。
// 走 ImGui 的前景 draw list：浮层必须压在同一帧里已经画好的内容之上。
void Tooltip(Rect anchor, std::string_view text, bool below = false);

// ---- 20. Divider（ui.css:134-138 .msep）----
// 1px line-subtle 水平发丝线，上下各留 5px（.msep 的 margin）。菜单与分区之间用。
void Divider(ImDrawList* draw, Rect bounds);

// ---- 20b. ListRow（ui.css:196-199 .list-row / views.css 各处「条目行」）----
//
// 为什么把它收进 kit：页面层曾有 **5 处**各写各的列表行（队列 / palette / 产物 /
// 报告 / 总控页产物），每一处的行高、hover 底、图标位置、文字 Y 都各算各的 ——
// 同一份逻辑两处实现，而「每一处都各自算错了一次 Y」。抽到这里之后，
// 垂直居中只有 `CenterTextY` 一个入口，页面层不该再出现 `y + 6.0f` 这种算式。
//
// 结构（从左到右，都是可选项）：
//   [icon 16×16] 标题 …… 右侧次要文字  [chevron 10]
// 全部按**行中心**对齐：标题/次要文字走 CenterTextY，图标与 chevron 按中心 −半高。
//
// `hit` 由本函数**一次性**取齐并返回（hovered + clicked 同一个 Hit）——
// 不要在调用点写 `Hovered(idA)` 再 `Clicked(idB)`，那会在同一矩形上叠两个
// InvisibleButton，ImGui 只让先注册的那个拿到 HoveredId，第二个永远
// clicked=false（WorkspaceA 产物行踩过）。
//
// `trailing` 非空时，标题可用宽度自动收窄到 chevron 之前（不必由调用点算）。
struct ListRowSpec {
    std::string_view id;         // 空 = 不做命中测试（纯展示行，如胶片格）
    std::string_view icon;       // 可空
    std::string_view title;      // 主文字
    std::string_view trailing;   // 可空：右侧次要文字
    std::string_view chevron;    // 默认 "chevron"；传空串则不画
    bool selected = false;       // 选中底（fill-selected）
    bool disabled = false;       // 降低对比度
    float iconSize = 16.0f;
    float titleSize = 12.0f;     // 标题字号
    float trailingSize = 10.5f;  // 次要文字字号
    float paddingX = 8.0f;       // 左右内边距
    float iconGap = 8.0f;        // 图标与标题间距
    bool titleMono = false;      // 标题用等宽（如 ch001 这类码）
    bool trailingMono = false;
    // 标题色。0 = text-secondary（列表默认）。有些行是「可点开的实体名」，
    // 设计稿给的是 text-primary（产物文件名就是这一类）—— 那是**刻意的层级差**，
    // 不是随手写的，别在迁移时"顺手统一"成 secondary。
    ImU32 titleColor = 0;
    // 外层已判定「此刻不该接受鼠标」时置 true：**照常画文字与图标，但不注册
    // 命中、也不画 hover 底**。这不是可选的礼貌开关 —— 浮层（命令面板 / 设置
    // 模态 / 报告模态 / 主题菜单）开着时，底下的 chrome 必须完全不响应，
    // 否则浮层没盖住的地方会透出下层的高亮，看着像「点不穿的假浮层」。
    bool suppressed = false;
    // 图标色。0 = text-muted。产物行的文件夹图标是 accent-hover 色（它是个
    // 可点开实体的标记），别在迁移时统一成 muted。
    ImU32 iconColor = 0;
};
// 返回本帧的 Hit（见上：hovered 与 clicked 必须来自同一次命中测试）。
Hit ListRow(ImDrawList* draw, Rect bounds, const ListRowSpec& spec);
// 行高。paddingY 上下内边距，iconSize 只影响下限（图标比文字高时以图标为准）。
[[nodiscard]] float ListRowHeight(const ListRowSpec& spec, float paddingY = 6.0f);

// ---- 20c. ListCard（ProjectHub.jsx:39/226 的 .card.hoverable 条目卡）----
//
// 与 ListRow 是**两种不同的东西**，不要合并：这一族是双行卡（标题 + 描述），
// hover 走**投影档**（ui.css:196-199 的 shadow-1）而不是填色底，选中态是
// accent 描边环（ProjectHub.jsx:40 的 `0 0 0 3px var(--accent-dim)`）而不是
// 换底色 —— 视觉形态不同，折成同一个函数只会让两边都不对。
//
// 两行文字的**块**按卡中心对齐：块高 = titleSize + gap + descSize，
// 块顶 = 卡中心 − 块高/2，行内再各自按块内偏移落字。调用点不再自己排两行的 Y。
struct ListCardSpec {
    std::string_view id;
    std::string_view icon;       // 可空：左侧图标（DrawIconCentered）
    float iconSize = 20.0f;
    std::string_view title;
    std::string_view description;// 可空
    float titleSize = 13.0f;
    float descSize = 11.5f;
    float textGap = 4.0f;        // 标题与描述的基线间距
    float paddingX = 14.0f;
    // 文字左缩进相对 `paddingX` 的**额外**偏移。有缩略图（Art，不是 kit 图标）
    // 的卡要用它把文字让开 —— 缩略图宽度是那一族自己定的，不该由本组件猜。
    float textInset = 0.0f;
    bool selected = false;
    bool hoverable = true;       // false = 不注册命中也不给投影（纯展示格）
};
// 返回本帧的 Hit。footer 回调可选：给了就在卡的右半部画自定义内容
// （如「打开」按钮），**由本函数在整卡命中之后调用** —— ImGui 是先注册者独占
// HoveredId，顺序反了按钮永远点不动（ProjectHub.cpp 的打开列表踩过）。
Hit ListCard(ImDrawList* draw, Rect bounds, const ListCardSpec& spec,
             const std::function<void(ImDrawList*, Rect)>& footer = {});
[[nodiscard]] float ListCardHeight(const ListCardSpec& spec, float paddingY = 14.0f);

// ---- 21. DataTable（ui.css:721-768 .table）----
// th：11.5/600 muted + letter-spacing .03em + pad 8/12 + 下边 line-normal + 底 bg-panel；
// td：pad 9/12 + 下边 line-subtle + secondary；行 hover = fill-hover；
// 选中 = fill-selected + **左侧 2px accent 内阴影** + 字转 primary；
// .num = 等宽 11.5 muted；.center = 居中；.compact = pad 7/8 + 12px（views.css:286）。
struct TableColumn {
    std::string_view title;
    float width = 0.0f;   // <= 0 = 按剩余宽度均分
    bool numeric = false; // .table .num：等宽 11.5 muted
    bool centered = false;// .table.center
    bool sortable = false; // th 可点排序（Overview.jsx:96 用「↓」后缀）
    // 等宽但**不**降为 muted（`.table .num` 那一档）。规则码（C1…C12）这类
    // 「可点的标识」要 accent 色，等宽只是字形选择，不是语义降级。
    bool mono = false;
    // 该列的单元格画成**小号 Tag**（状态 / 级别 / 结论）。Tag 自带量宽与色，
    // 按单元格左内边距摆、垂直居中在行中心 —— 调用点不用再排一次 Y。
    bool tag = false;
};
struct TableRow {
    std::vector<std::string> cells;
    bool selected = false;
    // `tag` 列用这一行给色调。与 cells **下标一一对应**（不需要的列留 Idle）。
    std::vector<theme::Tone> tones;
};
struct TableSort {
    int column = -1;      // -1 = 未排序
    bool ascending = true;
};
// 排序指示按设计稿在标题右侧留 12px（' ↓' 的宽度量级），画一个小三角而不是字符：
// 图集里没有 ↓（U+2193 在 GetGlyphRangesChineseSimplifiedCommon 之外，见 Fonts.cpp:272），
// 用字符会渲染成豆腐块 —— 与 Fonts.h 里「不能有豆腐块」是同一条硬判据。
// 返回整表高度（表头 + 所有行），调用方据此决定要不要滚。
float DataTable(ImDrawList* draw, Rect bounds, const std::vector<TableColumn>& columns,
                const std::vector<TableRow>& rows, TableSort& sort, bool compact,
                std::string_view id);
[[nodiscard]] float TableHeaderHeight(bool compact);
[[nodiscard]] float TableRowHeight(bool compact);

// ---- 22. Tree（ui.css:770-811 .tree）----
// 节点 h28 pad 0 8 r6 gap6 12.5；hover = fill-hover + primary；
// 选中 = fill-selected + primary + 左侧 2px accent；tw 14×14 muted，展开旋转 90°；
// 子层 margin-left 14 + 1px line-subtle 竖线 + padding-left 6。
struct TreeNode {
    std::string label;
    std::string icon;     // 可空（叶子）
    std::string trailing; // 右侧计数/状态文字（Shell.jsx:490 的 3/8）
    bool hasChildren = false;
    bool expanded = false;
    std::vector<TreeNode> children;
};
// selectedIndex 是**可见节点**的扁平序号（先序），点击后写回。返回占用高度。
// 展开态存在 nodes 上（调用方持有），组件本身无跨帧状态 —— 与 Views.h 的约定一致。
float Tree(ImDrawList* draw, Rect bounds, std::vector<TreeNode>& nodes, int& selectedIndex,
           std::string_view id);
[[nodiscard]] float TreeNodeHeight();

// ---- 23. Menu（shell.css:98-145 .menu-pop）----
// 面板 min-w180 + bg-overlay + 1px line-normal + r-md10 + shadow-2 + pad5；
// 项 pad 7/10 r6 gap9 12.5；hover = fill-hover + primary；选中 = accent + accent-dim 底；
// .msep = 1px line-subtle 上下各 5px；.mlabel = 10.5/700 muted + 字距 .06em。
enum class MenuRowKind { Item, Separator, Label };
struct MenuRow {
    MenuRowKind kind = MenuRowKind::Item;
    std::string label;
    std::string icon;      // 可空
    bool selected = false; // .mi.on
    bool disabled = false;
};
// 贴 anchor 右下展开（.menu-pop 的 top: calc(100% + 6px); right: 0）。
// 返回本帧点中的**行**下标（-1 = 没点中 / 点了禁用项）。关闭与否由调用方按返回值决定。
int Menu(ImDrawList* draw, Rect anchor, const std::vector<MenuRow>& rows, std::string_view id);
[[nodiscard]] float MenuWidth(const std::vector<MenuRow>& rows);
[[nodiscard]] float MenuHeight(const std::vector<MenuRow>& rows);

} // namespace shine::kit
