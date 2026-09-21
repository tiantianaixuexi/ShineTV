# 新会话交接 · 小说 Extractor「语义引用」重构（LLM 管语义 / SQLite 管 ID）

> 起因：用户指出 —— **"如果 SQLite 才是权威数据源，提示词里没必要把 ID 规则写得这么死"**。
> 目标：让**模型只负责语义**（谁 / 在哪 / 什么变化 / 新出现什么 / 关系建立结束），
> **`*_id` 的解析、kind 匹配、存在性、外键全部由下游 SQLite 层做**。
> 副产品：**K03 从"模型的负担"变成"解析器的自检"**；extractor 提示词可砍 **40–60%**。

---

## 0. 必读（按序，别重开设计讨论）

1. `e:\c++\ShineTV\.codebuddy\memory\MEMORY.md` —— 长期记忆（**尤其 "架构规律" 那两条**）
2. `.codebuddy/memory/2026-09-20.md`、`.codebuddy/memory/2026-09-21.md` —— S74–S84 全过程（含全部实测证据）
3. 本文 §2 的"关键源码位置"（新会话最需要的就是这张表）

## 1. 当前状态（2026-09-21 收工时）

- 分支 `s62-extractor-agent-tools`；**1–20 章全部 `done`**（18 章）。
- ⚠️ **ch16 挂在 `review` 状态** —— 它是"跨章污染 bug"（`chapter_id` 采信模型）的受害者，**需要重跑一次**。
- ⚠️ **一大批改动尚未提交**（`git status`）：`CMakeLists/App/NovelCli/NovelPipeline/NovelChecks/NovelCommit(.h)/NovelGraph/NovelMcpTools/NovelDirector/AgentKit/ToolRegistry(.h)` + **新增 `NovelRepair.{h,cpp}`**。
- 自检 **26 项全 ok**；`prompt_rule_version=18`。
- 已建立的机制（**别拆掉**）：Anthropic 协议工具循环（`MakeLlmCreateRawAnthropic`）、契约规则 **D9/D10/D11**、
  重做回灌 `commit.error`、`list_id_directory` 工具、锁库工具循环**预置默认关**（`SHINE_TOOL_PREFETCH=1` 才开）、
  库级一致性扫描/修复 `NovelRepair`（R1–R9 + `--novel-repair`、MCP 的 `novel_consistency_report/repair`）。

## 2. 关键源码位置（照这张表改，别到处找）

| 关注点 | 位置 |
|---|---|
| extractor 系统提示词（**唯一来源**） | `src/agent/NovelDirector.cpp` `ExtractSystemPrompt()` |
| 提示词组装（指令 = 本 agent 提示词 + 结尾"可用工具"行） | `src/agent/AgentKit.cpp` `BuildSystemPrompt()` |
| Extractor 白名单（tools_json）+ outHint | `src/agent/AgentKit.cpp` `BuiltInAgents()` 的 `extract` 条目 |
| 工具注册（白名单挂钩点 + `list_id_directory`） | `src/agent/AgentKit.cpp` `RegisterToolsFor()` / `AgentKit::Run()`（**在 Run 里按白名单 `Unregister`**） |
| StateDiff 解析（含字符串引用回填） | `src/novel/NovelCommit.cpp` `StateDiffFromJson()` / `FillStringRefs()` / `IsTempIdLike()` |
| 契约校验（D 规则） | `src/novel/NovelCommit.cpp` `ValidateStateDiff()`（**D9/D10/D11 都在这**） |
| 引用解析（temp_id → 真 id） | `src/novel/NovelCommit.cpp` `ResolveRef()` / `CheckRef()`（14 块事务里逐块调用） |
| 库级一致性规则 | `src/novel/NovelRepair.{h,cpp}`（R1–R9；**新规则加在这**） |
| 提示词/规则版本（改了给 LLM 看的东西就要抬） | `src/novel/NovelChecks.cpp` 的 `prompt_rule_version` |
| 原始请求落盘（排查用） | `SHINE_DUMP_LLM_REQ=<目录>` → `extract_req_NN_{instructions.txt,tools.json,messages.json}` |

## 3. 铁律（这几条是 S74–S84 血泪换的，务必守住）

1. **"落库时的守卫"必须有一条对应的契约规则**（否则"为什么被拒"到不了模型眼前，重做就是瞎改）。
2. **歧义绝不静默猜**：解析多个候选时必须**拒**并列出候选 id。
3. **别替模型伪造它的动作**（`SHINE_TOOL_PREFETCH` 那次：伪造 `assistant(function_call)` ⇒ 模型再也不调工具）。
4. **提示词里只允许出现"模型当场能用到的东西"**（形状/字段清单/工具/规则）；
   文档编号（`02 §2.5`）、事故史（"真跑实证…"）、悬空指涉（"下方清单"）一律留在**代码注释**里。
5. **契约一收紧，自检 fixture 是第一波受害者**（D9 上线时 4 处 fixture 全中 —— 记得一起改）。
6. **`event` 与"持续实体"要分开对待**（事件是"一次性实例"，名字还是代码生成的 `"ch{} 事件"`）。

## 3.5 核心决策（用户 2026-09-21 修正 —— **这一节优先于本文其它描述**）

