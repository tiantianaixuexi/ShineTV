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

// ---- ScrimPaint（只画，不注册命中）----
// `.scrim` 是 `position: fixed; inset: 0` —— **铺满整个屏幕**，不是铺满面板。
// design-spec §2.2 已把 backdrop-filter 列为**已知降级**：ImGui 直绘没有背景
// 合成，这里只铺实色。
//
// ⚠️ 模态的遮罩**只能用这个**，不能用下面的 `Scrim`：`Scrim` 会注册一个全屏
// `HitTest`，而 ImGui 同窗口内先注册者独占 HoveredId（imgui.cpp:5161）⇒ 遮罩
// 先注册时，面板里每一个按钮就永远 hovered=false，而遮罩和按钮**外观都画得好
// 好的**。面板框本身用 `ModalFrameRect` / `Drawer`，本函数只负责背景。
void ScrimPaint(ImDrawList* draw, Rect screen);

// ---- OverlayPanel（浮层面板底板）----
// bg-overlay + 1px line-normal + 常驻 shadow-2（ui.css:943-946 `.modal`）。
// 圆角由调用方给：模态 14（r-lg），抽屉 0（.drawer 是直角）。
//
// 凡是「自己摆位置的浮层」（顶部下拉的命令面板那种，既不居中也没有 .modal-h）
// 用 `ScrimPaint` + `OverlayPanel` 拼；居中且带标准头部的走 `ModalFrameRect`。
void OverlayPanel(ImDrawList* draw, Rect frame, float radius = 14.0f);

// ---- Scrim（只画 + 注册命中；仅在没有面板按钮的浮层上用）----
// 在 ScrimPaint 之上再注册一个热区，返回 true = 本帧点了遮罩本身（点内容区
// 不算，Overlays.jsx:35/162 的 `e.target === e.currentTarget` 语义）—— 调用方
// 据此关闭浮层。
// id 必传：同一帧里若有嵌套浮层，固定 ID 会让两个遮罩抢同一个 item。
// ⚠️ 面板里有任何可点控件时**不要用这个**（见上面 ScrimPaint 的说明），
// 「点外面关闭」自己拿 frame 手算点击是否落在面板外即可。
bool Scrim(ImDrawList* draw, Rect screen, std::string_view id);

// ---- Modal（ui.css:943-976 .modal）----
// 居中面板：bg-overlay + 1px line-normal + r-lg14 + shadow-2；
// 默认宽 min(560, 100vw-48)，高 max min(640, 100vh-64)。
// 返回**内容区**（modal-b 的 padding 18 之内）。title 非空才画 modal-h；
// 否则直接是内容区（Overlays.jsx:163 的 ConfirmModal 就是无头弹层）。
// ⚠️ 面板**盖不住**后面画的东西：它和页面共用一条 draw list，所以调用方必须
// 在一帧的**最后**才画浮层（外壳的 onFrame 收尾处）。只有 Tooltip 走了前景
// draw list —— 那个是在绘制途中随控件冒出来的，没法等到帧尾。
//
// 遮罩只画不注册（「点外面关闭」自己手算），所以这里**没有** scrim id 参数
// —— 原来那个 `id` 只服务于 `Scrim` 注册的全屏热区，遮罩改成只画后它就没人用了。
Rect Modal(ImDrawList* draw, Rect screen, std::string_view title, std::string_view icon,
           float width = 0.0f);

// ---- ModalFrame（Modal 的完整分块形式）----
//
// 有些模态要自己摆页脚（ProjectHub 的向导 / 打开 / 确认三个对话框都有按钮条），
// 只拿到内容区不够用。这一个把 **面板框 / 头部 / 内容区 / 页脚** 四块一次交出来，
// 几何与 Modal **同源**（同一份 headerH / padding 常量），不是另算一遍。
//
// `footerButtons > 0` 才切出页脚带（`.modal-f`，ui.css:970-975）：
// 12/18 内边距 + 1px 上边 + 按钮高。footerOut 给按钮可摆的矩形。
//
// ⚠️ 与 Modal 一样：面板画在**当前 draw list** 上，调用方必须在一帧的**最后**
// 画它（外壳 onFrame 收尾处），否则会被后面画的内容盖住。
//
// ⚠️ 遮罩**只画不注册命中**：注册全屏 InvisibleButton 会先拿到 HoveredId，
// 模态里每一个按钮就永远点不动（ImGui 先注册者独占）。「点外面关闭」由调用方
// 用返回的 `frame` 手算点击是否落在面板外 —— 既不抢 HoveredId，也避开本工程
// 会 0xC0000005 的 `ImGui::IsMouseHoveringRect`。
struct ModalFrame {
    Rect frame;   // 整个面板（含头与脚）
    Rect header;  // .modal-h：14/18 内边距 + 1px 下边
    Rect body;    // .modal-b：18 内边距之内
    Rect footer;  // .modal-f（footerButtons == 0 时高度为 0）
};
ModalFrame ModalFrameRect(ImDrawList* draw, Rect screen, std::string_view title,
                          std::string_view icon, float width, float height,
                          int footerButtons = 0);

// ---- Drawer（ui.css:647-686）----
// 右侧 390 宽通栏抽屉：bg-overlay + 1px 左边线 + shadow-2；
// 头 h = 14/16 内边距 + 1px 下边（drawer-h）；脚 12/16 + 1px 上边（drawer-f）。
// 返回**内容区**（drawer-b 的 padding 16 之内）。footerOut 非空时画脚条
// 并把按钮可摆的矩形写进去（Overlays.jsx:128-146 的 drawer-f）；
// footerButtons 只作为「要不要画脚条」的开关保留给调用方读，组件不代画按钮。
//
// ⚠️ 遮罩只画不注册：页脚那排按钮是**调用方在返回之后**才画的，若遮罩注册了
// 全屏热区，按钮就永远抢不到 HoveredId（先注册者独占）—— 外观全对、只有按不动。
Rect Drawer(ImDrawList* draw, Rect screen, std::string_view title, std::string_view icon,
            int footerButtons = 0, Rect* footerOut = nullptr);
[[nodiscard]] float DrawerWidth();

// ---- Toast（ui.css:978-1012）----
// 固定在右下：right 16 / bottom 40（.toasts），列方向、gap 8。
// 单条：min-w 260 max-w 380 + 10/14 内边距 + 1px line-normal +
// **左侧 3px 状态色竖条**（border-left）+ r-md10 + shadow-2 + 12.5px。
// 这是**最后一条**（最靠近屏幕底部那条）该在哪：stackBottom 给 .toasts 的下边。
// 返回这条的高度，调用方往上累加即可排出多条的间距。
//
// `alpha` 是本体的整体不透明度（含投影）：toast 尾巴有 0.4s 淡出，投影必须
// 一起淡 —— 本体没了影子还挂着，看着像渲染残留。1.0 = 不透明（默认）。
float Toast(ImDrawList* draw, Rect screen, std::string_view text, theme::Tone tone,
            std::string_view icon, float above = 0.0f, float alpha = 1.0f);
// .toasts 的固定边距：right 16 / bottom 40。
inline constexpr float kToastRight = 16.0f;
inline constexpr float kToastBottom = 40.0f;
inline constexpr float kToastGap = 8.0f;

} // namespace shine::kit
