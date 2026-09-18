# 小说板块 · 当前写死变量/字段清单

> 目的：把现有 schema / C++ 类型 / 工具签名里**写死的体系字段**全部摊开，供你指定「哪些应由 AI 自己生成并存储，再通过 MCP 读取理解」，而不是继续用固定表结构限定小说体系。
>
> 代码落点：
> - 类型：`src/novel/NovelTypes.h`
> - Schema：`src/novel/NovelDb.cpp`（v3 + v4 视觉表）
> - 图谱 API：`src/novel/NovelGraph.h`
> - 上下文：`src/agent/ContextBuilder.h`
> - 本地工具 / MCP 语义：`src/agent/BuiltinTools.cpp`、`docs/compose/plans/novel-agent/P10-mcp/PLAN.md`
> - 设计契约：`docs/compose/spec/novel-agent.md`（S2.3 六域）
>
> 说明：列在「枚举/默认值」列的字符串，在代码或设计文档里被当作**固定词表**使用；正文 TEXT 列虽然可写任意文案，但**表结构本身仍是按某种小说体系预切的**，新体系（多宇宙、卧底人设层、修仙境界表等）没有独立扩展槽。

---

## 0. 总览：六域 + 视觉 + 系统

| 域 | 主实体 kind（写死） | 详情表（写死列） |
|----|---------------------|------------------|
| 世界 | universe, world_rule, history, culture, language, religion, economy, tech, society, calendar | universe_details / world_rule_details / history_details / society_details / tech_details / culture_details / language_details / religion_details / economy_details / calendar_* |
| 人物 | person, creature | entity_personas / character_arcs / character_status / emotion_events / dialogue_styles / character_knowledge / relations |
| 物品地点势力能力 | clothing, prop, treasure, item, location, faction, power_system, ability, resource | item_details / location_details / location_distances / faction_details / faction_members / ability_details / power_system_details / resource_holdings |
| 叙事 | event, plot, arc, conflict, theme, motif, ending | volumes / chapters / scenes / plots / plot_beats / themes / motifs / conflict_details / causal_links / writing_style |
| 谜与知情 | secret, foreshadowing, mystery | foreshadowings / secrets / secret_knowledge / mysteries / mystery_beats |
| 系统 | （元数据） | meta / memories / entity_versions / dependencies / author_rules / canon_logs / audit_logs |
| 视觉 | visual_assets.kind 写死 | visual_assets / visual_states / scene_visuals / scene_layouts / camera_defs / composition_defs / lighting_defs / visual_styles / prompt_layers / shots |

**你已指出的问题（与清单对应）**：

| 问题 | 现状写死点 | 例子 |
|------|------------|------|
| 世界观可多层、多宇宙，甚至不同章不同宇宙 | `universe_details.kind` 固定 `world\|plane\|planet\|isekai`；`GetWorldSlice()` 固定拼 rules/locations/factions/powerSystems | 后期发现「多元宇宙 / 平行线 / 时间层」时，kind 不够用，也没有「章节级世界观绑定」表 |
| 人物多面性（表面反派、背地卧底） | `entity_personas` 固定单行 persona；`relations.rel_type` 固定词表；秘密只能走 `secrets`+`secret_knowledge` | 没有「同一角色多套身份/面具/立场层」的通用字段，AI 无法自行新增「双面身份」结构 |
| 不同小说不同力量/经济/宗教体系 | 各 `*_details` 列名按网文常见结构预切 | 修仙「境界-灵根-丹田」、克苏鲁「理智-眷属」、赛博「义体-黑市」塞不进固定列时只能塞 prose |
| AI 应自己生成字段并存储，再经 MCP 取回理解 | MCP tool 名/参数 schema 写死；`WorldSlice`/`CharacterSlice` 返回结构写死 | AI 无法「新建一种自定义维度」并让后续生成自动读到 |

---

## 1. 实体 kind 全量（`src/novel/NovelTypes.h` `namespace kind`）

