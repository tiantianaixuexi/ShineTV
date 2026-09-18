# 小说 Agent · 进度表（2026-09-19 盘点回写）

> **唯一勾选入口**（小说线）。历史 P1–P9 明细：完成分支 `Plan/归档/novel-agent/PROGRESS.md`。
> 主工程总进度：`Plan/PROGRESS.md`。施工图：`P10-mcp/PLAN.md`；**P9 源码在 `f7a0cb8`（`NovelImageGen/Store`）**。
>
> 图例：`[ ]` 未做 · `[~]` 进行中 · `[x]` 完成 · `[!]` 阻塞 · `[📦]` 完成分支已有、本树可能无源码

---

## 总览

| 阶段 | 状态 | 备注 |
|------|------|------|
| P1 OpenAI 客户端 | **[x]** | 本树 `src/openai/*`；`SHINE_OPENAI_CHECK` PASS；真调用需 Key |
| P2 知识图谱 | **[x]** | `NovelDb` + `NovelGraph`；**schema 已是 v5**（旧文写 v3 作废） |
| P3 记忆/Context | **[x]** | NovelMemory + ContextBuilder |
| P4 Local Tools | **[x]** | ToolRegistry + BuiltinTools + RunToolLoop |
| P5 Agent 管线 | **[x]** | NovelDirector mock PASS |
| P6 ImGui | **[x]** | NovelView 生成/取消/PROPOSED；多模型 Provider |
| P7 文本 MVP | **[x]** | SeedSample + 两章上下文自检 |
| P8 视觉体系 | **[x]** | NovelVisual 九层 Prompt |
| P9 出图 | **[x] 📦** | 完成分支：`generated_images` + NovelImageStore + UI CANON；`imagegen:ok`。**本树无源码 → 先合并** |
| P10 MCP 对外 | **[~]** | 见下表 |

---

## P10 · MCP

| ID | 任务 | 状态 | 备注 |
|----|------|------|------|
| P10.1 | stdio Server | **[x] 📦** | 完成分支 `--mcp-stdio` |
| P10.2 | 只读 tools 对齐 P4 | **[x]** | **本树已有** `src/novel/NovelMcpTools.*`（`novel_*` 全套）+ 自检 |
| P10.3 | HTTP/SSE remote | **[x] 📦** | 完成分支 `McpSse` / `McpHttpClient` / `McpRemoteAgent` |
| P10.4 | Redis/Garnet 缓存 | **[~]** | `src/db/redis` 基建在；**小说/Agent 尚无 cache 调用点**；待 Garnet `127.0.0.1:6379` 实测命中日志 |
| P10.5 | 写开关 + audit | **[x]** | **本树已有** `SHINE_MCP_ALLOW_WRITE` + Settings；写工具默认拒 |
| P10.6 | 客户端文档 | **[x] 📦** | 完成分支 `P10-mcp/CLIENT.md` |

---

## 附加交付（代码已完成，旧表未单列）

| ID | 内容 | 状态 | 证据 |
|----|------|------|------|
| X.1 | **AgentKit 多 Agent**（内置工具集、route、invoke 包、工具白名单∩MCP） | **[x]** | `src/agent/AgentKit.*`；`docs/novel-多Agent名册.md`；自检 `agents:ok` |
| X.2 | **NovelFields 动态字段**（`field_defs` / `entity_fields`） | **[x]** | `src/novel/NovelFields.*`；`fields:ok` |
| X.3 | **多模型 LLM**（OpenAI / MiMo / MiniMax / Custom + Anthropic Messages 协议） | **[x]** | `OpenAIProvider/Chat/Anthropic/Config`；设置窗已切换 |
| X.4 | **NovelDb schema v5**（视觉 v4 + agents v5 迁移） | **[x]** | `NovelDb.cpp` `kTargetSchemaVersion=5` |
| X.5 | **进程内 MCP 注册**（bootstrap → novel tools） | **[x]** | `McpBootstrap` + `RunNovelMcpSelfCheck` |

---

## 阻塞与风险

| 项 | 说明 | 状态 |
|----|------|------|
| LLM Key 联调 | MiMo/MiniMax/OpenAI/Anthropic 代码已接；需配 Key 真调用生成/出图 | **[~]** |
| Garnet cache 实测 | 启动 Garnet → 看 cache 命中日志 → MCP 两次读实体对比 | **[~]** |
| 工作树分支 | P9 / P10.1 / P10.3 / P10.6 源码在 `f7a0cb8`；未合并则本树无法编译验证这些项 | **[!] 合并前勿重做** |
| Comfy 出图后端 | 可选；主线图库/Comfy 另有路径 | **[~]** |

---

## 更新约定

1. 只在本文件改状态（小说线）；主工程大表在 `Plan/PROGRESS.md`。  
2. `[📦]` 项合并并复验后改 `[x]` 并写一行证据。  
3. 阻塞写进上表，不要只写在聊天里。  
4. **禁止**因本树缺文件就把完成分支已有的项当成未设计/重写一遍。
