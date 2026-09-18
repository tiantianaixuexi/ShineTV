---
name: shinetv-graph
description: ShineTV GraphHost（src/graph）与 VisualNodeSystem 集成：NodeArea、节点工厂、选中信息、存盘路径。当用户改中央节点图、加节点类型、存盘加载或问 VNS 怎么用时使用。
---

# GraphHost / VisualNodeSystem

路径：`src/graph/GraphHost.*`。库：`third/VisualNodeSystem`（编入 exe）。

## 职责

| 能力 | API |
|------|-----|
| 初始化 | `graph::Init()`（须在 ImGui Context + 中文字体之后） |
| 每帧画布 | `graph::DrawCanvas()`（在「图」窗口 child 内） |
| 存盘/加载 | `SaveGraph()` / `LoadGraph()` → `%APPDATA%/ShineTVStudio/graph.json` |
| 创建节点 | `SpawnNode(type, x, y)` — UI **不要**直接碰 `VisNodeSys::Node*` |
| 选中 | `SelectedNodes()` / `SelectedCount()` / `DeleteSelected()` |
| 统计 | `NodeCount()` / `ConnectionCount()` / `Zoom()` |
| 视图 | `CenterView()` / `AddGroupCommentAt(x,y,caption)` |

## 生命周期

```
App::Init
  → graph::Init()
      NODE_SYSTEM.Initialize(false)
      RegisterShineNodes()   // NODE_FACTORY
      CreateNodeArea + SetIsFillingWindow(true)
      Load graph.json 或 SeedDemoGraph

每帧 DrawGraphPanel
  → graph::DrawCanvas() → area->Update()  // Input + Render（内部 BeginChild）

App::Shutdown
  → graph::Shutdown()  // 自动 SaveGraph + DeleteNodeArea
```

## 节点类型（P2 占位）

`ShineLoadImage` `ShineLoadModel` `ShinePrompt` `ShineSampler` `ShinePreview` `ShineSaveImage`

- 实现类：匿名 `ShineBasicNode`（一点进一点出）
- 注册：`NODE_FACTORY.RegisterNodeType(type, ctor, copyCtor)`
- **P3** 换成 object_info 动态类型时，工厂类型名与 API `class_type` 对齐

## 约定

1. UI 只调 `shine::graph::*`，不 include VNS 头进 `App.cpp`（保持 incomplete type）
2. `Node` 析构是 **protected**，禁止外层 `delete` 节点；交给 `NodeArea::Delete`
3. 存盘用 VNS `ToJson` / `LoadFromFile`（jsoncpp）；业务工作流 JSON 仍用 yyjson（P3）
4. 右键菜单：`SetMainContextMenuFunction`（添加节点/注释/居中/清空/存盘）
5. 拖放 payload：`SHINE_NODE_TYPE`（type 字符串），在画布 `AcceptDragDropTarget`

## 与属性栏联动

`SelectedNodes()` 返回 `SelectedNodeInfo`（name/type/id/pos/size/sockets），属性面板直接显示。

## 相关 skill

`shinetv-ui-layout`（图面板工具条）、`shinetv-structure`、`shinetv-build`（VNS 源已在 CMake）。