| # | kind 常量 | 设计域 | 现状角色 |
|---|-----------|--------|----------|
| 1 | `universe` | 世界 | 宇宙/位面/世界节点 |
| 2 | `world_rule` | 世界 | 宪法规则（能/不能/代价） |
| 3 | `history` | 世界 | 历史事件/纪元 |
| 4 | `culture` | 世界 | 文化 |
| 5 | `language` | 世界 | 语言 |
| 6 | `religion` | 世界 | 宗教神话 |
| 7 | `economy` | 世界 | 经济 |
| 8 | `tech` | 世界 | 科技 |
| 9 | `society` | 世界 | 社会制度 |
| 10 | `calendar` | 世界 | 纪元/历法 |
| 11 | `person` | 人物 | 人物 |
| 12 | `creature` | 人物 | 生物/非人 |
| 13 | `clothing` | 装备 | 服饰 |
| 14 | `prop` | 装备 | 道具 |
| 15 | `treasure` | 装备 | 法宝/珍品 |
| 16 | `item` | 装备 | 物品（子类靠 item_details） |
| 17 | `location` | 空间 | 地点 |
| 18 | `faction` | 势力 | 势力/组织 |
| 19 | `power_system` | 能力 | 力量体系 |
| 20 | `ability` | 能力 | 技能/法术/境界 |
| 21 | `event` | 叙事 | 事件 |
| 22 | `plot` | 叙事 | 剧情线 |
| 23 | `arc` | 叙事 | 弧线（也可落在 character_arcs） |
| 24 | `conflict` | 叙事 | 冲突 |
| 25 | `theme` | 叙事 | 主题 |
| 26 | `motif` | 叙事 | 母题/意象 |
| 27 | `ending` | 叙事 | 结局 |
| 28 | `secret` | 谜 | 秘密 |
| 29 | `foreshadowing` | 谜 | 伏笔 |
| 30 | `mystery` | 谜 | 谜团/信息节奏 |
| 31 | `resource` | 资源 | 资源 |

**写死点**：kind 是**编译期常量列表**，新体系节点类型（例如「血脉图腾」「天道层级」「宇宙通道」「卧底任务线」）只能塞进已有 kind 或改代码。

`EntityRow` 通用列（所有 kind 共用，相对可扩展）：

| 字段 | 类型 | 默认/说明 |
|------|------|-----------|
| id | RowId | |
| kind | string | 见上表，写死词表 |
| name | string | |
| summary | string | 自由文本 |
| status | string | 默认 `active`；设计注释 `active\|dead\|destroyed\|archived` |
| meta_json | string | 默认 `{}`（**唯一现成的动态扩展槽**） |
| created_chapter | RowId | |
| updated | int64 | |

---

## 2. 人物域写死字段

### 2.1 `entity_personas` / `PersonaRow`（人设，单行、列写死）

| 字段 | 设计含义 | 写死问题示例 |
|------|----------|--------------|
| entity_id | PK，kind=person | |
| age | 年龄 | 可用 |
| appearance | 外貌 | 可用 |
| personality | 性格 | **单值**，表达不了「表里两套性格」 |
| background | 背景 | |
| values | 价值观 | |
| desire | 欲望 | |
| goal | 目标 | 单目标，多线目标/伪装目标不好拆 |
| fear | 恐惧 | |
| weakness | 弱点 | |
| strength | 优点 | |
| ability_note | 能力概述 | 细能力靠 ability 实体，体系本身仍写死 |
| knowledge_note | 知识概况 | |
| memory_note | 重要记忆 | |

→ 与你举的「反派其实是卧底」直接冲突：没有 `identity_layers` / `masks` / `true_faction` 之类字段，只能靠 secrets 或 prose。

### 2.2 `character_arcs`（弧光阶段）

