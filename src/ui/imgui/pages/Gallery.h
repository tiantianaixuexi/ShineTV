#pragma once
// shine::pages::Gallery —— 组件画廊（P3.7）
//
// design-spec 把它定位成「P3 的验收判据 + 后续每个组件的回归基准」。
// 照 webui/src/views/Gallery.jsx 铺全部组件 × 全部状态，5 套主题各截一轮。
//
// 网格用 repeat(auto-fit, minmax(340px,1fr))（design-spec §8）—— ImGui 不能继承
// CSS 的 auto-fit，必须在 C++ 里按可用宽算列数。
#pragma once

#include "ui/imgui/kit/Widgets.h"

namespace shine::pages {

class GalleryPage {
public:
    // 骨架 .vw = padding 20px 24px 26px
    void Draw(kit::Rect area, ImDrawList* draw);

private:
    int selectedTab_ = 0;
};

} // namespace shine::pages
