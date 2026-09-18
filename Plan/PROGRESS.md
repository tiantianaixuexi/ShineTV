# ShineTV Studio — 进度（唯一勾选入口）

> **规则**：一次只做一个 S；做完 `configure → build → 运行`，逐条对验收，再回来勾选并写证据。
> 本文件**只放勾选与状态**：施工图 `任务/<大类>.md`｜**实测证据 `证据.md`**｜
> 踩坑与验收手法 `坑与手法.md`｜已完成的旧线 `归档-已完成.md`。
> 各小类下的 `证据：证据.md → X` 是**当时的记录索引**；`证据.md` 自 2026-09-17 起**只保留最近几条**，早期条目不再保留。

> 状态口径：⬜ 未开始 ｜ 🟡 进行中 ｜ ✅ 通过 ｜ ⛔ 阻塞（必须写原因）。

## 总览

| 大类 | 内容 | 小类 | 小任务 | 已完成 | 状态 |
|------|------|------|--------|--------|------|
| **P3** | 节点与图 | 8 | 58 | 58 | ✅ 通过 |
| **P4** | 媒体与纹理 | 5 | 27 | 27 | ✅ 通过 |
| **P5** | 视频分镜 | 7 | 36 | 31 | 🟡 进行中 |
| **P6** | 画布 inpaint | 4 | 17 | 0 | ⬜ 未开始 |
| **P7** | MCP 服务 | 5 | 21 | 9 | 🟡 进行中 |
| **P8** | 收尾 | 4 | 13 | 0 | ⬜ 未开始 |
| **G** | 图片库（并行线） | 15 | 79 | 35 | 🟡 进行中 |
| 归档 | P0 / P1 / P2 / P2.9 / R 线 | — | — | — | ✅ `归档-已完成.md` |

**合计**：251 个小任务，已完成 **154**。

## 现在做哪个

- **已完成**：**P3（58/58，含新增 P3.7 从 ComfyUI 拉取工作流）**、P4（27/27）、**P5.1–P5.6（31/36）**、G-S0–S4（29/79）、**P7.1–P7.2（9/21）**；归档见 `归档-已完成.md`（P0/P1/P2/P2.9/R）。
- **P3.7 的连带发现（重要，影响 P5.4）**：官方 H3 参考图模板（`/templates/video_minimax_h3_r2v.json`）已能完整拉下来并生成节点（26 节点 / 33 连线）；
  它是 **`BasicGuider`、无 `ConditioningZeroOut`、无 `MiniMaxH3SigmaShift`**，而 P5.4 手写的是
  `CFGGuider + ConditioningZeroOut + SigmaShift`（`/object_info` 校验能过但不是官方式样）→ **P5.4 值得按官方模板复核**。
- **P5 进行中**：只剩 **P5.7（分镜图生成，5）**（P5.6 视频结果预览已完成：播放 / 首帧缩略图 / 文件缺失与自动选中）。
  ⚠️ **P5.7 缺前置**：本机 `models/checkpoints` 里**没有 SD/SDXL 出图模型**（只有 `ltx-2-19b-dev-fp8.safetensors`，`diffusion_models` 为空），
  `CheckpointLoaderSimple` 无模型可选 → 要真机验收 P5.7 得先放一个 SD1.5/SDXL checkpoint 进去（或在 P5.7 里做"无模型时给出中文提示"的降级）。
- **G 线也不阻塞**：可交叉做 **G-S5（挂进六区）** —— 施工图 `任务/G-图片库.md` 的 `G-S5`：
  `GalleryModel` + `Gallery.h/.cpp` 模块入口 + `ui/GalleryView`（先列表）+ 侧栏 `DrawGallerySidePanel()`
  （来源切换/置灰，直接用 G-S4 的 `IsSourceAvailable` / `SourceUnavailableHint`）+ `App.cpp` 挂载
  （**必须加 `gallery::Tick()`**，且 Shutdown 顺序在 `async::Shutdown()` 之前）+ `DockLayout` + `Ctrl+O`。
  之后 **G-S6（Resize + 最简网格）= 第一次可用（缩略图墙）**。
