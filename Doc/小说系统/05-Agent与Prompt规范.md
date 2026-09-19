# 05 · Agent 与 Prompt 规范

> 本卷定义「每个阶段怎么写提示、边界在哪」：Prompt 的外置位置与命名、各 Agent 的职责与禁区、输出格式强约束、负面约束清单、工具白名单来源，以及总控 Prompt 的要点。Prompt 的**派生产物**属性见 `00` §2.5，契约字段见 `02`。

## 0. 本卷范围与不做什么

**范围**

- Prompt 外置规则：放哪、怎么命名、怎么加载、怎么版本化。
- 各阶段 Agent 的职责与**禁区**（谁不能改世界状态）。
- 输出格式强约束（`json_schema`）的写法要求。
- 负面约束清单与失败示例。
- 工具白名单的**来源规则**（必须来自 DB，禁止硬编码）。
- 总控 Prompt 要点。
- 现有 15 个内置 Agent 到阶段表的归位。

**不做**

- 不写具体 Prompt 全文（本卷给要点与约束；全文放 `prompts/`）。
- 不定义契约字段 → `02`。
- 不定义阶段顺序 → `03`。
- 不定义上下文选材 → `04`。

## 1. 现有落点

### 1.1 正文链四角色（`src/agent/NovelDirector.cpp:102-131`）

| 角色 | 行号 | Prompt 要求其输出 | 现存问题 |
|---|---|---|---|
| `planner` | `:103-111` | `{chapter_title, goal, scenes[{ord,location,cast,goal,conflict,result,emotion}], foreshadowing[], ending_hook}`，场景 2–4 个，结尾留钩子 | 与 `02` §2.2 的 `ChapterPlan` **不一致**（缺 events/knowledge/pov 等） |
| `writer` | `:112-116` | 严格按计划 + 上下文写正文；守文风与硬规则；POV 未知情秘密不得写穿；**只输出正文，不输出 JSON 或解释** | 无长度约束、无段落结构约束 |
| `critic` | `:117-122` | 检查人设/世界观/时间线/能力/POV/伏笔/作者规则；输出 `{"passed":bool,"issues":[{type,severity(high\|mid\|low),description}]}` | 无 rubric、无阈值、无 `checks_run`；且**判定用子串匹配**（`:250-252`） |
| `extractor` | `:123-129` | `{summary,new_entities[{kind,name}],events[{title}],foreshadow_updates[{title,status}]}` | 与 `02` §2.5 的 `StateDiff` **差距巨大**；产物被丢弃（`:275-281`） |

### 1.2 Prompt 加载

- `LoadPrompt(role)`（`NovelDirector.cpp:136-146`）：仅当 `promptsDir_` 非空时读 `<promptsDir_>/<role>.md`（`:137-144`），否则回退 `DefaultPrompt`（`:145`）。
- `SetPromptsDir`（`NovelDirector.h:81`）：**全仓零调用点**（已核实）。
- 仓库中**不存在 `prompts/` 目录**。
- 结论：**实际恒走硬编码 `DefaultPrompt`**。契约 `docs/compose/spec/novel-agent.md:347` 声称「模板文件 `prompts/*.md` 运行时读，不写死在 cpp」——**与实现不符**。

### 1.3 多 Agent 能力（`src/agent/AgentKit.cpp`）

- `AgentDefRow`（`AgentKit.h:19-31`）：`id`、`agent_id`、`name`、`role_tags`、`system_prompt`、`tools_json`、`output_hint`、`enabled`、`is_builtin`、`version`、`updated`。
- **内置 15 个 Agent**（`AgentKit.cpp:1116-1162`）：`novel_writer`(1117)、`character`(1120)、`item`(1123)、`world`(1126)、`place`(1129)、`faction`(1132)、`event`(1135)、`mystery`(1138)、`extract`(1141)、`review`(1144)、`memory`(1147)、`visual`(1150)、`field_builder`(1153)、`agent_meta`(1156)、`router`(1159)。
- `DefaultPromptFor(agentId)`（`:1182-1265`）：15 套硬编码 Prompt（与 `NovelDirector` 的四套**并存且不同源**）。
- `Route`（`:719-774`）：关键词表 `kRules`（`:732-747`）**顺序**匹配；未命中默认 `novel_writer`（`:773`）。库内 `routing_hints` 字段可增强（`:723-726`）。
- `RegisterToolsFor`（`:831-875`）：**按 `agentId` 的 C++ 硬编码分支**决定工具，而非读 `tools_json`。
- `ResolveTools`（`:908-945`）/ `CallToolAsAgent`（`:947-997`，`WhitelistHas` 拒非白名单 `:956-960`）：这才是真正按 `tools_json` 求交集的路径。
- `BuildInvokePackage`（`:1000-1056`）：组装执行包（prompt + 允许工具）。
- `Run`（`:1057`）：工具环入口，**全仓唯一调用点 `:1507`（自检）**。

