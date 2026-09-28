---
id: contracts.protocols
kind: contract
status: current
scope: external-protocols
source_of_truth:
  - src/comfy/ComfyClient.h
  - src/comfy/ComfySocket.h
  - src/comfy/ComfyNodeDef.h
  - src/flow/GraphCompiler.h
  - src/mcp/MCPServer.h
  - src/mcp/HttpServer.h
  - src/llm/OpenAIClient.h
last_verified: 2026-09-25
---

# 外部协议契约

## ComfyUI HTTP

当前客户端使用的主要端点：

- `GET /queue`
- `GET /object_info`、`GET /object_info/{class}`
- `GET /history`、`GET /history/{prompt_id}`
- `POST /prompt`
- `POST /interrupt`
- `POST /view`
- `POST /api/system_stats` 或设置中使用的系统统计端点
- `POST /api/free`（释放 VRAM）

`/prompt` 的 `client_id` 必须与 WebSocket `clientId` 相同。`/prompt` 400 的 `node_errors` 要转为节点级中文错误，而不是只显示原始 JSON。

## ComfyUI WebSocket

- 地址：`ws://<host>/ws?clientId=<uuid>`；
- 文本帧：`{"type": "...", "data": {...}}`；
- 关键事件：`status`、`execution_start`、`execution_cached`、`executing`、`progress`、`executed`、`execution_error`、`execution_interrupted`；
- 完成：匹配的 `prompt_id` 收到 `executing` 且 `data.node == null`；
- 错误：先 WS，再用 `/history/{prompt_id}.status.messages` 补漏；
- 二进制预览帧头部字段未由本项目完全验证，只能按实际响应和魔数处理。

解析器应宽容未知字段/事件，记录未识别 `type`；不能因服务小版本新增字段而崩溃。

## ComfyUI 节点定义

`ComfyNodeDef` 自动识别 v2 形状（顶层 `inputs` 对象、输出带 `index/is_list`），并只读兼容 v1 的 `input/required/optional/hidden` 形状。`/object_info` 的原始响应是实际版本依据；不要凭空添加未验证端点。

## API JSON 与编辑器 JSON

- API 提交格式：节点 ID → `{class_type, inputs}`；连线值是 `[上游节点 ID, 输出槽位]`；
- 编辑器工作流 JSON：包含 `nodes`、`links`、`groups`、坐标/尺寸等；
- `GraphCompiler` 负责前者，`WorkflowIO` 负责导入/导出兼容；不能把编辑器 JSON 直接提交。

## MCP

`MCPServer` 处理 JSON-RPC，`HttpServer` 负责 HTTP/SSE。工具 schema 和结果包装由 `ToolRegistry`/`Schema` 生成；通知（无 `id`）返回空响应。HTTP 默认本机监听，写工具另有显式开关。

## LLM

`OpenAIClient` 同时处理 Responses 和 Chat Completions；`LlmComplete`、`ChatComplete`、`RunChatToolLoop` 的输入/错误边界不同。Provider 解析集中在 `OpenAIProvider`，密钥不进入日志。外部服务响应格式变化应在解析层兼容，不应让 UI 读取原始响应。