| 字段 | 默认/枚举 | 说明 |
|------|-----------|------|
| ord | | 阶段序 |
| stage | 文案，设计示例写死 | 懦弱少年 / 第一次杀人 / 心理变化 / 保护同伴 / 价值观冲突 / 重大选择 / 蜕变 |
| trigger_event_id | | 触发事件 |
| note | | |

→ stage 是自由文本列，但**示例阶段链按成长型主角预设**；多面人格、卧底双线弧光没有专用结构。

### 2.3 `character_status` / `CharacterStatusRow`（章级状态快照）

| 字段 | 默认/枚举 | 说明 |
|------|-----------|------|
| entity_id, chapter_id, location_id | | |
| body_state | 例：受伤 | |
| mind_state | 例：恐惧 | |
| emotion_json | `{}`；设计示例键写死 | `fear/anger/trust/sadness/hope…` |
| goal | | |
| relation_note | | |
| resource_note | | |
| secret_note | | |

### 2.4 `emotion_events`

| 字段 | 枚举写死 |
|------|----------|
| dim | `fear` / `anger` / `trust` / `sadness` / `hope` …（设计词表） |
| delta | 增减 |
| note | |

→ 情绪维度写死；不同小说可有「理智」「宿命值」「狂化」等自定义维度。

### 2.5 `relations` / `RelationRow`

| 字段 | 默认/枚举 |
|------|-----------|
| from_id, to_id | 有向 |
| **rel_type** | **写死示例**：`friend/enemy/love/master/family/rival/debt/ally/control/counter…`（注释里还有 secret 等） |
| strength | 0–100，默认 50 |
| from_chapter, to_chapter | 时间线，to=0 至今 |
| reason | |
| status | `active\|ended` |

→ 「表面敌对、实际同盟/卧底上下线」无法用类型表达，只能拆多条边 + 文案 reason。

### 2.6 `dialogue_styles`

| 字段 | 示例 |
|------|------|
| sentence_len | 短句 |
| vocabulary | 词汇倾向 |
| catchphrase | 口头禅 |
| taboo_words | 忌语 |
| habit | 少废话/反问/隐喻 |

### 2.7 `character_knowledge`

| 字段 | 说明 |
|------|------|
| fact_kind | `secret` / `world_rule` / `event` / `item`…（设计词表） |
| fact_id | |
| fact_text | |
| knows | 0/1 |
| chapter_known | |

---

## 3. 世界域写死字段

### 3.1 世界类实体 → 详情表（列名即体系假设）

| 表 | 写死列 | 枚举/默认 | 体系假设 |
|----|--------|-----------|----------|
| `universe_details` | parent_id, **kind**, note | kind: `world\|plane\|planet\|isekai` | 单一世界树；无「章节绑定哪一宇宙」 |
| `world_rule_details` | can_do, cannot_do, cost | | 三元规则模型 |
| `history_details` | era, summary, related_json | | |
| `society_details` | class_note, system_note, law, custom | | 阶级/制度/法/俗 |
| `tech_details` | level, transport, comm, productivity | | 工业文明四件套 |
| `culture_details` | art, food, dress, taboo_note | | 饮食服饰艺术 |
| `language_details` | script, speakers_faction_id, sample | | |
| `religion_details` | gods_json, doctrine, church_faction_id, relics_json, rituals_json, taboos_json, heresy_note, myth_note | | 神祇-教义-教会-圣物-仪式 |
| `economy_details` | note | | 总述塞 prose |
| `currency` | name, symbol, approx_value_note | | |
| `price_samples` | item_name, currency_id, amount, era_note, chapter_id | | |
| `calendar_eras` | name, start_note, end_note | | |
| `calendar_seasons` | era_id, name, ord | | |
| `world_meta` | key, value | | 仅松散键值 |

### 3.2 `WorldSlice` / `GetWorldSlice()`（给 ContextBuilder 的切片，结构写死）

