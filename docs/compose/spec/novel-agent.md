---
feature: novel-agent
status: designed
updated: 2026-02-14
branch: (no-git — 主树；与 novel-studio 共用 novel.db)
commits: n/a
---

# ShineTV 小说 Agent

> **执行计划树**：`docs/compose/plans/novel-agent/00-总纲.md`（P1–P10）  
> **MCP**：进程内 Agent 直连 `src/db`，不绕 MCP；对外/Responses remote 见 `P10-mcp/PLAN.md`。  
> 视觉细则在 P8 分册。编辑 UI：`novel-studio.md`。每工程 `novel.db`。

## Report

## [S1] Problem

长篇创作只靠「章节正文 + 少量设定」无法保证一致性。需要在工程库内维护可查询的叙事知识图谱（人物关系、事件因果、持有流转、伏笔秘密、知情边界），并由 Agent 在生成章节时检索相关切片、规划场景、写正文、审核修订、回写状态。网络与模型调用必须落在本仓库既有技术栈上，禁止在 UI 线程做同步 HTTP。

## [S2] Design

### [S2.1] 分层

```
app/novel          ImGui 工作区（生成入口、流式预览、图谱浏览）
  ↓
agent              运行时：Director / ContextBuilder / Tools / Planner·Writer·Critic
  ↓
openai             Responses 薄客户端（libhv + yyjson，无业务）
novel              工程库、知识图谱、记忆（SQLite novel.db）
  ↓
core / db / comfy·style HTTP   async · log · sqlite · HttpClient 模式
```

| 模块 | 路径 | 职责 |
|------|------|------|
| OpenAI 客户端 | `src/openai/` | POST `/v1/responses`，同步 + SSE；`std::expected` |
| Agent | `src/agent/` | 状态机、工具注册、上下文组装、三角色管线 |
| 知识图谱 | `src/novel/` | schema 迁移、实体/关系/事件/记忆 CRUD |
| UI | `src/app/novel/` | 打开工程后生成章、看进度、改设定 |

业务 JSON 一律 **yyjson**；HTTP 用 **libhv**（仿 `ComfyHttp`，独立文件）；异步 **shine::async**；日志 **shine::log**。语言特性按 `Doc/RULES-LANG.md`（`string_view` / `span` / `expected` / 禁止 `std::format`）。

### [S2.2] OpenAI 客户端 `src/openai/`

```cpp
namespace shine::openai {

struct ApiError { int http_status = 0; std::string code; std::string message; };

struct CreateRequest {
  std::string model;
  std::string instructions;
  // input / tools 由调用方用 yyjson 构造；Client 不拥有 doc 生命周期
  yyjson_val* input = nullptr;
  yyjson_val* tools = nullptr;
  bool store = false;
  std::string previous_response_id;
};

struct CreateResult {
  std::string id;
  std::string status;
  std::string output_text;  // 拼好的 message 文本
  yyjson_doc* raw = nullptr; // 调用方 doc_free
};

struct StreamEvent {
  enum class Type { Created, TextDelta, Completed, Error, Other };
  Type type = Type::Other;
  std::string text;
  std::string message;
};

class Client {
public:
  // 仅 worker；禁止 UI 线程
  [[nodiscard]] std::expected<CreateResult, ApiError>
  Create(const CreateRequest&, std::chrono::seconds timeout);

  void Stream(const CreateRequest&,
              std::function<void(const StreamEvent&)>&& on_event,
              std::function<void(std::expected<void, ApiError>)>&& on_done);
};

}
```

- 密钥：环境变量 `OPENAI_API_KEY` 或 `Settings()`（本地）；**禁止**入库、禁止打日志。
- Base URL、模型名可配置（planner/writer/critic 可分模型）。
- Structured Output：请求 `text.format` json_schema；解析失败重试 1 次后记日志并失败。
- 错误统一进 `ApiError`，上层 `PostToUi` 展示中文提示。

### [S2.3] 叙事知识图谱（`novel.db` v3）

