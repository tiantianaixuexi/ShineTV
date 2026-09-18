#pragma once
// R-S0：App.cpp 拆分的"共享内部头"。三件事：
//   ① 各窗口入口函数的声明（跨窗口互相调用靠它）；
//   ② 过渡别名 `ui` / `g_*`（原 App.cpp 匿名命名空间的全局；用 inline 变量让所有 TU 共享）；
//   ③ 原先的文件级小工具（StateColor / kActivityBarWidth）。
// **R-S2 起**：①保留（就是模块接口），②逐步替换为 `State().xxx` 并删除，③按归属下沉到 app::ui。
#include "app/UiState.h"

#include <imgui.h>

#include <array>
#include <string>

#include "comfy/ComfyTypes.h"

namespace shine::app {


// —— ② 原文件级小工具（②③ 合并：别名块已随 R-S2 清除）——
inline constexpr float kActivityBarWidth = 46.f;
inline ImVec4 StateColor(comfy::ConnectionState s) {
    switch (s) {
    case comfy::ConnectionState::Connected: return ImVec4(0.24f, 0.86f, 0.59f, 1.f);
    case comfy::ConnectionState::Connecting: return ImVec4(1.f, 0.62f, 0.11f, 1.f);
    case comfy::ConnectionState::Error: return ImVec4(0.90f, 0.22f, 0.27f, 1.f);
    default: return ImVec4(0.45f, 0.50f, 0.56f, 1.f);
    }
}

// —— ① 窗口入口 ——
void DrawConnectionStatus(const char* prefix = "");
void DrawMenuBar();
void DrawActivityBar(float height);
const char* SideViewTitle(SideView v);
void DrawSideBar();
void DrawDockedPanels(ImVec2 dockSize);
void DrawStatusBar(float width, float height);
void DrawAssetsPanel();
void DrawNodesPanel();
void DrawWorkflowsPanel();
void DrawComfySidePanel();
void DrawGraphPanel();
void DrawInspectorPanel();
void DrawPreviewPanel();
void DrawBottomPanel();
void DrawQueueTab();
void DrawLogTab();
void DrawSettingsWindow();
void DrawTemplateWindow(); // P3.7：工作流模板浏览器（独立浮窗）
void DrawNodeWindow();     // P3.7b：节点浏览器（独立浮窗）

} // namespace shine::app
