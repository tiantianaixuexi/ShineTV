# 多模型 API 规则备忘（ShineTV 小说 Agent）

> 官方文档抓取 2026-02。实现：src/openai/*

## 0. MiMo：按量 vs Token Plan（必读）

| 项 | 按量付费 | Token Plan（订阅） |
|----|----------|-------------------|
| Key 前缀 | sk-xxxxx | tp-xxxxx |
| OpenAI Base | https://api.xiaomimimo.com/v1 | https://token-plan-cn.xiaomimimo.com/v1 |
| Anthropic Base | https://api.xiaomimimo.com/anthropic | https://token-plan-cn.xiaomimimo.com/anthropic |
| 集群 | 默认国内 | cn / sgp / ams（以控制台为准） |
| 计费 | 按 Token 单价 | Credit 额度包（月/年套餐） |
| 模型 | mimo-v2.5-pro / mimo-v2.5 | 同左（v2 旧系列已下线） |
| 获取 | console/api-keys | console/plan-manage |

**设置**：Provider=MiMo → 勾选「使用 Token Plan」→ Base 自动切到 token-plan-cn…/v1  
新加坡/欧洲：手动改 Base 为 token-plan-sgp… 或 token-plan-ams…  
Key 填 tp- 开头；也可用环境变量 MIMO_API_KEY。

**注意（官方）**：Token Plan 主要供编程工具；自动化脚本/自定义后端可能被判定滥用并封 Key。

Credits 折算（语言模型）：
- mimo-v2.5-pro：缓存命中输入 2.5 / 未命中 300 / 输出 600
- mimo-v2.5：2 / 100 / 200
- 夜间 0:00-8:00（北京）0.8x 消耗

---

## 1. MiniMax

| 项 | 值 |
|----|-----|
| OpenAI Base | https://api.minimax.cn/v1 |
| Anthropic Base | https://api.minimax.cn/anthropic |
| 推荐模型 | MiniMax-M3（1M 上下文） |
| temperature | [0,2]，推荐 1.0 |
| thinking | M3 默认开；thinking.type=disabled 可关 |

---

## 2. 多轮对话（OpenAI Chat Completions）

POST {base}/chat/completions
Authorization: Bearer <key>

messages 按时间序：system / user / assistant / tool。

规则：
1. 完整保留历史 assistant（含 tool_calls、content）
2. MiniMax content 可能含 <think> 标签，回传时不要剥掉
3. MiMo 思考模式可有 reasoning_content，工具调用轮建议保留

finish_reason: stop | tool_calls | length | content_filter

---

## 3. 工具调用（OpenAI 格式，两家通用）

tools: [{type:function, function:{name, description, parameters}}]
tool_choice: auto

响应 finish_reason=tool_calls：
  message.tool_calls = [{id, function:{name, arguments}}]

回灌顺序：
1. assistant 原样（含 tool_calls）
2. 每 call 一条 {role:tool, tool_call_id, content}

循环 max 20；同 name+args 连续 3 次中止。

进程内 Agent 直连 SQLite，不经 MCP（P10 再对外暴露）。

---

## 4. 结构化输出

response_format: {"type":"json_object"}
system 必须写明只返回 JSON + 字段类型示例。
解析失败重试 1 次。

---

## 5. Anthropic Messages 协议

### 端点

POST {anthropic_base}/v1/messages

| Provider | anthropic_base |
|----------|----------------|
| MiMo 按量 | https://api.xiaomimimo.com/anthropic |
| MiMo Token Plan | https://token-plan-cn.xiaomimimo.com/anthropic |
| MiniMax | https://api.minimax.cn/anthropic |

### 请求头

x-api-key: <key>
anthropic-version: 2023-06-01
content-type: application/json

### 请求体

{
  "model": "MiniMax-M3",
  "max_tokens": 4096,
  "system": "系统提示",
  "messages": [
    {"role":"user","content":[{"type":"text","text":"你好"}]}
  ],
  "tools": [{
    "name": "get_weather",
    "description": "查天气",
    "input_schema": {"type":"object","properties":{"location":{"type":"string"}},"required":["location"]}
  }],
  "thinking": {"type":"disabled"}
}

### 响应

content 是块数组：
- {"type":"thinking","thinking":"…"}
- {"type":"text","text":"…"}
- {"type":"tool_use","id":"toolu_x","name":"…","input":{…}}

stop_reason: end_turn | tool_use | max_tokens

### 工具回灌（Anthropic）

assistant.content = 上一轮 content 数组原样
user.content = [{"type":"tool_result","tool_use_id":"toolu_x","content":"结果"}]

### thinking

- MiniMax-M3：默认关；{"type":"adaptive"} 开
- M2.x 无法关
- 多轮 tool_use：必须整块回传 thinking

### Python SDK 示例（参考）

export ANTHROPIC_BASE_URL=https://api.minimax.cn/anthropic
export ANTHROPIC_API_KEY=...

import anthropic
client = anthropic.Anthropic()
msg = client.messages.create(
  model="MiniMax-M3",
  max_tokens=1000,
  system="You are helpful.",
  messages=[{"role":"user","content":[{"type":"text","text":"Hi"}]}],
  tools=[{"name":"get_weather","description":"…","input_schema":{…}}]
)
# content 循环：thinking / text / tool_use

MiMo Token Plan 同理，BASE_URL 换成 token-plan-cn.xiaomimimo.com/anthropic，Key 用 tp-。

---

## 6. C++ 入口

| 能力 | 入口 |
|------|------|
| Provider/Token Plan | ResolveActiveProfile()（mimoTokenPlan 开关） |
| Chat 单轮/流式 | ChatComplete / ChatStream |
| 多轮+工具 | ChatSession / RunChatToolLoop |
| JSON | ChatSession::SetJsonObject |
| Anthropic | AnthropicComplete / AnthropicLlmComplete |
| 自定义头 POST | PostJsonHeaders |

---

## 7. 联调

设置选 MiMo + Token Plan + tp- Key → 保存 →
「联调-简单」或 SHINE_LLM_LIVE=1

401 Key 错；404 Base/模型错；额度尽 Token Plan 会拒。