统一「可命名对象」为 `entities`，用关系/事件/因果边构成图；叙事主轴用 `volumes→chapters→scenes→beats`。  
**覆盖原则**：下表按「世界 / 人物 / 物品地点势力能力 / 叙事 / 谜与知识 / 系统」六域，与需求清单一一对应，不靠 prose 吞掉字段。

#### entities.kind（全量）

| 域 | kind |
|----|------|
| 世界 | `universe` `world_rule` `history` `culture` `language` `religion` `economy` `tech` `society` `calendar` |
| 人物/生物 | `person` `creature` |
| 装备 | `clothing` `prop` `treasure` `item`（子类用 `item_details.item_type`） |
| 空间势力 | `location` `faction` |
| 能力 | `power_system` `ability` |
| 叙事 | `event` `plot` `arc` `conflict` `theme` `motif` `ending` |
| 谜 | `secret` `foreshadowing` `mystery` |
| 资源 | `resource` |

#### 覆盖对照（需求 → 落点）

| 需求块 | 落点 |
|--------|------|
| 世界观/宇宙/地理/历史/时间/社会/文化/科技/超自然/规则/世界秘密 | `world_*` 详情表 + `location_*` + `calendar_*` + `secrets`（world 级） |
| 人物全字段/弧光/成长/当前状态 | `entity_personas` + `character_arcs` + `character_status` + `character_knowledge` |
| 关系类型+强度时间线 | `relations`（已有） |
| 物品细分/Owner History | `item_details` + `entity_ownerships` + `entity_events` |
| 地点层级/距离旅行 | `location_details` + `location_distances` |
| 势力成员/敌盟 | `faction_details` + `faction_members` + `relations` |
| 能力体系/克制 | `power_system_details` + `ability_details` + `relations(counter)` |
| 事件+因果网 | `event_*` + `causal_links` |
| 剧情类型/转折/揭秘/收束 | `plots.kind` + `plot_beats.beat_type` |
| 伏笔状态机 | `foreshadowings` |
| 秘密/知情 | `secrets` + `secret_knowledge` + 泛化 `character_knowledge` |
| 时间线 | `calendar_*` + 各表 `chapter_id`/`time_label`/`ord` |
| 经济 | `economy_details` + `currency` + `price_samples` |
| 语言文化习俗禁忌 | `culture_details` + `customs` |
| 宗教神话 | `religion_details` |
| 主题/母题 | `themes` + `motifs`（含再现字段） |
| 叙事层级 Volume→Beat | `volumes` + `chapters` + `scenes` + `plot_beats` |
| Scene 全字段 | `scenes` + `scene_cast` + `scene_foreshadows` |
| 情绪 | `character_status.emotion_json` + `emotion_events` |
| 资源 | `resource_holdings` |
| 对话风格 | `dialogue_styles` |
| 文风/POV | `writing_style` + `scenes.pov_entity_id` |
| 信息节奏 | `mysteries` + `mystery_beats` |
| 冲突六型 | `conflict_details` |
| 作者规则/Canon/版本/依赖/审计 | `author_rules` `canon_logs` `entity_versions` `dependencies` `audit_logs` |

#### 核心表（骨架）

在 novel-studio 已定表上扩展（`entities` / `entity_images`(一张 sheet) / `entity_ownerships` / `entity_events` …）：