- **P5–P8 未开始**，依赖顺序见 `PLAN.md` §5；其中 P5/P6 可与 G 线交叉做。
- 提示：真机 ComfyUI 已可用（`F:\AI\ComfyUI-aki-v3`，无头启动命令见 `HANDOFF.md` §4）；
  **本机没有 SD/SDXL 出图模型**，"真出图"验收用不依赖模型的节点（`EmptyImage → SaveImage` 已验证可用）。
- 两条线互不阻塞；**G-S3/S8 与 P4.1 共用 `src/gpu/`**，已由 P4.1 先建，G 侧只接入不重做（见 `PLAN.md` §4）。


## P3 — 节点与图

> 施工图 `任务/P3-节点与图.md` ｜ 证据 `证据.md`

### P3.0 — WS 事件与错误采集层（忙碌判定 + 错误全字段）

✅ **10/10** ｜ 证据：`证据.md` → P3.0

- [x] S1 补齐 `PromptEvent` 字段
- [x] S2 WS 报文解析补全
- [x] S3 完成语义 + 帧静默计时
- [x] S4 `BusyState` 与健康摘要
- [x] S5 队列行状态机
- [x] S6 错误来源②：`/history` 覆盖式补齐
- [x] S7 错误来源③：`/prompt` 400 解析
- [x] S8 日志规范落地
- [x] S9 UI 接线
- [x] S10 验收自测（6 条全跑）

### P3.1 — 节点定义 JSON（v2.0 优先 / v1.0 兼容）→ `NodeTypeDef` 结构化解析

✅ **9/9** ｜ 证据：`证据.md` → P3.1

- [x] S1 骨架
- [x] S2 数据模型对齐 v2.0 规范
- [x] S3 解析 V2 形状：`inputs` 映射
- [x] S4 解析 V2 顶层与 `outputs`
- [x] S5 解析 V1 形状（只读兼容）
- [x] S6 自动识别形状 + 实测确认 V2 来源
- [x] S7 接进既有解析路径
- [x] S8 `ComfySession` 缓存与查询
- [x] S9 容错 + 验收

### P3.2 — `ShineComfyNode` 通用可编辑节点

✅ **7/7** ｜ 证据：`证据.md` → P3.2

- [x] S1 新建 `ComfyNode.h/.cpp` 骨架
- [x] S2 插口生成
- [x] S3 控件绘制（`Draw`）
- [x] S4 节点尺寸自适应
- [x] S5 存盘（`ToJson` / `FromJson`）
- [x] S6 插口规格稳定性
- [x] S7 验收自测（4 条）

### P3.3 — 动态注册 + 节点面板动态化

✅ **7/7** ｜ 证据：`证据.md` → P3.3

- [x] S1 动态注册接口
- [x] S2 类型注册 + 插口分色
- [x] S3 节点面板动态化
- [x] S4 创建方式两条都通
- [x] S5 离线兜底
- [x] S6 旧存档容错
- [x] S7 验收自测（4 条）

### P3.4 — 图编译器：VNS 图 → ComfyUI API JSON

✅ **7/7** ｜ 证据：`证据.md` → P3.4

- [x] S1 新建 `GraphCompiler.h/.cpp` 骨架
- [x] S2 `EnumerateLinks()`
- [x] S3 节点 id 发号（确定性）
- [x] S4 inputs 映射
- [x] S5 Reroute 透传 + 忽略项
- [x] S6 必填校验
- [x] S7 验收自测（4 条）

### P3.5 — 提交接线（运行 / 中断 / 错误回显）

✅ **6/6** ｜ 证据：`证据.md` → P3.5

- [x] S1 `GraphHost` 提交接口
- [x] S2 工具条按钮
- [x] S3 成功路径回显
- [x] S4 失败路径回显
- [x] S5 复核 `Doc/RULES-COMFY.md` §12
- [x] S6 验收自测（6 条）

### P3.6 — 工作流导入 / 导出

✅ **9/9** ｜ 证据：`证据.md` → P3.6

- [x] S1 骨架 + 格式判定
- [x] S2 导出 API 格式
- [x] S3 导入 API 格式：建节点
- [x] S4 导入 API 格式：连线与控件值
- [x] S5 导出工作流 v1.0
- [x] S6 导入工作流 v1.0（兼容 0.4）
- [x] S7 未知类型不静默丢弃
- [x] S8 接 UI 入口
- [x] S9 验收自测（5 条）

