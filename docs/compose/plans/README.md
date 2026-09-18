# compose · 执行计划根目录

小说 Agent 计划树（大计划 → 分册）：

**`novel-agent/`**

| 入口 | 文件 |
|------|------|
| 总纲 | [novel-agent/00-总纲.md](novel-agent/00-总纲.md) |
| 进度（唯一勾选） | [novel-agent/PROGRESS.md](novel-agent/PROGRESS.md) |
| 规范挂接 | [novel-agent/RULES-HOOK.md](novel-agent/RULES-HOOK.md) |

契约（接口/schema）：`docs/compose/spec/novel-agent.md`、`novel-studio.md`  
新会话交接：`Plan/HANDOFF-novel-agent.md`

```
novel-agent/
  00-总纲.md  RULES-HOOK.md  PROGRESS.md
  P1-openai/  P2-graph/  P3-memory/  P4-tools/
  P5-agent/   P6-ui/     P7-mvp/     P8-visual/
  P9-imagegen/ P10-mcp/
```