```sql
-- ── 人物 ──────────────────────────────────────────
entity_personas(
  entity_id PRIMARY KEY,
  name_ish TEXT, age TEXT, appearance, personality, background,
  values, desire, goal, fear, weakness, strength,
  ability_note, knowledge_note, memory_note)

character_arcs(id, entity_id, ord, stage, trigger_event_id, note)
-- stage 例：懦弱少年|第一次杀人|心理变化|保护同伴|价值观冲突|重大选择|蜕变

character_status(id, entity_id, chapter_id, location_id,
  body_state, mind_state, emotion_json,  -- {"fear":70,"anger":30,"trust":20}
  goal, relation_note, resource_note, secret_note, updated)

emotion_events(id, entity_id, chapter_id, event_id,
  dim TEXT,  -- fear|anger|trust|sadness|hope…
  delta INTEGER, note)

character_knowledge(id, entity_id, fact_kind, fact_id, fact_text,
  knows INTEGER, chapter_known)  -- 泛化知情：不限于 secret

dialogue_styles(entity_id, sentence_len, vocabulary, catchphrase, taboo_words, habit)

relations(id, from_id, to_id, rel_type, strength,
  from_chapter, to_chapter, reason, status)

-- ── 世界基础 ──────────────────────────────────────
world_meta(key, value)  -- 时代/文明总述等键值；细粒度用下列详情表
universe_details(entity_id, parent_id, kind, note)     -- world|plane|planet|isekai
world_rule_details(entity_id, can_do, cannot_do, cost)
history_details(entity_id, era, summary, related_json)
society_details(entity_id, class_note, system_note, law, custom)
tech_details(entity_id, level, transport, comm, productivity)
culture_details(entity_id, art, food, dress, taboo_note)
language_details(entity_id, script, speakers_faction_id, sample)
religion_details(entity_id, gods_json, doctrine, church_faction_id,
  relics_json, rituals_json, taboos_json, heresy_note, myth_note)
economy_details(entity_id, note)
currency(id, name, symbol, approx_value_note)
price_samples(id, item_name, currency_id, amount, era_note, chapter_id)

calendar_eras(id, name, start_note, end_note)
calendar_seasons(id, era_id, name, ord)
-- 事件/场景上的 time_label 用自由文本对齐纪元+季节

-- ── 地点 / 势力 / 能力 / 物品 / 资源 ──────────────
location_details(entity_id, parent_id, loc_type,
  -- continent|country|city|town|village|building|room|dungeon|realm|battlefield|special
  geo_note, weather, environment, population, danger,
  resource_note, special_rule, history_note, controlling_faction_id)

location_distances(id, from_id, to_id, distance_km, travel_note, days_estimate)

faction_details(entity_id, faction_type,
  -- nation|royal|sect|church|company|family|army|gang|academy|org|secret
  leader_id, base_location_id, goal, values, politics,
  military, economy_note, secret, history_note)

faction_members(id, faction_id, entity_id, role, from_chapter, to_chapter)

power_system_details(entity_id, note)
ability_details(entity_id, system_id, ability_type,  -- skill|spell|realm|class|talent
  level, attr_note, cost, limit, side_effect)
-- 克制：relations(from=ability, to=ability, rel_type='counter')

item_details(entity_id, item_type,
  -- weapon|armor|accessory|artifact|consumable|drug|food|book|letter|key|treasure|relic|misc
  origin, maker, props, power, appear_chapter, state, secret_note)

resource_holdings(id, owner_id, resource_id, amount, chapter_id, note)
-- resource 实体：money|weapon|food|drug|mana|energy|manpower|army|info

-- ── 事件 / 因果 ───────────────────────────────────
event_details(entity_id, time_label, location_id, cause_note, result_note)
event_participants(event_id, entity_id, role)  -- actor|victim|witness|faction…
causal_links(id, cause_event_id, effect_event_id,
  link_type,  -- causes|enables|prevents|escalates|reveals
  note, ord)

-- ── 叙事结构 ──────────────────────────────────────
volumes(id, title, ord, summary)
chapters(id, volume_id, ord, title, status, summary, body, pov_entity_id, words, updated)
-- novel-studio 的 chapters 并入 volume_id 列

scenes(id, chapter_id, ord, title, location_id, time_label, pov_entity_id,
  conflict_id, goal, action, conflict, result, emotion,
  info_reveal, hook, body)
scene_cast(scene_id, entity_id, role)
scene_foreshadows(scene_id, foreshadowing_id, action)  -- plant|develop|payoff

plots(id, kind,  -- main|sub|character|romance|mystery|political
  title, status, intro_ch, target_ch, note)
plot_beats(id, plot_id, chapter_id, ord, beat_type,
  -- setup|rising|turning_point|climax|falling|resolution|revelation
  title, summary, cast_json)

themes(id, title, statement, linked_plot_id)
motifs(id, symbol, meaning, first_ch, last_ch, transform_note)

conflict_details(entity_id, conflict_type, side_a_json, side_b_json,
  cause, goal_a, goal_b, escalation, resolution, consequence,
  intro_ch, peak_ch, end_ch)

writing_style(id CHECK(id=1), pov_mode, sentence_len, density,
  dialogue_ratio, action_ratio, thought_ratio, env_ratio,
  humor, serious, pacing, note)

-- ── 谜 / 伏笔 / 秘密 / 知情 ───────────────────────
foreshadowings(id, title, content, status,  -- PLANNED|PLANTED|DEVELOPING|REVEALED|RESOLVED
  setup_ch, payoff_ch, importance, truth, entity_ids_json)
secrets(id, content, truth, reveal_ch, reveal_condition, entity_id, scope)
  -- scope: world|character|faction
secret_knowledge(secret_id, entity_id, knows, chapter_known)
mysteries(id, entity_id, question, answer, status, ask_ch, answer_ch, importance)
mystery_beats(id, mystery_id, beat_type, chapter_id, content, target_entity_id, ord)
  -- question|hint|reveal|answer|red_herring

-- ── 记忆 / 系统 ───────────────────────────────────
memories(id, kind, entity_id, chapter_id, content, summary, embedding_blob, created)
  -- kind: short_term|long_term|semantic|episodic|chapter_summary|dialogue|event…
entity_versions(id, entity_id, ver, snapshot_json, note, created)
dependencies(id, from_kind, from_id, to_kind, to_id, dep_type, note)
author_rules(id, rule, severity, note)
canon_logs(id, target_kind, target_id, status, note, created)
audit_logs(id, actor, action, target_kind, target_id, detail, created)
```