### 1.4 工具环参数（`src/agent/ToolRegistry`）

- `kMaxToolCalls = 20`（`ToolRegistry.h:46`）、`kMaxRepeat = 3`（`:47`）。
- `RunToolLoop`（`ToolRegistry.cpp:131-268`）：`CheckLoopGuard`（`:112-127`）、`NoteCall`、超限返回「工具循环超出最大迭代」（`:267`）。

### 1.5 MCP 侧 Agent 工具

`NovelMcpTools.cpp` 中 `novel_list_agents`(871)、`novel_get_agent`(882)、`novel_route_task`(893)、`novel_agent_tools`(904)、`novel_agent_invoke`(917) 已可被外部 AI 调用。

## 2. 目标设计

### 2.1 Prompt 外置规则

**目录**：`prompts/`（仓库根，或 `%APPDATA%\ShineTVStudio\prompts\` 覆盖）。

**命名**：`<chain>.<stage>.<agent_id>.md`

```
prompts/
  text.chapter_goal.planner.md
  text.outline.planner.md
  text.event_plan.event.md
  text.scene_plan.planner.md
  text.character_plan.character.md
  text.item_plan.item.md
  text.foreshadow_plan.mystery.md
  text.novel_write.writer.md
  text.chapter_review.review.md
  text.chapter_repair.writer.md
  text.state_extract.extract.md
  visual.scene_breakdown.visual.md
  visual.director_intent.visual.md
  visual.storyboard.visual.md
  visual.image_prompt.visual.md
  visual.video_prompt.visual.md
  visual.audio_prompt.visual.md
  orchestrator.md
