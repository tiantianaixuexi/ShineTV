---
id: refactor.dynamic-fields
kind: design
status: current
source_of_truth:
  - src/novel/NovelFields.h
  - src/novel/NovelFields.cpp
  - src/llm/AgentKit.cpp
last_verified: 2026-11-23
---

> **本页回答**：不同小说风格的动态字段怎么支持——AI 按风格自动生成字段、入库、升格为正式设定的完整机制；KV 存储的可行性结论；一致性与可扩展性怎么保证。

# 动态字段（AI 按小说风格扩展设定维度）

> **一句话现状**：已实现。`src/novel/NovelFields.*` 是「键值注册表 + 值表 + 别名表 + 门禁 + 升格」的完整子系统；
> AI 侧 `upsert_field_def` / `upsert_entity_field` 工具已接通（`src/llm/AgentKit.cpp`），
> 写手 Agent 的系统提示词明确要求「小说体系**不写死**：需要新维度时自己生成并存储」。
> **本文是它的契约说明**（代码注释引用的 `docs/20-contracts/novel-state.md` 不在本仓，本文补此缺口，见 §6）。

## 1. 设计思路：KV 语义建在 SQLite 上，不引第二存储

| 层 | 表 | 角色 |
|---|---|---|
| 键（契约） | `field_defs` | 字段**定义**：`scope`（entity\|world\|chapter\|agent\|custom）、`entity_kind` 约束、`field_key`、`title`、`value_type`（text\|number\|json\|enum）、`enum_json`、`description`、`created_by`（agent id）、`is_system`、`status` |
| 值 | `entity_fields` | 字段**值**：`entity_id` + `field_key` + `value_text/value_json` + `chapter_scope/chapter_to`（分章可见）+ `layer`（global\|public\|mask\|true\|private）+ `created_by`。世界级字段 = `entity_id=0`（`UpsertWorldField`） |
| 消歧 | `field_aliases` | 同义键 → 规范键；**读写双向**都过 `ResolveAlias` |
| 审计 | `audit_logs` / `canon_logs` | 升格与写入记账（`promote_field` 等） |

**KV 可行性评估结论：可行，且不需要外部 KV 库。** 现状已经是 schema-on-read 的 KV（键在注册表、值自由扩展、零 DDL 加维度）。评估过独立 KV（`third/hiredis` 在仓，Redis 可用）后**明确不引入**：

1. **一套真相**：字段值要与世界状态（`StateDiff`/`canon`）同库同事务，拆成 Redis 必然两份漂移（本仓在文档上已吃过「两份真相」的亏）。
2. **免迁移正好对上本仓纪律**：`NovelDb` 幂等 `IF NOT EXISTS`、**不做版本迁移**——KV-on-SQLite 加维度零 DDL，独立 KV 反而要自己管 schema 演化。
3. **校验要 JOIN**：K14/K15 门禁、四条件升格、`entity_kind` 约束都是 SQL 关系校验，纯 KV 做不了。
4. **规模远用不上**：单书 ≤500 定义（硬上限）× 实体 × 值，SQLite 单文件工程携带即全部，无服务依赖。
5. Redis 只配当**缓存**（若将来多进程并发热读），真相永远在 `novel.db`；派生索引（FTS/向量）同样只做缓存、可随时重建。

## 2. 字段生成与入库流程（AI 驱动）

```
风格分析（FieldAgent 读样章/写作意图）
  → list_field_defs 查重（含别名表）
  → upsert_field_def(scope/entity_kind/field_key/title/value_type/enum_json/description)
      键归一化 NormalizeKey（trim→lower→下划线）+ IsValidKey ^[a-z][a-z0-9_]{1,39}$
      → 落 status=PROPOSED（AI 提案）；系统种子 is_system=1 → CANON
  → 写值 upsert_entity_field（按 value_type/enum_json 校值；layer 校验 kLayerEnum）
      未登记键写值 → 不硬拒：自动补一条 PROPOSED 提案（"field_unregistered"，NovelFields.cpp:522）
  → 章节检查点：EvaluateFieldPromotion 四条件（全满足才升）
      ① 已出现 ≥5 次  ② 值类型始终一致  ③ 无同义键冲突（别名表）  ④ 最近 3 章内仍在使用
      → PromoteProposedFields：升 CANON + audit_logs(action='promote_field')
      未通过 → 保持 PROPOSED，进人工复核清单
  → 观测熔断：NovelRunLoop 观测 field_def_count，>500（kMaxFieldDefs）→ 停止码 S10
      （输出新增失败清单 / 人工清理 / 停用自动扩展，NovelRunLoop.cpp:243）
```

- AI 入口（`src/llm/AgentKit.cpp`）：内置工具 `list_field_defs` / `upsert_field_def` / `list_entity_fields` / `upsert_entity_field`；各 Agent 按 `tools_json` 白名单取用（约 10 个内置角色都带字段工具）。
- MCP 入口（`src/novel/NovelMcpTools.cpp`）：`novel_list_field_defs` / `novel_upsert_field_def`（写工具有总开关**默认关**，`SHINE_MCP_ALLOW_WRITE`）；外部 AI（Claude Desktop 等）同一套。
- 上下文出口：`EntityFieldsJson` 供 MCP/Context 切片；写手 Agent 提示词要求**写作前先** `list_entity_fields/list_field_defs` 理解当前设定（含动态字段与本章宇宙）。