### P3.7 — 从 ComfyUI 拉取工作流（远端导入）

✅ **3/3** ｜ 证据：`证据.md` → P3.7 ｜ 施工图：`任务/P3-节点与图.md` → P3.7

- [x] S1 `comfy/ComfyWorkflows.*`（三来源清单 + 正文；纯解析离线可测）
- [x] S2 **两个独立浏览器窗**（同一套布局：左分类 + 可拖分隔条 + 多行 tooltip、右条目表 + 搜索、点击才创建）：
      工作流模板（工具条「模板」/ 文件菜单）+ **节点浏览器**（工具条「节点」/ 视图菜单）；侧栏留入口按钮 + 真机自检钩子
- [x] S3 真机验收（官方 H3 模板 26 节点 / 33 连线）+ 修 v0.4 连线与插口类型语义


## P4 — 媒体与纹理

> 施工图 `任务/P4-媒体与纹理.md` ｜ 证据 `证据.md`

### P4.1 — 共享 GPU 纹理层 + `/view` 下载

✅ **8/8** ｜ 证据：`证据.md` → P4.1

- [x] S1 设备注入
- [x] S2 `GpuTexture`
- [x] S3 `GpuTextureManager`
- [x] S4 `GpuTextureCache`（通用 LRU）
- [x] S5 二进制下载
- [x] S6 `/view` 取图
- [x] S7 端到端画出来
- [x] S8 验收自测（4 条）

### P4.2 — 媒体历史与本地缓存

✅ **6/6** ｜ 证据：`证据.md` → P4.2

- [x] S1 `MediaLibrary` 骨架
- [x] S2 `Refresh(maxItems=100)`
- [x] S3 `EnsureLocal`（下载 + 落盘 + 缓存命中）
- [x] S4 缓存统计与设置项
- [x] S5 App 接线
- [x] S6 验收自测（4 条）

### P4.3 — 右栏预览面板真图渲染

✅ **4/4** ｜ 证据：`证据.md` → P4.3

- [x] S1 选中来源打通
- [x] S2 渲染样式
- [x] S3 点击与空态
- [x] S4 验收自测（3 条）

### P4.4 — 底栏「输出」页：历史结果列表

✅ **5/5** ｜ 证据：`证据.md` → P4.4

- [x] S1 `OutputView` 骨架
- [x] S2 列表
- [x] S3 右键菜单
- [x] S4 双击 + 自动置顶
- [x] S5 接入底栏 tab

### P4.5 — 生成过程预览

✅ **4/4** ｜ 证据：`证据.md` → P4.5

- [x] S1 接住二进制预览帧
- [x] S2 解码 + 节流 + 上屏
- [x] S3 兜底路径（必做）
- [x] S4 覆盖层与收尾


## P5 — 视频分镜

> 施工图 `任务/P5-视频分镜.md` ｜ 证据 `证据.md`

### P5.1 — 视频工程数据模型 + JSON 存盘

✅ **5/5** ｜ 证据：`证据.md` → P5.1

- [x] S1 `VideoTypes.h`
- [x] S2 `VideoProject` + 存盘
- [x] S3 `Sanitize()` 自动纠正
- [x] S4 目录设置项
- [x] S5 验收自测（3 条）

### P5.2 — `@image` / `@char` / `{{Mixed N}}` 引用解析器

✅ **5/5** ｜ 证据：`证据.md` → P5.2

- [x] S1 骨架
- [x] S2 `@image:` 解析
- [x] S3 `@char:` 解析
- [x] S4 顺序与去重
- [x] S5 异常与验收

### P5.3 — 分镜表 UI

✅ **5/5** ｜ 证据：`证据.md` → P5.3

- [x] S1 表格骨架
- [x] S2 行操作
- [x] S3 右侧编辑区
- [x] S4 拖拽与状态列
- [x] S5 App 接线 + 验收

### P5.4 — H3 工作流编译器（ref2va / fl2va + 链式）

✅ **7/7** ｜ 证据：`证据.md` → P5.4