```
WorldSlice {
  rules          // vector<EntityRow>  // world_rule
  locations      // vector<EntityRow>
  factions       // vector<EntityRow>
  powerSystems   // vector<EntityRow>
}
```

→ 不包含多宇宙列表、不包含「当前章激活的世界规则子集」、不包含 AI 自定义维度。

### 3.3 与「多宇宙 / 分章世界观」的缺口

| 你需要的能力 | 现状 |
|--------------|------|
| 多个 universe 实体并存 | 可以建多个 kind=universe 行，但 kind 只能是 `world/plane/planet/isekai` |
| 同一书不同章不同世界观 | **没有** chapter ↔ universe/rules 绑定表 |
| 后期揭示「其实有多重宇宙」 | 无版本/揭示节拍与 world 绑定；只能挂 secrets(scope=world) 的 prose |
| 某规则只在某宇宙/某卷生效 | world_rule 无 from_chapter/to_chapter 或 universe_id |

---

## 4. 物品 / 地点 / 势力 / 能力 / 资源

### 4.1 `item_details`

| 字段 | 枚举写死 |
|------|----------|
| **item_type** | `weapon\|armor\|accessory\|artifact\|consumable\|drug\|food\|book\|letter\|key\|treasure\|relic\|misc` |
| origin, maker, props, power | |
| appear_chapter | |
| state | 完好/破损… |
| secret_note | |

### 4.2 `location_details`

| 字段 | 枚举写死 |
|------|----------|
| parent_id | 层级：世界→大陆→国→城→建筑→房间（设计固定树） |
| **loc_type** | `continent\|country\|city\|town\|village\|building\|room\|dungeon\|realm\|battlefield\|special` |
| geo_note, weather, environment, population, danger | |
| resource_note, special_rule, history_note | |
| controlling_faction_id | |

`location_distances`：from_id, to_id, distance_km, travel_note, days_estimate。

### 4.3 `faction_details` / `faction_members`

| 字段 | 枚举写死 |
|------|----------|
| **faction_type** | `nation\|royal\|sect\|church\|company\|family\|army\|gang\|academy\|org\|secret` |
| leader_id, base_location_id | |
| goal, values, politics | |
| military, economy_note | |
| secret, history_note | |

`faction_members`：faction_id, entity_id, role, from_chapter, to_chapter。  
势力敌盟设计复用 `relations(rel_type=ally/enemy/control/secret…)`——**仍受 rel_type 词表限制**。

### 4.4 `power_system_details` / `ability_details`

| 表 | 字段 | 枚举写死 |
|----|------|----------|
| power_system_details | entity_id, note | 体系细节几乎全靠 note |
| ability_details | system_id, **ability_type**, level, attr_note, cost, limit, side_effect | ability_type: `skill\|spell\|realm\|class\|talent` |

克制：`relations(rel_type='counter')`。  
→ 「印共鸣 / 灵根 / 理智骰 / 义体槽」等非 skill/spell 体系没有结构化槽位。

### 4.5 `resource_holdings` / 资源类型

| 字段 | 说明 |
|------|------|
| owner_id, resource_id, amount, chapter_id, note | |

设计注释的资源类型写死：`money|weapon|food|drug|mana|energy|manpower|army|info`。

---

## 5. 叙事结构 / 冲突 / 因果 / 文风

### 5.1 层级表

| 表 | 写死字段 | 枚举 |
|----|----------|------|
| volumes | title, ord, summary | |
| chapters | volume_id, ord, title, status, summary, body, pov_entity_id, words, updated | status: `draft\|writing\|review\|done` |
| scenes | chapter_id, ord, title, location_id, time_label, pov_entity_id, conflict_id, goal, action, conflict, result, emotion, info_reveal, hook, body | 场景分析模型写死为 goal-action-conflict-result… |
| scene_cast | scene_id, entity_id, role | role 自由但无词表约束 |
| scene_foreshadows | scene_id, foreshadowing_id, **action** | `plant\|develop\|payoff` |

