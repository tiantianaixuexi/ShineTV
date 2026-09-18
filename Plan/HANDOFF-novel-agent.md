# 新会话交接 · 小说 Agent 开工

## 一句话

在 ShineTV 内按计划树实现小说 Agent（从 **P1 OpenAI 客户端** 或 **P2 图谱** 起），不要重开设计讨论。

## 必读（按序）

1. `Doc/AGENTS.md`、`Doc/RULES-AI.md`（+ 需要时 RULES-LANG / BUILD）
2. `docs/compose/plans/novel-agent/00-总纲.md`
3. `docs/compose/plans/novel-agent/RULES-HOOK.md`
4. `docs/compose/plans/novel-agent/PROGRESS.md`（**唯一进度勾选**）
5. 当前阶段分册：如 `P1-openai/PLAN.md` 或 `P2-graph/**`
6. 契约：`docs/compose/spec/novel-agent.md`（字段以分册为准）

## 开工默认

- 工作区：`E:\c++\ShineTV`（本仓库无 .git，主树施工）
- 一次只做一个 PROGRESS ID：实现 → `cmake` build → 运行/自检 → 勾选 + 备注
- UI 只进 `src/app/novel/`；业务 `src/novel` / `src/agent` / `src/openai`
- yyjson + libhv（openai 模块可用 libhv，见 RULES-HOOK 特例）
- 密钥不进仓库/日志

## 建议首个任务

**P1.1 + P1.3**：Settings 增加 openai 段 → libhv 最小 `POST /v1/responses` 同步调用（无 key 时错误清晰）。

或并行 **P2.B0/B1**：`NovelDb` 打开 + entities 骨架迁移。

## 可点预览（仅设计参考，非实现源）

- `novel-design-preview.html` 工作区全功能 + 流程时间线
- `novel-assets-preview.html` 档案四视图/流转
- `novel-agent-plan-preview.html` 计划导航（可选）

## 禁止

- 一次写完 P1–P10
- 引入 nlohmann / libcurl / 官方不存在的 C++ OpenAI SDK
- UI 线程同步 HTTP
- 把分册字段再压缩回总纲