图片：`工程/assets/<entity_id>/sheet.png` 一张四视图；库内相对路径。  
写路径：Agent 变更默认 `canon_logs=PROPOSED`，作者确认后 `CANON`；所有写操作记 `audit_logs`。

#### [S2.3.a] 因果系统（Causality）

```sql
causal_links(
  id, cause_event_id, effect_event_id,
  link_type CHECK(link_type IN ('causes','enables','prevents','escalates','reveals')),
  note, ord)
```

- DAG，应用层禁环；工具 `get_event_chain(event_id, depth)`。
- `entity_events` = 档案履历；`causal_links` = 世界线推演边。

#### [S2.3.b] 冲突系统（Conflict）

六型 + 双方 + 目标 + 升级阶段 + 解决与后果（见 `conflict_details`）。  
场景挂 `scenes.conflict_id`；Critic 查阶段跳跃与已结冲突仍推主线。

#### [S2.3.c] 信息节奏（Information Flow）

`mysteries` + `mystery_beats`（question/hint/reveal/answer/red_herring）。  
与 `foreshadowings`（作者埋点）、`secret_knowledge`（谁知道）三者分工；`reveal` 节拍回写知情表。POV 未到揭示节拍则 Writer 禁止写穿。

#### [S2.3.d] 依赖关系（Dependency Graph）

`dep_type`: `requires` | `mentions` | `constrains` | `invalidates`。  
编辑后沿边产出「待复查章节/实体」，不自动改正文。

#### [S2.3.e] 时间线（Timeline）

| 层 | 字段 |
|----|------|
| 世界历 | `calendar_eras` + `time_label` |
| 卷/章/场/拍 | `volumes.ord` … `plot_beats.ord` |
| 人生 | `character_arcs` + `character_status.chapter_id` |
| 移动 | `location_distances` + 事件/状态上的 location |

Critic：位置冲突、已死仍出场、旅行时间不合理。

#### [S2.3.f] 能力克制

`relations(rel_type='counter')` 连接 ability；PowerSystem 经 `ability_details.system_id` 归属。禁止无限制新能力：必须落在某 system 且遵守 `limit`/`side_effect`。

#### [S2.3.g] 核心图查询

```
person ─relation─ person
  │                  │
 participate        participate
  ▼                  ▼
event ──causal_links──► event
  │                      │
 occur_at / cause      affects
  ▼                      ▼
location / conflict ──drives── plot ──contains── beat / scene
  │                                 │
 seat / member                    plants / pays
  ▼                                 ▼
faction ──owns── item ──linked── foreshadowing / mystery / secret ──truth─►
```