```

**加载规则**：

| 规则 | 内容 |
|---|---|
| L1 | 加载顺序：`%APPDATA%` 覆盖目录 → 仓库 `prompts/` → 内置默认（三级回退，**必须至少有一级命中**） |
| L2 | 启动时必须**校验 18 个文件齐备**，缺失即报错（不允许静默回退到内置，避免「以为改了其实没生效」） |
| L3 | 每次加载记录 `hash` 与该 Prompt 产出的 `PromptArtifact.version` 关联 |
| L4 | Prompt 改动即 `contract_version` 变更的触发条件之一（若改了输出格式） |
| L5 | **禁止**在 cpp 中保留与之等价的第二份 Prompt（消除 `NovelDirector` 与 `AgentKit` 双源） |

**与 `03` §2.4 一致**：只加载当前阶段的那一个文件。

### 2.2 各阶段 Agent 职责与禁区

| 阶段 | Agent | 可以 | **禁止** |
|---|---|---|---|
| `CHAPTER_INIT` | 总控 | 读、整理、算哈希 | 生成任何文本内容 |
| `CHAPTER_GOAL` / `OUTLINE` | `planner` | 定目标、定结构 | 写正文；改世界状态；发明新实体 |
| `EVENT_PLAN` | `event` | 设计事件与因果 | 写正文；直接写库 |
| `CONTEXT_ASSEMBLY` | 总控 + 检索工具 | 拉取、裁剪、打分 | 生成内容 |
| `SCENE_PLAN` | `planner` | 规划场次 | 决定世界观规则 |
| `CHARACTER_PLAN` | `character` | 在场理由/目标/情绪/知情边界 | 越出 `01` §2.3.2 的知情边界 |
| `ITEM_PLAN` | `item` | 道具出现与流转 | 发明未登记物品（须走 `NewEntity`） |
| `FORESHADOW_PLAN` | `mystery` | 埋点、推进、回收设计 | 直接改 `foreshadowings.status` |
| `NOVEL_WRITE` | `writer` | **只**把已定事件转成文学表达 | 改剧情结果、改人物身份、改服装道具空间、写穿 POV 未知情秘密、输出 JSON 或解释 |
| `CHAPTER_REVIEW` | `review` | 出 `ReviewVerdict` | **修改正文**；**修改世界状态**（旧文档 `08` 已明确） |
| `CHAPTER_REPAIR` | `writer` | 按 issue 最小修改 | 借机重写全章（超出 `RepairRequest.scope`） |
| `STATE_EXTRACT` | `extract` | 产出 `StateDiff` | 写库（只产出契约对象） |
| `STATE_VALIDATE` | 校验器（非 LLM） | 断言不变式 | 做语义判断 |
| `COMMIT` | 状态层 | 事务提交、快照 | 调用 LLM |
| `SCENE_BREAKDOWN` … `STORYBOARD` | `visual` / `director` | 视觉设计 | 改正文、改叙事状态 |
| `PROMPT_GEN` | `visual` | 产出 `PromptArtifact` | 自行发明未登记的外观（须引用 `visual_assets`） |
| `GENERATION` | 状态层（Comfy） | 提交与轮询 | 调用 LLM |

**核心边界（旧文档 `03` 的「Prompt 是派生产物」的对应）**：

- **唯一允许写世界状态的入口是 `COMMIT`**，且它不调用 LLM。
- 其余任何阶段的产物都只是「提案」；即使 Agent 说「某人死了」，也要经 `STATE_EXTRACT` → `STATE_VALIDATE` → `COMMIT` 才成为事实。

### 2.3 输出格式强约束

| 要求 | 内容 |
|---|---|
| O1 | 所有产出契约对象的阶段，请求必须带 `text.format = json_schema`（OpenAI 系）或等价的结构化输出能力 |
| O2 | `json_schema` 必须**由 `02` 的契约自动派生**，不得手写第二份 |
| O3 | `additionalProperties = false`（对应 `02` §2.0 C5：禁止未定义字段） |
| O4 | 枚举字段必须写成 `enum`，不得用自由字符串 |
| O5 | 解析失败处理：**先按 `json_schema` 重试 1 次**（`03` §2.6），仍失败 → `code=contract` |
| O6 | **禁止**用子串匹配判断任何枚举字段（当前 `NovelDirector.cpp:250-252` 的 `"passed":true` 做法必须废弃） |
| O7 | `writer` 是唯一例外：输出纯文本，**不带 JSON**；但其成果必须能被 `review` 定位到段落（要求 writer 按场景分段，段首带 `[scene:<id>]` 标记） |

### 2.4 总控 Prompt 要点（`prompts/orchestrator.md`）

```text
你是本系统的总控。你的职责不是向用户解释下一步是什么，而是自主执行当前 Workflow，
并在每个阶段完成后自动进入下一个阶段。

你必须：
1. 先读 WorkflowState，确定当前链与当前阶段；
2. 只加载该阶段所需的能力与最小上下文（见 04）；
3. 在该阶段完成后，用 02 的契约校验产物；
4. 校验通过后落盘，再决定下一阶段；
5. 仅在 03 §2.6 的停止条件命中时停下，并输出「需要什么决策 / 卡在哪一步」。

