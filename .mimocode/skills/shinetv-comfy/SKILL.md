---
name: shinetv-comfy
description: ShineTV ComfyCore（src/comfy）架构：HTTP/WS 客户端、ComfySession、队列模型、线程与事件。当用户改连接/队列/进度/prompt 提交或问 ComfyUI API 怎么接时使用。
---

# ComfyCore（P1）架构

路径：`src/comfy/`。对齐 UE 版 `FShineComfyClient` / `FShineComfySocket` 语义。

## 文件职责

| 文件 | 职责 |
|------|------|
| `ComfyTypes.h/.cpp` | 结果/事件类型；`NormalizeBaseUrl` / `BuildApiUrl` / `BuildWebSocketUrl` / `MakeClientId` |
| `ComfyHttp.h/.cpp` | libhv 同步 GET/POST/multipart；**仅 worker 线程调用** |
| `ComfyClient.h/.cpp` | REST 封装 + yyjson 解析；`*Async` 走 stdexec → UI 回调 |
| `ComfySocket.h/.cpp` | `/ws` 单例；libhv `WebSocketClient` + `EventLoopThread`；自动重连 |
| `ComfySession.h/.cpp` | 进程内会话：baseUrl、clientId、状态灯、Tick 轮询、统一 API |
| `ComfyQueueModel.h/.cpp` | 线程安全本地队列视图（行 + 进度 + 状态） |

## 线程模型

```
UI 线程                     Worker (stdexec pool)           WS 线程 (libhv loop)
   |                              |                              |
   |-- Session::Tick ------------>|                              |
   |-- RefreshQueue (Async) ----->| HttpGet /queue               |
   |                              |-- PostToUi(result) -------->|
   |<-- DrainUiQueue -------------|                              |
   |                                                              |-- PromptEvent
   |                                                              |-- ApplyPromptEvent (QueueModel)
```

- HTTP：`comfy::FetchQueueAsync` 等，`RunPipeline` = `schedule(pool) | then | PostToUi`
- UI 邮箱：`async::PostToUi` / 每帧 `async::DrainUiQueue`（`App.cpp DrawFrame`）
- WS：`ComfySocket::EnsureConnected(baseUrl, clientId)`；回调在 WS 线程，Session 直接改 `QueueModel`（内部有锁）

## 对外入口（UI 应只用这个）

```cpp
auto& s = comfy::ComfySession::Instance();
s.Init(settings.comfyBaseUrl);   // App::Init
s.Tick(dt);                      // 每帧
s.SetBaseUrl(url);               // 设置页「保存并连接」
s.RefreshQueue();
s.RefreshObjectInfo();
s.SubmitPromptJson(apiJson, cb); // P3 图编译后调用
s.Interrupt(cb);
s.FreeVram(cb);
s.FetchSystemStats(cb);
s.Queue().Snapshot();            // 底栏表格
```

## REST 端点（P1）

`/system_stats` ping · `/object_info` · `/queue` · `/history` · `/prompt` · `/interrupt` · `/api/free` · `/api/system_stats` · `/upload/image`

## WS 事件

`status` · `execution_start` · `executing` · `progress` · `executed` · `execution_success` · `execution_error` · `execution_interrupted` · `execution_cached`

## 约定

1. **UI 不直接调 ComfyHttp**；只通过 Session / Client Async
2. 提交 prompt 必须带 `client_id`，与 WS 连接一致，事件才会回来
3. 完整产出清单用 `/history`（executed 事件往往只有第一个媒体）
4. 字符串格式化用 fmt：`log::Info("已提交 prompt {}", id)`，禁止 `vsnprintf`
5. 改解析逻辑时同步检查 UE 版 `ShineComfyClient.cpp` / `ShineComfySocket.cpp` 注释中的坑

## 相关 skill

`shinetv-thirdparty`（libhv/yyjson/stdexec）、`shinetv-ui-layout`（队列 UI）。
