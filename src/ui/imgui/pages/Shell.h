#pragma once
// shine::pages::Shell —— 应用外壳（顶栏 / 导航 / 侧栏 / 检查器 / 底栏 / 状态栏）
//
// 对应 refactor/phases.md P4。尺寸全部照 design-spec.md §4（100% 缩放下）：
//   TopBar 46 / Rail 56 / SidePanel 240 / Inspector 280 / Dock 190 / StatusBar 26 / Crumbs 34
//
// P0.4 阶段这里只放一个占位窗口：先把「宿主能开窗、能画一帧」这条判据立住，
// 外壳各项在 P4 逐项补齐。
namespace shine::pages {

class Shell {
public:
    void DrawFrame(float dt);

private:
    float lastDelta_ = 0.0f;
};

} // namespace shine::pages
