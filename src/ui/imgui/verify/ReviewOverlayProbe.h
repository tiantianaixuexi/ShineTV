#pragma once
// shine::imguiverify::detail —— 浮层点击判据
//
// 为什么必须有这一族：浮层（设置模态 / 报告模态 / 命令面板 / 主题菜单）的 item
// 是在**根窗口**里提交的，而工作区是 `BeginChild`。ImGui 定 `g.HoveredWindow`
// 是「从 `g.Windows` 末尾往前扫、取第一个命中」（imgui.cpp:6573 / 6607），
// `ItemHoverable` 第一句又判 `if (g.HoveredWindow != window) return false;`
// （:5156）—— 只要鼠标在工作区范围内，浮层按钮恒 hovered=false / clicked=false。
//
// 这个缺陷**在像素上完全看不出来**：按钮照画不误，几十张静息绿图零覆盖，
// hover 探针也抓不到（它们只打根窗口控件和 child 内部的控件）。
// 所以判据只能读**产品自己的开态**：点完 `settingsOpen()` 变没变。
//
// 这一族里每条判据都有**前置条件**，而「前置不成立时它本来会报通过」正是最险
// 的一种假绿 —— 前置条件的检查写在各自的注释里。

#include "ui/imgui/verify/ReviewSession.h"

namespace shine::imguiverify::detail {

// 跑完浮层点击判据的三组探针，并把 `overlay-clicks=ok/total` 写进 manifest。
// 同时填 Session::overlayClicksPassed / overlayClickTotal。
void RunOverlayProbes(Session& session);

} // namespace shine::imguiverify::detail
