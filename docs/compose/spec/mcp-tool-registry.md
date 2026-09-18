---
feature: mcp-tool-registry
status: delivered
updated: 2026-02-14
branch: (no-git — 本仓库当前无 .git，直接在主树施工)
commits: n/a
---

# MCP 工具注册地基（分模块注册）

> 项目施工图：`Plan/任务/P7-MCP.md` **P7.1**（对齐 + 扩展「分模块注册」）。
> 只读参考：`Plugins/ShineMCP`（UE 原实现，不直接编译）。
> 语言与分层：`Doc/RULES-LANG.md` / `Doc/RULES-AI.md` / `Doc/AGENTS.md`。

## Report

**What was built** — 在 `src/mcp/` 落地 MCP 工具注册地基：`MCPTypes`（`CallStatus`/`CallOutcome`/`Tool`/`ModuleInfo`）、yyjson `schema::*` 助手、进程级 `ToolRegistry`（同名覆盖、`EnsureModule`/`ClearModule` 分模块、`BuildToolsListJson`、`Call` 四条路径含异常捕获）、`McpBootstrap::RegisterAllModules` 显式顺序接线（当前 demo 模块 `mcp_ping`/`mcp_registry_info`，预留 comfy/graph/… 扩展点）。CMake 已登记三份 `.cpp`；`App.cpp` 启动调用 bootstrap，并提供 `SHINE_MCP_CHECK=1` 自检开关。**未**打开 `WITH_HTTP_SERVER`，**未**实现 JSON-RPC/SSE/业务工具集（属 P7.2–P7.5）。

**Verification** — `cmake --build build -j 8` PASS（链接 `ShineTVStudio.exe`）；控制台自检 `build/mcp_selfcheck.exe`（同源码）输出 `mcp 自检 PASS` / `MCP_SELF_CHECK=PASS Exit=0`，覆盖：同名覆盖幂等、`EnsureModule` 幂等、`ClearModule` 级联、`tools/list` 形态、Call Ok/`{}`/NotFound/BadArguments/InternalError。GUI exe 重定向无 stdout（WIN32 子系统），故用控制台入口取证。Review（独立 subagent）结论：S2.9 与 T1–T7 **无 critical 未达标项**。

**Journey log** — ① 本仓库无 `.git`，compose 走主树 + `docs/compose/spec/*`，与 `ui-to-app.md` 同款。② `yyjson_mut_val_mut_copy` 只拷 mut 节点，嵌 immutable schema 要用 `yyjson_val_mut_copy`；`yyjson_mut_obj_add_strn` 的 key 必须 NUL 结尾，跨 `string_view` 要用临时 `std::string`/`strncpy`。③ WIN32 GUI 的 spdlog stdout 抓不到 → MCP 验收用独立控制台 exe 编同源码。④ `ToolRegistry` 构造开放以便自检用局部实例，进程业务仍用 `Instance()`。⑤ demo 结果文本已改 yyjson 拼装；`Call` 先拷贝 `ToolHandler` 再调用，避免重入 `Register` 悬垂。

## [S1] Problem

ShineTV 需要把应用能力通过 **MCP（Model Context Protocol）** 暴露给外部 AI 客户端。仓库已有完整 P7 施工图，但 `src/mcp/` **尚未创建**，HTTP / JSON-RPC 也未接线。

当前痛点是：若按 UE 原版 `FShineMCPBuiltinTools::RegisterAll()` 把全部工具堆在一个文件里，业务模块（comfy / graph / gallery / video…）每加一个工具都要改 MCP 侧大文件，**注册点会成为热点与冲突源**。

需要一个**可独立交付的 MCP 工具注册地基**：

1. 类型与进程级 `ToolRegistry`（同名覆盖、幂等）；
2. **显式分模块注册 API** —— 每个业务模块只暴露自己的 `RegisterMcpTools(ToolRegistry&)`，启动时由 bootstrap 按固定顺序接入；
3. MCP `tools/list` JSON 形态与 `Call()` 语义就绪，供 P7.2/P7.3 协议层直接消费。

## [S2] Design

### [S2.1] 范围与边界

| 在范围内 | 不在范围内（后续 P7） |
|----------|------------------------|
| `src/mcp/` 类型、Schema 助手、`ToolRegistry`、模块分组 | libhv HTTP Server（P7.2，`WITH_HTTP_SERVER` 仍 OFF） |
| 显式模块注册入口 + bootstrap 接线 | JSON-RPC / SSE / MCP 协议端点（P7.3） |
| `BuildToolsListJson()` / `Call()` | 完整 Comfy / ShineTV 业务工具集（P7.4） |
| 1～2 个**桩工具**用于编译与自检 | 设置 UI / 端口 / 鉴权（P7.5） |

