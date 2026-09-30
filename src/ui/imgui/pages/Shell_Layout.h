#pragma once
// shine::pages —— 外壳版式的**固定档**（design-spec §4，100% 缩放下）
//
// 这七个数原先是 Shell.cpp 顶部匿名命名空间里的 `constexpr`，而顶栏 / 导航栏 /
// 侧栏 / 检查器 / 底栏 / 状态栏 / 面包屑**七块外壳各自都要用**。它们一旦各自
// 写一份，就是七份可能互相打架的尺寸；留在一个 .cpp 里则那个文件永远拆不开。
//
// ⚠️ 只有「**换一块外壳照样成立**」的量才放这里。命令面板的行高、侧栏树的扁平
//    序号、布局文件的 magic 都不属于版式 —— 它们各自只服务于一块，留在对应 .cpp。
#include <cstdint>

namespace shine::pages {

// design-spec §4 的全部固定档（100% 缩放下）
constexpr float kTopBarHeight = 46.0f;
constexpr float kRailWidth = 56.0f;
constexpr float kSidePanelWidth = 240.0f;
constexpr float kInspectorWidth = 280.0f;
constexpr float kDockHeight = 190.0f;
constexpr float kStatusBarHeight = 26.0f;
constexpr float kCrumbHeight = 34.0f;

} // namespace shine::pages
