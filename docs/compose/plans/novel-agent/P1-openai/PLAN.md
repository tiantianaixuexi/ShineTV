# P1 · OpenAI Responses 客户端

模块：`src/openai/`  
契约：`docs/compose/spec/novel-agent.md` [S2.2]

## 范围

- 配置与密钥
- `Create`（同步 POST `/v1/responses`）
- yyjson 解析 `output_text` + 保留 raw
- `Stream`（SSE）
- 错误模型
- **不含** Tool Calling 循环（P4）、业务 prompt（P5）

## 文件

```
src/openai/
  OpenAITypes.h      ApiError / CreateRequest / CreateResult / StreamEvent
  OpenAIClient.h/.cpp
  OpenAIStream.h/.cpp
```

CMake：上述 `.cpp` 加入 `ShineTVStudio`。

## 步骤

| ID | 内容 | 验收 |
|----|------|------|
| P1.1 | Settings：`openaiBaseUrl` `openaiApiKey` `openaiModelDefault`（及可选 planner/writer/critic 模型） | 设置可读写；日志不出现 key |
| P1.2 | `OpenAITypes` + `ApiError` | 头文件可被空 TU include 编译 |
| P1.3 | libhv HTTPS POST，body 用 yyjson 拼 `{model,instructions,input}` | 有 key 返回文本；无 key/坏 key 错误文案中文且不含密钥 |
| P1.4 | 解析 `output` 数组中 `type=message` 的 text → `output_text`；`raw` 交调用方 free | 手工样例 JSON 可解析 |
| P1.5 | SSE：`response.output_text.delta` 等事件；完成/错误回调 | 增量日志可见；断流可识别 |
| P1.6 | 超时、HTTP 4xx/5xx、JSON 坏包路径 | 三种错误可区分 |

## 明确不做

- MCP、代理自动发现、重试风暴（Create 解析失败仅 1 次重试，见 spec）
- 在 UI 线程调用

## 自检

```powershell
# 有 OPENAI_API_KEY 时
# 临时自检入口或日志：输入「写一句雪原」应得到中文
cmake --build build -j 8
```
