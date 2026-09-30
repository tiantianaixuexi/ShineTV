#pragma once
// shine::kit::Widgets —— 组件套件的**门面**（facade）
//
// ⚠️ 本文件是纯转发头：它不再声明任何控件，只 include 各控件族的分头文件。
//    下游 `#include "ui/imgui/kit/Widgets.h"` **不需要改**，导出的公开符号集
//    与拆分前完全一致（`kit::Rect` 也仍从这里拿到 —— kit/Draw.h 与多个 pages
//    依赖这一点）。
//
//    拆分前的 1282 行单文件按「控件族」切成了：
//      Widget_Core.h     Rect / RectAt / Hit / HitTest / Clicked / Hovered / 重复命中自检
//      Widget_Color.h    主题取色 + alpha 运算（ColorOf / ColorText… / WithAlpha…）
//      Widget_Button.h   Button 系（Button / IconButton）
//      Widget_Badge.h    徽标系（Tag / StatusDot / Kbd）
//      Widget_Card.h     容器卡（Card / CardHeaderRow）
//      Widget_Choice.h   单选系（Segmented / Chip / Tabs，共享 SegmentOption）
//      Widget_Input.h    输入系（Field / Input / TextArea / Select / Switch / Checkbox）
//      Widget_Status.h   状态展示（Progress / Empty / KeyValues / Spinner / Divider）
//      Widget_Overlay.h  浮层系（Tooltip / Menu）
//      Widget_List.h     条目行（ListRow / ListCard）
//      Widget_Table.h    数据表（DataTable）
//      Widget_Tree.h     树（Tree）
//
//    契约与实现见各分头文件；注释里的设计稿出处（UI.jsx / ui.css / shell.css /
//    views.css 行号）随代码一起搬到了对应族。
//
// 权威是 webui/src/components/UI.jsx（文件头自己写着「对应 Qt 端 kit::widgets
// 语义」）+ webui/src/styles/ui.css 的尺寸。**不是**把 Qt 的 32 个 widget 逐个翻译。
// 全部尺寸来自 design-spec.md §5，逐条标了 CSS 出处。
//
// 统一约定：
//   * 所有控件吃 **绝对矩形**（design-spec 的布局是 CSS 绝对定位式），
//     不走 ImGui 的线性布局 —— 否则外壳的自绘栅格无法控制。
//   * 命中测试统一用 InvisibleButton 占位（保持 ImGui 的 ID 栈与焦点语义）。
//   * 颜色一律从 theme 取，**不许**在调用点写 ImVec4 字面量（check-layers 规则 3）。

#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Icon.h"
#include "ui/imgui/kit/Widget_Badge.h"
#include "ui/imgui/kit/Widget_Button.h"
#include "ui/imgui/kit/Widget_Card.h"
#include "ui/imgui/kit/Widget_Choice.h"
#include "ui/imgui/kit/Widget_Color.h"
#include "ui/imgui/kit/Widget_Core.h"
#include "ui/imgui/kit/Widget_Input.h"
#include "ui/imgui/kit/Widget_List.h"
#include "ui/imgui/kit/Widget_Overlay.h"
#include "ui/imgui/kit/Widget_Status.h"
#include "ui/imgui/kit/Widget_Table.h"
#include "ui/imgui/kit/Widget_Tree.h"
#include "ui/imgui/theme/Theme.h"

#include <imgui.h>

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