### 5.2 `plots` / `plot_beats`

| 字段 | 枚举写死 |
|------|----------|
| plots.kind | `main\|sub\|character\|romance\|mystery\|political` |
| plots.status | 默认 `active` |
| plot_beats.beat_type | `setup\|rising\|turning_point\|climax\|falling\|resolution\|revelation` |

### 5.3 `conflict_details`

| 字段 | 枚举写死 |
|------|----------|
| **conflict_type** | `person_vs_person`（DB 默认）；设计文档：`person_person / person_self / person_society / person_nature / person_state / person_world` |
| side_a_json, side_b_json | |
| cause, goal_a, goal_b | |
| escalation | 设计：`latent→emerging→open→climax→resolved/stalemate`（列是 TEXT，状态机在应用层） |
| resolution, consequence | |
| intro_ch, peak_ch, end_ch | |

### 5.4 `causal_links` / `EventDetailRow` / `CausalLinkRow`

| 字段 | 枚举写死 |
|------|----------|
| causal_links.**link_type** | `causes\|enables\|prevents\|escalates\|reveals`（DB 默认 `causes`） |
| note, ord | |

`event_details`：time_label, location_id, cause_note, result_note。  
`event_participants`：event_id, entity_id, role（`actor|victim|witness|faction…` 设计词表）。

### 5.5 `themes` / `motifs` / `writing_style`

| 表 | 字段 | 枚举/默认 |
|----|------|-----------|
| themes | title, statement, linked_plot_id | |
| motifs | symbol, meaning, first_ch, last_ch, transform_note | |
| **writing_style** | **单行 id=1** | pov_mode 默认 `third_limited`；设计：`first / third_limited / omniscient / multi_pov`；sentence_len 默认 `medium`；dialogue/action/thought/env ratio 默认 0.3/0.3/0.2/0.2；humor 默认 0；serious 默认 50 |

→ 全书**一套**文风；分卷/分 POV 角色/分宇宙不同文风没有结构。

---

## 6. 谜 / 伏笔 / 秘密 / 知情

| 表 | 关键写死字段 | 枚举 |
|----|--------------|------|
| foreshadowings | status | **状态机**：`PLANNED\|PLANTED\|DEVELOPING\|REVEALED\|RESOLVED` |
| | setup_ch, payoff_ch, importance, truth, entity_ids_json | |
| secrets | **scope** | `world\|character\|faction` |
| | content, truth, reveal_ch, reveal_condition, entity_id | |
| secret_knowledge | secret_id, entity_id, knows, chapter_known | |
| mysteries | **status** | 设计：`open\|developing\|answered\|abandoned` |
| | question, answer, ask_ch, answer_ch, importance, entity_id | |
| mystery_beats | **beat_type** | `question\|hint\|reveal\|answer\|red_herring` |

`ForeshadowRow` C++ 与表一致；`SecretRow.scope` 默认 `character`。

→ 多面人物（卧底）可以借用 secrets，但**没有**「身份层 / 公开伪装 / 真实效忠」这类可被工具结构化检索的字段。

---

## 7. 记忆 / Canon / 依赖 / 规则 / 审计

| 表 | 写死字段 | 枚举 |
|----|----------|------|
| memories | kind | 设计：`short_term\|long_term\|semantic\|episodic\|chapter_summary\|dialogue\|event…`；DB 默认 `short_term` |
| | entity_id, chapter_id, content, summary, embedding_blob, created | embedding 列已预留，检索仍未接 |
| entity_versions | entity_id, ver, snapshot_json, note, created | 快照是 JSON，粒度未定义 |
| dependencies | from_kind/from_id/to_kind/to_id/dep_type/note | dep_type: `requires\|mentions\|constrains\|invalidates`（默认 `mentions`） |
| author_rules | rule, severity, note | severity 默认 `warn` |
| canon_logs | target_kind, target_id, status | 默认 `PROPOSED`；设计链 PROPOSED→CANON |
| audit_logs | actor, action, target_kind, target_id, detail | actor 默认 `user` |

