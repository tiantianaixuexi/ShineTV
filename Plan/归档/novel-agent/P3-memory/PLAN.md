# P3 · 记忆与 ContextBuilder

依赖：P2 图谱查询 API  
模块：`src/novel/NovelMemory.*` `src/agent/ContextBuilder.*`

## 四层

| 层 | 内容 | 来源 |
|----|------|------|
| L1 | 当前任务、当前章、选中实体 | 调用参数 / UI |
| L2 | 前 1–3 章摘要 | chapters.summary / memories |
| L3 | 相关人物状态、地点、势力、能力、未回收伏笔、持有、未结冲突 | graph 查询 |
| L4 | 长期事件链、主题母题、按知情过滤的秘密、mystery 未答问题 | graph + memories |

## 组装规则

```
Build(chapter_id, task, pov_entity_id?) → ContextText
```

1. 固定前缀：书名、writing_style、author_rules（error 级）
2. L2 最近摘要（截断字数上限可配）
3. 本章场景 cast → CharacterSlice（人设压缩 + 最新 status + arc 当前阶段 + dialogue）
4. 本章地点 → WorldSlice / location_details / travel 若有移动
5. 未回收 foreshadowings（status≠RESOLVED）+ 本章应 plant/payoff
6. 开放 conflicts / mysteries（本章相关优先）
7. **过滤**：secret 知情、mystery 未到 reveal 节贴的真相、非 CANON（默认）
8. 末尾：任务说明（写第 N 章 / 改某段）

原则：**相关 > 全量**；总 token 预算字段 `memoryTopK` / 字符上限。

## API

```cpp
struct ContextBuildInput {
  int64_t chapter_id;
  std::string task;
  std::optional<int64_t> pov_entity_id;
};
struct ContextBuildOutput {
  std::string text;           // 直接可作 instructions 附加段
  std::vector<int64_t> used_entity_ids;  // 便于 debug
};
std::expected<ContextBuildOutput, DbError> Build(const ContextBuildInput&);
```

## 验收

- [ ] L1–L4 段落齐全
- [ ] POV 未知情秘密不出现在 text
- [ ] 未回收伏笔列表非空（有数据时）
- [ ] 字符上限可截断且不截断半行 UTF-8（按字节回退）

## 任务

| ID | 内容 |
|----|------|
| P3.1 | NovelMemory：写 chapter_summary / 提取占位 |
| P3.2 | CharacterSlice / WorldSlice 拼装 |
| P3.3 | 过滤器（canon / 知情 / mystery） |
| P3.4 | Build() 全流程 + 日志 used ids |
| P3.5 | 单元自检：固定样例库跑 Build 看 text 片段 |
