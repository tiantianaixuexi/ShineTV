# P5 · Director 与四角色管线

依赖：P3 ContextBuilder、P4 Tools、P1 Stream  
模块：`src/agent/NovelDirector.*` `Planner.*` `Writer.*` `Critic.*` `Extractor.*`  
Prompt：`prompts/planner.md` 等，**运行时读文件**

## 状态机

```
IDLE
 → ANALYZE     读章/上下文需求
 → PLAN        Planner structured JSON
 → RETRIEVE    ContextBuilder
 → WRITE       Writer（可 Stream 事件到 UI 队列）
 → REVIEW      Critic issues
 → REVISION    Writer 改稿（≤ max_revisions=3）
 → EXTRACT     Extractor 写图谱
 → SAVE        chapters/scenes/memories
 → DONE

ERROR → RETRY（有限次）→ FAILED
```

| 限制 | 默认 |
|------|------|
| max_tool_calls | 20 |
| max_revisions | 3 |
| timeout | Settings |

## Planner

输入：Context + 「写第 N 章」  
输出 JSON：

```json
{
  "chapter_number": 3,
  "chapter_title": "…",
  "goal": "…",
  "scenes": [
    {"ord":1,"location":"…","cast":["…"],"goal":"…","conflict":"…","result":"…","emotion":"…"}
  ],
  "foreshadowing": ["plant|x", "payoff|y"],
  "ending_hook": "…",
  "new_events": [{"title":"…","cause_event_id":12}]
}
```

- Structured Output json_schema
- 解析失败：重试 1 次 → FAILED

## Writer

输入：Context + Plan + writing_style + 相关 dialogue_styles  
输出：正文（stream delta）  
禁止：改库；POV 越界；未揭示真相

## Critic

检查清单（issues[]）：

- 人物一致性 / 性格突变
- 世界观与 world_rule
- 时间线与位置
- 能力 limit / counter
- 关系与冲突阶段
- 伏笔状态
- POV 越界
- author_rules
- 重复与语言

```json
{"passed":false,"issues":[{"type":"pov","severity":"high","description":"…"}]}
```

## Extractor

从正文+plan 抽取 → PROPOSED：

- 新人物/地点/物品（若 plan 声明）
- 事件与 causal_links
- relations 变化
- character_status 快照
- emotion_events
- memories chapter_summary
- foreshadowing 状态推进
- dependencies mentions

作者确认后 CANON（P6 UI 可后置按钮；MVP 可自动 CANON 样例工程以打通）。

## API

```cpp
struct GenerateChapterRequest { int64_t chapter_id; std::string user_hint; };
struct GenerateChapterProgress { enum Phase {…}; std::string text_delta; int percent; };
void GenerateChapterAsync(GenerateChapterRequest,
  std::function<void(const GenerateChapterProgress&)>,
  std::function<void(std::expected<GenerateChapterResult, AgentError>)>);
```

全程 worker；进度 `PostToUi`。

## 验收

- [ ] 状态机单线程走通（可 mock Client）
- [ ] max_revisions 生效
- [ ] 生成后 chapters.body 非空 + memories 有 summary
- [ ] Stream delta 到达回调

## 任务

| ID | 内容 |
|----|------|
| P5.1 | prompts/*.md 模板加载 |
| P5.2 | Planner |
| P5.3 | Writer |
| P5.4 | Critic |
| P5.5 | Extractor |
| P5.6 | Director 状态机 + Async 入口 |
