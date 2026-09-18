#pragma once
// shine::mcp —— 显式分模块注册 bootstrap
//
// 业务模块约定（与 RegisterShineNodes / RegisterBuiltinDecoders 同风格）：
//   namespace shine::comfy { void RegisterMcpTools(mcp::ToolRegistry& reg); }
//
// 推荐接入顺序（稳定、与依赖无关）：
//   demo → comfy → graph → gallery → media → video → novel
// 新模块就绪后只在 RegisterAllModules 里加：EnsureModule + Xxx::RegisterMcpTools(reg)
#include "mcp/MCPTypes.h"
#include "mcp/ToolRegistry.h"

namespace shine::mcp {

// 注册全部模块（幂等）。当前落地 demo 桩；P7.4 业务模块按上表追加。
void RegisterAllModules(ToolRegistry& reg);

// demo 模块：mcp_ping / mcp_registry_info（自检与协议联调用）
void RegisterDemoModule(ToolRegistry& reg);

// 自检：同名覆盖 / 模块分组 / tools/list / Call 四条路径
[[nodiscard]] bool RunRegistrySelfCheck();

// P7.2：HTTP 骨架自检（见 HttpServer.h）
[[nodiscard]] bool RunHttpServerSelfCheck();

// P7.3：JSON-RPC 协议自检（定义在 MCPServer.cpp）
[[nodiscard]] bool RunMcpProtocolSelfCheck();

} // namespace shine::mcp