### [S2.4] 记忆与上下文

| 层 | 内容 |
|----|------|
| L1 | 当前任务、当前章、选中实体 |
| L2 | 前 1–3 章摘要 |
| L3 | 相关人物状态、地点、势力、未回收伏笔、持有 |
| L4 | 长期事件、主题、按 `secret_knowledge` 过滤后的秘密 |

`ContextBuilder::Build(chapter_id, task)` → 中文 prompt 段（模板文件 `prompts/*.md`，运行时读，不写死在 cpp）。  
原则：相关切片优先，不全量灌库；POV 不知情的秘密不得进入该章 context。

### [S2.5] Agent 管线

```
IDLE → ANALYZE → PLAN → RETRIEVE → WRITE → REVIEW
     → REVISION(≤3) → EXTRACT → SAVE → DONE
失败 → RETRY → FAILED
```

| 角色 | 职责 | 输出 |
|------|------|------|
| Planner | 章目标、场景表、冲突、伏笔、结尾钩子 | 结构化 JSON |
| Writer | 按 plan + context 写正文（可 stream） | 文本 → `chapters.body` |
| Critic | 人设/世界观/时间线/能力/POV/伏笔 | issues 列表 |
| Extractor | 新实体、关系、事件、记忆、状态快照 | 写 graph |

限制：`max_tool_calls=20`，`max_revisions=3`；同一 tool 连续重复达阈值则中止并报错。

### [S2.6] Local Tools（第一期只读 + 受控写）

```
get_entity  list_entities  get_relations  get_ownership
get_chapter get_recent_chapters
get_location  travel_estimate
get_foreshadows  get_secrets_for
get_event_chain          -- 因果链 depth 跳
get_conflict  list_open_conflicts
get_mystery  list_open_mysteries
list_dependencies        -- 改设定后的待复查
get_world_slice          -- 规则/宗教/文化/经济摘要
get_writing_style  get_dialogue_style
list_resource_holdings
search_memory
upsert_entity  upsert_event  link_relation  link_causal   # 写 → PROPOSED
```

`ToolRegistry` 导出 Responses `tools` schema；执行在 worker；结果 JSON 回灌对话。

### [S2.7] UI 接入

- 小说工作区：`生成本章`、进度、流式正文预览（`PostToUi`）
- 图谱页：关系/事件/伏笔浏览
- 与四视图 sheet、持有跳转共用同一套实体 ID

### [S2.8] 配置

`openaiBaseUrl` / `openaiApiKey` / `openaiModel*` / `agentMaxToolCalls` / `agentMaxRevisions` / `memoryTopK`

## [S3] Out of Scope

- MCP Server/Client、外部向量库、Web UI、多 Agent 全阵容
- 全书百章自动巡检（先单章管线）
- 引入 libcurl / nlohmann / 第三方 OpenAI 绑定
- 改 `third/`

## 阶段任务

> 完整分步、字段级施工图见 `docs/compose/plans/novel-agent/`（P1–P7 及 P2 六域分册）。下表仅阶段门禁摘要。

| 阶段 | 门禁 |
|------|------|
| P1 | 同步+流式 Responses 可用 |
| P2 | 六域表 + 因果/冲突/谜/依赖可查（B1–B12 见 P2 分册） |
| P3 | ContextBuilder L1–L4 + 知情过滤 |
| P4 | Tools + calling loop |
| P5 | Plan→Write→Review→Extract→Save |
| P6 | ImGui 生成入口 |
| P7 | MVP 剧本通过 |

## Tasks

- [ ] T1: 按 `P1-openai/PLAN.md` 交付客户端 (covers: S2.2)
- [ ] T2: 按 `P2-graph/**` 交付图谱全量 (covers: S2.3)
- [ ] T3: 按 `P3-memory` + `P4-tools` (covers: S2.4–S2.6)
- [ ] T4: 按 `P5-agent` 管线 (covers: S2.5)
- [ ] T5: 按 `P6-ui` + `P7-mvp` (covers: S2.7)
- [ ] T6: prompts/ 与 skill/文档同步 (covers: S2.1)
