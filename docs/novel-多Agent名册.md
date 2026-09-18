# ShineTV 小说 · 多 Agent 名册

> 设计原则：**体系不写死**。设定维度由 Agent 动态生成（`field_defs` / `entity_fields`），
> 其它 Agent 与 MCP 通过工具读回理解。人物多面性用 `layer`（mask/true），
> 分章不同世界观用 `chapter_scope`。

## 入口

| 项 | 位置 |
|----|------|
| 注册表 / 路由 / 执行 | `src/agent/AgentKit.h` / `AgentKit.cpp` |
| 动态字段 | `src/novel/NovelFields.h` / `.cpp` |
| schema | `novel.db` v5：`agent_defs` + `field_defs` + `entity_fields` |
| 自检 | `SHINE_NOVEL_GRAPH_CHECK=1` → `agents:ok` / `fields:ok` |

## 内置 Agent（15）

| agent_id | 名称 | 做什么 | 关键工具 |
|----------|------|--------|----------|
| `novel_writer` | 写小说 Agent | 章正文 / 章规划；先读动态字段再写 | get_entity, list_entity_fields, upsert_* |
| `character` | 创建人物 Agent | 人物实体 + 多面身份字段（mask/true） | upsert_entity, upsert_entity_field, upsert_field_def |
| `item` | 创建物品 Agent | 物品/法宝；属性走动态字段 | upsert_entity, upsert_entity_field |
| `world` | 世界观 Agent | 多宇宙、规则、分章宇宙 | upsert_entity, upsert_world 级字段 |
| `place` | 地点 Agent | 地点实体与地理自定义字段 | upsert_entity, upsert_entity_field |
| `faction` | 势力 Agent | 组织/宗门/敌盟（rel_type 可扩展） | upsert_entity, link_relation |
| `event` | 事件 Agent | 事件与因果描述 | upsert_entity, upsert_entity_field |
| `mystery` | 伏笔秘密 Agent | 揭示节奏 + 知情 layer | upsert_entity_field, list_entity_fields |
| `extract` | 抽取 Agent | 从正文抽实体/字段并落库 | upsert_entity, upsert_field_def |
| `review` | 审校 Agent | 对照动态字段查一致性 | list_entity_fields, list_field_defs |
| `memory` | 记忆检索 Agent | 按任务捞相关实体与字段 | list_entities, list_entity_fields |
| `visual` | 视觉 Agent | 外观阶段/伪装，不与 true 层混淆 | upsert_entity_field |
| `field_builder` | **添加字段 Agent** | 新维度：定义 + 写入 | **upsert_field_def**, upsert_entity_field |
| `agent_meta` | **Agent 更新 Agent** | 改其它 Agent 的 prompt/工具/启停，或注册新 Agent | **upsert_agent**, list_agents, get_agent |
| `router` | 调度 Agent | 按任务文本选 Agent | route_task |

## 内置字段种子（系统最小集，可扩展）

| key | scope | 用途 |
|-----|-------|------|
| `active_universe` | world | 当前宇宙 |
| `universe_layers` | world | 宇宙列表 JSON |
| `world_rules_active` | world | 激活规则 |
| `identity_layers` | person | 多面身份 JSON |
| `true_faction` | person | 真实效忠 |
| `public_mask` | person | 公开伪装 |
| `power_profile` | person | 自定义力量画像 |
| `custom_traits` | entity | 任意扩展 |
| `pov_knowledge_scope` | chapter | 本章知情 |
| `universe_of_chapter` | chapter | **本章宇宙（分章世界观）** |
| `routing_hints` | agent | 影响路由 |

## 工具（进程内 / 后续 MCP 同源）

**字段**：`list_field_defs` `upsert_field_def` `upsert_entity_field` `list_entity_fields`  
**图谱**：`get_entity` `list_entities` `upsert_entity` `link_relation`  
**元**：`list_agents` `get_agent` `upsert_agent` `route_task`

## 用法示意

```
任务 → router.Route(text) → 某 Agent
         ↓
  BuildSystemPrompt(agent)  // 注入：动态字段定义 + 本章世界级字段
         ↓
  ToolLoop(Agent 白名单工具) // 生成 / 读写 novel.db
         ↓
  其它 Agent 或 MCP 再 list_* 读回理解
```

示例：

