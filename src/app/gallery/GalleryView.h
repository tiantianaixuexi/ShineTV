#pragma once
// shine::app::gallery —— 图库视图（G-S5 S3；自 gallery/ui 迁入 app）
//
// 三块（**都只在 UI 线程调用**）：
//   * `DrawGalleryWindow()`       → 停靠窗口「图库」的内容（与「图」同区、tab 切换）：工具条 + 列表
//   * `DrawGallerySidePanel()`    → 侧栏「图库」：来源切换 / 目录 / 缩略图尺寸档 / 缓存与统计
//   * `DrawGalleryViewerWindow()` → 「查看器」浮窗内容（**G-S5 只是占位**；完整实现在 G-S10）
//
// 本步只出**列表**（文件名 + 尺寸 + 格式 + 大小）；缩略图网格自 G-S6 起。
// 颜色一律取自 `theme::Current()`；选中/操作只改 `gallery::Model()`。

namespace shine::app::gallery {

void DrawGalleryWindow();
void DrawGallerySidePanel();
void DrawGalleryViewerWindow();

} // namespace shine::app::gallery