### `entity_names` 是「名字 → 实体候选」的**解析层**，**不是实体身份**

❌ **不要**把 `UNIQUE(kind, name_norm)` 当成实体唯一性的最终规则 —— 那会让"数据库替你决定两个李默是不是同一个人"，
以后真要两个同名实体时**得跟约束打架**。⇒ 表里**只建索引，不建 UNIQUE**：

```sql
entity_names(
  entity_id, kind, name, name_norm,   -- name_norm：去空白 / 全角→半角 / 小写
  is_primary, created_chapter, alias_of
)
-- 索引：INDEX(kind, name_norm)；**不要 UNIQUE**
```

resolver 查到**多个候选**时返回**候选列表**（`李默 → #43 / #108`），由**上下文/规则/人**决定，而不是数据库替你决定。

### 四种情况的处理（这是"候选去重策略"，不是"数据库语义约束"）

| 情况 | 处理 |
|---|---|
| **同一实体重复声明**（如 `撑伞人影` #43/#108 确为同一角色） | **复用**已有 entity（`reuse_entity`） |
| **不确定是不是同一个** | **绝不自动合并** ⇒ 进"候选 / 待确认"（R10 报告） |
| **明确是两个同名实体**（两个"李默"） | **新建** entity，用 `alias` / `display_name` 区分 |
| **事件** | **不用名字去重** —— 用事件自己的唯一键/来源定位（`DedupByPersistentName()` 已排除 `event`） |

⚠️ 由此推出：**S84 的"同名 ⇒ 无条件复用"必须补一个"显式新建"通道**
（否则模型想建"第二个李默"会被复用掉）。候选新建的表达方式在 T3 定。

### 保留 `entity_ref` 与 `temp_id` 的**语义区别**（这个边界值得留）

```jsonc
{"entity_ref": "撑伞人影", "kind": "person"}   // = 我认为这是**库里已存在的**哪个实体
{"temp_id":    "谭工",     "kind": "person"}   // = 我在**本章新建**的实体
```
⇒ StateDiff 因此能**自解释**（"引用的" vs "新建的"一眼可分），resolver 的语义也干净。
`en:` / `ev:` 前缀**去掉**（引入 `entity_ref` 后它没有任何价值）。

### 优先级（用户修正后的顺序）

> **① entity_ref + SQLite resolver**（架构收益最大：把 LLM 从 ID 管理里解放出来）
> **② R10 历史重复诊断**（摸清脏数据，**不急着自动合并**）
> **③ entity_names / alias**（做好扩展点，**现在别复杂化** —— 已知库里还没有合法同名实例）
> **④ 事件唯一化**（数据清洁，**不阻挡前三件事**）

## 4. 计划表（一次做一个 T，做完就 build + 自检 + 记录）

> ⚠️ **执行顺序按 §3.5 的优先级**（T1 → T3 → T2 → T4/T5 → T6；T2 事件唯一化排最后），
> 并且 T1 起就必须按 §3.5 的三条修正来实现（**不加 UNIQUE / 多候选返回列表 / 保留 `entity_ref`↔`temp_id` 边界**）。

### T0 收尾（先做）
- 提交现有改动（S67b–S84）；**重跑 ch16** 使其 `done`。
- 判据：`chapters` 1–20 全 `done`；`git status` 只剩 `runtime/`（测试产物，不入库）。

### T1 语义引用解析器（**核心**）
- `ResolveRef` / `FillStringRefs` 扩展：**`*_ref` 通道（`entity_ref`/`location_ref`/…）**接受
  **① 本章 temp_id ② 实体名字 ③ 数字 id（兼容）**；**`temp_id` 与 `entity_ref` 语义不同**（§3.5）。
  名字匹配 = **同 kind + 归一化名字**（去空白、全角→半角、小写）。
  ⚠️ 过渡期可先复用现有 `*_temp_id` 字段承载"名字"，**T4 再正式引入 `entity_ref`**（避免一次改两处）。
- 五分支决策树（用户拍板，见 §3.5）：数字 id ⇒ 用；本章 `temp_id` ⇒ 用；纯数字字符串 ⇒ 当 id；
  名字 ⇒ **唯一命中复用 / 多候选拒 + 列表 / 零候选拒（"要新建请进 entities[]"）**；**同名但 kind 不符 ⇒ 拒**。
- 自检断言（加在 `RunCommitSelfCheck`）：唯一命中复用 / 重名拒 + 候选 / 未命中拒 / kind 不符拒。
- 判据：自检 0 fail；**不动提示词**也能跑通一章（兼容验证）。

#### ✅ T1 已完成（2026-09-21，尚未提交）
- **代码**（全在 `src/novel/NovelCommit.cpp`）：
  - `NormalizeRefName`（去空白 + 全角空格 + ASCII 小写）、`EntityNameIndex`（**库内名字索引：只建 map、无 UNIQUE、排除 `event`**）、
    `RefCtx` / `ResolveRefFull`（**五分支决策树**）/ `RefFail`（失败报文，歧义时带候选列表）；
  - 门禁 `CheckRef` 改为**也认名字**（`RefGate`：本章 temp_id / 本章新建名字 / 库内名字），
    并在 `ValidateStateDiff` 为每列传**期望 kind**（`characters[].entity_id`=person、`items[].owner_id`=person、
    `items[].item_id`=item、`location`=location、关系端点/参与者=不限）；
  - `FillStringRefs`：从"只认 `前缀:数字`"改为**原样收下名字**（`*_id` 里写字符串、或新增 `*_ref` 键，都收）。
