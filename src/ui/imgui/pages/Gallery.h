#pragma once
// shine::pages::Gallery —— 组件画廊（原生 ImGui 试点页）
//
// 历史上它是「P3 的验收判据 + 每个组件的回归基准」：照 webui/src/views/Gallery.jsx
// 铺全部组件 × 全部状态，5 套主题各截一轮。
//
// ⚠️ 2026-11-23 起 **1:1 复刻 webui 的约束已去掉**，本页改成 ImGui 原生控件 + 流式布局：
//   * 卡片高度由内容决定（BeginChild + AutoResizeY），不再是 repeat(auto-fit,minmax(340px,1fr))
//     的两列网格，也没有 entries[12].height 那张高度表；
//   * 命中 / 焦点 / 文本编辑 / 排序 / 弹出层由 ImGui 负责，页面不再自己 HitTest；
//   * 它仍然承担原来的职责 —— **证明这些控件真的可用**。控件换了实现，判据没变。
// 验收点见 Gallery.cpp 顶部注释。
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