**明确不做**：不与 `src/agent/ToolRegistry`（小说 Agent / OpenAI tools）合并；两者协议与生命周期不同，命名空间隔离：`shine::mcp` vs `shine::agent`。

### [S2.2] 分层与目录

```
业务模块（comfy / graph / gallery / …）
    │  void RegisterMcpTools(mcp::ToolRegistry&)
    ↓
src/mcp/McpBootstrap     显式顺序调用各模块注册
    ↓
src/mcp/ToolRegistry     进程单例：Register / Find / Call / BuildToolsListJson
    ↓
src/mcp/MCPTypes + Schema   工具定义 / CallOutcome / JSON Schema 助手
```

| 文件 | 职责 |
|------|------|
| `src/mcp/MCPTypes.h` | `CallStatus` / `CallOutcome` / `Tool` / `ModuleInfo`；handler 签名 |
| `src/mcp/Schema.h` / `Schema.cpp` | `schema::Object / AddString / AddNumber / AddInteger / AddBoolean / AddStringArray` |
| `src/mcp/ToolRegistry.h` / `.cpp` | 进程单例；模块表 + 工具表；list / call |
| `src/mcp/McpBootstrap.h` / `.cpp` | `RegisterAllModules(ToolRegistry&)`；`kMcpServerName/Version` |

业务模块**不** `#include` libhv / MCP 协议细节；只依赖 `mcp/MCPTypes.h` + `mcp/ToolRegistry.h`（或仅 bootstrap 调用的声明头）。

### [S2.3] 核心契约（C++26，照 `Doc/RULES-LANG.md` §13.3）

```cpp
namespace shine::mcp {

// server 名/版本（协议层 initialize 用；本 feature 先定常量）
inline constexpr std::string_view kServerName = "ShineTVStudio";
inline constexpr std::string_view kServerVersion = "0.1.0";

enum class CallStatus { Ok, NotFound, BadArguments, InternalError };

struct CallOutcome {
  CallStatus status = CallStatus::Ok;
  std::string text;          // Ok → 给客户端的结果文本/JSON 字符串；否则中文错误说明
  [[nodiscard]] bool ok() const noexcept { return status == CallStatus::Ok; }
};

// args：tools/call 的 arguments 对象（yyjson object）；空参可为 nullptr
using ToolHandler = std::function<CallOutcome(yyjson_val* args)>;

struct ModuleInfo {
  std::string id;            // "comfy" / "graph" / "gallery"
  std::string title;         // 展示名，可中文
};

struct Tool {
  std::string name;          // 全局唯一，同名 Register 覆盖（幂等）
  std::string title;         // 可空 → list 时省略或回退 name
  std::string description;   // 给模型看
  std::string moduleId;      // 归属模块；空 = 未分组
  std::string schemaJson;    // inputSchema 的 JSON **对象**文本，owned
  ToolHandler handler;       // 必填；空 handler 注册时拒绝
};

} // namespace shine::mcp
```

**schemaJson 约定**：由 `schema::Object(required)` + `schema::Add*(doc, obj, ...)` 在模块注册时拼好并序列化为字符串存入 `Tool::schemaJson`；`BuildToolsListJson` 原样内嵌（解析校验失败则该工具 schema 退化为 `{"type":"object"}` 并打 warn）。

**Call 语义**：

| 情况 | `CallStatus` | `text` |
|------|--------------|--------|
| 工具不存在 | `NotFound` | `未知工具: <name>` |
| arguments 非法 JSON 对象 | `BadArguments` | `参数不是合法 JSON 对象` |
| 工具 handler 返回业务失败 | 由 handler 决定（通常 `InternalError` 或 `BadArguments`） | 中文说明 |
| handler 抛异常 | `InternalError` | `工具执行异常: <what>`（禁止崩溃进程） |

### [S2.4] ToolRegistry API

