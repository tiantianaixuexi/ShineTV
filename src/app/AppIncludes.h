#pragma once
// 自动生成（R-S0 拆分）：App.cpp 的 include 块 —— 搬出去的各窗口 .cpp 统一包含本头，
// 这样原先可见的符号（imgui / graph / comfy / media / theme / util …）在新 TU 里同样可见。
// 后续可按窗口收窄（R-S1 之后逐步换掉），现在优先保证"机械搬家、行为不变"。
#include "app/App.h"
#include "app/DockLayout.h"
#include "app/FileDialog.h"
#include "comfy/ComfySession.h"
#include "comfy/ComfyWorkflows.h" // P3.7：从 ComfyUI 拉取工作流
#include "core/Async.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "gallery/Gallery.h"
#include "gallery/ImageLoader.h"
#include "graph/GraphHost.h"
#include "graph/WorkflowIO.h"
#include "gpu/GpuDevice.h"
#include "gpu/GpuTextureCache.h"
#include "gpu/GpuTextureManager.h"
#include "media/MediaLibrary.h"
#include "app/output/OutputView.h"
#include "theme/Theme.h"
#include "util/Strings.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include "app/UiState.h"                        // R-S0：应用级 UI 状态（原 g_* 全局）
#include "app/shots/ShotTableView.h"            // P5.3：分镜表（停靠窗口 + 侧栏面板）
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "app/AppInternal.h"
