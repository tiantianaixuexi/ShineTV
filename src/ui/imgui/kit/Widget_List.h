#pragma once
// shine::kit::Widget_List —— 条目行（ListRow / ListCard）
//
// 从 kit/Widgets.cpp 拆出。两者是**两种不同的东西**，不要合并：ListRow 是单行条目，
// ListCard 是双行卡（hover 走投影、选中走 accent 描边环）。

#include "ui/imgui/kit/Widget_Core.h"

#include <functional>
#include <string_view>

namespace shine::kit {

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

} // namespace shine::kit
