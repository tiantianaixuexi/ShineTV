---
id: modules.novel
kind: reference
status: current
scope: novel
source_of_truth:
  - src/novel/NovelDb.h
  - src/novel/NovelDb.cpp
  - src/novel/NovelGraph.h
  - src/novel/NovelDirector.h
  - src/novel/NovelRunLoop.h
  - src/novel/NovelPipeline.h
  - src/novel/NovelInit.h
  - src/novel/NovelCommit.h
last_verified: 2026-09-25
---

# 小说模块

## 责任与命名空间

`src/novel` 是小说世界状态和正文生产核心。代码中有三个常用命名空间：

- `shine::novelcore`：数据库、图谱、行类型、初始化、校验、提交和无人值守循环；
- `shine::novel`：生成一章、共用 LLM 回调和 headless CLI；
- `shine::agent`：章节 Director、Agent 定义、工具循环和角色模型路由。

不要因为目录名是 `novel` 就假设所有符号都在同一命名空间。

## 数据库

`NovelDb::Open` 打开一部书的 `novel.db` 并执行迁移；当前实现目标 schema 版本为 `12`（见 `NovelDb.cpp` 的 `kTargetSchemaVersion`）。数据库使用 `db::sqlite::Database`，路径是 `std::filesystem::path`，写入在 worker，UI 只持有句柄和查询结果。

主要表族：

- 世界图谱：`entities`、`relations`、`personas`、`character_statuses`、事件/因果、伏笔、秘密、剧情线、谜团；
- 叙事：`volumes`、`chapters`、`scenes`、`shots`、视觉布局/镜头/提示词层；
- 影视化：`visual_assets`、`visual_states`、`visual_artifacts`、`generated_images`、`prompt_artifacts`；
- Agent/动态字段：`agent_defs`、`field_defs`、审计/Canon 相关表。

DDL、迁移和补列的唯一实现位于 `NovelDb.cpp`；CLI、自检和 MCP 不应手抄另一份 schema。

## 图谱 API

`NovelGraph` 以 `expected<T, DbError>` 提供实体、关系、角色状态、卷/章/场、事件因果、伏笔/秘密、知情、参与、剧情线、谜团、Arc、写作风格、作者规则、持有、地点距离、快照、Canon 和审计操作。

典型规则：

- `ListKnowledge(entityId, chapterId)` 可按章节时点过滤知情状态；
- `CharacterKnows` 是“截至某章是否知道”的规范查询；
- `SnapshotEntity` 写前快照，`CommitChapterState` 负责回写门禁和事务；
- 坏 JSON/SQLite 错误必须转成 `DbError`，不能静默当空结果。

## 初始化

`NovelInit` 提供：

- `RunInitSkeleton`：不调用 LLM，建立结构骨架；
- `CheckInitGate`：检查初始化门禁，失败返回 `InitFailure`（编号、实际差异、修复提示）；
- `InitStageCatalog` / `RunAll...` 相关阶段入口；
- `EnsureProjectSeeds`：幂等补齐字段定义和内置 Agent。

门禁失败不能警告后放行。`auto` 运行必须先满足初始化、评审模型、LLM 可用性和预算等前置条件。

## 章节生成

`NovelDirector::GenerateChapter` 在 worker 中执行，阶段抽象为：

```text
Analyze → Plan → Retrieve → Write → Review → Revision → Extract → Save → Done
```

`LlmRole` 分为 `Planner`、`Writer`、`Critic`、`Extractor`。调用方注入 `LlmCallFn`，因此离线自检可以注入 mock，不依赖真实 Provider。

`NovelPipeline::GenerateOneChapter` 是 UI、CLI 和 MCP 共用入口；`resume=true` 才启用阶段产物哈希复用，默认生成路径是重写语义。返回 `ChapterGenOutcome`，包含正文、修订次数、LLM 调用数、评审结果、状态提交/幂等结果和错误说明。

## 状态回写

`StateDiff` 是正文产生的状态变化载体。`CommitChapterState` 不调用模型，只接受 diff 和上下文，验证后执行事务写入。核心门禁 G1–G5：

- G1：评审通过；
- G2：机器校验通过；
- G3：提交前快照可读；
- G4：diff 合法且非空（或显式声明无变化）；
- G5：没有未解决的高严重度问题。

同一章 + 同一 diff hash 应返回 `skipped`，不得重复应用。

## 自动运行

`NovelRunLoop` 定义 `RunRequest`、`RunLimits`、`RunOutcome`、停止条件、检查点、状态指纹和报告。预算/停止策略是接口契约；真实 LLM 和 ComfyUI 是否满足前置必须联调确认。

## Headless CLI

`NovelCli.h` 定义 `--novel-init`、`--novel-storyboard`、`--novel-generate`、`--novel-run` 及公共参数。CLI 与 UI 共用 `NovelPipeline`，不要为命令行复制生成逻辑。
