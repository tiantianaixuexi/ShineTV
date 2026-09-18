# ShineTV Studio — 基线与边界（参考材料，非任务清单）

> 本文件放**背景与约束**，不要往这里加任务：现状盘点（源插件能力）、技术栈与目录、已完成基线事实、明确不做清单。
> 任务清单见 `Plan/PLAN.md`；执行规则见 `Doc/RULES-AI.md`；语言约定见 `Doc/RULES-LANG.md`；Comfy 规程见 `Doc/RULES-COMFY.md`；视觉规范见 `Doc/STYLE-UI.md`；图库施工图见 `Plan/任务/G-图片库.md`。
> 章节号沿用拆分前 `PLAN.md` 的编号（§0 / §2 / §4 / §8），便于旧引用对照。

---

## 0. 现状盘点（源插件能力清单）

| 模块 | 源路径 | 能力 | 独立版落点 |
|------|--------|------|-----------|
| Comfy 客户端 | `ShineEditor/.../ShineComfyClient` | HTTP REST：queue/history/object_info/upload/interrupt | `src/comfy/`（P1 已有） |
| Comfy WS | `ShineComfySocket` | 进度、预览、执行状态 | `src/comfy/ComfySocket`（P1 已有） |
| 图编译 | `ShineVideoGraphCompiler` + Builders | 业务图 → ComfyUI API JSON（含 MiniMax H3） | `src/graph/GraphCompiler`（P3.4）+ `src/video/H3WorkflowBuilder`（P5.4） |
| 视频工程 | `ShineVideoProject` / Shot / Character | 分镜、链式首尾帧、@image/@char 引用 | `src/video/`（P5） |
| AI 贴图 | `ShineAIPaint*` | 画布 + 遮罩 + Comfy inpaint | `src/paint/`（P6） |
| 场景截图 | `ShineSceneCapture` / MCP Capture | UE 场景 → 图 | **不做**（改为导入图片 / 图库选图） |
| MCP | `ShineMCP` | Tool Registry + HTTP Server + 35 个内置工具 | `src/mcp/`（P7，只移植 Comfy 与本站能力） |
| 纹理图 | `ShineTextureEditor` | 纹理节点图（部分） | **不做** |
| 媒体预览 | `SShineComfyMediaPreview` | 图/视频预览 | `src/media/`（P4；视频不内嵌播放） |

`third/` 已有：imgui（**docking**）、VisualNodeSystem、libhv、yyjson、zmij、zlib、spdlog、fmt、mimalloc、function2、stdexec、**ImAnim**（ImGui 动画，待接入）。

---


## 2. 技术栈与目录

```
E:\c++\ShineTV\
├── Doc/AGENTS.md / Doc/BUILD.md / Plan/PLAN.md / `Plan/任务/G-图片库.md`
├── .mimocode/skills/            # 项目 Skill
├── CMakeLists.txt               # 源文件逐个列举，新增 .cpp 必须手工登记
├── src/
│   ├── main.cpp                 # Win32 + DX11 + mi_process_init + AttachDevice
│   ├── app/                     # 主循环、七区布局、中文字体、快捷键（P8.2）
│   ├── theme/                   # 主题 Token + 预设
│   ├── core/                    # Log(spdlog+fmt) / Async(stdexec) / Settings
│   ├── util/                    # 纯函数工具：Random / Strings / Time / Json（header-only，见 `Doc/RULES-LANG.md` §13.7）
│   ├── comfy/                   # HTTP/WS/Session/队列（P1 完成）+ NodeDef（P3.1）
│   ├── graph/                   # VNS GraphHost（P2 完成）+ ComfyNode（P3.2）+ GraphCompiler（P3.4）
│   ├── gpu/                     # 共享 DX11 纹理层（P4.1，与图库共用）
│   ├── gallery/                 # 图片库（并行线 G，见 `Plan/任务/G-图片库.md`）
│   ├── media/                   # 媒体历史 + 下载 + 预览（P4）
│   ├── video/                   # 视频工程 / 分镜 / H3 编译（P5）
│   ├── paint/                   # inpaint 画布（P6）
│   └── mcp/                     # Tool Registry + HTTP + MCP 协议（P7）
├── third/                       # imgui docking, VNS, libhv, yyjson, zlib, spdlog, fmt, mimalloc, stdexec
└── Plugins/                     # 原 UE 插件（只读参考，不编译）
```