你不得：
- 持有或缓存任何专项 Agent 的 Prompt 全文；
- 代替专项 Agent 生成正文、评审或提取；
- 直接修改世界状态（唯一入口是 COMMIT）。
```

### 2.5 负面约束清单（通用，所有阶段共享）

| # | 约束 |
|---|---|
| N1 | 不得输出契约未定义的字段（`02` C5） |
| N2 | 不得用 `ord` 做跨表关联，一律用 `chapter_id` 引用（`02` C7） |
| N3 | 不得发明未登记的实体；新增必须走 `StateDiff.entities`（`NewEntity`） |
| N4 | 不得让 `dead`/`destroyed` 实体作为行动者（不变式 I3） |
| N5 | 不得让 POV 使用其不知情的信息（不变式 I2） |
| N6 | 不得回退伏笔状态（`01` §2.3.3） |
| N7 | 不得在正文中出现未在 `02` 契约中声明的头部标记（除 `[scene:<id>]`） |
| N8 | 不得复述上下文原文作为输出（防止「把材料抄一遍」） |
| N9 | 不得在 `writer` 输出中包含解释、标题、Markdown 装饰 |
| N10 | `review` 不得输出「建议」而未给出 `issue_id` 与 `severity` |

### 2.6 工具白名单来源规则

| 规则 | 内容 |
|---|---|
| W1 | 白名单 = `agent_defs.tools_json`（DB） ∩ 阶段声明的允许工具（`03` §2.4） |
| W2 | **禁止**再用 `AgentKit::RegisterToolsFor` 的 C++ 硬编码分支 |
| W3 | 写入型工具（`novel_upsert_*` / `novel_link_*`）在本系统的生成链路中**一律不授予**；写入只经 `COMMIT`（`07`） |
| W4 | `mcpAllowWrite` 只影响**外部 MCP 客户端**，不影响内部生成链路的授信 |
| W5 | 每个阶段的实际工具集合必须写入 `audit_logs.detail`，可回溯 |
| W6 | 工具返回超长时按 `04` §2.7 的预算裁剪，并把裁剪记录进 `ContextReport` |
| W7 | 每个工具的 `input_schema` 必须**可序列化且非空**。退化为 `{"type":"object"}`（`src/mcp/Schema.cpp:124-135` 的 `ToJsonString` 失败分支）时，**该工具视为不可用并告警**——禁止静默降级，因为模型会拿不到任何参数名/类型（对应 G22） |
| W8 | 依赖写开关（`mcpAllowWrite`）的自检，必须在**自己的作用域内**保存→复位→还原开关，**不得依赖外部配置**（对应 G23） |

### 2.7 现有 15 个内置 Agent 到阶段表的归位

| 现有 `agent_id` | 归位阶段 | 处置 |
|---|---|---|
| `novel_writer` | `NOVEL_WRITE` / `CHAPTER_REPAIR` | 保留 |
| `planner`（现为 NovelDirector 内部角色，非 `agent_defs`） | `CHAPTER_GOAL` / `OUTLINE` / `SCENE_PLAN` | **需补登记为 `agent_defs` 记录** |
| `character` | `CHARACTER_PLAN` | 保留 |
| `item` | `ITEM_PLAN` | 保留 |
| `world` | `CHAPTER_INIT` 的检索增强 | 保留（改名 `world_context`） |
| `place` | `SCENE_PLAN` 的地点侧 | 保留（合并入 `character` 之外的地点分支） |
| `faction` | `SCENE_PLAN` 的势力侧 | 保留 |
| `event` | `EVENT_PLAN` | 保留 |
| `mystery` | `FORESHADOW_PLAN` | 保留 |
| `extract` | `STATE_EXTRACT` | 保留 |
| `review` | `CHAPTER_REVIEW` | 保留 |
| `memory` | `L4` 摘要树生成 | 保留 |
| `visual` | V0–V10（+ 可选下游 V11 出图） | 保留；**V0 资产生产**（正脸→四视图→服装，`11` §2.6）也归它 |
| `field_builder` | `08` 的字段登记提案 | 保留（写入需走 Canon 门禁） |
| `agent_meta` | 运维 | 保留 |
| `router` | 已被 `03` 的阶段表取代 | **降级为兜底**：仅当阶段表未命中时使用 |

## 3. 差距（逐条：现状 → 缺口 → 影响）

| # | 现状 | 缺口 | 影响 | 严重度 |
|---|---|---|---|---|
| 05-1 | Prompt 硬编码在 cpp；`SetPromptsDir` 零调用点；无 `prompts/` 目录 | 无外置、无版本化 | 无法 A/B、无法复现（G9） | **A** |
| 05-2 | `NovelDirector::DefaultPrompt`（4 套）与 `AgentKit::DefaultPromptFor`（15 套）**双源** | 同一能力两处定义 | 改一处不生效 | **A** |
| 05-3 | `planner` 的 Prompt 输出与 `02` §2.2 的 `ChapterPlan` 不一致 | 契约与 Prompt 脱节 | 无法按契约校验 | **S** |
| 05-4 | `extractor` 的 Prompt 输出与 `02` §2.5 的 `StateDiff` 差距巨大 | 同上 | 回写不可实现（接 G2） | **S** |
| 05-5 | Critic 判定靠子串匹配 | 无枚举、无 schema | 评审不可信（G3） | **S** |
| 05-6 | 未实现结构化输出 | 无 `json_schema` | 契约只能事后校验 | **A** |
| 05-7 | `RegisterToolsFor` 硬编码白名单 | 白名单不可配 | 改白名单需改代码 | **A** |
| 05-8 | 无负面约束清单 | 各阶段约束散落 | 输出漂移 | **A** |
| 05-9 | 15 个 Agent 中 `router` 与阶段表功能重叠 | 两套调度并存 | 行为不可预测 | **B** |
| 05-10 | 无总控 Prompt | 无自主推进 | 需人工逐步驱动 | **S** |
| 05-11 | `writer` 输出无分段标记 | `review` 无法定位到段 | 修复粒度粗（只能整章重写） | **A** |
| 05-12 | 写入型 MCP 工具对生成链路开放（受 `mcpAllowWrite` 控制） | 绕过 `COMMIT` 的风险 | 状态可被旁路修改 | **A** |
| 05-13 | **所有 MCP 工具的 `input_schema` 序列化失败**：`src/mcp/Schema.cpp:124-135` 的 `yyjson_mut_val_write(schema, 0, &len)` 恒返回 nullptr → 退化为 `{"type":"object"}`。实测一次自检跑出 **94 次**该警告（≈ 全部注册工具） | Agent 拿不到参数名/类型/必填 | **「MCP 按需取数」的前提被破坏**：`04` §2.7 的工具目录、W1 的白名单全部失效。与 `01` §2.9.7「参数是数据」的设计直接冲突 | **S** |
| 05-14 | `RunNovelMcpSelfCheck` 断言「写工具应默认拒绝」，但**不复位写开关**；只要 `settings.json` 里 `mcpAllowWrite=true` 就必然 fail（本机实测：`novel MCP 自检 FAIL：写工具应默认拒绝，got ok=true`） | 自检不健壮、依赖外部配置 | 假红，掩盖真问题（见 05-13） | **B** |

## 4. 验收判据

- [ ] §2.1 的 18 个 Prompt 文件名清单完整，且每份都能对到 `03` 的一个阶段。
- [ ] §2.2 职责表每一行都有明确的「禁止」列，且 `review` / `COMMIT` 的禁区与旧文档 `08` 一致。
- [ ] §2.3 的 O1–O7 七条全部可判定；O6 明确废弃子串匹配。
- [ ] §2.4 的 `orchestrator.md` 要点包含「唯一写入口是 COMMIT」。
- [ ] §2.5 的 N1–N10 每条都能映射到一条不变式或契约规则。
- [ ] §2.6 明确「写入型工具不授予生成链路」，且 W2 明确禁止继续使用硬编码分支。
- [ ] §2.7 的 15 个现有 Agent **每一个**都有归位结论（保留/改名/合并/降级），无遗漏。
- [ ] 全集中不存在「Prompt 写死在 cpp」为推荐做法的表述；`docs/compose/spec/novel-agent.md:347` 的不符之处已在 `00` §1.2 标注。
- [ ] §3 的 **14** 条差距在 `00` §3 有编号对应（05-13 → G22、05-14 → G23）。

## 5. 与其它卷的关系

- 契约字段与 `json_schema` 的来源 → `02`（O2 要求自动派生）。
- 阶段与能力映射 → `03` §2.4。
- 上下文的选材与预算 → `04`。
- 评审 rubric 与阈值 → `06`。
- `STATE_EXTRACT` 产物的校验与提交 → `07`。
- 字段登记提案的处理 → `08`。
- 模型分层（哪个阶段用哪个模型）→ `09` §2.4。
- 影视化链各 Agent 的 Prompt 要点 → `13`。
