---
name: shinetv-graph
description: ShineTV 当前 flow 图模型、节点目录、工作流导入导出、Comfy API 编译和 FlowCanvas DTO 边界。
---

# 流程图与节点画布

先读：[流程/Comfy 模块](../../../docs/10-modules/flow-comfy.md)、[源码索引](../../../docs/90-reference/source-map.md)。

## 分层

```text
flow::GraphHost / GraphCompiler / WorkflowIO
             ↓ DTO
kit::FlowCanvas（QGraphicsView）
```

`FlowCanvas` 不依赖 `GraphHost` 内部对象；出图和出片工作区共用这一 Qt 画布。

## 主要 API

- `GraphHost::RegisterComfyNodes`、`NodeCatalog`、`SpawnNode`、`TryConnect`；
- `ImportGraphFile/Text`、`SaveGraph/LoadGraph`；
- `CompileToApiJson` 输出 `{id:{class_type,inputs}}`；
- `FlowCanvas::SetGraph`、`SetNodeState`、选择/连线/缩放回调。

## 规则

1. 节点类型来自 `/object_info`，注册幂等；未知类型要报告。
2. 必填输入缺失、类型不匹配或 object info 未就绪时不能提交。
3. 编辑器工作流 JSON 与 API JSON 分开处理。
4. 节点/连线 ID 和遍历顺序稳定，保证编译确定性。
5. UI 只操作 DTO；图模型不依赖 Qt。
