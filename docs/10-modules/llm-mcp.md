---
id: modules.llm-mcp
kind: reference
status: current
scope: llm-mcp
source_of_truth:
  - src/llm/OpenAIClient.h
  - src/llm/OpenAIProvider.h
  - src/llm/AgentKit.h
  - src/llm/ToolRegistry.h
  - src/mcp/ToolRegistry.h
  - src/mcp/HttpServer.h
  - src/mcp/MCPServer.h
  - src/mcp/McpBootstrap.h
  - src/novel/NovelMcpTools.h
last_verified: 2026-09-25
---

# LLM、Agent 与 MCP

## LLM 边界

`src/llm` 提供 OpenAI Responses、OpenAI-compatible Chat Completions、Anthropic 兼容路径和 Provider 解析。同步客户端只能在 worker 线程调用；流式回调也在调用 worker 中触发，UI 消费需要自行通过 `PostToUi` 投递。

`AppSettings` 保存 Provider、base URL、模型、key 和运行预算。key 不得进入日志、截图、文档或提交；设置文件本身可能包含敏感信息。

## AgentKit

`shine::agent::AgentKit` 面向小说 Agent：

- Agent 定义存 `agent_defs`，内置定义可更新 prompt；
- `Route` 按任务选择 Agent；
- `BuildSystemPrompt` 合并内置 prompt、库内覆盖和动态字段说明；
- `ResolveTools` 计算 Agent 白名单与本地/MCP 工具交集；
- `Run` 可注入 mock `CreateFn`，用于离线自检；
- `BuildInvokePackage` 只输出 prompt 和允许工具，不注入全表。

Agent 运行时不能绕过 `tools_json` 白名单直接调用所有工具。

## MCP 工具注册

`shine::mcp::ToolRegistry` 是进程级注册表，支持模块、同名覆盖、查找、`tools/list` JSON 和 `Call`。协议层只消费注册表，不直接知道小说或 Comfy 的业务规则。

`RegisterAllModules` 当前注册 demo、小说 `novel_*`、Comfy 内置工具和 ShineTV 工具；`novel_generate_chapter` 的实现由装配层注入 `NovelPipeline`，未注入时明确返回内部错误。

## MCP 传输

`HttpServer` 封装 libhv HTTP server：

| 路由 | 用途 |
|---|---|
| `GET /health` | 服务、版本、端口、工具数 |
| `POST /mcp`、`/messages`、`/message` | JSON-RPC；部分路径支持 SSE 响应 |
| `GET /sse`、`/mcp/sse` | 长连接事件流 |
| `GET /tools` | 工具列表 |
| `POST /tools/<name>` | 工具调用 |

默认监听 `127.0.0.1`，是否启用由 `AppSettings::mcpEnabled` 决定。HTTP handler 默认调度到 UI；测试/无 UI 场景可使用当前线程 dispatcher。

`--mcp-stdio` 走 `RunStdioServerMain`：stdin 收 JSON-RPC，stdout 只输出协议响应，日志转 stderr。

## 写权限

MCP 写工具默认关闭。只有设置 `mcpAllowWrite` 或 `SHINE_MCP_ALLOW_WRITE` 明确开启才允许；自检可以用 `SetMcpWriteForceDeny` 强制拒绝，覆盖其他来源。业务工具仍需自己的读写权限和事务边界。

## 协议文档原则

- JSON-RPC 错误、工具参数 schema 和 MCP 内容形态由 `src/mcp` 定义；
- OpenAI/ComfyUI 外部协议可能随服务版本变化，解析器宽容未知字段但必须记录未识别事件；
- 不要在本文件复制完整外部 API；实现旁边保留原始响应和针对性测试。