**依赖策略**（全源码编入 exe，`-static` 链接，不引预编译 dll/lib）：

- 网络：libhv `hv_static`（HTTP + WebSocket）+ libhv HTTP Server（**P7.2 需打开 `WITH_HTTP_SERVER`**，当前 `CMakeLists.txt:21` 为 OFF）
- JSON：yyjson（Comfy / 设置 / 工程存盘）；VNS 图存盘沿用其自带 jsoncpp —— **两者不要混用**
- 格式化：fmt（`FMT_HEADER_ONLY` + `SPDLOG_FMT_EXTERNAL`）
- 日志：spdlog（stdout + UI sink，P8.4 加文件 sink）
- 内存：mimalloc（`static.c` + `mi_process_init()`）
- 异步：stdexec（`async::RunOnWorker / PostToUi / DrainUiQueue`，**线程池 4 线程，不要自建**）
- 节点图：VisualNodeSystem + imgui docking
- 图片：libpng + zlib（GALLERY S0 接入），供图库解码与画布 PNG 编解码复用

---


## 当前阶段（2026-09-19 盘点后，以 `Plan/PROGRESS.md` 为准）

- **P3 / P4 / P7 主线已完成**；**P5.1–P5.6 完成，余 P5.7**；**P6 / P8 未开始**。
- **G 图片库线在 `refactor/libhv-log-to-shine`（`c14133e`）已完成 G-S5–S14**（AVIF 搁置）—— **当前 main 工作树可能没有全套源码，禁止重做**。
- **小说 Agent**：P1–P8 本树代码完成；P9/P10 远程部分在完成分支；**AgentKit / NovelFields / 多模型 LLM / schema v5 已实现**（见 `docs/compose/plans/novel-agent/PROGRESS.md`）。
- 开工顺序：**T0 合并 f7a0cb8 → P5.7 → P6 → P8**（+ novel P10.4 / Key）。
- 进度与盘点：`Plan/PLAN.md` §0、`Plan/PROGRESS.md`。

> 下列 §4「已完成基线」中的部分条目（HTTP Server 未编译、无 `/view`、无 GraphCompiler 等）为 **P3/P4 之前的历史快照**，
> **不要当作当前能力清单**；当前能力以源码与 `Plan/PROGRESS.md` 为准。

---

## 4. 已完成基线（历史快照，后续步骤可直接依赖的事实，不要重新发明）