- **堵了一个后门**：`characters[]` 的 reason / D4 检查原先遇 `entity_id<=0` 直接 `continue` ⇒ "用名字"会绕过它；
  现在**先把名字解析成真实 id 再检查**。
- **顺手修**：`items[].op="lose"` 的持有者解析不到时，原先 `owner_id=0` 的 UPDATE 匹配不到任何行 ⇒
  **静默无操作**（比报错更坏）；改为**拒提交**。
- **自检**：`commit:ok`、**0 fail**；新增 ⑧f 四条断言全绿，报文正是 §3.5 的口径，例如
  `「S65新人物」**有歧义** ⇒ #8（第1章 person「S65新人物」） / #11（第1章 person「S65新人物」）。…系统不按"最近出现"替你猜`。
- **兼容判据**：**不动提示词**、在真实库 `rain-signal-clean` 上跑 **第 16 章 ⇒ 提交成功**
  （`RepairLlmJson=0 / 归一化 0 / TLS 0`）⇒ 模型仍走 id/temp_id 老路 **零回归**（`chapters` 1–20 现全 `done`）。
- ⚠️ **遗留（按计划留给 T3/T4）**：**显式新建通道**（`entities[]` 中"即使同名也新建"）与 `entity_names` 表未做；
  `*_ref` 键已能收，但**提示词还没教模型用名字**（T4/T5 一起做，`prompt_rule_version` 抬到 19）。

### T2 事件名唯一化 + 历史脏名
- 事件 name 由 `"ch{} 事件"` 改为**含序号唯一**（如 `ch18 事件#3`）。
- 历史 `ch1 事件`×11 等：**写成 `NovelRepair` 的一条规则（R11）报告 + 给改名 SQL**，不手改库。
- 判据：`NovelRepair` 扫描 R11 归零。

### T3 R10：同名同 kind 重复实体（**报告 + 合并方案，不自动并**）
- 现状实测：**7 组**（`撑伞人影` #43/#108、`旧信号塔·塔顶灯室` ×3、`备用信号机` ×2 …）。
- 报告含：两边 id/created_chapter/summary + 各挂多少关系/出场（用于人工判断"是不是同一个人"）。
- ⚠️ **不加后缀、不自动合并** —— 加后缀会把"同一个人的重复声明"变成两个名字（更割裂）；
  自动并会误伤"真·同名的两个人"。
- **同时建 `entity_names`（§3.5 的表：无 UNIQUE、只建索引 + `alias_of`）**，并给 S84 的"同名复用"补上
  **显式新建通道**（模型要建"第二个李默"时不被复用掉）。
- 判据：7 组都能列出来；合并 SQL 交用户确认后执行。

#### ✅ T3 已完成（2026-09-21，尚未提交）—— 用户 6 条验收标准**全过**

**T3a（R10 只读诊断）**
- `NovelRepair` 新增 **R10 `entity_duplicate_name`**（`severity=warn`、`fixable=false`）：**只查同 kind** +
  **同 `name_norm`**；报告每份的 `id / created_chapter / 关系数 / 出场章数 / 知情数 / summary`，
  `suggest` 只给"**不自动 merge**、请人工确认后自行执行 merge SQL + 候选 id 列表"。
- 🔑 **归一化口径唯一**：新增 `src/novel/NovelNames.h`（`NormalizeEntityName` / `TrimEntityRef`），
  resolver（`NovelCommit`）、R10（`NovelRepair`）、`entity_names.name_norm`（`NovelDb`/`NovelGraph`）
  **全部调它** —— 不再"SQL 一套、C++ 一套"。
- 自检新增 6 条断言（同 kind 命中、**带空格也归一化命中**、**同名的 location 不进组**、`fixable=false`、
  `--apply` 一条不修、两行都还在）⇒ 全 PASS。
- 真库实测：R10 报出 **3 组**（`item 备用信号机 #52/#159`、`location 旧信号塔·塔顶灯室 #59/#76/#134`、
  `person 撑伞人影 #43/#108`，证据含"关系 8/出场 8 vs 关系 2/出场 2"）；`--apply` 后实体总数 **199 不变、
  7 行逐字未动**（验收 1–3 ✅）。

**T3b（`entity_names` + `force_new`）**
- 新表 `entity_names`（v13，`ApplyCanonicalSchema` 里建）：`(entity_id, kind, name, name_norm, is_primary,
  created_chapter, alias_of)`，**只建索引、故意不建 UNIQUE**；`alias_of reserved; semantics not implemented`。
- `EnsureEntityNames()` **回填**（幂等，C++ 侧算 `name_norm`）：真库首次补 **199 行**；
  `UpsertEntity` 写入时维护主名行 ⇒ 自检断言 **`is_primary=1` 行数 == `entities` 行数**（验收 4 ✅）。
