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

## P7.3 — MCP 协议端点  ⬜ 0/5


- **S1 骨架与路由注册** — 新增 `src/mcp/MCPServer.h/.cpp`；`HttpServer.cpp` 注册 `/health`(GET) / `/mcp`(POST，别名 `/messages`、`/message`) / `/sse`(GET，别名 `/mcp/sse`) / `/tools`(GET) / `/tools/<name>`(POST)；CMake 登记。
- **S2 JSON-RPC 主入口** — 分发 `initialize` / `ping` / `tools/list` / `tools/call`；协议版本 `2025-06-18`，兼容 `2025-03-26`、`2024-11-05`；响应严格 `{"jsonrpc":"2.0","id":..,"result":..}`。判据：`initialize` → `tools/list` → `tools/call comfy_ping` 用 curl 全跑通。
- **S3 空实现与通知** — `resources/list` / `prompts/list` 返回空；`notifications/initialized` / `notifications/cancelled` 返回**无 `id` 的空响应**。
- **S4 SSE** — `GET /sse` 先发 `event: endpoint`，随后保持连接（心跳注释帧），连接不断。
- **S5 错误码与验收** — 未知方法 / 非法 JSON-RPC 版本返回规范错误码不崩；`tools/call` 结果按 `{"isError":false,"content":[{"type":"text","text":"<JSON 字符串>"}]}` 形态；跑验收 4 条（每条贴请求与响应）并贴 `../PROGRESS.md`。

---

## P7.4 — 内置工具集  ⬜ 0/4


- **S1 骨架与注册钩子** — 新增 `src/mcp/BuiltinTools.h/.cpp`；`MCPServer.cpp` 启动时调用注册函数把全部内置工具塞进 `ToolRegistry`；CMake 登记。判据：`tools/list` 能列出已注册工具。
- **S2 Comfy 桥接 9 个工具** — `comfy_ping` / `comfy_list_checkpoints` / `comfy_object_info`(classType) / `comfy_upload_image`(filePath→input) / `comfy_submit`(promptJson→promptId) / `comfy_queue` / `comfy_prompt_result`(promptId) / `comfy_download_image`(filename/subfolder/type/savePath) / `comfy_interrupt`。判据：每个工具单独 curl 调用有合理返回。
- **S3 ShineTV 能力 5 个工具** — `shinetv_status`（无参）/ `shinetv_gallery_scan`(source,path?) / `shinetv_gallery_upload`(path) / `shinetv_graph_compile`（无参）/ `shinetv_graph_submit`（无参，返回 promptId）。判据：`shinetv_graph_submit` 能从外部触发真实生成，底栏队列同步出现任务。
- **S4 校验与验收** — 参数缺失/类型错误 → `isError:true` + 中文说明；不存在的工作流 → 返回 ComfyUI 原始错误；**未连接时所有工具给"未连接"提示而不是超时**；跑验收 1–4 并贴 `../PROGRESS.md`。

---

## P7.5 — MCP 设置与安全  ⬜ 0/3


- **S1 设置段 UI** — `App.cpp:787-848` `DrawSettingsWindow` 新增「MCP」段：开关 / 监听地址 / 端口 / 累计请求数 / 最近一次调用日志（工具名 + 时间 + 成功/失败）。
- **S2 运行时启停** — `MCPServer.cpp` 支持运行时启停：**保存后立即生效**（改端口 → 旧的释放、新的监听；关开关 → 停服）。判据：重启后配置保留。
- **S3 端口校验 + 验收** — 非法端口（<1024 或 >65535）就地报错并**拒绝保存**；默认 `127.0.0.1:8931`；跑验收 3 条并贴 `../PROGRESS.md`。

---
