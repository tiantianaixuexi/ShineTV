---
id: modules.flow-comfy
kind: reference
status: current
scope: flow-comfy
source_of_truth:
  - src/flow/GraphHost.h
  - src/flow/GraphCompiler.h
  - src/flow/WorkflowIO.h
  - src/flow/ApiGraphValidator.h
  - src/comfy/ComfySession.h
  - src/comfy/ComfyClient.h
  - src/comfy/ComfySocket.h
  - src/comfy/ComfyNodeDef.h
  - src/ui/kit/canvas/FlowCanvas.h
last_verified: 2026-09-25
---

# 流程图与 ComfyUI

## 图模型

`flow::GraphHost` 只持有节点、端口、连线、值、选择和视口状态；不包含 Qt 类型。`kit::FlowCanvas` 只消费 DTO：

```text
FlowCanvasNode { id, title, type, geometry, ports, state, control }
FlowCanvasLink { from_node/from_port, to_node/to_port, type }
```

因此 UI 可以替换或独立测试，不能让画布直接操作 `GraphHost` 内部对象。

## 图目录与编译

`GraphHost::RegisterComfyNodes` 从 `ComfySession::ObjectInfoNodes()` 幂等注册节点类型；`NodeCatalog` 按 category 返回目录。`CompileToApiJson` 输出 ComfyUI API 格式：

```json
{
  "node-id": {
    "class_type": "NodeClass",
    "inputs": { "input": ["upstream-id", 0] }
  }
}
```

必填输入既无连线也无控件值时编译失败；同一图按稳定节点顺序应产生确定性结果。`WorkflowIO` 区分编辑器工作流 JSON 与 API JSON，不能把坐标/分组格式直接 POST 到 `/prompt`。

## 导入与校验

- `ImportGraphFile` / `ImportGraphText` 按内容识别 API JSON、工作流 1.0/0.4 和自家模型格式；
- `ImportReport` 应报告未注册类型和告警；
- `ApiGraphValidator` 使用本机 `/object_info` 校验类名、输入名和类型；
- `object_info` 未就绪时 `VideoTaskRunner::CheckAgainstComfyUI` 返回 blocked，不允许“跳过校验”。

## Comfy 会话

`ComfySession` 是进程级门面，组合：

- `ComfyClient`：异步 REST；
- `ComfySocket`：WebSocket 事件；
- `ComfyQueueModel`：本地队列/状态；
- `ComfyNodeDef`：节点定义 v2 优先、v1 只读兼容。

`ComfyClient` 的异步方法在 worker 执行，完成后通过 `async::PostToUi` 回调。解析函数使用 yyjson，返回结构保留现有 `ok/error` 形状以兼容调用方。

## 协议判定

- WebSocket 地址：`ws://<host>/ws?clientId=<uuid>`；`client_id` 必须与 `/prompt` 请求一致。
- 完成判定：`type == "executing"`、`data.node == null` 且 `prompt_id` 匹配；不能只看 `execution_success`。
- 错误来源顺序：WS `execution_error` → `/history/{prompt_id}` 的 `status.messages` → `/prompt` 400 的 `node_errors`。
- 忙碌不等于卡死。只有 WS 静默、队列仍运行且队列签名未变化达到阈值，才可标记疑似卡住。
- 空闲时长时间无帧不能直接判定断线；应结合探活节流和 `onclose`。
- 二进制预览帧前 8 字节是外部格式头；本项目只按魔数识别图片，不猜测未由本机验证的头字段。

## 诊断顺序

1. 读 `ComfySession::HealthSummary()`、`Busy()`、`LastErrorDetail()` 和最近日志。
2. 检查 `ClientId`、base URL、WS 连接和 `/object_info` 形状。
3. 用原始响应验证节点定义和工作流字段；未知字段应宽容忽略并记录。
4. 再决定重连、重提交或中断；不要把一次 HTTP 超时直接报告为连接失败。