- `UpsertEntity` 的复用判定改为**归一化名**比对（带"精确名"兜底）⇒ `旧信号塔` 与 `旧信号塔␠` 不再建两份。
- **`force_new`**：`EntityRow` / `NewEntityDelta` 各加一个字段（C++26 静态反射自动成为 JSON 键
  `force_new`）；`entities[]` 带它 ⇒ **明知同名也新建**，并记
  `audit_logs(action='force_new_entity', detail='… new_id=N conflict_ids=[…]')`（验收 5 ✅）。
- 顺带堵洞：`RefCtx.chapterNames` 由 `name → id` 改为 `name → id **列表**` ⇒ 同一章声明两个同名实体
  再按名字引用时**拒**（不悄悄取最后一个）。
- 自检断言：归一化同名复用 / `force_new` 新建出第二个同名人 / 镜像一致 / audit 存在 /
  **`force_new` 能从 JSON 反射进来**（模型的真实入口）⇒ 全 PASS（验收 5 ✅）。
- 验收 6（歧义必须拒绝、不自动猜）由 T1 的 ⑧f(b) 覆盖 ✅。

### T4 契约正式化：`*_ref`（名字引用）写进 `02` + 补 D 规则 + 断言
> **边界（2026-09-21 校准）**：T4 **只改"契约文档 + 校验代码 + 断言"**，**不碰提示词、不抬版本号**。
> 理由：`prompt_rule_version`（现 = **18**，在 `NovelChecks.cpp` 的 spec 种子里）是给**运行时提示词**用的；
> 文档 `Doc/小说系统/02-数据契约.md` **不被运行时读取** ⇒ 只改文档不必抬版，否则"版本变了、提示词没变"
> 等于白漂一次哈希（K23 那类事故）。**抬到 19 与提示词改动同批做（T5）**。

**已确认的现状（决定 T4 其实很轻）**：
- 代码 **已经能收** `*_ref`（`FillStringRefs` 的 `FirstRefStr` 会认 `entity_ref` / `location_ref` / `from_ref` /
  `to_ref` / `item_ref` / `owner_ref`），**只是契约文档里没写**；
- `temp_id` **不要求前缀**（`CheckRef`/`DeclaredTempIds` 只要求非空；resolver 按**字符串精确匹配** `tempIds`）；
  被禁的是 **D9**：`LooksLikeNumericTempId()` = "前缀 + 纯数字"（`en:1` / `ev:7`）——**这条 T4 保留**
  （去前缀 ≠ 允许歧义形状：`en:7` 长得像 id 7，是"把序号当 id 填"的源头）；
- `NormalizeNumericTempRefs()`（S65b）只对**含 `前缀:序号` 的** temp_id 建序号索引 ⇒ 无语义前缀的 temp_id
  天然不参与那种归一化（无需改，但要在文档里说明"为什么不再需要它"）。

**改动清单**：
| 文件 | 改什么 |
|---|---|
| `Doc/小说系统/02-数据契约.md` §2.5 | 引用字段的**合法写法**正式写成三种：`*_id`（数字，库内 id）/ `*_ref`（**名字**，同 kind 唯一命中才解析）/ `*_temp_id`（本章标签）。**行为规则**（唯一命中⇒复用；多候选⇒拒+候选列表；零候选⇒拒并提示"新建请进 `entities[]`"；同 kind 不匹配⇒拒）；`temp_id` **不要求前缀**，但**禁止** `前缀+纯数字`（D9）；`entities[].force_new` 的语义与审计 |
| `src/novel/NovelCommit.cpp` | 新增 **D12**（自相矛盾检测，见下）+ 自检断言：`*_ref` 三路（唯一/歧义/未命中）+ `*_id` 与 `*_ref` 冲突 + 旧写法 `en:x` 兼容回归 |
| `Doc/小说系统/06-评审与校验.md` | 把 **D12** 与 T3a 的 **R10**（库级）登记进去（章级 `06` vs 库级一致性扫描的分工写清） |

**新增 D12（用户 2026-09-21 精确化：禁止语义冲突，**不是**禁止冗余表达）**：
同一引用字段同时给了 `*_id` 与 `*_ref`（或 `*_temp_id`）时，逐情形判定：

| # | 情形 | 处理 |
|---|---|---|
| 1 | `*_id` 无效（不存在 / kind 不符） | 按既有字段校验规则 **拒** |
| 2 | `*_ref` 无匹配 / 多候选 | 按 `*_ref` 的**未命中 / 歧义**规则 **拒** |
| 3 | 两者解析到**同一实体** | **允许提交**（冗余但一致），canonical id = `*_id` |
| 4 | 两者解析到**不同实体** | **D12 拒绝** |

> ⚠️ 情形 3 必须放行 —— 否则模型"为了兼容旧输出而两个都写"会被误杀（那只是**冗余**，不是**冲突**）。
```
D12：characters[0] 同时给了 entity_id=43 与 entity_ref=「撑伞人影」（#108）—— 两个说法指向**不同**实体，
     请只留一个（模型不该自相矛盾；静默取一个会让它学不到）
```

### ★ 三条必须钉死的语义（用户 2026-09-21 追加，开工前补齐）