---

## 8. 视觉体系（NovelDb v4 + NovelVisual）

### 8.1 `visual_assets` / `VisualAssetRow`

| 字段 | 默认/枚举写死 |
|------|----------------|
| **kind** | 默认 `character`；设计/C++：`character\|clothing\|item\|location\|prop` |
| entity_id, name | |
| base_desc, materials_colors | |
| permanent_tags_json | 默认 `[]` |
| sheet_rel_path | 四视图路径 |
| **canon_status** | 默认 `DRAFT` |
| note | |

### 8.2 `visual_states` / `VisualStateRow`（阶段）

| 字段 | 说明 |
|------|------|
| stage_key, stage_label | 自由文本；**设计示例阶段链写死**：人物 少年→…→巅峰；法宝 普通→封印→…→最终形；城市 和平→…→重建 |
| ord, from_chapter, to_chapter | 区间解析规则写死：from≤N 且 (to=0 或 to≥N)，取 from 最大 |
| appearance, materials_colors, clothing_asset_id, item_asset_ids_json, effects, environment_hint | |
| canon_status | 默认 `DRAFT` |

自检样例写死 stage_key：`youth` / `wounded`。

### 8.3 分镜与镜头定义表

| 表 | 写死字段 / 默认 |
|----|-----------------|
| scene_visuals | env_desc, time_of_day, weather, mood, canon_status=DRAFT |
| scene_layouts | layout_name, layout_json, note |
| camera_defs | name, **shot_size 默认 medium**, **angle 默认 eye**, lens_note, **movement 默认 static**, text |
| composition_defs | name, **rule 默认 rule_of_thirds**, framing, text |
| lighting_defs | name, time_hint, key_light, mood, text |
| visual_styles | name 默认 `default`, payload_json, text |
| shots / ShotRow | ord, duration_note, camera_id, character_ids_json, action, expression, prop_ids_json, lighting_id, composition_id, dialogue, narration, sfx, mood, prompt_text, negative_text, reference_json, canon_status 默认 **PROPOSED** |
| visual_canon_logs | target_kind, target_id, status 默认 PROPOSED |
| prompt_layers | owner_kind 默认 `asset`, **layer 词表写死**：`base/stage/scene/action/camera/composition/lighting/style/quality/negative`（九层），text, model_hint, version, canon_status=DRAFT |

### 8.4 `Assemble` 组装顺序（代码写死）

`NovelVisual::Assemble` 拼接顺序固定：Base → Stage → Scene → Action → Camera → Composition → Lighting → Style → Quality → Negative。  
新层级（例如「宇宙滤镜」「阵营徽记」「卧底伪装层」）不在九层内。

---

## 9. MCP / 本地工具（字段与 schema 写死）

### 9.1 已实现 Local Tools（`BuiltinTools.cpp`）

| Tool 名 | 参数（写死） | 返回结构（写死） |
|---------|--------------|------------------|
| get_entity | id / name | id, kind, name, summary, status（**不含 meta_json**） |
| list_entities | kind, filter | id, kind, name |
| get_relations | entity_id | from, to, type, strength, status |
| get_chapter | id | id, title, summary, body(截断 800), words |
| get_recent_chapters | k（默认 3） | id, title, summary |
| get_foreshadows | （无） | id, title, status |
| get_secrets_for | entity_id | id, content（知情过滤） |
| get_event_chain | event_id, depth（默认 3） | cause, effect, type |
| get_ownership | entity_id | item_id, from_ch, note |
| upsert_entity | **kind**, name, summary, id | id；写 canon PROPOSED |
| link_relation | from_id, to_id, **rel_type** | id |
| link_causal | cause_event_id, effect_event_id, **link_type** | id |

