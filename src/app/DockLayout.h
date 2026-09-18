#pragma once

#include <imgui.h>

namespace shine::app::dock {

// VS Code 风格：侧栏 | 中央图 | 右栏 | 底栏（活动栏在 Dock 外）
void BuildDefaultLayout(ImGuiID dockspaceId, ImVec2 dockSize);
bool NeedsRebuild();
void RequestRebuild();

} // namespace shine::app::dock