**① `force_new` 与「同 kind 名字唯一」的关系 —— 采用方案 A，并**统一用"唯一命中"判定**：**
- `*_ref` 的正常语义 = "按名字解析**已有**实体"；`force_new` 是**实体创建指令**，**不改变**任何 `*_ref` 的解析规则。
- `force_new` **允许**产生同 kind 同名实体（"两个李默"是真实的写作需求）；但一旦产生，**该名字从此不再是唯一引用键**
  ⇒ 之后所有 `*_ref` 必须按"**多候选 ⇒ 拒 + 候选列表**"处理（绝不静默取"本章的"或"最近的"）。
- 🔴 **由此暴露的实现漏洞（必须修）**：现实现里 `ResolveRefFull` 先查"本章 `entities[]` 声明的名字"、
  **命中就直接返回** ⇒ 当本章 `force_new` 建了一个同名实体时，"本章优先"会把库里那个同名实体**悄悄赢掉**。
  **修法：候选 = 本章声明的 ∪ 库内同 kind 同名的（按 id 去重）**，再统一判"唯一命中 / 多候选"。
  同时门禁（`CheckRef`）也要拦：**本章 `force_new` 声明的名字与库里已有同名实体并存 ⇒ 提交前就拒**（给候选）。
- 契约措辞：**「若 `force_new` 产生同 kind 同名实体，则该名字不再是唯一引用键；要继续用名字引用，必须先消除歧义
  （改用 id / temp_id，或给可区分的名字）。」**
- ⚠️ **明确不做的（防自相矛盾）**：**不禁止** `force_new` 造同名实体 —— 那会推翻已验收的 T3b 验收标准 5
  （"`force_new=true` 能明确创建同名新实体"）。禁止只落在**引用**这一侧：**本任务只做"引用歧义就拒"**。
  门禁那条拦截也是**按引用值**判（不是按"声明"判）：只有当某引用值 = 本章 `force_new` 声明的名字、
  **且**库内同 kind 同名已有实体时才拒 —— 一个**只声明、不被引用**的 `force_new` 依然放行（⑧f/T3b 保持绿）。

**② `*_ref` 的字符串匹配规则（正式定义，不留"按名字匹配"这种模糊话）：**
- **取字符串**：去**首尾 ASCII 空白**（`TrimEntityRef`）—— `*_temp_id` 的精确匹配也用它。
- **比对**：`NormalizeEntityName()` —— 去掉**所有空白**（含**全角空格 U+3000**）+ **ASCII 大小写折叠**；
  中文及其它字节原样保留。
- **明确不做**：Unicode **NFC/NFD 归一化**、**模糊/同义词/繁简**匹配、去标点、去括号。
  ⇒ `李默（小）` 与 `李默` 是**两个名字**；`Alice` 与 `alice` 是**同一个**；`李 默` 与 `李默` 是**同一个**。
- **同源保证**：库里 `entity_names.name_norm`、R10 诊断、resolver 三处**调同一个函数**（`NovelNames.h`）。
- 已知代价（记账，不实现）：NFD 输入（某些 macOS 输入法）可能与库里 NFC 名不匹配 ⇒ 报文里会走"未命中 +
  相近名字提示"这条可见路径，**不静默错**。

**③ D9 的定位（保留，且扩到引用值）**：措辞改为
> `*_ref` / `*_temp_id` **禁止采用「已知 temp-id 前缀 + 纯数字」的保留形状**（如 `en:7`、`ev:7`）；
> 该规则与 `*_ref` 是否需要前缀**无关**。
代码：把 `LooksLikeNumericTempId()` 的检查从 `entities[].temp_id` **扩到所有引用值**（`*_ref` / `*_temp_id`）
—— 否则 `entity_ref="en:7"` 只会得到"库里没有名为「en:7」的实体"这种**误导性**报文（真实原因是**形状被保留**）。

### ★ 边界声明（写进 HANDOFF，防执行者越界）

> **T4 不改变事件身份模型。** 事件仍使用既有 `temp_id` / `id` 语义；**事件名字引用 / 事件唯一化不属于本任务范围**，
> 由 **T2** 处理。（`events[].temp_id`、`causal[].cause/effect` 在 T4 中**不动**。）

**判据（机器可验）—— ⑧f 扩成 6 条（用户指定编号）**：
| 断言 | 内容 |
|---|---|
| **⑧f-1** | `*_ref` 唯一命中 ⇒ 提交成功且**落成真实 id** |
| **⑧f-2** | `*_ref` 多候选 ⇒ **拒**，报文含**两个候选 id** |
| **⑧f-3** | `*_ref` 零候选 ⇒ **拒**，报文含 `entities[]`（新建通道） |
| **⑧f-4** | `*_id` + `*_ref` **同一实体** ⇒ **允许提交**，最终 id == `*_id`（D12 正例） |
| **⑧f-5** | `*_id` + `*_ref` **不同实体** ⇒ **D12 拒**（反例） |
| **⑧f-6** | 旧写法回归：`temp_id = en:语义标签` + 用该 temp_id 引用 ⇒ 仍通过 |
| **⑧f-7（新增，来自①）** | 本章 `force_new` 建同名实体后，用该名字引用 ⇒ **拒 + 列出"本章新建的"与"库里已有的"两个候选** |