```cpp
namespace shine::mcp {

class ToolRegistry {
public:
  [[nodiscard]] static ToolRegistry& Instance();

  // 模块：Ensure 幂等（同 id 已存在则只更新 title）
  void EnsureModule(ModuleInfo info);
  void UnregisterModule(std::string_view id); // 可选是否级联删工具 → 默认级联
  [[nodiscard]] std::vector<ModuleInfo> Modules() const;
  [[nodiscard]] std::vector<std::string> ToolNames(std::string_view moduleId) const;

  // 工具：同名覆盖；name 空 / handler 空 → 拒绝并 warn
  // schemaJson 非 object → warn 并在注册时退化为 {"type":"object","properties":{},"required":[]}
  //（list 路径对坏 schema 同样退化，不因单工具拖垮 tools/list）
  void Register(Tool tool);
  bool Unregister(std::string_view name);
  void Clear() noexcept;                 // 清空工具与模块
  void ClearModule(std::string_view id); // 只清该模块工具 + 删除模块

  [[nodiscard]] const Tool* Find(std::string_view name) const;
  [[nodiscard]] std::size_t ToolCount() const noexcept;
  [[nodiscard]] std::size_t ModuleCount() const noexcept;

  // MCP tools/list 结果体：{"tools":[{"name","title","description","inputSchema"}]}
  // moduleId 不进 tools/list（MCP 规范无此字段）；分组只服务本进程诊断与后续过滤
  [[nodiscard]] std::string BuildToolsListJson() const;

  [[nodiscard]] CallOutcome Call(std::string_view name, yyjson_val* args);

private:
  // 存储：map/unordered_map by tool name；modules 有序 vector 或 map by id
};

// 显式模块注册总入口（幂等：重复调用同名覆盖，不重复堆叠）
void RegisterAllModules(ToolRegistry& reg);

} // namespace shine::mcp
```

**分模块注册约定（业务侧）**：

每个业务模块在自己的头文件暴露**唯一**入口（命名与现有 `RegisterShineNodes` / `RegisterBuiltinDecoders` 一致）：

```cpp
// 例如 src/comfy/ComfyMcpTools.h
namespace shine::comfy {
void RegisterMcpTools(mcp::ToolRegistry& reg);
}

// src/graph/GraphMcpTools.h
namespace shine::graph {
void RegisterMcpTools(mcp::ToolRegistry& reg);
}
```

本 feature **只接线 bootstrap + 桩模块**（`src/mcp` 内部 `RegisterDemoModule`：`mcp_ping`、`mcp_registry_info`），不批量实现 comfy/graph 业务工具（属 P7.4）。bootstrap 预留注释位与调用顺序表，业务模块就绪后只加一行 `EnsureModule` + 一行 `Xxx::RegisterMcpTools(reg)`。

**推荐 bootstrap 顺序**（稳定、与依赖无关）：

```text
demo（本 feature 桩） → comfy → graph → gallery → media → video → novel
```

### [S2.5] BuildToolsListJson 形态

```json
{
  "tools": [
    {
      "name": "mcp_ping",
      "title": "MCP 存活探测",
      "description": "返回 pong 与服务器名称/版本，用于验证注册表可用。",
      "inputSchema": { "type": "object", "properties": {}, "required": [] }
    }
  ]
}
```

- 字段：`name` 必填；`title` / `description` 有则写；`inputSchema` 必填对象。
- JSON 用 **yyjson** 拼装；禁止 `std::format` / 手写字符串拼 JSON 嵌套。
- 键序不保证稳定，客户端按名读取。

### [S2.6] Schema 助手

与 UE 版 `ShineMCPSchema` 对等，但改用 yyjson：

```cpp
namespace shine::mcp::schema {
[[nodiscard]] yyjson_mut_doc* NewDoc();
[[nodiscard]] yyjson_mut_val* Object(yyjson_mut_doc* doc,
                                     std::span<const std::string_view> required = {});
void AddString(yyjson_mut_doc* doc, yyjson_mut_val* schema,
               std::string_view name, std::string_view description,
               bool required = false, std::string_view defaultValue = {});
void AddNumber(...);
void AddInteger(...);
void AddBoolean(...);
void AddStringArray(...);
// 序列化 inputSchema 对象为 owned 字符串（给 Tool::schemaJson）
[[nodiscard]] std::string ToJsonString(yyjson_mut_doc* doc, yyjson_mut_val* schema);
}
```

模块注册时典型写法：

```cpp
auto* doc = schema::NewDoc();
auto* s = schema::Object(doc, {"path"});
schema::AddString(doc, s, "path", "图片绝对路径", true);
Tool t{
  .name = "demo_echo",
  .title = "回声",
  .description = "调试用：回显 arguments JSON",
  .moduleId = "demo",
  .schemaJson = schema::ToJsonString(doc, s),
  .handler = [](yyjson_val* args) -> CallOutcome { /* ... */ },
};
yyjson_mut_doc_free(doc);
reg.Register(std::move(t));
```

### [S2.7] 线程与安全