- [x] S1 骨架
- [x] S2 对齐工具
- [x] S3 模型段
- [x] S4 每段 conditioning 与采样
- [x] S5 链式首尾帧
- [x] S6 收尾与异常
- [x] S7 验收自测（4 条）

### P5.5 — 视频任务执行器

✅ **6/6** ｜ 证据：`证据.md` → P5.5

- [x] S1 骨架与状态机
- [x] S2 上传（工作副本）
- [x] S3 提交与运行
- [x] S4 完成判定与兜底
- [x] S5 落盘回填与取消
- [x] S6 UI 接线与验收

### P5.6 — 视频结果预览

✅ **3/3** ｜ 证据：`证据.md` → P5.6

- [x] S1 播放（分镜表**状态列**「播放」+ 编辑区「最近一次运行」每个产物「播放/定位」；`util::Shell.h` 统一交给系统播放器；缺失/无关联给中文提示）
- [x] S2 首帧缩略图（`src/media/VideoThumb.*` = Windows Shell 缩略图 + `System.Video.FrameWidth/Height`；`MediaItem` 带分辨率，`TextureState::Unavailable`；「输出」页视频行显示首帧 + `704×1280`）
- [x] S3 文件缺失与自动选中 + 验收（缺失 → 按钮置灰 + 红字「文件不存在」不崩；完成后 `RefreshAndSelectLatest()` 自动选中最近输出；自检 6 条全 PASS + 截图复核）

### P5.7 — 分镜图生成（SceneToImage / SD1.5 线）

⬜ **0/5** ｜ 证据：`证据.md` → P5.7

- [ ] S1 骨架
- [ ] S2 基础 img2img 线
- [ ] S3 ControlNet 线
- [ ] S4 参数与降级
- [ ] S5 UI 与验收


## P6 — 画布 inpaint

> 施工图 `任务/P6-画布inpaint.md` ｜ 证据 `证据.md`

### P6.1 — 画布数据模型

⬜ **0/5** ｜ 证据：`证据.md` → P6.1

- [ ] S1 骨架
- [ ] S2 底图 + 初始快照
- [ ] S3 遮罩
- [ ] S4 笔画
- [ ] S5 无 UI 自测

### P6.2 — 画布 UI

⬜ **0/4** ｜ 证据：`证据.md` → P6.2

- [ ] S1 视图骨架 + 中心 tab
- [ ] S2 交互
- [ ] S3 工具条与显示
- [ ] S4 纹理上传与验收

### P6.3 — inpaint 服务（编码 / 九节点图 / 取回）

⬜ **0/5** ｜ 证据：`证据.md` → P6.3

- [ ] S1 `PngCodec`
- [ ] S2 `BuildInpaintGraph`（九节点图）
- [ ] S3 `RunInpaint` 全流程
- [ ] S4 遮罩语义与失败处理
- [ ] S5 参数区与验收

### P6.4 — 画布与图库 / 媒体联动

⬜ **0/3** ｜ 证据：`证据.md` → P6.4

- [ ] S1 发送到画布
- [ ] S2 结果落盘与回流
- [ ] S3 导出 PNG + 验收


## P7 — MCP 服务

> 施工图 `任务/P7-MCP.md` ｜ 证据 `证据.md`

### P7.1 — Tool Registry

✅ **4/4** ｜ 证据：`证据.md` → P7.1 ｜ Spec：`docs/compose/spec/mcp-tool-registry.md`

- [x] S1 骨架
- [x] S2 注册与查找
- [x] S3 `BuildToolsListJson()`
- [x] S4 `Call()` 与验收

> 扩展（compose `mcp-tool-registry`）：显式分模块注册 API（`ModuleInfo` / `EnsureModule` / `ClearModule` / `McpBootstrap::RegisterAllModules` + demo 桩 `mcp_ping`/`mcp_registry_info`）。

### P7.2 — HTTP Server（libhv）

✅ **5/5** ｜ 证据：`证据.md` → P7.2

- [x] S1 打开 libhv server 并建骨架
- [x] S2 启停与错误
- [x] S3 路由与默认响应
- [x] S4 UI 线程投递
- [x] S5 Settings + `/health` + 验收

