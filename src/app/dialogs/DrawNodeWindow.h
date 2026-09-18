#pragma once
#include "app/AppInternal.h"

// 节点浏览器（P3.7b）：独立浮窗，左边分类、右边该分类下的节点，**点击即建到画布视口中心**（也可拖到画布）。
// 数据来自 `/object_info`（`graph::NodeCatalog()`），未连 ComfyUI 时显示内置示例兜底。
namespace shine::app {

void DrawNodeWindow();

} // namespace shine::app
