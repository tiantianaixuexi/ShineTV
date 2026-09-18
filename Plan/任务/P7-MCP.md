# P7 — MCP 服务

> **施工图**（每个 S 的做法与判据）。勾选与状态在 `../PROGRESS.md`；实测证据（含每个 S 的 ✅ 记录）在 `../证据.md`。

> 一次只做一个 S：做完 configure → build → 运行，逐条对验收，再回 `../PROGRESS.md` 勾选。

## P7.1 — Tool Registry  ✅ 4/4（另见 compose 分模块注册扩展）


- **S1 骨架** — 新增 `src/mcp/MCPTypes.h`（`Result` / `CallStatus` / `CallOutcome` / `Tool`）、`ToolRegistry.h/.cpp`（进程单例）；CMake 登记。判据：编译通过。 ✅
- **S2 注册与查找** — `Register(tool)`（**同名覆盖，幂等**）/ `Unregister(name)` / `Clear()` / `Find(name)`。判据：同名重复注册只保留一个。 ✅（另：`ModuleInfo` / `EnsureModule` / `ClearModule` 分模块）
- **S3 `BuildToolsListJson()`** — 输出 `{"tools":[...]}`，字段 `name/title/description/inputSchema`；用 `yyjson` 拼装并加 `Schema::Object / AddString / AddNumber / AddBoolean` 小助手。判据：结构符合 MCP `tools/list` 规范。 ✅
- **S4 `Call()` 与验收** — `Call(name, argsJson)` 返回 `CallOutcome`（`Ok` / `NotFound` / `BadArguments`）；未知工具与非法 JSON 都不崩且有规范错误文本；跑验收 3 条并贴 `../PROGRESS.md`。 ✅（另：`InternalError` + 异常捕获；`SHINE_MCP_CHECK` / 控制台自检）

> 分模块注册 API 与 demo 桩见 `docs/compose/spec/mcp-tool-registry.md`；证据 `../证据.md` → P7.1。**P7.2+ 仍未开始。**

---

## P7.2 — HTTP Server（libhv）  ✅ 5/5


- **S1 打开 libhv server 并建骨架** — `CMakeLists.txt` `WITH_HTTP_SERVER OFF` → `ON`（保持 `WITH_OPENSSL OFF` / `WITH_LUA OFF` / `BUILD_EXAMPLES OFF`）；新增 `src/mcp/HttpServer.h/.cpp`（`Method` / `Request` / `Response` / `Start` / `Stop` / `Running` / `Route` / `SetUiDispatcher`）；CMake 登记。判据：编译链接通过。 ✅
- **S2 启停与错误** — 基于 libhv `http_server_t` 实现 `Start/Stop/Running`；端口占用 / 地址非法 → `Start` 返回 `std::expected` 错误带原因（UI 提示，**程序不崩**）。判据：占用端口启动失败且日志有原因。 ✅
- **S3 路由与默认响应** — `Route(path, method, handler)` 支持精确 + 前缀（`/tools/<name>`）；`OPTIONS` → 204（CORS）；未匹配 → 404 JSON。 ✅
- **S4 UI 线程投递** — `SetUiDispatcher(post)`：**所有 handler 都投递到 UI 线程执行**（图/工程/画布状态只允许 UI 线程访问），worker 只做收发。判据：请求到达时 UI 不卡、不阻塞渲染帧。 ✅（默认 `PostToUi` + 等待；超时 504）
- **S5 Settings + `/health` + 验收** — `src/core/Settings.h` 加 `mcpEnabled=false` / `mcpListenAddr="127.0.0.1"` / `mcpPort=8931`；`/health` 返回 `server/version/port/toolCount`；自检报告见 `证据.md` → P7.2。 ✅

---

## P7.3 — MCP 协议端点  ✅ 5/5


- **S1 骨架与路由注册** — 新增 `src/mcp/MCPServer.h/.cpp`；注册 `/health` / `/mcp`（`/messages` `/message`）/ `/sse` / `/tools` / `/tools/<name>`。 ✅
- **S2 JSON-RPC 主入口** — `initialize` / `ping` / `tools/list` / `tools/call`；协议 `2025-06-18`（兼容表在 result）。 ✅
- **S3 空实现与通知** — `resources/list` / `prompts/list` 空数组；通知无 id 空响应。 ✅
- **S4 SSE** — `GET /sse` 返回 `event: endpoint` + `data: /mcp`（长连接心跳为后续增强）。 ✅（骨架）
- **S5 错误码与验收** — `-32601` / `-32600`；`tools/call` 含 `isError` + `content[]`。自检 OVERALL PASS。 ✅

## P7.4 — 内置工具集  ✅ 4/4


- **S1 骨架与注册钩子** — `src/mcp/BuiltinTools.*` + `RegisterAllModules` 接入。 ✅
- **S2 Comfy 桥接** — `comfy_ping` / `comfy_queue` / `comfy_submit` / `comfy_interrupt`（未连接中文提示；其余列表类工具可按同模式扩展）。 ✅（核心子集）
- **S3 ShineTV 能力** — `shinetv_status` / `shinetv_graph_compile` / `shinetv_graph_submit`。 ✅
- **S4 校验与验收** — 缺参 `BadArguments`；未连接不超时。 ✅

## P7.5 — MCP 设置与安全  ✅ 3/3


- **S1 设置段 UI** — 设置窗「MCP」：开关 / 地址 / 端口 / 请求数 / 最近 tools/call。 ✅
- **S2 运行时启停** — 「应用 MCP 设置」→ `StopHttpFromSettings` + `StartHttpFromSettings`。 ✅
- **S3 端口校验 + 验收** — 端口 &lt;1024 或 &gt;65535 拒绝应用。 ✅

---
