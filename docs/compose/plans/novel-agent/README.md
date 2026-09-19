# 小说 Agent 计划文档索引

> ⚠️ **P1–P9 分册已于 2026-09-19 物理删除**（`Plan/归档/` 下 31 个文件，提交 `0bdf565`，合并 `d872896`）。
> 磁盘上不再有这些文件，原文只存在于 git 历史中，按下面的命令取。

```bash
git show 0bdf565^:Plan/归档/novel-agent/00-总纲.md
git show 0bdf565^:Plan/归档/novel-agent/PROGRESS.md
git show 0bdf565^:Plan/归档/novel-agent/P9-imagegen/PLAN.md
```

| 用途 | 位置（已删，取原文用 `git show 0bdf565^:<路径>`） |
|------|------|
| 总纲 | `Plan/归档/novel-agent/00-总纲.md` |
| 规则钩子 | `Plan/归档/novel-agent/RULES-HOOK.md` |
| 历史进度 P1–P9 | `Plan/归档/novel-agent/PROGRESS.md` |
| 各阶段施工图 | `Plan/归档/novel-agent/P1-openai/` … `P9-imagegen/PLAN.md` |
| 世界观/人物/伏笔等设定表 | `Plan/归档/novel-agent/P2-graph/**` |
| **当前进行中** | 本目录 `PROGRESS.md` + `P10-mcp/PLAN.md` + `P10-mcp/CLIENT.md` |
| **小说生产系统规格（现行权威）** | `Doc/小说系统/00-总纲与索引.md`（14 卷） |
| Agent 名册 | `docs/novel-多Agent名册.md` |
| 代码入口 | `src/novel/` `src/agent/` `src/openai/` `src/mcp/Mcp*` |

> 现行小说线的规格已迁到 `Doc/小说系统/`（唯一入口 `00-总纲与索引.md`）；本目录只保留 P10-MCP 的接入说明。
> 更早的路径（`Plan/归档` 之前的 `ed6e48f`）：`git show ed6e48f:docs/compose/plans/novel-agent/<file>`。
