---
id: contracts.novel-state
kind: contract
status: current
scope: novel-state
source_of_truth:
  - src/novel/NovelTypes.h
  - src/novel/NovelDb.cpp
  - src/novel/NovelGraph.h
  - src/novel/NovelCommit.h
  - src/novel/NovelChecks.h
  - src/novel/NovelInit.h
last_verified: 2026-09-25
---

# 小说状态契约

## 权威与版本

- SQLite `novel.db` 是世界状态唯一权威。
- `NovelDb.cpp` 的 `kTargetSchemaVersion` 当前为 `12`；打开时建规范表并补旧库缺列。
- `NovelDb::ApplyCanonicalSchema` 和 `EnsureSchemaUpToDate` 是自检/CLI 的统一入口；不要复制 DDL。

## 核心实体

`NovelTypes.h` 定义稳定行类型：

- `EntityRow`：实体类型、名称、摘要、状态和元数据；
- `RelationRow`：实体关系和强度；
- `PersonaRow` / `CharacterStatusRow`：角色设定和按章状态；
- `VolumeRow` / `ChapterRow` / `SceneRow`：卷、章、场；
- `CausalLinkRow`、`ForeshadowRow`、`SecretRow`、`PlotRow`、`MysteryRow`：因果与叙事结构；
- `CharacterKnowledgeRow`：角色在章节时点的知情边界；
- `EntityVersionRow` / `EntitySnapshot`：写前快照；
- `WritingStyleRow` / `AuthorRuleRow`：全书风格和作者硬规则。

实体 `kind` 使用 `NovelTypes.h` 中 `shine::novelcore::kind` 的稳定字符串；新增类别必须同步表、校验和 UI 映射。

## StateDiff

`StateDiff` 是正文到世界状态的唯一回写载体，包含：

```text
entities / characters / relationships / items / locations
 events / causal / plotlines / foreshadows / mysteries
 knowledge / timeline
```

关键字段：

- `contract_version`：状态差异契约版本；
- `producer`：通常为 `extractor`；
- `input_state_hash`：生成差异时输入状态指纹；
- `no_change_declared`：明确声明无变化时，所有子数组必须为空。

`StateDiff::Hash()` 是提交幂等键；`ValidateStateDiff` 检查实体引用、死亡/销毁状态、无变化声明和实体类别等机器规则。

## 提交门禁

`CommitChapterState` 的 G1–G5：

| 门禁 | 含义 |
|---|---|
| G1 | 章节评审通过 |
| G2 | `NovelChecks` 机器校验通过 |
| G3 | 提交前快照已写入 |
| G4 | StateDiff 合法且有变化或明确无变化 |
| G5 | 无未解决 high issue |

任一门禁失败都不能部分提交；事务失败必须回滚。提交成功后写实体版本/章级快照，重复 diff 返回 `skipped`。

## 初始化门禁

`NovelInit::CheckInitGate` 返回 N1–N14 的 `InitFailure`：`n_id`、实际 `detail`、修复 `fix_hint`。门禁失败不能只警告后放行；自动运行必须拒绝启动并展示原因。

## 查询规则

- 章节时点过滤通过 `chapter_id` / `chapter_known` 表达；不要用当前状态覆盖历史时点。
- `GetCharacterSlice` / `GetWorldSlice` 是上下文组装的查询入口；不要在 Prompt 中直接塞整库。
- Canon、PROPOSED 和审计日志是不同概念：`SetCanon` 改状态，`LogAudit` 记行为，快照用于恢复。