> `WITH_HTTP_SERVER=ON`；`src/mcp/HttpServer.*`；Settings `mcpEnabled/mcpListenAddr/mcpPort`（默认关）；`SHINE_MCP_HTTP_CHECK=1` 自检 OVERALL PASS。

### P7.3 — MCP 协议端点

⬜ **0/5** ｜ 证据：`证据.md` → P7.3

- [ ] S1 骨架与路由注册
- [ ] S2 JSON-RPC 主入口
- [ ] S3 空实现与通知
- [ ] S4 SSE
- [ ] S5 错误码与验收

### P7.4 — 内置工具集

⬜ **0/4** ｜ 证据：`证据.md` → P7.4

- [ ] S1 骨架与注册钩子
- [ ] S2 Comfy 桥接 9 个工具
- [ ] S3 ShineTV 能力 5 个工具
- [ ] S4 校验与验收

### P7.5 — MCP 设置与安全

⬜ **0/3** ｜ 证据：`证据.md` → P7.5

- [ ] S1 设置段 UI
- [ ] S2 运行时启停
- [ ] S3 端口校验 + 验收


## P8 — 收尾

> 施工图 `任务/P8-收尾.md` ｜ 证据 `证据.md`

### P8.1 — 设置集中化与校验

⬜ **0/3** ｜ 证据：`证据.md` → P8.1

- [ ] S1 分区集中
- [ ] S2 字段补齐
- [ ] S3 校验与验收

### P8.2 — 快捷键系统

⬜ **0/3** ｜ 证据：`证据.md` → P8.2

- [ ] S1 注册表骨架
- [ ] S2 新增快捷键 + 守卫
- [ ] S3 帮助页与验收

### P8.3 — 打包与首次运行

⬜ **0/3** ｜ 证据：`证据.md` → P8.3

- [ ] S1 Release 静态单文件
- [ ] S2 版本号单一来源
- [ ] S3 首启引导 + 验收

### P8.4 — 稳定性收尾

⬜ **0/4** ｜ 证据：`证据.md` → P8.4

- [ ] S1 Shutdown 顺序审计
- [ ] S2 设备丢失恢复
- [ ] S3 Worker 兜底
- [ ] S4 日志落盘 + 稳定性验收


## G — 图片库（并行线）

> 施工图 `任务/G-图片库.md` ｜ 参考材料 `任务/G-参考.md` ｜ 证据 `证据.md`

### G-S0 — 前置接入（依赖 + CMake + 设备注入 + 设置项）

✅ **9/9** ｜ 证据：`证据.md` → G-S0

- [x] S1 zlib 接进 CMake
- [x] S2 vendor libpng
- [x] S3 vendor imageinfo
- [x] S4 CMake：libpng 静态库 + 链接
- [x] S5 `src/gpu/GpuDevice.h/.cpp`（共享层）
- [x] S6 设备注入
- [x] S7 Settings 字段
- [x] S8 设置窗口「图库」段
- [x] S9 验收自测（3 条）

### G-S10 — 独立查看器（缩放 / 平移 / 切换）

⬜ **0/5** ｜ 证据：`证据.md` → G-S10

- [ ] S1 打开/关闭接口
- [ ] S2 缩放与平移
- [ ] S3 键盘与浮层
- [ ] S4 原图加载
- [ ] S5 验收（3 条）

### G-S11 — 交互动作（右键菜单 / @image / 拖拽）

⬜ **0/5** ｜ 证据：`证据.md` → G-S11

- [ ] S1 `FileActions.h/.cpp`
- [ ] S2 右键菜单与多选
- [ ] S3 设为工作流输入（`UploadToComfy`）
- [ ] S4 拖拽
- [ ] S5 错误规范 + 验收（4 条）

### G-S12 — 多格式（拆成三个小步，逐个落地）

⬜ **0/4** ｜ 证据：`证据.md` → G-S12

- [ ] S1 JpegDecoder（S12a）
- [ ] S2 WebpDecoder（S12b）
- [ ] S3 AvifDecoder（S12c）
- [ ] S4 隔离验证 + 验收

### G-S13 — EXIF 方向 + 元数据面板

⬜ **0/5** ｜ 证据：`证据.md` → G-S13

