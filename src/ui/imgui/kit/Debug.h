#pragma once
// shine::kit::DebugWindows —— 直接用 ImGui 自带的调试/参考窗口
//
// 这一组是**不重写的部分**：ImGui 官方把它们作为学习与诊断的标配，
// imgui_demo.cpp 早已编进 exe，之前一直没被调用。
//
//   * ShowDemoWindow     —— 官方组件画廊。本项目另有自己的组件画廊（对齐 webui
//                           设计稿），但**这一份必须留着**：它是「我调用方式对不对」
//                           的对照基准，出问题时先跟它比。
//   * ShowMetricsWindow  —— 帧的 draw call / 顶点数 / 各窗口 rect / 活动 ID。
//                           自绘 UI 最缺的就是这个：没画出来时它能立刻指出
//                           是被裁剪了、还是 alpha 为 0、还是尺寸为 0。
//   * ShowDebugLogWindow —— 焦点、导航、ID 冲突的逐条流水。自绘控件大量用
//                           SetCursorScreenPos + InvisibleButton，ID 撞车只有它能报。
//   * ShowStyleEditor    —— 31 个颜色 token 与 ImGuiStyle 的实时编辑器。
//                           样式编辑器页（P4.10）的底座直接用它，不要手写取色器。
#pragma once

#include <imgui.h>

namespace shine::kit {

struct DebugWindows {
    bool demo = false;
    bool metrics = false;
    bool log = false;
    bool about = false;
};

// 每帧调用。F1 = Metrics，F2 = Debug Log，Shift+F1 = Demo 窗口。
void DrawDebugWindows(DebugWindows& state);

// 样式编辑器：把 theme 的 31 个 token 摆成可编辑的网格。
// 改动先落在 ImGuiStyle 上，Save 时回写到 theme::Current()。
void DrawStyleEditorPanel();
bool StyleEditorSaveRequested();
void StyleEditorRequestSave();
void StyleEditorClearSaveRequest();

} // namespace shine::kit