2. **兼容回归**：`en:语义标签` 的旧写法全链路仍通过；`--novel-generate <一章>` 提交成功（老提示词下零回归）。
3. `SHINE_NOVEL_GRAPH_CHECK` **0 fail**；真库 `--novel-repair` 干净（R1–R5 无 error）。
4. 文档判据：`02` §2.5 有 `*_ref` 正式条目 + 上述三条语义（`force_new` 关系 / 匹配规则 / D9 措辞）；
   `06` 有 D12；**不再**出现"必须 `en:` 前缀"之类表述。

**T4 的执行顺序（一次做完）**：① `02`/`06` 文档改写 → ② 代码：**union 判定**（本条①的漏洞）
+ **D12** + **D9 扩到引用值** + 门禁的 `force_new` 同名拦截 → ③ ⑧f-1..7 断言 → ④ 真库跑一章 + 自检 0 fail。

### T5 提示词大砍（砍 40–60%）
- 删：ID 类型规则 / K03 说明 / `list_id_directory` 用法 / "0 不能填" / "temp_id 序号不是 id" 等段落。
- 压成一句原则：
  > **模型不生成数据库 ID。** 已有实体用**名字**引用；新实体用 `temp_id`（仅本次 StateDiff 内部关联）。
  > 数据库 ID、存在性、kind 匹配、外键由**下游解析**。
- 判据：`SHINE_DUMP_LLM_REQ` 落盘自查无 §/文档编号/事故史/悬空指涉。
  🔴 **基线更正**（2026-09-21 实测，原记的 7117 已过时）：真库 `agent_defs.extract.system_prompt` =
  **4722 字节**（`version=3`）、`output_hint` = 142 字节 ⇒ 主判据 = 降 **≥40%**（4722 → **≤2833**）。

#### ★ T5 定稿（用户 2026-09-21 校准）—— 开工前的唯一语义确认：**契约仍允许模型写 `*_id`**

**答"T4 最终契约是否仍允许模型显式生成 `*_id`？" = 允许**（已核实，不是凭记忆）：
- `02` §2.5 的字段类型**仍是** `item_id:Id` / `owner_id:Id` / `location_id:Id`（`*_id` 是槽位**主字段**）；
- 同一节明写 **「`*_id`（数字）⇒ 直接用」**；D12 只要求"**不自相矛盾**"，不禁止它出现；
- ⇒ **T5 保留"可用数字 id"这句**，但**必须加限定**，否则会把模型带回旧路。

**根因澄清（消除你感到的错位）**：这条规则的核心分野是 **"生成" vs "引用"** ——
> **模型不"生成"数据库 ID，但可以"照抄"系统给出的 ID。**

歧义被拒时，**候选列表本身就是系统给的**（`#43 / #201`，或 `list_entities` 工具结果）⇒ 模型
**从那里面挑一个填**，是"引用"不是"生成"。**绝不允许**它凭记忆编一个数字（那正是 K03/序号锚定的老毛病）。

**三行 v2（写进 spec）**：
> 1. **模型不生成数据库 ID。** 已有实体用**名字**引用（若库里同名同 kind 有多个 ⇒ 请求会被拒**并列出候选**）。
> 2. 新实体用 `temp_id`（**仅本次 StateDiff 内部关联**）；要建"**同名但不同的人**"用 `entities[].force_new=true`。
> 3. **数据库 ID、存在性、kind 匹配、外键由下游解析**（你不用管，也别猜）。歧义时要么**照候选列表里的 id 填**，
>    要么用 `force_new` 表达新建。

**明确不做（防"顺手收紧契约"）**：T5 **只改提示词**，**不**改 `02` 字段类型、**不**改 D12/D9 语义、
**不**动外部 MCP 写路径。若将来要"**彻底禁止模型写 `*_id`**"，那是**独立的契约收紧任务**
（要改 `02` 字段类型 + 校验 + 外部写路径 + 工具输出），**另立 T7 单独拍板**，不塞进 T5。

**分批（用户认可）**：
| 批 | 删什么 | 之后做什么 |
|---|---|---|
| **A** | ID 类型规则（`*_id` 该填什么、序号 vs id 的告诫） | 编译 → 自检 → **跑一章** |
| **B** | K03 详细说明 / `list_id_directory` 用法 / "0 不能填" / "temp_id 序号不是 id" / 事故史 / 悬空指涉 | 编译 → 自检 → **再跑一章** |

**验收（用户补了第 4 条，我加第 6 条）**：
1. `system_prompt` 4722 → **≤2833 字节**（≥40%）；
2. `SHINE_DUMP_LLM_REQ`：无文档编号 / 事故史 / 实现细节堆砌 / 悬空指涉；
3. **两次实跑均提交成功**；
4. **K03=0 必须定义为"模型输出仍满足现有 K03 契约"** —— ⚠️ **不是**"K03 检查结果为 0"；
   反例保护：**不许**为了让指标变绿而删 K03 检查/错误路径（T5 **一行校验代码都不改**，`git diff` 里
   `NovelChecks.cpp` 只允许动 **spec 文本**）；
5. 旧 `en:` 语义标签回归保持绿（⑧f-6 已钉）；
6. **老库 spec 必须真刷新**（否则"改了不生效"）：真库核
   `SELECT length(system_prompt), instr(system_prompt,'prompt_rule_version=19') FROM agent_defs WHERE agent_id='extract'`
   —— 走 S63 的"**按内容刷新**"机制（内容变才刷、哈希不白漂），`prompt_rule_version` **18 → 19**。

