---
id: reference.source-map
kind: index
status: current
scope: source-navigation
source_of_truth:
  - src/
  - CMakeLists.txt
  - tools/
  - scripts/
last_verified: 2026-09-25
---

# 源码索引

## 按问题定位

| 问题 | 先看 | 再看 |
|---|---|---|
| 程序如何启动/退出 | `src/ui/app/main.cpp` | `src/ui/app/AppEntry.cpp`, `StartupChecks.cpp` |
| 项目如何创建/打开 | `src/project/Project.h` | `Project.cpp`, `ProjectTemplate.cpp`, `ProjectIndex.h` |
| 数据库/Redis | `src/db/Db.h` | `src/db/sqlite/SqliteDb.h`, `src/db/redis/Redis.h` |
| 小说库和 schema | `src/novel/NovelDb.h` | `NovelDb.cpp`, `NovelTypes.h` |
| 查询世界状态 | `src/novel/NovelGraph.h` | `ContextBuilder.h`, `NovelChecks.h` |
| 初始化门禁 | `src/novel/NovelInit.h` | `NovelInit.cpp` |
| 生成一章 | `src/novel/NovelPipeline.h` | `NovelDirector.h`, `NovelRunLoop.h` |
| StateDiff/提交 | `src/novel/NovelCommit.h` | `NovelChecks.h`, `NovelDb.cpp` |
| 视觉阶段 | `src/novel/NovelVisualStages.h` | `NovelStoryboard.h`, `NovelContinuity.h` |
| 资产派生 | `src/novel/NovelAssetPipeline.h` | `src/visual/ReferenceLibrary.h` |
| 分镜/视频模型 | `src/visual/VideoTypes.h` | `VideoProject.h`, `VideoTaskRunner.h` |
| 图保存/导入 | `src/flow/GraphHost.h` | `WorkflowIO.h`, `GraphCompiler.h` |
| 节点定义 | `src/comfy/ComfyNodeDef.h` | `ComfySession.h`, `ComfySocket.h` |
| Comfy HTTP/WS | `src/comfy/ComfyClient.h` | `ComfyHttp.h`, `ComfySocket.h` |
| 出图/出片工作区 | `src/ui/pages/imageflow/`, `src/ui/pages/videoflow/` | `src/visual/`, `src/flow/` |
| 流水线 | `src/pipeline/StageMachine.h` | `Runner.h`, `Budget.h`, `Ledger.h` |
| LLM Provider | `src/llm/OpenAIClient.h` | `OpenAIProvider.h`, `OpenAIChat.cpp` |
| Agent 工具 | `src/llm/AgentKit.h` | `ToolRegistry.h`, `src/novel/NovelMcpTools.h` |
| MCP HTTP/stdio | `src/mcp/HttpServer.h` | `MCPServer.h`, `McpBootstrap.h` |
| 图库/媒体 | `src/media/MediaLibrary.h` | `Gallery.h`, `ThumbnailService.h` |
| 图片解码 | `src/media/decoders/IImageDecoder.h` | `PngDecoder.*`, `JpegDecoder.*`, `WebpDecoder.*` |
| inpaint | `src/paint/PaintCanvas.h` | `PaintService.h`, `PngCodec.h` |
| 主题/QSS | `src/ui/kit/theme/Token.h` | `Theme.cpp`, `QssBuilder.cpp`, `ThemeService.cpp` |
| 通用控件 | `src/ui/kit/controls/`, `src/ui/kit/data/`, `src/ui/kit/images/` | `WidgetCommon.h` |\n| Qt 布局助手 | `src/ui/layout/QtLayout.h` | `widget/theme/CssColor.h` |
| 流程画布 | `src/ui/kit/canvas/FlowCanvas.h` | `src/ui/pages/imageflow/`, `src/ui/pages/videoflow/` |
| 设置/路径 | `src/core/Settings.h` | `Settings.cpp`, `src/util/Encoding.h` |
| 异步/日志 | `src/core/Async.h` | `Async.cpp`, `Log.h`, `Log.cpp` |
| 构建目标 | `CMakeLists.txt` | `docs/30-engineering/build.md` |
| 静态门禁 | `tools/check-layers.ps1` | `check-theme.ps1`, `check-colors.ps1` |
| 进程/打包 | `scripts/studio.ps1` | `scripts/package-qt.ps1` |

## 目录阅读规则

- 先读头文件，再读对应 `.cpp`；不要从文件名猜接口。
- 看到 `Pxx/Sxx/Vxx/Txx` 等标识时，把它当作历史验收上下文，不要在当前文档中重建开发计划。
- `src/visual` 的主要命名空间是 `shine::video`；`src/novel` 同时有 `novelcore`、`novel`、`agent`。
- `src/ui/app` 的验收类不是产品 API；只有 `MainWindow` 暴露的自动化探针在需要时使用。
- `third/` 是 vendored 依赖；除非任务明确要求，不要修改它。

## AI 技能入口

`.mimocode/skills/` 保留当前可执行技能：`shinetv-structure`、`shinetv-build`、`shinetv-comfy`、`shinetv-db`、`shinetv-garnet`、`shinetv-graph`、`shinetv-thirdparty`、`shinetv-ui-layout`。每个技能只指向当前源码/文档；修改模块边界时同步对应 `SKILL.md`。历史会话记忆不作为项目事实来源。

## 变更影响提示

| 改动 | 至少检查 |
|---|---|
| `ProjectFile` 字段 | 序列化/迁移、项目 UI、模板、引用文档 |
| `Shot`/`VideoProject` 字段 | 反射存盘、编译器、校验、UI 表格 |
| `StateDiff` 字段 | JSON 解析、提交事务、校验、上下文/报告 |
| 阶段枚举 | 阶段机、Runner、检查和 UI Gantt |
| Comfy 事件/字段 | Socket、Session、Client、错误 UI、协议测试 |
| 主题 token | QSS、控件、颜色门禁、主题 JSON |
| 新 `.cpp` | `CMakeLists.txt` 对应 target |
| 文档路径 | 根 README、索引、脚本和源码注释中的引用 |