1. **卧底反派**：`character` → `upsert_entity(person)` + `public_mask=反派军师` + `true_faction=北境卧底网`
2. **后期发现多宇宙**：`world` / `field_builder` → 实体 + `universe_layers`；某章 `universe_of_chapter=镜像宇宙, chapter_scope=50`
3. **新体系字段**：`field_builder` → `upsert_field_def(bloodline_seal)` → 写入角色
4. **改写手策略**：`agent_meta` → `upsert_agent(novel_writer, system_prompt=…)`，version+1

## 与旧「写死六域」关系

- 旧 kind/详情表仍可作**兼容层**，不再是唯一真相源。
- 新设定优先走 **动态字段 + 自定义 kind**，避免再往固定列里塞。
- 字段清单见 `docs/novel-字段清单.md`（哪些写死点已被动态字段覆盖）。

## 前端获取与数组解析

小说工作区（打开工程后）新增 **「多 Agent / 动态字段」** 页：

| Tab | 展示 | 数组处理 |
|-----|------|----------|
| Agents | 名称/id/版本/角色 | **`tools_json` → `ParseStringArray`**，逐条 Bullet，非法 JSON 标橙色，不当逗号纯文本硬切 |
| 字段定义 | scope/key/type | **`enum_json` → 数组**；有 enum 显示 `enum(n) a\|b`，否则显示说明 |
| 实体字段 | key/layer/ch/value | **`value_json` 按类型解析**：array 显示 `数组(n)` + 对象数组展开 `layer=…`；object 显示摘要 |
| 路由 | 输入任务 → agent_id | 复用 `AgentKit::Route` |

解析 API（`src/util/Json.h`，前端/MCP 共用）：

- `ParseStringArray` / `JoinArray` / `ArrayLen` / `ArrayBrief`
- `ParseObjectArray` / `ObjArrayField`（如 identity_layers 的 layer）
- `Classify` / `KindLabel` / `ValueBrief`

样例数据（「写入样例设定」）会写入：`public_mask`、`true_faction`、
`identity_layers`（对象数组）、世界级 `active_universe`（对象数组）。

自检：`jsonparse:ok`（`RunJsonArrayParseSelfCheck`，已挂在 `SHINE_NOVEL_GRAPH_CHECK`）。

## Agent 如何用 MCP（禁止一股脑注入）

```text
外部 AI / Claude Desktop
        │  tools/list  → 看到 novel 模块全部 novel_*（诊断面）
        ▼
 novel_route_task(task)  → agent_id
 novel_agent_invoke      → { system_prompt, allowed_tools[] }   ← 只含白名单
        │
        ▼
 仅对 allowed_tools 里的名称做 tools/call
        │
进程内 AgentKit
  CallToolAsAgent(agent, tool, args)
    1) tools_json 白名单校验（不在名单 → 拒绝）
    2) 优先本地工具（RegisterToolsFor）
    3) 否则 MCP novel_* 
    4) 都没有 → not_found
```

| 工具 | 作用 |
|------|------|
| `novel_route_task` | 任务 → agent_id |
| `novel_get_agent` | 读 prompt + tools 白名单 |
| `novel_agent_tools` | 白名单 ∩（local ∪ mcp）；`mcp_module_all` 仅诊断 |
| `novel_agent_invoke` | 组装执行包（prompt + allowed_tools），**不含**全表工具 |
| `novel_list_entities` / `novel_get_entity` / `novel_list_field_defs` / `novel_list_entity_fields` | 读图谱与动态字段（数组已结构化） |
| `novel_upsert_*` / `novel_link_relation` | 写；**默认拒绝**，`SHINE_MCP_ALLOW_WRITE=1` 才开 |

代码：

- MCP 注册：`src/novel/NovelMcpTools.*`（`RegisterMcpTools` 已挂进 `src/mcp/McpBootstrap.cpp`）
- 调度/白名单：`src/agent/AgentKit.*` → `ResolveTools` / `CallToolAsAgent` / `BuildInvokePackage`
- 自检：`novelmcp:ok`

## 自检证据（2026-09-17）

```
fields:ok
multiagent:ok
agents:ok
jsonparse:ok
novelmcp:ok
```

覆盖：15 Agent seed、路由、身份层/分章宇宙、field_builder、agent_meta、前端数组解析、
**MCP 注册 / 路由 / 白名单调度 / 写保护 / 动态字段 value_kind=array**。