说明：`upsert_entity.kind` 是自由 string，但下游查询切片、ContextBuilder、设计文档仍按六域 kind 理解；**没有**「AI 声明自定义字段/自定义维度并被结构化存取」的工具。

### 9.2 规划中 MCP Tool（P10，尚未全部落地）

novel_get_entity / list_entities / get_relations / ownership / event_chain / get_chapter / recent_chapters / get_foreshadows / secrets_for / mysteries / search_memory / **get_world_slice** / **get_character_slice** / get_visual_asset / assemble_prompt_preview / novel_upsert_*

→ world_slice / character_slice 的**返回形状与 §3.2、§2 CharacterSlice 写死绑定**。

### 9.3 `CharacterSlice`（C++ 结构写死）

```
CharacterSlice {
  entity
  persona          // 单行 PersonaRow
  status           // 单行 CharacterStatusRow
  relations[]
  knowledge[]
  openForeshadows[]
}
```

→ 无身份层、无多宇宙归属、无自定义字段列表。

---

## 10. ContextBuilder L1–L4（组装逻辑写死）

| 层 | 内容（设计 + 实现方向） | 写死点 |
|----|-------------------------|--------|
| L1 | 当前任务、当前章、选中实体 | 输入只有 chapter_id / task / pov_entity_id / maxBytes |
| L2 | 前 1–3 章摘要 | 窗口固定 |
| L3 | 相关人物状态、地点、势力、未回收伏笔、持有 | 依赖固定 WorldSlice/CharacterSlice |
| L4 | 长期事件、主题、按 secret_knowledge 过滤后的秘密 | 过滤规则固定为「POV 不知情秘密不进」 |

输出：`ContextBuildOutput { text, used_entity_ids }` —— **没有**「本章激活的自定义字段块」槽位。

---

## 11. Agent 管线与配置（相关写死）

| 项 | 现状 |
|----|------|
| 管线状态 | `IDLE → ANALYZE → PLAN → RETRIEVE → WRITE → REVIEW → REVISION(≤3) → EXTRACT → SAVE → DONE`，失败 RETRY/FAILED |
| 限制 | max_tool_calls=20，max_revisions=3 |
| 角色 | Planner / Writer / Critic / Extractor 职责表固定 |
| 写路径 | Agent 变更默认 canon_logs=PROPOSED，作者确认后 CANON |
| Settings | novelRootDir；openai* / agentMaxToolCalls / agentMaxRevisions / memoryTopK 等 |

---

## 12. 「写死 vs 可动态」粗分类（供你勾选）

图例：  
- **硬枚举**：代码/文档当封闭词表  
- **半开放**：TEXT 可写，但表结构/切片不认新维度  
- **已有槽**：已预留 JSON 等扩展位  

