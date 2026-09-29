#pragma once
// shine::kit::Overlays —— 全局浮层原语（P3.3）
//
// 权威是 webui/src/components/Overlays.jsx + webui/src/styles/ui.css 的
// 「弹层：Modal / Drawer / Toast」一节（ui.css:931-1012）与
// .drawer-scrim/.drawer（ui.css:639-686）。命令面板的壳在
// CommandPalette.jsx + shell.css:476-569，本文件只提供它缺的浮层**原语**：
// 遮罩、面板框、抽屉框、Toast。页面自己决定开什么时候、画什么内容。
//
// 与 Widgets 同一套约定：吃绝对矩形、颜色全走取色器、只用 ImDrawList。
// 浮层必须画在 **前景 draw list**：外壳是一帧一整块自绘，画在普通 draw list
// 上会被后面的控件盖住（这是 Modal/Toast 最容易踩的坑）。
#include "ui/imgui/kit/Widgets.h"

#include <string>
#include <string_view>
#include <vector>

namespace shine::kit {

// ---- Scrim（ui.css:932-942 .scrim / ui.css:639-646 .drawer-scrim）----
// 固定定位铺满 + var(--scrim) 底。design-spec §2.2 已把 backdrop-filter
// 列为**已知降级**：ImGui 直绘没有背景合成，这里只铺实色。
// 返回 true = 本帧点了遮罩本身（点内容区不算，Overlays.jsx:35/162 的
// `e.target === e.currentTarget` 语义）—— 调用方据此关闭浮层。
// id 必传：同一帧里若有嵌套浮层，固定 ID 会让两个遮罩抢同一个 item。
bool Scrim(ImDrawList* draw, Rect screen, std::string_view id);

// ---- Modal（ui.css:943-976 .modal）----
// 居中面板：bg-overlay + 1px line-normal + r-lg14 + shadow-2；
// 默认宽 min(560, 100vw-48)，高 max min(640, 100vh-64)。
// 返回**内容区**（modal-b 的 padding 18 之内）。title 非空才画 modal-h；
// 否则直接是内容区（Overlays.jsx:163 的 ConfirmModal 就是无头弹层）。
// ⚠️ 面板**盖不住**后面画的东西：它和页面共用一条 draw list，所以调用方必须
// 在一帧的**最后**才画浮层（外壳的 onFrame 收尾处）。只有 Tooltip 走了前景
// draw list —— 那个是在绘制途中随控件冒出来的，没法等到帧尾。
Rect Modal(ImDrawList* draw, Rect screen, std::string_view title, std::string_view icon,
           float width = 0.0f, std::string_view id = "kit-modal");

// ---- Drawer（ui.css:647-686）----
// 右侧 390 宽通栏抽屉：bg-overlay + 1px 左边线 + shadow-2；
// 头 h = 14/16 内边距 + 1px 下边（drawer-h）；脚 12/16 + 1px 上边（drawer-f）。
// 返回**内容区**（drawer-b 的 padding 16 之内）。footerOut 非空时画脚条
// 并把按钮可摆的矩形写进去（Overlays.jsx:128-146 的 drawer-f）；
// footerButtons 只作为「要不要画脚条」的开关保留给调用方读，组件不代画按钮。
Rect Drawer(ImDrawList* draw, Rect screen, std::string_view title, std::string_view icon,
            int footerButtons = 0, Rect* footerOut = nullptr,
            std::string_view id = "kit-drawer");
[[nodiscard]] float DrawerWidth();

// ---- Toast（ui.css:978-1012）----
// 固定在右下：right 16 / bottom 40（.toasts），列方向、gap 8。
// 单条：min-w 260 max-w 380 + 10/14 内边距 + 1px line-normal +
// **左侧 3px 状态色竖条**（border-left）+ r-md10 + shadow-2 + 12.5px。
// 这是**最后一条**（最靠近屏幕底部那条）该在哪：stackBottom 给 .toasts 的下边。
// 返回这条的高度，调用方往上累加即可排出多条的间距。
float Toast(ImDrawList* draw, Rect screen, std::string_view text, theme::Tone tone,
            std::string_view icon, float above = 0.0f);
// .toasts 的固定边距：right 16 / bottom 40。
inline constexpr float kToastRight = 16.0f;
inline constexpr float kToastBottom = 40.0f;
inline constexpr float kToastGap = 8.0f;

} // namespace shine::kit
