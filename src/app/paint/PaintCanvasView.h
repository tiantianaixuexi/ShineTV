#pragma once
// shine::app::paint —— 画布视图（P6.2–P6.4）
#include <string_view>

namespace shine::app::paint {

void DrawPaintCanvasWindow();  // 中央「画布」
void DrawPaintSidePanel();     // 侧栏工具 + inpaint 参数

// P6.4：图库/输出「发送到画布」——把图片载入为底图
[[nodiscard]] bool LoadImageToCanvas(std::string_view utf8Path);

} // namespace shine::app::paint
