# P10 · MCP 客户端配置（可复制）

> **P1–P9 计划分册已删除**（2026-09-19，`Plan/归档/` 31 个文件，提交 `0bdf565`）；原文在 git 历史：`git show 0bdf565^:Plan/归档/novel-agent/<file>`。
> 依赖：应用内已 `RegisterAllModules`（含 `novel_*`）。  
> 库：先在 UI 打开小说工程，或配置 `mcpNovelDbPath` / `SHINE_NOVEL_DB`。  
> 写工具：默认关。设置窗勾选「允许 MCP 写工具」或 `SHINE_MCP_ALLOW_WRITE=1`；写结果仍 `PROPOSED` + `audit_logs`。

## A. HTTP JSON-RPC（推荐，本机 UI 已启动时）

1. 设置 → **MCP 服务** → 勾选「启用 MCP HTTP」→ 端口默认 `8931` → **应用**。
2. Endpoint：`http://127.0.0.1:8931/mcp`（POST，`Content-Type: application/json`）。
3. 健康检查：`GET http://127.0.0.1:8931/health`。

### 手测

```text
POST http://127.0.0.1:8931/mcp
Content-Type: application/json

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}
{"jsonrpc":"2.0","id":2,"method":"tools/list"}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"novel_get_entity","arguments":{"name":"林默"}}}
```

### Claude Desktop（HTTP 形态）

在 Claude Desktop 的 MCP 配置里使用 **自定义/HTTP** 类型（各版本字段略有差异），核心是：

```json
{
  "mcpServers": {
    "shinetv-novel": {
      "type": "http",
      "url": "http://127.0.0.1:8931/mcp"
    }
  }
}
```

> 若客户端只支持 stdio，用下一节 B。

**P10.3 Streamable HTTP / SSE（2025-06-18 风格）**

| 通道 | 行为 |
|------|------|
| `POST /mcp` + `Accept: application/json` | 纯 JSON-RPC 响应（默认） |
| `POST /mcp` + `Accept: text/event-stream` 或 `?stream=1` | **Streamable**：`text/event-stream`，帧 `event: ping` + `event: message`（内含 JSON-RPC result） |
| `GET /sse`（别名 `/mcp/sse`） | **长连接** handshake：`endpoint` → `session` → 周期 `ping`（约 2 分钟） |
| Header `Mcp-Session-Id` | 可选；服务器会在 session 帧里回分配 id |

### 自建 Agent（连 MiMo / MiniMax，不依赖 Claude Desktop）

```
你的程序 / 脚本
   │  1) POST /mcp  tools/list
   │  2) 调用 MiMo Chat Completions（Settings.llmProvider=mimo 或自己发 HTTP）
   │     tools 数组 = 上一步的 inputSchema（仅白名单 novel_*）
   │  3) 若 finish_reason=tool_calls → POST /mcp tools/call
   │  4) 把 tool 结果回灌 messages，再调 LLM
   ▼
ShineTV MCP（HTTP 或 --mcp-stdio）
   └── novel.db（SQLite 权威）
```

应用内同款演示：`shine::mcp::RunMcpBridgedAgent(task)`  
——当前 Provider（可 MiMo）+ 进程内 `ToolRegistry` 跑 tool loop；配好 `MIMO_API_KEY` 后可联调。

### 手测

```text
POST http://127.0.0.1:8931/mcp
Content-Type: application/json
Accept: text/event-stream
Mcp-Session-Id: sess-demo-1

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}
```

期望响应 `Content-Type: text/event-stream`，body 含 `event: message` 与 initialize result。

```text
GET http://127.0.0.1:8931/sse
Accept: text/event-stream
```

期望持续收到 `endpoint` / `session` / `ping`。

## B. stdio（`ShineTVStudio.exe --mcp-stdio`）

```powershell
$env:SHINE_NOVEL_DB = "E:\path\to\novels\你的书名\novel.db"
# 可选写开关：
# $env:SHINE_MCP_ALLOW_WRITE = "1"
& "E:\c++\ShineTV\build\ShineTVStudio.exe" --mcp-stdio
```

- **stdout 只输出** JSON-RPC 响应（一行一条）；日志在控制台 stderr/内部缓冲，不进协议流。
- 未设 `SHINE_NOVEL_DB` 时：尝试 `Settings.mcpNovelDbPath`，再否则小说根目录下**第一本**工程。

### Claude Desktop stdio 片段

```json
{
  "mcpServers": {
    "shinetv-novel": {
      "command": "E:\\c++\\ShineTV\\build\\ShineTVStudio.exe",
      "args": ["--mcp-stdio"],
      "env": {
        "SHINE_NOVEL_DB": "E:\\path\\to\\novels\\你的书名\\novel.db"
      }
    }
  }
}
```

## C. 主要 novel_* 工具

| 工具 | 说明 |
|------|------|
| `novel_route_task` / `novel_agent_invoke` / `novel_agent_tools` | 路由 → 执行包（**只注入白名单**） |
| `novel_get_entity` / `novel_list_entities` | 实体（含 meta_json；MCP 另附 dynamic_fields） |
| `novel_get_relations` / `novel_get_chapter` / `novel_get_recent_chapters` | 关系 / 章节 |
| `novel_get_foreshadows` / `novel_get_secrets_for` | 伏笔 / 知情秘密 |
| `novel_get_event_chain` / `novel_get_ownership` | 因果链 / 持有 |
| `novel_get_character_slice` / `novel_get_world_slice` / `novel_search_memory` | 切片 / 记忆 |
| `novel_list_field_defs` / `novel_list_entity_fields` | 动态字段 |
| `novel_upsert_*` / `novel_link_relation` / `novel_link_causal` | **写**，默认拒绝 |

另有 `mcp_ping` / `comfy_*` / `shinetv_*` 等非小说工具。

## D. Redis（可选）

- 计划键：`novel:cache:entity:{id}`（TTL 60s），写路径会 `DEL`。
- **当前**：`db::Init()` 未必被 App 调用；池 `redisReady()==false` 时缓存**静默跳过**，全部直连 SQLite（权威）。
- Redis 挂了不影响 MCP 只读；不要把缓存当真相源。

## E. 安全

- 默认只读；写开关进 Settings/环境变量，**不进仓库**。
- 不暴露任意 SQL；密钥不会出现在 MCP 回复里。
- HTTP 监听默认 `127.0.0.1`，勿改成 `0.0.0.0` 除非你清楚风险。