### T6 A/B 验证（**终局判据**）
- 对照：取一章**已 done** 的旧章重跑（`--novel-generate <id>`），比：`K03 次数 / 契约 D 触发 / 重做次数 / instructions 字节`。
- 再跑 `--novel-run auto --max 20`：**无硬停**（S1/S4 不触发）；`NovelRepair` R1–R11 干净。
- 注意：`auto` 模式的前置门禁已实测满足（N1–N14 全过）。

### ★ R10 三组处置方案（dry-run，2026-09-21 出；**人审通过后才 apply**）

> 原则：**R10 只报告；合并 / 改名必须人审**。下面每组的外键计数 = **执行清单**（要重指多少行）。
> 编号沿用 `--novel-repair` 的 dry-run 口径；apply 必须**幂等 + 带碰撞预检**（`event_participants`
> 是 `UNIQUE(event_id,entity_id)`、`character_status` 是 `UNIQUE(entity_id,chapter_id)`，直接 UPDATE 会撞）。

**① `person`「撑伞人影」#43 / #108 —— 建议【合并 → 保留 #43】**
| | #43 | #108 |
|---|---|---|
| 建章 | 第5章 | 第10章 |
| 关系 / 事件参与 / 出场 / 持有 / 快照(`entity_versions`) | 8 / 13 / 9 / 2 / 11 | 2 / 5 / 2 / 0 / 3 |

**决定性证据**：事件 **#145**（"撑伞人影在塔基水泥房门外递出牛皮纸新信封…"）**两份都是 actor**
⇒ 是**同一角色的重复声明**，不是两个同名人物（两份 summary 也在讲同一件事的两面）。
执行：① `relations` 108→43（2 行）；② `event_participants` 5 行 → 43，**若 `(event_id,43)` 已存在则删 108 那行**
（#145 即此情形）；③ `character_status` 2 行 → 43，同章冲突则删 108 那行；④ `entity_versions` 3 行 → 43；
⑤ 删 `entities#108` + 其 `entity_names` 行 + 写 `audit_logs(action='merge_entity')`。

**② `location`「旧信号塔·塔顶灯室」#59 / #76 / #134 —— 建议【软合并 → 保留 #59】**
| | #59 | #76 | #134 |
|---|---|---|---|
| 建章 | 第8章 | 第7章 | 第13章 |
| `event_details.location_id` | 12 | 14 | **0** |
| `character_status.location_id` | 9 | 6 | **0** |

保留**引用更多、且模型此刻仍在用**的 #59（第 21 章的 diff 仍在写 `location_id=59`），把 #76、#134 折进它
（同章 `character_status` 冲突保留 #59 行）。⚠️ **#134 虽然零引用，也绝不 DELETE** —— 理由见下节。

### 🔴 修正：**永不物理删除实体** —— 用"软合并 / 重定向"（用户 2026-09-21 追问逼出来的）

**为什么不能删**（我原方案错了）：名字层面的解析不会坏（名字仍指向幸存者），但 **id 层面会坏** ——
#134 一旦不存在，**任何**旧产物 / 快照 / `audit_logs.detail` / **外部 MCP 客户端** / 模型上下文里
"记得 134"的东西，都会变成**悬空 id** ⇒ K02/K03 直接拒；而且**要到第 1000 章才炸**，那时极难回溯。
另外合并后**没人知道 134 曾存在、曾指哪里** ⇒ 历史行看起来像坏数据。

**设计（正好把 §3.5 预留的列用起来，不再"只留列不实现"）**：
1. `entities` 新增列 **`merged_into INTEGER NOT NULL DEFAULT 0`**（schema v14；0 = 活的实体）；
2. **凡是"按名字找候选"的地方一律只看 `merged_into=0`**：`EntityNameIndex`（resolver）、**R10 扫描**、
   D12/T4 的冲突判定 —— 退休行**不再参与**，所以"同名歧义"随之消失；
3. **`ResolveRefFull` 的数字 id 那一步加"重定向"**：`id>0` 且该实体 `merged_into<>0` ⇒ **解析到幸存者**
   ＋记一条 warn（`*_id=134 → 重定向到 #59`），**不报"不存在"** ⇒ 旧 id 在**第 1000 章依然可用**；
4. `NovelRepair` 增规则 **R11 `entity_merged_redirect`**（`fixable=true`，但**只做**：设 `merged_into` +
   把外键引用重指到幸存者；**绝不 DELETE 任何实体行**）；apply 后写
   `audit_logs(action='merge_entity', detail='#134→#59 …')` 保留来历；
5. **R10 加 `WHERE merged_into=0` 过滤**（否则退休行会被永远报成"重复"）；
6. 改名（③）同理：**只改名字，不删行**。

**代价（明说）**：表里会留"退休行"、resolver 多一步重定向查询 —— 换来的是
**"1000 章后旧 id 仍然可解析、任何历史产物可重放"**。这个交换我认为必须做。

