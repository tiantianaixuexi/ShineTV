# ShineTV Studio — ComfyUI 错误捕获与连接健康规范（强制）

> **核心：ComfyUI 正在执行任务 ≠ 卡死。** 探活超时不改变连接状态，也不改 UI 文案。
> 依据：ComfyUI 官方文档 `comms_overview` / `comms_messages` / `comms_routes` / `api-examples`（2026-09 核实）。
> 适用 P3–P7 全部任务。章节号沿用拆分前 `PLAN.md` 的编号（本文件 §12）。
> **§12.6 是外部规范版本表（节点定义 JSON v2.0/1.0、工作流 JSON v1.0/0.4）** —— 动 ComfyUI 节点/工作流相关代码前必读，新代码一律按新版本写。

---

## 12. 错误捕获与连接健康规范（强制，适用于 P3–P7 全部任务）

> 依据：ComfyUI 官方文档 `docs.comfy.org/development/comfyui-server/` 的 **comms_overview / comms_messages / comms_routes / api-examples** 四页（2026-09 核实）。
> 本文只写**已核实**的内容；官方未明示的（如二进制帧头的字段划分）标注为"必须以本机实测为准"。

### 12.1 连接与报文（已核实）

| 项 | 事实 |
|----|------|
| WS 地址 | `ws://<host>/ws?clientId=<uuid>`（**`clientId` 是查询参数，大小写敏感**） |
| client_id 一致性 | 同一个 uuid 必须放进 `POST /prompt` 请求体的 `client_id`；否则事件不会回到该连接 |
| 文本帧 | `{"type": "<事件名>", "data": { ... }}` |
| 二进制帧 | 前 **8 字节**为头，其后为图像数据（官方示例用 `out[8:]` 取图）。**头内字段划分官方未逐一给出 → 实现时必须用本机实际帧校验，禁止写死推测值** |
| 完成判定 | `type == "executing"` 且 `data.node == null` 且 `data.prompt_id` 匹配 |
| 事件清单 | `status`/`execution_start`/`execution_cached`/`executing`/`progress`/`executed`/`execution_success`/`execution_error`/`execution_interrupted`（另有 `progress_state`，本仓库现有代码已在白名单里但未解析） |

**实现细化（2026-09-16，P3.0 已落地，后续照此做）**

libhv 的 `WebSocketClient` 只把**数据帧**交给 `onmessage`，**ping/pong 不外露**，所以不能靠"收到帧"直接证明连接活着。落地做法：

1. 每 **15s** 由 libhv 发 ping（`setPingInterval(15000)`）；
2. 记录"最近一次收到数据帧"的单调时间（`ComfySocket::SinceLastFrame()`，**二进制预览帧也算**）；
3. **30s 无帧只触发一次 HTTP 探活**（`PingAsync`，20s 节流），探活也失败才判 `Connecting` 重连并写明原因；
4. **进程被杀 / 网络断开由 libhv 的 `onclose` 立即捕获**（实测日志 `Comfy WS 断开，将自动重连`），不依赖 30s 计时。

> 为什么不能严格照字面"30s 无帧即断开"：ComfyUI 空闲时可能长时间不发 `status`，那样会把**健康的空闲连接**每 30s 重连一次。
> §12.4 的"忙碌 ≠ 卡死"同理：忙碌时的静默**绝不改连接状态**，90s 且 `queue_running` 未变才提示"疑似卡住（仍在队列中）"。

### 12.2 事件 → 日志 / UI 映射（P3.0 必须逐条实现）

| WS 事件 | 关键字段 | 日志级别 | UI 表现 |
|---------|----------|----------|---------|
| `status` | `data.exec_info.queue_remaining` | info（仅变化时） | 状态栏「队列剩余 N」 |
| `execution_start` | `prompt_id` | info | 任务行 → 运行中 |
| `execution_cached` | `nodes[]` | info（计数） | 任务行提示"跳过 N 个缓存节点" |
| `executing` | `node`（**`null` = 完成**）、`prompt_id` | info | 「执行中 · 节点 X」；`null` → 结束 |
| `progress` | `node`、`value`、`max` | info（节流 1s/条） | 进度条 N/M |
| `progress_state` | 节点级进度字典（版本差异大） | info（同上） | 有则显示，无则忽略 |
| `executed` | `node`、`output`（含 `images[].filename/subfolder/type`） | info | 产出文件入队（P4 下载） |
| `execution_success` | `prompt_id`、`timestamp` | info | 任务行 → 完成 |
| `execution_interrupted` | `prompt_id`、`node_id`、`node_type`、`executed[]` | warn | 任务行 → **已中断**（≠ 失败） |
| `execution_error` | 见 12.3 | **error（多行）** | 任务行 → 失败 + 可展开回溯 |

### 12.3 错误来源优先级（三条都要接）