| 块 | 字段/结构 | 类型 | 建议是否优先讨论改 AI 动态 |
|----|-----------|------|------------------------------|
| 实体 | kind 全量 31 个 | 硬枚举 | **高** — 新体系节点类型 |
| 人物 | persona 单行 13 列 | 半开放 | **高** — 多面身份/卧底 |
| 人物 | rel_type 词表 | 硬枚举 | **高** — 表面敌/真实同盟 |
| 人物 | emotion dim 词表 | 硬枚举 | 中 — 自定义情绪维度 |
| 世界 | universe_details.kind | 硬枚举 | **高** — 多宇宙类型 |
| 世界 | chapter↔世界观绑定 | **缺失** | **高** — 分章/分卷世界观 |
| 世界 | world_rule 无章区间/宇宙归属 | **缺失** | **高** — 规则分宇宙生效 |
| 世界 | tech/culture/religion 等详情列 | 半开放 | **高** — 非工业/非一神教体系 |
| 切片 | WorldSlice 四字段 | 硬结构 | **高** — MCP/AI 读到的形状 |
| 切片 | CharacterSlice 固定 | 硬结构 | **高** |
| 能力 | ability_type / power_system.note | 硬枚举+塞 prose | **高** — 修仙/克苏鲁等体系 |
| 物品 | item_type 词表 | 硬枚举 | 中 |
| 地点 | loc_type / 层级树 | 硬枚举 | 中 |
| 势力 | faction_type 词表 | 硬枚举 | 中 — 秘密结社/跨宇宙组织 |
| 叙事 | plots.kind / beat_type | 硬枚举 | 中 |
| 冲突 | conflict_type 六型 | 硬枚举 | 中 |
| 因果 | link_type 五词 | 硬枚举 | 中 |
| 文风 | writing_style 单行 + pov_mode | 硬枚举+单例 | 中 — 分宇宙/分角色文风 |
| 伏笔 | status 状态机 | 硬枚举 | 低（流程可保留） |
| 秘密 | scope 三词 | 硬枚举 | 中 — 身份/宇宙级秘密 |
| 谜 | mystery_beats.beat_type | 硬枚举 | 低 |
| 记忆 | memories.kind | 硬枚举 | 中 |
| 依赖 | dep_type | 硬枚举 | 低 |
| 视觉 | asset.kind / 九层 prompt | 硬枚举 | **高** — 新视觉层级 |
| 视觉 | camera/composition/light 默认值 | 硬枚举 | 中 |
| 工具 | tool 名与 JSON Schema | 硬结构 | **高** — 应支持 AI 生成字段的读写工具 |
| Context | L1–L4 组装 | 硬结构 | **高** — 应注入 AI 字段 |
| entity.meta_json | `{}` | **已有槽** | **高** — 可能是过渡方案 |
| memories.content / snapshot_json | 自由 JSON | **已有槽** | 中 |

---

## 13. 请你指定（回复时直接点编号或字段名即可）

例如：「1 的 kind、2.1 的 persona、3.1 的 universe kind、3.2 WorldSlice、9.1 的工具 —— 这些改成 AI 生成字段。」

建议你在下一条消息里尽量说清（我按你的说法改设计，而不是我替你定）：

1. **哪些块**改成「AI 自定义字段」（可多选，用 §12 的块名或章节号）。
2. **AI 字段怎么存**倾向：继续 `entities.meta_json` / 通用 `entity_fields` 键值表 / 每类可插拔 JSON schema / 完全自由的「字段注册表」。
3. **多宇宙 / 分章世界观**：字段挂在 universe 实体上、挂 chapter 上，还是单独「世界观层」实体？
4. **人物多面性**：用多条 identity/mask 记录 + 知情过滤，还是允许 AI 任意给人物加自定义维度？
5. **MCP 形态**：AI 是「写库时自己发明字段名」+「读库时 list 自定义字段」，还是仍提供少量固定工具、只是返回里带上动态字段？
6. **固定骨架要不要留**：kind 人物/地点/章节是否仍保留为系统级最小集，其余全部开放？

---

## 附录 · 代码入口速查

| 内容 | 路径 |
|------|------|
| kind 常量与 Row 结构 | `src/novel/NovelTypes.h` |
| v3/v4 建表 SQL | `src/novel/NovelDb.cpp` |
| 图谱 CRUD / Slice | `src/novel/NovelGraph.h` / `NovelGraph.cpp` |
| 记忆 | `src/novel/NovelMemory.h` |
| 视觉 | `src/novel/NovelVisual.h` / `.cpp` |
| 上下文 | `src/agent/ContextBuilder.h` / `.cpp` |
| 工具注册 | `src/agent/BuiltinTools.cpp`、`ToolRegistry.*` |
| 管线 | `src/agent/NovelDirector.*` |
| UI | `src/app/novel/NovelView.*` |
| 设计契约 | `docs/compose/spec/novel-agent.md` |
| 六域施工图 | `docs/compose/plans/novel-agent/P2-graph/**` |
| MCP 计划 | `docs/compose/plans/novel-agent/P10-mcp/PLAN.md` |
