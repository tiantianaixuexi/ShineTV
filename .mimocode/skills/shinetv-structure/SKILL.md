---
name: shinetv-structure
description: ShineTV Studio 当前 Qt Widgets 架构、目录职责与分层边界；用于定位代码、选择模块落点或设计新模块。
---

# ShineTV 当前结构

先读：[架构总览](../../../docs/00-overview/architecture.md)、[源码索引](../../../docs/90-reference/source-map.md)。

## 分层

```text
ShineTVStudio  src/app       Qt 页面、装配、验收开关
      ↓
shine_kit      src/kit       theme/motion/widgets/data/images/FlowCanvas
      ↓
shine_core     src/*         模型、协议、存储、网络、生成与编排（禁止 Qt）
```

## 目录速查

| 目录 | 当前职责 |
|---|---|
| `src/core` `src/util` | 设置、日志、异步、编码/文件/反射工具 |
| `src/net` `src/db` | libhv HTTP；SQLite/Redis 适配 |
| `src/llm` `src/comfy` `src/mcp` | LLM、ComfyUI、MCP 协议与工具 |
| `src/flow` `src/visual` `src/novel` | 图模型、视觉生成、小说世界状态 |
| `src/media` `src/paint` `src/gpu` | 媒体/图片、inpaint、纹理资源 |
| `src/project` `src/pipeline` | 项目文件/模板；阶段/预算/检查点 |
| `src/app` | `MainWindow`、工作区、CLI/自检入口 |
| `src/kit` | 可复用 Qt UI，不依赖业务类型 |

## 落点规则

1. 业务模型/协议放 `shine_core` 对应域；页面放 `src/app/<feature>`；通用控件放 `src/kit`。
2. `shine_core` 不得 include Qt；页面不得直接操作第三方 C API。
3. IO/网络/解码/生成在 worker，结果经 `async::PostToUi` 回 UI。
4. 新 `.cpp` 手工登记 `CMakeLists.txt`；不新建第二套线程池、HTTP、JSON 或日志设施。
5. 修改跨模块契约时同步对应文档，不创建阶段计划或进度文件。

## 当前入口

- 启动：`src/app/main.cpp` → `AppEntry.cpp`。
- 项目：`project::ProjectService`。
- 小说：`shine::novelcore` / `shine::novel` / `shine::agent`。
- 生成：`shine::video::VideoTaskRunner`。
- 流程图：`shine::flow::GraphHost` + `shine::kit::FlowCanvas`。
