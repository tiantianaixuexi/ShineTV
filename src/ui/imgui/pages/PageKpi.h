#pragma once
// shine::pages —— KPI 卡（右上角 accent 柔光 + 数值 + 单位 + 口径脚注）
//
// 为什么提成组件：总控页一处定义、五处调用（阶段进度 / LLM 调用 / 估算成本 /
// 镜头 / 阶段产物）。它原先是 WorkspaceA.cpp 匿名命名空间里的 `KpiCard`，而
// 「右上角那团柔光」是**整页 KPI 行的共同视觉**，不是某一页的私有装饰。
//
// 收录标准（与 PageCommon.h 同一把尺）：**换一页也照样成立**。目前只有总控页
// 用它，但那不是收录的理由也不是不收录的理由 —— 理由是这 5 张卡**逐像素同构**，
// 复制一份就多一处会各自漂移的数值。
// ⚠️ 走 kit/Widgets.h 这层**门面**（它转 include 其余 Widget_*.h），不要直接写
//    某个 Widget_*.h：kit 正在按组件拆，下游 include 门面才不会跟着动。
#include "ui/imgui/kit/Widgets.h"
#include "ui/imgui/theme/Theme.h"

#include <string_view>

namespace shine::pages {

// `tone` 取 theme::Tone：右上角柔光按色调取色（Accent=0 / Info / Ok / Warn /
// Danger / Busy / Idle，见 theme/Theme.h）。
void KpiCard(ImDrawList* draw, kit::Rect bounds, std::string_view label, std::string_view value,
             std::string_view unit, std::string_view footnote, theme::Tone tone);

} // namespace shine::pages
