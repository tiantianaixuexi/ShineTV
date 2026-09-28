---
id: docs.index
kind: index
status: current
scope: documentation
source_of_truth:
  - README.md
  - AGENTS.md
  - CMakeLists.txt
  - src/
last_verified: 2026-09-25
---

# ShineTV Studio 文档索引

本目录是当前项目参考文档，不保存开发计划、阶段进度或交接任务。文档按“问题域”而不是按历史阶段组织；每份文档只保留一个主要主题。

## 分类

### 00 总览

- [`00-overview/architecture.md`](00-overview/architecture.md)：当前分层、依赖方向、命名空间和运行边界。
- [`00-overview/runtime.md`](00-overview/runtime.md)：应用启动、退出、命令行分派和线程模型。
- [`00-overview/product.md`](00-overview/product.md)：产品工作流、当前范围和明确边界。

### 10 模块

- [`10-modules/project.md`](10-modules/project.md)：项目索引、`project.json`、模板和多书目录。
- [`10-modules/data-storage.md`](10-modules/data-storage.md)：SQLite、Redis、连接池和 schema 入口。
- [`10-modules/novel.md`](10-modules/novel.md)：小说 SQLite、图谱、初始化、章节生成和状态提交。
- [`10-modules/visual-storyboard.md`](10-modules/visual-storyboard.md)：视觉资产、参考库、叙事分镜和视频任务。
- [`10-modules/flow-comfy.md`](10-modules/flow-comfy.md)：流程图模型、工作流格式、ComfyUI 协议和出图提交。
- [`10-modules/pipeline.md`](10-modules/pipeline.md)：阶段机、预算、检查点、账本和总控台。
- [`10-modules/media-paint.md`](10-modules/media-paint.md)：媒体库、图片解码/缓存、视频缩略图和 inpaint。
- [`10-modules/llm-mcp.md`](10-modules/llm-mcp.md)：LLM Provider、AgentKit 和 MCP 传输/工具。
- [`10-modules/ui-kit.md`](10-modules/ui-kit.md)：主题 Token、控件、FlowCanvas 和 Qt 工作区。

### 20 契约

- [`20-contracts/project-layout.md`](20-contracts/project-layout.md)：项目目录和文件约定。
- [`20-contracts/novel-state.md`](20-contracts/novel-state.md)：世界状态、StateDiff、门禁和提交不变量。
- [`20-contracts/visual-generation.md`](20-contracts/visual-generation.md)：分镜尺寸、帧数、参考图和降级账。
- [`20-contracts/protocols.md`](20-contracts/protocols.md)：ComfyUI、MCP、LLM 边界格式。

### 30 工程

- [`30-engineering/build.md`](30-engineering/build.md)：工具链、CMake 目标和构建/打包命令。
- [`30-engineering/coding-rules.md`](30-engineering/coding-rules.md)：C++、线程、错误、路径和 UI 编码规则。
- [`30-engineering/checks.md`](30-engineering/checks.md)：PowerShell 门禁、自检、截图和稳定性脚本。

### 40 运行

- [`40-operations/environment.md`](40-operations/environment.md)：环境变量、命令行模式和 AppData 路径。
- [`40-operations/verification.md`](40-operations/verification.md)：验证层级、证据记录和报告格式。
- [`40-operations/troubleshooting.md`](40-operations/troubleshooting.md)：按症状定位 ComfyUI、LLM、构建、中文路径和 UI 问题。

### 90 参考

- [`90-reference/source-map.md`](90-reference/source-map.md)：从任务到目录、头文件和关键符号的索引。
- [`90-reference/glossary.md`](90-reference/glossary.md)：项目术语、状态词和协议缩写。

## 按任务阅读

| 任务 | 必读顺序 |
|---|---|
| 改数据库/缓存 | `data-storage.md` → `project-layout.md` → `coding-rules.md` |
| 改项目创建/打开 | `project.md` → `project-layout.md` → `runtime.md` |
| 改小说数据或生成 | `novel.md` → `novel-state.md` → `llm-mcp.md` → `pipeline.md` |
| 改分镜/出图/出片 | `novel.md` → `visual-storyboard.md` → `flow-comfy.md` → `visual-generation.md` |
| 改 ComfyUI/MCP/LLM 协议 | `flow-comfy.md` → `llm-mcp.md` → `protocols.md` |
| 改 Qt 页面/主题/控件 | `ui-kit.md` → `coding-rules.md` → `architecture.md` |
| 改构建或检查脚本 | `build.md` → `checks.md` → `environment.md` |
| 定位陌生代码 | `source-map.md` → 对应模块文档 → 头文件/实现 |

## 事实标记

文档中的内容按以下顺序解释：

1. **源码事实**：由当前 `CMakeLists.txt`、头文件或实现直接证明。
2. **项目约定**：团队/AI 修改时必须遵守的边界，不代表第三方库行为。
3. **外部前提**：依赖 ComfyUI、LLM、Windows、Qt 或网络的协议/环境，需要联调确认。
4. **待验证**：设计目标或自检目标；只有实际运行并记录输出后才可称为通过。

## 元数据约定

```yaml
---
id: stable.dot.separated.id
kind: reference | contract | operations | index | instruction
status: current
scope: bounded-topic
source_of_truth:
  - src/
last_verified: YYYY-MM-DD
---
```

AI 读取文档时，先检查 `source_of_truth` 和 `last_verified`；若与当前源码冲突，以源码为准并更新文档。