## 3. 一致性怎么保证

1. **DDL 单一来源**：`NovelFields::EnsureSchema` 是 field_defs / entity_fields / field_aliases 的唯一建表入口（`NovelDb::Migrate`、`AgentKit::EnsureSchemaAndSeed`、自检共用）——S2b 踩过「三处各写 DDL、漏一处静默丢种子」，已收口；幂等可重复调。
2. **状态机 + 审计**：`PROPOSED → CANON`（AI 提案永远先进 PROPOSED）；升格与写入落 `audit_logs`/`canon_logs`；`is_system=1` 的系统最小集 Agent **可读不可删**。
3. **门禁进判据体系**：NovelChecks K14 `field.registered`（不变式 I12：值必须在 field_defs 登记）/ K15 `field.type_valid`（按 value_type/enum_json 校值）——不是靠约定，是检查点会红的判据。
4. **键与层的强制归一**：`NormalizeKey` + `IsValidKey` + `IsValidLayer`（global|public|mask|true|private）+ `kValueTypeEnum`；同义键经 `field_aliases` 合并，读写都解析别名，防「同一维度三种拼法」。
5. **升格四条件防泛滥**：用不满 5 次 / 类型不稳 / 有同义冲突 / 近 3 章不再用 → 不进 CANON，走人工复核；500 键硬上限 + RunLoop 熔断兜底。
6. **知情过滤同源**：值的 `layer`（mask/true/private）与 secrets/`secret_knowledge` 的 POV 过滤同族，ContextBuilder 的 L4 知情过滤按同一套层语义走。

## 4. 可扩展性怎么保证

- **加维度零 DDL**：新风格（无限流/克苏鲁/宫斗…）= 新增 field_defs 行 + entity_fields 值，不动表结构、不改代码、不迁移。
- **scope 五类**：entity（实体专属，可按 entity_kind 约束）/ world / chapter / agent / custom——风格差异可挂不同作用域。
- **类型受控的自由**：text|number|json|enum 四型 + enum_json 可选值；「自由」不等于「随便」，K15 会校。
- **新字段即刻可用**：`EntityFieldsJson`/MCP 工具面按注册表动态出，UI 与上下文切片无需感知具体键名。
- **演化路径**：值表按 entity_id 检索（现成索引）；量大后加 FTS5/向量**派生**索引（可重建，不改真相层）；外部缓存同理。

## 5. 已知边界（用之前要知道）

- ⚠️ **上下文预算**：动态字段进生成上下文走 `EntityFieldsJson`/L3/L4 切片，受 ContextBuilder 12000 字节预算约束（超限显式截断）；字段定义本身（≤500 条）不要整表塞 prompt。
- ⚠️ **chapter/agent scope 的值挂载**：`FieldDefRow.scope` 有 chapter|agent，但 `entity_fields` 以 `entity_id` 为轴（world=0）；chapter/agent 维度的值挂法在实现里以约定承载（`chapter_scope` 视窗 + created_by），跨 scope 查询前先 `ListFieldDefs(scope)` 确认。
- ⚠️ **语义检索缺口**：字段值无全文/向量索引，"按描述找设定"仍是结构化查询或 LIKE；量大后按 §4 派生索引补。

## 6. 文档缺口（本文的由来）

- 代码注释多处引用契约 `docs/20-contracts/novel-state.md`（字段门禁规则）与编号规格 `02`/`08` §2.3/§2.4/§2.6/`09`/`10`，但**这些文档不在本仓**（`docs/` 不存在）。
- 处置：动态字段的契约说明以**本文**为准（依据源码逐条核写，见 `source_of_truth`）；引用编号规格处以代码注释 + 本文互证。若日后找回原契约文档，放 `docs/` 详版层并在此登记。

## 7. 证据索引（要改机制时从这里进）

| 事实 | 落点 |
|---|---|
| 字段定义/值/别名的全部门禁与升格 | `src/novel/NovelFields.h`（契约注释）+ `NovelFields.cpp` |
| DDL 单一来源 / 旧库 ALTER 回填 | `NovelFields::EnsureSchema`（`NovelFields.cpp:359` 附近）+ `NovelDb.cpp:936` |
| AI 工具与提示词（"不写死小说体系"） | `src/llm/AgentKit.cpp:902,1304,1333,1356` |
| MCP 工具 + 写开关默认关 | `src/novel/NovelMcpTools.cpp:492,1227,1396` |
| K14/K15 校验 | `src/novel/NovelChecks.cpp:58,935-959` |
| 500 上限熔断 S10 | `src/novel/NovelRunLoop.cpp:243` |
| 系统种子（≥11 条 + 15 内置 Agent） | `src/novel/NovelInit.cpp:220`（门禁 N12/N13） |