| 事实 | 位置 |
|------|------|
| DX11 设备是 `main.cpp` 文件级 static，唯一注入时机在 `ImGui_ImplDX11_Init()` 之后 | `src/main.cpp:17-18,150-152` |
| 异步只有五个函数：`async::Init/Shutdown/RunOnWorker/PostToUi/DrainUiQueue`，线程池**固定 4 线程** | `src/core/Async.h`、`Async.cpp:22` |
| `DrainUiQueue()` 已每帧在 `DrawFrame()` 调用 | `src/app/App.cpp:878` |
| `ComfySession` 已实现但**全仓无调用方**：`SubmitPromptJson(json, cb)`、`FetchHistory(maxItems, cb)` | `ComfySession.h:31-32` |
| `ObjectInfoRaw()` 保留完整 object_info JSON 但**从未被解析**；`ParseObjectInfoJson` 只填 `ok/rawJson/nodeClassCount` | `ComfyClient.cpp:252-270` |
| 已有端点：`/queue` `/object_info` `/history` `/prompt` `/interrupt` `/api/free` `/api/system_stats`；**没有 `/view`** | `ComfyClient.cpp:333-468` |
| 提交请求体只含 `prompt` + `client_id`（可选 `prompt_id/front`），**不用 `extra_data`** | `ComfyClient.cpp:383-413` |
| WS 已处理 `status / execution_start / executing / progress / execution_cached / executed / execution_success / execution_error / execution_interrupted`；**二进制预览帧被直接丢弃** | `ComfySocket.cpp:222-321` |
| WS 报文结构是 `{"type": "...", "data": {...}}`；连接地址是 `ws://<host>/ws?clientId=<uuid>`，且**同一个 uuid 必须放进 `/prompt` 请求体的 `client_id`**，事件才会回到本连接 | 官方 `api-examples` |
| **执行完成的官方判定**：`type == "executing"` 且 `data.node == null` 且 `data.prompt_id` 匹配（不是 `execution_success` 单独判定） | 官方 `api-examples` |
| `execution_error` 的完整字段只出现在 `/history/{prompt_id}` 的 `status.messages` 里（同一 dict 也会由 WS 推送）：`node_id / node_type / executed[] / exception_message / exception_type / traceback[] / current_inputs / current_outputs / timestamp` —— **当前 `PromptEvent` 只存了 3 个字段，缺 5 个** | 官方 `comms_routes` |
| `status` 事件的 `exec_info.queue_remaining` 是**唯一**可靠的队列剩余量来源（`ParseQueueJson` 从不填 `queueRemaining`） | `ComfyClient.cpp:168-199` |
| `HttpUploadImage(url, fileName, bytes, fields, timeout)` 已实现，multipart 字段名硬编码 `name="image"`，**无调用方** | `ComfyHttp.h:21-22`、`ComfyHttp.cpp:79-112` |
| VNS 支持运行时 `RegisterNodeType`，但**同 type 不可重复注册、无注销接口** | `VisualNodeFactory.h:27`、`VisualNodeFactory.cpp:14-47` |
| VNS 节点子类可覆写 `Draw()` 画控件、覆写 `ToJson/FromJson` 存自定义字段 | `VisualNode.h:73,126,127`；范例 `FloatVariableNode.cpp:43,50,77` |
| VNS 插口"类型"只是字符串标签（`AllowedTypes`），连接按 SocketID 匹配，**无 `GetSocketByName`** | `VisualNodeSocket.h:32,46-47,59` |
| `NodeArea::Connections` 是**私有**成员，没有公开的"枚举全部连线"API，需从节点侧反查 | `VisualNodeArea.h:386` |
| `ShineBasicNode` 固定 1 进 1 出 `"ANY"`；6 种占位类型硬编码在**两处** | `GraphHost.cpp:40-52,60-67`；`App.cpp:202-212` |
| 图存盘是 VNS 单 NodeArea 格式（jsoncpp），路径 `%APPDATA%\ShineTVStudio\graph.json` | `GraphHost.cpp:213-242`、`VisualNodeArea.cpp:188-276` |
| 全仓**没有任何"图 → ComfyUI API JSON"的代码** | 零命中 |
| libhv HTTP Server 当前**未编译进来** | `CMakeLists.txt:21` |
| ImGui 版本 **1.93.0 WIP（19297）**：`ImTextureID = ImU64`，`ImGui::Image` 收 `ImTextureRef` | `third/imgui/imgui.h:32-33,355,673` |

---


## 8. 明确不做的（首版）

- 不嵌回 UE 插件 / 不做 UObject 资产 / 不做 `.uasset` 存盘
- 不做 3D 场景截图与 3D 涂装（`ShineSceneCapture` / 画布在模型上涂画）
- 不做纹理节点图（`ShineTextureEditor`）
- 不做内嵌视频解码播放（视频交给系统播放器）
- 不做 SQLite / 索引数据库（图库同理，见 `Plan/任务/G-图片库.md` §2）
- 不做在线模型下载管理、不做安装包与自动更新
- 不做 UE 专有 MCP 工具（关卡/Actor/Python/截图/导入纹理/打开资产）
- 不做多用户 / 远程访问（MCP 默认仅本机、无鉴权）

---

