#pragma once
// shine::mcp —— 内置业务工具（P7.4）
#include "mcp/ToolRegistry.h"

namespace shine::mcp {

// Comfy 桥接工具：comfy_ping / comfy_queue / comfy_interrupt / comfy_submit / ...
void RegisterComfyBuiltinTools(ToolRegistry& reg);

// ShineTV 能力：shinetv_status / shinetv_graph_compile / shinetv_graph_submit / ...
void RegisterShineTvBuiltinTools(ToolRegistry& reg);

// 组合：EnsureModule + 上面两个（MCPServer / bootstrap 启动时调用，幂等）
void RegisterAllBuiltinTools(ToolRegistry& reg);

} // namespace shine::mcp