- **Registry 仅 UI / 启动线程写入**；`Call` 在 P7.2 之后由 HTTP handler 投递到 **UI 线程**再进 Registry（本 feature 无 HTTP，自检在同一线程）。
- 本阶段不加锁；若未来 worker 直接 `Call`，在 P7.2 的 dispatcher 契约里保证，**不在本 feature 造假线程安全**。
- 工具 handler 内遵守：`shine::log` 记日志；不阻塞 UI；Comfy 相关未来工具未连接时返回中文「未连接」而不是超时（P7.4 再落地）。

### [S2.8] CMake 与项目进度

- 根 `CMakeLists.txt` 的 `add_executable` **手工**追加：
  - `src/mcp/Schema.cpp`
  - `src/mcp/ToolRegistry.cpp`
  - `src/mcp/McpBootstrap.cpp`
- 不打开 `WITH_HTTP_SERVER`（仍 OFF，留给 P7.2）。
- 完成后同步：
  - `Plan/PROGRESS.md` → P7.1 S1–S4 勾选状态；
  - `Plan/证据.md` → P7.1 实测证据；
  - 本 spec `status` / `Report`。

### [S2.9] 验收（可观测）

1. **编译**：`cmake --build build -j 8` 通过，链接出 `ShineTVStudio.exe`。
2. **同名覆盖**：连续 `Register` 两次同 `name` → `ToolCount()==1`，后者 handler 生效。
3. **模块分组**：`EnsureModule` 两次同 id → `ModuleCount` 不增；`ClearModule("demo")` 后 demo 工具消失、其他仍在。
4. **tools/list**：`BuildToolsListJson()` 可被 yyjson 解析；含 `mcp_ping` 且 `inputSchema` 为对象。
5. **Call 三条**：
   - `mcp_ping` + `nullptr`/`{}` → `Ok`，text 含 `pong` 与 server 名；
   - 未知名 → `NotFound`，text 含工具名；
   - 非法 arguments（非对象的数组/标量视图）→ `BadArguments`，进程不崩。
6. **自检入口**：提供 `mcp::RunRegistrySelfCheck()`（类似 `agent::RunToolsSelfCheck`），启动可选调用或测试路径调用，日志输出 PASS/FAIL 摘要。

## [S3] Out of Scope

- libhv `HttpServer`、`WITH_HTTP_SERVER` 切换、端口占用处理（P7.2）。
- JSON-RPC `initialize` / `tools/call` 线协议、SSE、错误码表（P7.3）。
- `comfy_*` / `shinetv_*` 真实业务工具与「未连接」降级全文（P7.4）。
- 设置面板 MCP 段、运行时启停、端口校验（P7.5）。
- 把 `src/agent` 小说工具迁入 MCP。
- 远程访问、鉴权、多用户（`Doc/BASELINE.md` §8 明确不做）。

## Tasks

- [x] T1: 新增 `MCPTypes.h` + `Schema.h/.cpp` — acceptance: 类型与 schema 助手头文件完整，`schema::Object/AddString/ToJsonString` 可编译；无业务逻辑依赖。(covers: S2.3, S2.6)
- [x] T2: 新增 `ToolRegistry.h/.cpp`（EnsureModule / Register 同名覆盖 / Find / ClearModule / Modules） — acceptance: 模块与工具 CRUD 契约齐全；注册拒绝空 name/handler 并 warn。(covers: S2.4; depends: T1)
- [x] T3: 实现 `BuildToolsListJson()`（yyjson） — acceptance: 输出符合 S2.5 形态；坏 schemaJson 退化为 `{"type":"object"}` 不崩。(covers: S2.5; depends: T2)
- [x] T4: 实现 `Call()` 三条路径 + 异常捕获 — acceptance: Ok / NotFound / BadArguments / InternalError 行为符合 S2.3 表；handler 抛异常进程不崩。(covers: S2.3, S2.7; depends: T2)
- [x] T5: `McpBootstrap` + demo 模块（`mcp_ping` / `mcp_registry_info`） + CMake 登记 — acceptance: `RegisterAllModules` 幂等；demo 工具可 Find/Call；`add_executable` 含三份新 `.cpp`。(covers: S2.4, S2.8; depends: T4)
- [x] T6: `RunRegistrySelfCheck` + 构建运行验收 — acceptance: S2.9 六条全过，证据写入 `Plan/证据.md` 并勾 `Plan/PROGRESS.md` P7.1。(covers: S2.9; depends: T5)
- [x] T7: 确认业务模块扩展点文档化（bootstrap 顺序表 + 示例 `RegisterMcpTools` 签名） — acceptance: 本 spec S2.4 与 bootstrap 头文件注释一致，后续 P7.4 可按表加行。(covers: S2.4; depends: T5)