- [ ] S1 `ExifOrientation.h/.cpp`
- [ ] S2 读取来源（不引通用 EXIF 库）
- [ ] S3 接入 `ImageLoader`
- [ ] S4 属性面板
- [ ] S5 验收（3 条）

### G-S14 — 磁盘缩略图缓存 + 搜索排序 + 最终验收

⬜ **0/5** ｜ 证据：`证据.md` → G-S14

- [ ] S1 `cache/DiskThumbCache.h/.cpp`
- [ ] S2 ThumbnailService 接入磁盘层
- [ ] S3 `GalleryModel` 排序与过滤实装
- [ ] S4 `GalleryView` 顶部控件
- [ ] S5 验收（4 条）

### G-S1 — Image / ImageInfo / MetaProbe

✅ **5/5** ｜ 证据：`证据.md` → G-S1

- [x] S1 `GalleryTypes.h`
- [x] S2 `Image.h/.cpp`
- [x] S3 `MetaProbe.h/.cpp`
- [x] S4 CMake 登记 + 隔离检查
- [x] S5 验收自测（3 条）

### G-S2 — IImageDecoder / ImageLoader / PngDecoder

✅ **5/5** ｜ 证据：`证据.md` → G-S2

- [x] S1 `decoders/IImageDecoder.h`
- [x] S2 `decoders/PngDecoder`
- [x] S3 `ImageLoader`
- [x] S4 CMake + 符号隔离
- [x] S5 验收自测（2 条）

### G-S3 — GpuTexture / GpuTextureManager

✅ **5/5** ｜ 证据：`证据.md` → G-S3

- [x] S1 `GpuTexture.h/.cpp`
- [x] S2 `GpuTextureManager`
- [x] S3 ImGui 用法对齐
- [x] S4 临时验收代码
- [x] S5 验收自测（3 条）

### G-S4 — ImageScanner（本地 + ComfyUI 目录）

✅ **5/5** ｜ 证据：`证据.md` → G-S4

- [x] S1 `ImageScanner.h/.cpp`
- [x] S2 `ScanAsync`（worker + PostToUi）
- [x] S3 `ui/FolderPicker`
- [x] S4 Comfy 来源可用性
- [x] S5 CMake + 验收（3 条）

### G-S5 — 挂进六区

✅ **6/6** ｜ 证据：`证据.md` → G-S5

- [x] S1 `GalleryModel`
- [x] S2 `Gallery.h/.cpp`（模块入口）
- [x] S3 `ui/GalleryView`（先出列表）
- [x] S4 App.cpp 挂载（逐处核对待办）
- [x] S5 DockLayout
- [x] S6 CMake + 验收（4 条）

### G-S6 — Resize + 最简网格 = **第一次可用**

⬜ **0/5** ｜ 证据：`证据.md` → G-S6

- [ ] S1 `Resize.h/.cpp`
- [ ] S2 `GalleryLayout.h/.cpp`
- [ ] S3 `GalleryView` 列表 → 网格
- [ ] S4 `ui/GalleryViewer` 第一版
- [ ] S5 验收（第一次可用，5 条全打勾）

### G-S7 — 异步缩略图管线

⬜ **0/5** ｜ 证据：`证据.md` → G-S7

- [ ] S1 `ThumbnailService` 骨架
- [ ] S2 worker 流水线
- [ ] S3 并发与队列上限
- [ ] S4 失败处理与日志
- [ ] S5 GalleryView 接线 + 验收（3 条）

### G-S8 — CPU 缩略图 LRU + GPU 纹理 LRU

⬜ **0/5** ｜ 证据：`证据.md` → G-S8

- [ ] S1 `cache/CpuThumbCache`
- [ ] S2 `src/gpu/GpuTextureCache`（共享 LRU）
- [ ] S3 ThumbnailService 两级缓存接入
- [ ] S4 设备丢失 + 可观测
- [ ] S5 验收（3 条）

### G-S9 — 虚拟化网格

⬜ **0/5** ｜ 证据：`证据.md` → G-S9

- [ ] S1 `GalleryLayout` 可见区间
- [ ] S2 `GalleryView` 只请求可见项
- [ ] S3 `Ctrl+滚轮` 切尺寸档
- [ ] S4 优先级接线
- [ ] S5 验收（3 条）
