# P7 · MVP 端到端验收

依赖：P1–P6 最小路径可跑

## 剧本《北境余烬》（或新建）

1. 新建小说工程（novel.db v3）
2. 填写：
   - world_rule：王印会侵蚀神智
   - 人物：林昭（目标/恐惧/秘密）
   - 地点：灰烬城外
   - 未回收伏笔 1 条
3. 第 1 章：点「生成本章」
4. 观察：Plan → Write 流式 → Review → Save
5. 检查库：
   - chapters.body 非空
   - memories 有 chapter_summary
   - character_status 有快照
6. 第 2 章再生成：
   - Context 含第 1 章摘要
   - 含未回收伏笔
   - POV 秘密未写穿（人工读）
7. 工具：模型至少调用过 get_character / get_foreshadows 之一（日志）

## 完成定义

| 项 | 标准 |
|----|------|
| 构建 | `cmake --build` 通过 |
| 生成 | 一章正文入库 |
| 一致性信号 | 第 2 章能引用第 1 章事实 |
| 限制 | 修订 ≤3；tool ≤20 |
| 线程 | UI 不冻结 |

## 不在 MVP

- 百章巡检、MCP、向量检索、多 Agent、Web

## 任务

- [ ] V1 样例数据脚本或手工步骤固化到本文
- [ ] V2 全流程跑通并记录证据到 `Plan/证据.md` 风格附录（可选）
- [ ] V3 更新 `Plan/PROGRESS.md` 若项目采用
