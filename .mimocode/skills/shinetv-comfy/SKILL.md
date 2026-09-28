---
name: shinetv-comfy
description: ShineTV 当前 ComfyUI HTTP/WebSocket 会话、节点定义、队列健康、提交校验与线程边界。
---

# ComfyUI 接入

先读：[流程/Comfy 模块](../../../docs/10-modules/flow-comfy.md)、[协议契约](../../../docs/20-contracts/protocols.md)。

## 代码入口

| 符号 | 作用 |
|---|---|
| `comfy::ComfySession` | 进程级 base URL、WS、队列、节点目录门面 |
| `comfy::ComfyClient` | REST 与 yyjson 解析；异步回调回 UI |
| `comfy::ComfySocket` | WebSocket 事件、二进制预览、重连 |
| `comfy::ComfyNodeDef` | 节点定义 v2 优先、v1 只读兼容 |
| `flow::GraphHost` | 图模型、节点目录、导入/编译 |
| `video::VideoTaskRunner` | 任务状态、上传、提交、历史和落盘 |

## 线程

`ComfyHttp`/`ComfyClient` 同步函数只在 worker；WS 回调只在网络线程触碰会话数据，UI 状态通过 `async::PostToUi` 更新。页面使用 Session/Client Async，不直接调用同步 HTTP。

## 判定规则

- WS：`ws://<host>/ws?clientId=<uuid>`；`/prompt` 的 `client_id` 必须相同。
- 完成：匹配的 `prompt_id` 收到 `executing` 且 `data.node == null`。
- 错误：WS `execution_error` → `/history/{prompt_id}` → `/prompt` 400 `node_errors`。
- 忙碌不是卡死；只有 WS 静默、运行队列未变且达到阈值才标记 `Stalled`。
- `/object_info` 未就绪时拒绝提交；不能把未校验当通过。
- 节点/工作流字段以本机原始响应验证；解析器宽容未知字段但记录未知事件。

## 关键端点

`/queue`、`/object_info`、`/history`、`/prompt`、`/interrupt`、`/view`、`/api/system_stats`、`/api/free`。

## 诊断顺序

先读 `HealthSummary()`、`Busy()`、`LastErrorDetail()` 和最近日志，再决定重连、重提交或中断；不要用一次探活超时推断连接失败。
