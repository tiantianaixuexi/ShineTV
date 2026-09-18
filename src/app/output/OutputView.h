#pragma once
// shine::app::output —— 底栏「输出」页：历史结果浏览器（P4.4；自 media/ui 迁入 app）
// 数据来自 `MediaLibrary`（时间倒序）；缩略图**按需加载**（只请求可见行），列表用 ImGuiListClipper。
namespace shine::app::output {

void DrawOutputView();

} // namespace shine::app::output
