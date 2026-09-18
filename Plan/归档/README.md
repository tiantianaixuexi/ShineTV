# Plan 归档目录

> **文档不删，只搬家。** 已完成线的施工图 / 交接 / 历史结论都在这里。
> 进行中的施工图仍在 `Plan/任务/`；小说 Agent 进行中的 P10 仍在 `docs/compose/plans/novel-agent/`。

## 本目录结构

| 路径 | 内容 |
|------|------|
| `任务/P3-节点与图.md` | 主线 P3 NodeKit 施工图（已通过） |
| `任务/P4-媒体与纹理.md` | 主线 P4 媒体纹理施工图（已通过） |
| `任务/P7-MCP.md` | 主线 P7 MCP 服务施工图（已通过；小说侧增强见 novel-agent P10） |
| `归档-已完成.md` | P0 / P1 / P2 / P2.9 / R 线历史结论 |
| `HANDOFF-novel-agent.md` | 小说 Agent 开工交接（旧入口） |
| `novel-agent/` | 小说 Agent **已完成**计划树：`00-总纲` `RULES-HOOK` `PROGRESS` `API-PROVIDERS` + **P1–P9 分册** |
| `novel-agent/P10-mcp/` | （若在此）小说 MCP；**进行中文件以仓库当前路径为准** |

## 仍在外层的进行中施工图

| 文件 | 线 | 状态摘要（见 `Plan/PROGRESS.md`） |
|------|-----|-----------------------------------|
| `Plan/任务/P5-视频分镜.md` | P5 | 31/36，剩 **P5.7** 分镜图（缺 SD 模型时可先做降级） |
| `Plan/任务/P6-画布inpaint.md` | P6 | 0/17 未开始 |
| `Plan/任务/P8-收尾.md` | P8 | 0/13 未开始 |
| `Plan/任务/G-图片库.md` | G 并行 | 40/79（**G-S6 ✅ 第一次可用**），下一步 **G-S7** |
| `Plan/任务/G-参考.md` | G 参考 | 验收清单等，保留 |
| `docs/compose/plans/novel-agent/P10-mcp/` | 小说 MCP | P10.1–P10.3、P10.5–P10.6 已过；**P10.4** Garnet cache 待实测 |

## 已完成但仍在原仓库路径的文档（规则，不要动）

`Doc/AGENTS.md` · `Doc/RULES-AI.md` · `Doc/RULES-LANG.md` · `Doc/RULES-COMFY.md` · `Doc/BUILD.md` · `Doc/STYLE-UI.md` · `Doc/BASELINE.md`

设计契约：`docs/compose/spec/*`（小说 Agent / novel-studio / mcp-tool-registry 等）— **实现对照用，保留**。

## 汇总时注意

- 主线 **唯一勾选入口**：`Plan/PROGRESS.md`
- 小说 Agent 进度以 `docs/compose/plans/novel-agent/PROGRESS.md` 为准（进行中）+ 本归档 `novel-agent/PROGRESS.md`（P1–P9 历史）
- 开工步骤仍见 `Plan/PLAN.md` §1