#### ✅ R11-1 已完成（2026-09-21，schema + resolver；**未提交**）
- `entities.merged_into`（**v14**，`ALTER` + `idx_entities_merged`，幂等）已落地：真库 207 行 →
  `live 207 / retired 0`。
- **resolver 透明重定向**（`ResolveRefFull` 的 `id>0` 分支）：跟到活实体；**有限跳数（8）+ 环检测**兜底；
  **留痕** —— `log::Warn("id #134 is retired; redirected to #59（旧 id 仍可解析，但新写入请用幸存者）")`。
- `D12` 的冲突比较改用**重定向后的 `live`** ⇒ "写退休 id + 写幸存者名字"判为**一致**（不误判冲突）。
- **名字候选三处只看活实体**（`merged_into=0`）：`EntityNameIndex`（resolver + 门禁）、
  `UpsertEntity` 的复用查询（`JOIN entities` 过滤）、`NovelRepair` 的 R10 扫描。
- 自检 **0 fail**；R10 仍报 3 组（当前无退休行）。

#### ⏳ R11-2（下一步，未做）
1. `NovelRepair` 新增 **R11 `entity_merged_redirect`**：dry-run 出**合并计划**（survivor 建议 + 每个 loser 的
   **外键重指清单** + **碰撞预检**）；**apply 必须显式点名 survivor**（`--merge "43<108;59<76,134"` /
   MCP 参数 `merge`）⇒ 保留"**永不自动 merge**"。
2. **apply 语义（用户硬验收 A：链式必须收敛）**：`merged_into` **直接指向活实体**（134→59、76→59、59=0，
   **不留链**）；外键**全部重指**到 survivor（`relations` / `event_participants` / `character_status` /
   `event_details.location_id` / `entity_versions`；**碰撞时删重复行** —— `UNIQUE(event_id,entity_id)` 等）；
   **绝不 DELETE 实体行**；写 `audit_logs(action=merge_entity, detail=…)`。
3. 自检断言：**redirect 收敛**（C→B→A 压平为 C→A）/ **留痕**（warn 可见）/ 重指后 FK 全在 survivor /
   退休行不再进名字候选（**同名歧义消失**）/ R10 复扫归零。
4. 之后才做三组处置（survivor 已拍：撑伞人影 #43、旧信号塔·塔顶灯室 #59；备用信号机**改名**，
   名字要**先摊证据后定**，不预设"甲/乙"）。

**③ `item`「备用信号机」#52 / #159 —— 建议【不合并，改名区分】（三选一，待拍板）**
| | #52 | #159 |
|---|---|---|
| 建章 | 第6章 | 第16章 |
| 事件参与 | 1（#57「发出同频杂音」） | 2（#164/#165「耳机实时回播口哨」） |
| 关系 / 持有 / 动态字段 | 0 / 0 / 0 | 0 / 0 / 0 |

两份**不共享任何事件**、描述不同（"调至父亲惯用频段" vs "频段旋钮未被转动"）⇒ 很可能是**两台真机**。
- **A（推荐）**：`备用信号机甲` / `备用信号机乙` —— 中性、最小改动；
- **B**：语义区分（如 `备用信号机（仓区）` / `备用信号机（塔基）`）—— 需作者确认叙事位置；
- **C**：只改 #159、保留 #52 为原名 —— ⚠️ **不推荐**：裸名仍唯一 ⇒ 模型若想指 #159 那台会**静默指错 #52**
  （把"歧义"降级成"静默错指"，正违契约）。

A/B 都会让裸名「备用信号机」**零候选** ⇒ 未来引用**可见地拒** + 相近名提示（符合"不擅自消除歧义"）。
改名执行：`entities.name` + `entity_names.name/name_norm` 同步 + `audit_logs(action='rename_entity')`；
⚠️ 盘上历史 `work/chNNN/12_state_diff.json` 的旧名**不改**（冻结产物）——将来若重提某章、其 `*_ref` 用旧名，
会得到"库里没有这个名字 + 相近名"的**可见**拒绝。

**顺序**：dry-run（本文件 + 证据表）→ 人审 → apply → `--novel-repair` 复扫（3 组归零）→ **再**做 T5-B 批。

## 5. 风险与对策（诚实记录）

| 风险 | 对策 |
|---|---|
| 模型写名字变体（`林澈` vs `林澈（主角）`） | 解析**拒**（不静默错）＋重做时**回灌候选名单**（机制现成） |
| 同名但**不同人**（如两个"李默"） | 解析**拒**并要模型给可区分的名字；将来再上 `entity_names(kind,name_norm) UNIQUE` + alias（**现在库里没有这种实例，别过度设计**） |
| 事件被误按名字去重 | 已用 `DedupByPersistentName()` 排除 `event`；**别把它打开** |
| 提示词大砍后行为回退 | 一次只砍一类，砍完立刻跑一章对照；`prompt_rule_version` 每次抬 |

## 6. 禁止

- 一次性做完 T1–T6（一次一个 T，做完就验证）。
- 手改真实库（一律走 `NovelRepair` 规则或给出 SQL 交用户确认）。
- 把"事实"塞进 prompt（`00` §2 总纲 / S48 / S62 的教训）；事实要**给工具**或**由系统预置为工具结果**。
- 用 `tool_choice` 强制工具调用（该端点不支持，见 2026-09-20 记忆）。
