# 记忆边界（AI Memory）

- **事件记忆**（何时做了何事 / 为什么这么改 / 踩了什么坑）→ `.memsearch/memory/*.md`（唯一真源），按 [.agents/skills/memsearch/SKILL.md](../../.agents/skills/memsearch/SKILL.md) 写入与召回（`memsearch search`）。
- **工程事实**（代码在哪 / 怎么编 / 模块职责 / 门禁判据）→ `KnowledgeBase/`（本库，薄层速查，入口 [INDEX.md](../INDEX.md)）与 `refactor/`（长文）。
- **两者不互相复制**：判据是「下次还需要重新知道这件事吗？」——静态查证进文档，回忆过程进记忆（详见 [AGENTS.md](../../AGENTS.md)『文档分工与会话记忆』）。
- 索引是**派生缓存**（`~/.memsearch/milvus.db`，隔离靠 collection：记忆 `shinetv_memory` / 知识库 `shinetv_kb`，见 [.memsearch.toml](../../.memsearch.toml)）；文档改动后 `memsearch index KnowledgeBase refactor --collection shinetv_kb` 刷新。
- ✅〔源〕`.memsearch/memory/` 已有事件记忆正文（按日期归档，2026-11-23 实测 4 个文件）；写入流程见 SKILL.md §3.3（DSH 无 Stop hook，会话收尾手动补一条）。
