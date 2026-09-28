---
id: overview.architecture
kind: reference
status: current
scope: architecture
source_of_truth:
  - CMakeLists.txt
  - tools/check-layers.ps1
  - src/pages/shell/MainWindow.h
  - src/core/Async.h
last_verified: 2026-09-25
---

# 架构总览

## 分层图

```text
┌──────────────────────────────────────────────────────────┐
│ ShineTVStudio：Qt 6 Widgets 应用壳、工作区、验收开关       │
│ src/pages  src/app                                       │
└───────────────┬──────────────────────────────────────────┘
                │ 依赖
┌───────────────▼──────────────────────────────────────────┐
│ shine_kit：主题、动效、通用控件、FlowCanvas              │
│ src/widget                                               │
└───────────────┬──────────────────────────────────────────┘
                │ 依赖
┌───────────────▼──────────────────────────────────────────┐
│ shine_core：模型、协议、存储、网络、生成与编排            │
│ src/core util net db llm comfy media flow visual novel   │
│ src/paint mcp project pipeline gpu                       │
└──────────────────────────────────────────────────────────┘
```

这是**目标方向**。当前 `CMakeLists.txt` 将 `project` 和 `pipeline` 源文件登记在 `shine_core` 静态库中；它们仍应保持对业务层的单向依赖，不因物理归类而向 UI 反向泄漏。

## 目录职责

| 目录 | 允许承担 | 不应承担 |
|---|---|---|
| `src/app` | QApplication、启动、验收开关 | 业务协议实现、页面布局 |
| `src/pages` | 各业务页面与工作区 | 可复用控件实现 |\n| `src/widget` | 主题、动效、通用控件、FlowCanvas | 具体小说/项目业务类型和业务判断 |
| `src/core` | 设置、日志、异步邮箱 | Qt 控件和业务协议 |
| `src/util` | 纯工具和静态反射 | 状态、线程和业务副作用 |
| `src/net` | libhv 出站 HTTP 薄封装 | Comfy/LLM 业务错误解释 |
| `src/db` | SQLite/Redis 适配 | UI 状态和协议路由 |
| `src/llm` | Provider、流式响应、AgentKit | 小说状态提交规则 |
| `src/comfy` | ComfyUI HTTP/WS、节点定义、队列 | 小说世界状态和 Qt 页面 |
| `src/media` | 媒体库、图片解码、缓存、预览输入 | 生成工作流业务 |
| `src/flow` | 图模型、节点目录、IO、编译、绑定和校验 | Qt 绘制和项目页面布局 |
| `src/visual` | 资产/分镜/视频模型、任务执行和降级账 | 小说数据库 schema |
| `src/novel` | 小说库、图谱、生成、初始化、状态提交 | Qt 页面布局 |
| `src/paint` | CPU 画布、遮罩、PNG 编解码适配 | Comfy 会话和项目文件契约 |
| `src/mcp` | MCP 注册、JSON-RPC、HTTP/SSE/stdio | 小说业务规则本身 |
| `src/project` | 项目文件、索引、模板、项目引用 | 小说/视觉/Comfy 业务 |
| `src/pipeline` | 阶段、预算、账本、检查点、Runner | 页面布局和外部协议细节 |

## 依赖规则

1. `shine_core` 源文件不得 `#include <Q...>`；运行 `tools/check-layers.ps1` 验证。
2. `src/widget` 只能依赖 `shine_core`、`src/util` 和 Qt，不 include `novel/`、`project/` 等业务头。
3. 页面只调用业务层公开接口；业务层不回调 QWidget。
4. 同一基础能力只保留一个实现：HTTP 用 `net`，日志用 `core/Log`，异步用 `core/Async`，反射用 `util/Reflect`。
5. 外部协议在适配层转换；不要让 Win32/yyjson/libhv 的原始类型穿透到业务接口。

## 运行时边界

- Qt 事件循环和 UI 状态在主线程。
- `shine::async::RunOnWorker` 执行 IO/解析/解码/网络/生成工作；`PostToUi` + `DrainUiQueue` 回到 UI。
- ComfyUI WebSocket 回调在网络线程触发；状态修改必须投递 UI。
- MCP HTTP handler 默认通过 dispatcher 进入 UI；无 UI 的测试/自检可显式使用当前线程。
- `VideoTaskRunner::Tick()` 和流水线视图由 UI 调用；worker 只生产结果。

## 架构变更判定

新增功能前先回答：

- 这是模型/协议、存储、基础设施、通用 UI 还是具体页面？
- 是否已有唯一实现可复用？
- 新接口的错误、线程和路径契约是什么？
- 如何用离线自检或最小运行场景证明边界没有被破坏？

答案应写入对应模块文档的“关键符号/不变量”，而不是新建阶段计划。