1. **WS `execution_error`**（实时，优先）：`data` 字段与 `/history` 里那份完全一致。
2. **`GET /history/{prompt_id}` → `status.messages`**（补漏）：形如
   `[["execution_start",{...}], ["execution_cached",{...}], ["execution_error",{...}]]`，
   每项是 `[事件名, 数据]` 二元组；`status.status_str` 为 `success` / `error`。WS 漏收时用它补齐，**覆盖式更新，不重复报错**。
3. **`POST /prompt` 400**（提交期校验失败）：
   ```json
   { "error": { "type": "...", "message": "...", "details": "..." },
     "node_errors": { "3": { "errors": [ { "type": "value_not_in_list", "message": "Value not in list",
                                          "details": "ckpt_name: 'x.safetensors' not in [...]",
                                          "extra_info": { "input_name": "ckpt_name", "received_value": "x.safetensors" } } ],
                        "dependent_outputs": ["9"], "class_type": "CheckpointLoaderSimple" } } }
   ```
   → 逐条转成中文，标到具体节点；**不要只显示一段原始 JSON**。

`execution_error` 数据字段：`prompt_id`、`node_id`、`node_type`、`executed[]`、`exception_message`、`exception_type`、`traceback[]`、`current_inputs`、`current_outputs`、`timestamp`。

### 12.4 忙碌 vs 卡死（本规范的核心）

| 状态 | 触发条件 | 文案 | 允许的后续动作 |
|------|----------|------|----------------|
| `Disconnected` | WS 未连接 | 未连接（原因） | 自动重连，不弹"卡死" |
| `Idle` | WS 已连接、无任务 | 已连接 · 空闲 | 可提交新任务 |
| `Queued` | 有 prompt 在 `queue_pending` | 排队中（前面 N 个） | 等待，探活失败不算故障 |
| `Running` | 有 prompt 在 `queue_running` | 执行中 · 节点 X（N/M）· 队列剩余 K | **等待**；探活超时只打 warn |
| `Interrupted` | 收到 `execution_interrupted` | 已中断 | 可重新提交 |
| `Stalled` | `Running` **且** 90s 无任何 WS 帧 **且** `/queue` 的 `queue_running` 未变化 | 疑似卡住（仍在队列中） | 提示"可点中断"，**不得报"连接错误"** |

硬性要求：

- 探活（`/system_stats` 或 `/prompt` GET）**只在用户主动点「测试连接」时执行**，且必须在 worker 线程、超时 ≥ 10s。
- 探活超时但 `Busy()` 为 `Queued`/`Running` → 日志写 `service busy, probe timeout ignored: ...`，**连接状态保持"已连接"**。
- 任何"卡死/断开"的结论都必须基于 WS 帧静默 + 队列未变化，**不能基于一次 HTTP 超时**。
- **空闲静默 ≠ 断线**：空闲时 30s 无帧只触发一次 HTTP 探活（见 §12.1 实现细化），探活失败才重连。
- 自动化验证（AI 跑构建后的自检）如果发现"没反应"，第一步是读 `HealthSummary()` 与最近 20 条日志，而不是重试探测或重启服务。

### 12.5 日志模板（照抄）

```text
[info ] ws connected client=ab12cd34 (127.0.0.1:8188)
[info ] ws event executing prompt=3f9c1a node=5 type=KSampler
[info ] ws event progress  prompt=3f9c1a node=5 12/20
[info ] ws event executed  prompt=3f9c1a node=9 image=Shine_00012_.png subfolder= type=output
[info ] queue running=1 pending=0
[warn ] ws event interrupted prompt=3f9c1a node=5 type=KSampler
[error] exec failed prompt=3f9c1a node=5 type=KSampler
[error]   exception_type : torch.cuda.OutOfMemoryError
[error]   exception_msg  : CUDA out of memory. Tried to allocate 2.00 GiB
[error]   traceback     : File "execution.py", line 152, in execute
[error]   traceback     : File "nodes.py", line 1203, in sample
[error]   hint          : 显存不足：降低分辨率/步数，或先执行「释放显存」
[warn ] service busy, probe timeout ignored: running node=5 (12/20), queue_remaining=1
[warn ] submit rejected: node 3 CheckpointLoaderSimple ckpt_name 'x.safetensors' not in list
```

### 12.6 外部规范版本（节点定义 JSON / 工作流 JSON）—— **一律用新版本**

官方已把节点定义与工作流做成 **JSON Schema 规范**（draft-07）并带版本号。**新代码按新版本写，旧版本只做读取兼容。**

#### A. 节点定义 JSON v2.0（`ComfyNodeDefV2`）＝ 目标格式

