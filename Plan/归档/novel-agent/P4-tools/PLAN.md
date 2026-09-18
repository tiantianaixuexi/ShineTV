# P4 · Local Tools 与 Function Calling

依赖：P1 Client、P2 Graph  
模块：`src/agent/ToolRegistry.*` `src/agent/tools/*`

## Tool 抽象

```cpp
class Tool {
public:
  virtual ~Tool() = default;
  [[nodiscard]] virtual std::string_view Name() const = 0;
  [[nodiscard]] virtual std::string_view Description() const = 0;
  [[nodiscard]] virtual yyjson_val* Schema(yyjson_doc* doc) const = 0; // parameters
  [[nodiscard]] virtual std::expected<yyjson_doc*, ToolError>
  Execute(yyjson_val* args) = 0;
};

class ToolRegistry {
  void Register(std::unique_ptr<Tool>);
  yyjson_val* ExportOpenAiTools(yyjson_doc*) const;  // [{type:function, name, description, parameters}]
  std::expected<yyjson_doc*, ToolError> Execute(std::string_view name, yyjson_val* args);
};
```

## 第一批只读 Tools

| name | 参数 | 返回 |
|------|------|------|
| get_entity | id 或 name | 实体+详情摘要 |
| list_entities | kind, filter | 列表 |
| get_relations | entity_id | 关系边 |
| get_ownership | entity_id | 当前/历史持有 |
| get_chapter | n 或 id | 标题+摘要+截断正文 |
| get_recent_chapters | k | 摘要列表 |
| get_location | id | 详情+距离邻居 |
| travel_estimate | a, b | 距离/天数 |
| get_foreshadows | status? | 列表 |
| get_secrets_for | entity_id | **按知情过滤** 后可见项 |
| get_event_chain | event_id, depth | 因果链 |
| get_conflict / list_open_conflicts | | |
| get_mystery / list_open_mysteries | | |
| list_dependencies | kind, id | 待复查 |
| get_world_slice | | 规则等摘要 |
| get_writing_style / get_dialogue_style | entity_id? | |
| list_resource_holdings | owner_id | |
| search_memory | query, k | memories 前 k |

## 受控写 Tools（结果 PROPOSED）

| name | 说明 |
|------|------|
| upsert_entity | 建/改实体 → canon_logs PROPOSED |
| upsert_event | 事件 |
| link_relation | 关系 |
| link_causal | 因果边 |

写权限：默认关闭直至作者设置 `agentAllowPropose=true`。

## Function Calling 循环

```
request.tools = ExportOpenAiTools()
loop:
  res = Client.Create(request)
  calls = parse function_call items
  if none: return final
  for call in calls:
    result = Registry.Execute(...)
    append function_call_output item
  if ++steps > max_tool_calls: fail
```

- 同一 tool+args 连续 ≥3 次 → 中止
- Tool 错误：包装为 JSON error 回模型，不抛崩

## 验收

- [ ] Export schema 合法
- [ ] 每个只读 tool 本地 Execute 单测（样例库）
- [ ] 循环：mock 或真 key 下 tool 往返一次
- [ ] 超限与重复调用保护

## 任务

| ID | 内容 |
|----|------|
| P4.1 | Tool/ToolRegistry |
| P4.2 | 只读 tools 全表 |
| P4.3 | 写 tools + PROPOSED |
| P4.4 | Create 循环集成 |
| P4.5 | 保护与日志 |
