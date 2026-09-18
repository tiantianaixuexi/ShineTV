# P10 · MCP：对外暴露小说状态

## 先分清两条路

| 场景 | 通道 | 是否要 MCP |
|------|------|------------|
| **ShineTV 内** NovelAgent 读写 `novel.db` / Redis | `src/novel` + `src/db`（进程内） | **不要**。绕 MCP 只会多一层 IPC/延迟 |
| 外部工具（Claude Desktop / 其它 Agent / 脚本）动态读小说状态 | **MCP Server** | **要** |
| OpenAI Responses **remote MCP** 工具 | MCP over HTTP | **要** |
| 多进程：UI 进程 vs 批处理进程共享状态 | Redis 作缓存/锁 + MCP 作统一入口 | 可选 |

结论：**P4 Local Tools 是进程内快捷方式；P10 把同一套能力暴露成 MCP**，数据仍落 SQLite（权威）+ Redis（可选：热点缓存、任务队列、跨进程锁）。

```
┌──────────────── ShineTV 进程 ────────────────┐
│  ImGui ──► NovelDirector ──► Local Tools     │
│                              │               │
│                              ▼               │
│                         NovelGraph / db      │
│                    SQLite novel.db · Redis   │
└───────────────────────┬──────────────────────┘
                        │ 同一套 NovelService
          ┌─────────────┼─────────────┐
          ▼             ▼             ▼
     MCP Server    (可选)HTTP      日志/导出
   stdio / SSE      只读 API
          │
          ▼
   Claude / 外部 Agent / Responses remote MCP
```

## 依赖

- **硬依赖**：P2 图谱查询 API、P4 的 Tool 语义（名称/参数尽量对齐）
- **不依赖**：P5 文本生成也可先暴露只读 MCP
- Redis：已有 `src/db/redis`；小说热点（当前章状态、未回收伏笔）可写缓存，**以 SQLite 为准**

## 模块位置

```
src/novel/mcp/          或 src/mcp/（与现有目录约定对齐后二选一，推荐 src/mcp 若将来多业务共用）
  McpServer.h/.cpp      JSON-RPC 2.0 + initialize/tools/list/tools/call
  McpTransport.h/.cpp   stdio（首期）+ 可选 HTTP/SSE（remote）
  NovelMcpTools.cpp     把 NovelService 注册成 MCP tools
```

**禁止**在 `third/` 乱加；协议自研薄层或评估极小依赖，JSON 用 yyjson。

## Tool 清单（与 P4 对齐，首期只读优先）

| MCP tool | 数据源 |
|----------|--------|
| novel_get_entity | SQLite |
| novel_list_entities | SQLite |
| novel_get_relations / ownership / event_chain | SQLite |
| novel_get_chapter / recent_chapters | SQLite |
| novel_get_foreshadows / secrets_for / mysteries | SQLite |
| novel_search_memory | SQLite（后续 embedding） |
| novel_get_world_slice / character_slice | Graph API |
| novel_get_visual_asset / assemble_prompt_preview | P8（可后置） |
| novel_upsert_* | 写：默认拒绝或需 capability；成功写 audit_logs |

对齐：`list_tools` 的 JSON Schema 与 P4 `ToolRegistry::ExportOpenAiTools` **同源生成**，避免两套漂移。

## Redis 用法（可选增强）

| 用途 | 键例 | 说明 |
|------|------|------|
| 当前工程路径 | `novel:active_project` | 多客户端一致 |
| 热点切片缓存 | `novel:cache:char:{id}:ch{N}` | TTL 短；写后 DEL |
| 生成任务队列 | `novel:job:{id}` | 与 UI 异步任务对齐 |
| 分布式锁 | `novel:lock:write:{db}` | 多进程写 SQLite 时 |

规则：**缓存可丢，SQLite 不可丢**；Redis 挂了 MCP 仍应能直连 SQLite（降级只读）。

## 协议范围（首期）

| 做 | 不做（首期） |
|----|----------------|
| initialize / tools/list / tools/call | resources 全量、prompts 模板市场 |
| stdio（本地 Claude Desktop 等） | 多租户鉴权 SaaS |
| 错误 JSON-RPC 规范码 | 热更新任意二进制插件 |
| 日志与 audit | |

Remote MCP（HTTP+SSE）列 **P10.3**，供 Responses `tools: mcp`。

## 安全

- 默认 **只读 tools**
- 写 tool 需 Settings `mcpAllowWrite` + 仍走 PROPOSED
- 不暴露任意 SQL
- 不把 API key 传给 MCP 客户端
- 路径限制在当前工程目录

## 验收

- [x] Claude Desktop（或 mcp inspector）能 list/call `novel_get_entity`（HTTP `/mcp` 或 stdio；配置见 `CLIENT.md`）
- [x] 与 Local Tools 同一实体核心字段一致（id/kind/name/summary/status/meta_json；MCP 另附 dynamic_fields）
- [x] SQLite 不可用时错误清晰（NeedDb 中文提示 + mcpNovelDbPath/SHINE_NOVEL_DB）
- [~] Redis 可选：有则缓存 key `novel:cache:entity:{id}`；无池则直连 SQLite（降级路径默认）
- [x] 写 tool 默认关闭（Settings `mcpAllowWrite` / env；拒绝文案可操作）

## 任务

| ID | 内容 | 门禁 | 状态 |
|----|------|------|------|
| P10.1 | stdio MCP Server 骨架 + initialize/tools/list | inspector 通 | [x] `RunStdioServerMain` |
| P10.2 | 只读 tools 接 NovelService（复用 P4） | 与 P4 字段一致 | [x] `novel_*` 与 BuiltinTools 对齐 |
| P10.3 | HTTP/SSE transport（remote MCP） | Responses/外部可连 | [x] streamable POST + GET /sse 长连接 |
| P10.4 | Redis 缓存/锁可选接通 | 降级路径测过 | [~] 有池则 cache，无则忽略 |
| P10.5 | 写 tools + 开关 + audit | 默认拒写 | [x] Settings+audit |
| P10.6 | 文档：Claude Desktop 配置片段 | 用户可复制 | [x] `CLIENT.md` |

## 客户端配置

见 **`CLIENT.md`**（HTTP + stdio + Claude Desktop JSON 片段 + 工具表）。

## 何时排期

```
P2 Graph ──► P4 Local Tools ──► P5/P6/P7 文本 MVP
                 │
                 └──► P10.1–P10.2（只读 MCP）   ← MVP 后立刻可做
P8 Visual ──► P10 可挂 visual tools
```

- **最早**：P4 完成后（工具语义已稳定）
- **不必等** P9 出图
- 与 P5–P7 可并行做只读 MCP，方便你用外部模型「摸」小说状态