- 根定义 `#/definitions/ComfyNodeDefV2`；顶层 **`additionalProperties: false`**（不许多字段）。
- 顶层必填 8 个：`inputs` / `outputs` / `name` / `display_name` / `description` / `category` / `output_node` / `python_module`；可选 `hidden` / `deprecated` / `experimental`。
- **`inputs` 是映射（对象）**：`{ "输入名": { "type": …, "name": … } }`；每项必填 `type` + `name`。
- `inputs` 每项公共可选字段：`default` / `defaultInput` / `forceInput` / `tooltip` / `hidden` / `advanced` / `rawLink` / `lazy` / **`isOptional`**。
- 类型专属字段：
  - `INT`：`min` / `max` / `step` / `display`(`slider`\|`number`\|`knob`) / `control_after_generate`
  - `FLOAT`：同上 + `round`（数字或 `false`）
  - `BOOLEAN`：`label_on` / `label_off`
  - `STRING`：`multiline` / `dynamicPrompts` / `defaultVal` / `placeholder`
  - `COMBO`：`options` / `control_after_generate` / `image_upload` / `image_folder`(`input`\|`output`\|`temp`) / `allow_batch` / `video_upload` / `remote{route,refresh,response_key,query_params,refresh_button,control_after_refresh,timeout,max_retries}`
- `outputs` 是**数组**，每项必填 `index` / `name` / `type` / `is_list`，可选 `options` / `tooltip`（同样 `additionalProperties: false`）。

#### B. 节点定义 JSON v1.0（`ComfyNodeDefV1`）＝ 只读兼容

- 顶层是 **`input`（单数）**，内含 `required` / `optional` / `hidden` **三个映射**。
- **单个输入是定长二元数组** `[类型标识, 选项对象]`：类型标识可为 `"INT"/"FLOAT"/"BOOLEAN"/"STRING"/"COMBO"`、**枚举数组**（旧式 COMBO，选项就在数组里）或任意自定义类型名（`IMAGE`/`LATENT`/`MODEL`…）。
- 输出侧是**分散数组**：`output`（类型名）/ `output_is_list` / `output_name` / `output_tooltips`；`output_node` 是布尔（"是输出节点"），与 `output` 数组含义不同。
- 无 `isOptional`（可选性由落在 `optional` 映射表达）、无 `defaultVal`。

#### C. 工作流 JSON v1.0（`workflow_json`）＝ 目标格式（**编辑器格式，不是提交格式**）

- `version` 必须为数字 **`1`**；必需 `version` / `state` / `nodes`；可选 `links` / `groups` / `reroutes` / `extra` / `models` / `config`。
- 节点项必需：`id` / `type` / `pos` / `size` / `flags` / `order` / `mode` / `properties`（另含 `inputs` / `outputs` / `widgets_values` / `color` / `bgcolor`）；`pos`/`size`/`offset`/`bounding` 支持 `[x,y]` 与 `{0:x,1:y}` 两种写法。
- 连线项必需：`id` / `origin_id` / `origin_slot` / `target_id` / `target_slot` / `type`（+ `parentId`）；**`reroutes` 是独立数组**（v1.0 特性）。
- 旧版 **0.4** 单独成文，读取时要兼容。
- **别混用**：工作流 JSON（编辑器，带坐标/分组/reroute）≠ **API 格式**（`{"<id>":{"class_type","inputs"}}`，POST `/prompt` 用）。

#### D. 获取方式（**必须实测，禁止杜撰端点**）

- 节点发现的既有端点是 **`GET /object_info`**（全量）与 **`GET /object_info/{node_class}`**（单节点）；其载荷是 **V1 形状**（`input` / `input_order` / `output` / `output_is_list` …）。
- **V2 载荷从哪个端点返回，官方规范页未写明**（该页只定义 Schema）。因此硬性要求：
  1. 解析器**自动识别形状**（V2 判据：顶层有 `inputs` 对象，且 `outputs[]` 项含 `index` / `is_list`），不依赖外部开关；
  2. 首次联调必须用 `/object_info` 与 `/object_info/{class}` 的**原始响应**实测本机版本给的是哪种形状，并把结论与样例写回 `Plan/任务/P3-节点与图.md`；
  3. **禁止**调用未证实的端点（例如凭空杜撰 `/api/node_defs`）。

出处（2026-09 核实）：
- 节点定义 JSON v2.0：`https://docs.comfy.org/zh/specs/nodedef_json`
- 节点定义 JSON 1.0：`https://docs.comfy.org/zh/specs/nodedef_json_1_0`
- 工作流 JSON v1.0：`https://docs.comfy.org/zh/specs/workflow_json`
- 工作流 API 格式：`https://docs.comfy.org/zh/development/api-development/workflow-api-format`
- 规范变更讨论：`https://github.com/comfy-org/rfcs`

### 12.7 官方文档出处

- 总览：`https://docs.comfy.org/development/comfyui-server/comms_overview`
- WS 消息：`https://docs.comfy.org/development/comfyui-server/comms_messages`
- HTTP 路由：`https://docs.comfy.org/development/comfyui-server/comms_routes`
- 示例（含 WS + History 组合、`clientId` 用法、二进制帧 `out[8:]`）：`https://docs.comfy.org/zh/development/comfyui-server/api-examples`

> 版本差异提醒：官方文档明确部分字段结构会随版本变化（例如 `/queue` 条目格式、`progress_state` 的节点级结构）。
> 因此 P3.0 的实现要求是：**解析器对未知字段宽容、对缺失字段不崩**，并在日志里打印未识别事件的 `type`（便于按本机版本补齐）。

---

