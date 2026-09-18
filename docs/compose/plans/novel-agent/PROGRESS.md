# 小说 Agent · 进度表（进行中）

> **唯一勾选入口**（小说线）。历史 P1–P9 全文在 `Plan/归档/novel-agent/PROGRESS.md`。
> 主工程总进度见 `Plan/PROGRESS.md`。施工图：P10 见 `P10-mcp/PLAN.md`；已完成分册在 `Plan/归档/novel-agent/`。

图例：`[ ]` 未做 · `[~]` 进行中 · `[x]` 完成 · `[!]` 阻塞

---

## 总览（小说 Agent）

| 阶段 | 状态 | 备注 |
|------|------|------|
| P1–P9 OpenAI→图谱→记忆→Tools→Agent→UI→MVP→视觉→出图 | **[x]** | 明细见归档 PROGRESS；离线自检 PASS |
| P10 MCP 对外 | **[~]** | 见下表 |

## P10 · MCP（当前）

| ID | 任务 | 状态 | 备注 |
|----|------|------|------|
| P10.1 | stdio Server | [x] | `--mcp-stdio` |
| P10.2 | 只读 tools 对齐 P4 | [x] | `novel_*` 全套 |
| P10.3 | HTTP/SSE remote | [x] | streamable POST + `/sse` |
| P10.4 | Redis/Garnet 缓存 | [~] | `db::Init` 已挂；Garnet 本机 6379 运行中；待 cache 日志实测 |
| P10.5 | 写开关 + audit | [x] | Settings `mcpAllowWrite` |
| P10.6 | 客户端文档 | [x] | `P10-mcp/CLIENT.md` |

## 阻塞与风险

| 项 | 说明 | 状态 |
|----|------|------|
| LLM Key 联调 | MiMo/MiniMax/OpenAI/Anthropic 代码已接；需配 Key 真调用 | [~] |
| Garnet cache 实测 | 启动日志 + MCP 两次读实体 | [~] |
| Comfy 出图后端 | 占位，可选 | [~] |

## 更新约定

1. 只在本文件改状态勾选（进行中小说线）  
2. 备注一行证据  
3. 阻塞写进上表  
