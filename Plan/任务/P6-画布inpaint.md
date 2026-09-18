# P6 — 画布 inpaint

> **施工图**（每个 S 的做法与判据）。勾选与状态在 `../PROGRESS.md`；实测证据（含每个 S 的 ✅ 记录）在 `../证据.md`。

> 一次只做一个 S：做完 configure → build → 运行，逐条对验收，再回 `../PROGRESS.md` 勾选。

## P6.1 — 画布数据模型  ✅ 5/5（SHINE_PAINT_CHECK pass=13 fail=0）


- **S1 骨架** — 新增 `src/paint/PaintTypes.h`（`Tool` / `MaskMode` / `Settings`）、`PaintCanvas.h/.cpp`（声明全部公开接口，实现先留空）；CMake 登记。判据：编译通过。
- **S2 底图 + 初始快照** — `Resize(w,h,defaultRgb)` 分配 RGBA8 底图并保存初始快照；`BasePixels()` 返回 `std::span<const std::byte>`；`Revision()` 每次修改递增；`ResetToInitial()` 恢复快照。判据：`FillAll(Paint)` 后 `ResetToInitial()` 完全回到初始图。
- **S3 遮罩** — `MaskPixels()` 返回 `span<const uint8_t>`，**只允许 0 / 255**；`ClearMask()` 全 0；`FillAll(MaskPaint/MaskErase)` 全 255/全 0。判据：任意操作后抽样检查无中间值。
- **S4 笔画** — `BeginStroke/EndStroke`；`PaintStroke(fromUv, toUv, radiusUv, color, tool)` 做线段光栅化 + 圆头；`fromUv≈toUv` 也要落**一个圆点**；UV 边界环绕时不断笔；`Tool::Erase` 回写初始快照像素。判据：画 100 笔不越界、无空洞。
- **S5 无 UI 自测** — 写一段临时自测（或日志断言）覆盖验收 1–3，跑完后把结果贴 `../PROGRESS.md`，并**删掉临时代码**。

---

## P6.2 — 画布 UI  ✅ 4/4（自检 + 截图：侧栏工具 + 中央「画布」tab）


- **S1 视图骨架 + 中心 tab** — 新增 `src/paint/ui/PaintCanvasView.h/.cpp`（`Draw()`）；`src/app/App.cpp` 中央区增加「画布」tab（与「图」「图库」并列）；CMake 登记。判据：切到画布 tab 显示空白画布。
- **S2 交互** — 滚轮**以光标为锚点**缩放、空格/中键拖拽平移、`[` `]` 调笔刷半径、`Esc` 取消当前笔画；落点换算成 UV 后传给 `PaintStroke`。判据：缩放平移后笔刷落点与光标一致（不漂移）。
- **S3 工具条与显示** — 画底 / 擦底 / 画遮罩 / 擦遮罩 + 半径 + 颜色 + `FillAll` / `ClearMask` / `Reset`；底图纹理 + 遮罩半透明叠加（可切换可见性）。判据：切换工具后上一个笔画不残留（`EndStroke` 正确收尾）。
- **S4 纹理上传与验收** — 脏区或整图上传（≤ 2048² 时整图上传即可），仅 `Revision()` 变化时上传；跑验收 4 条（1024² 涂抹 ≥30fps、窗口拉宽收窄布局不错乱）并贴 `../PROGRESS.md`。

---

## P6.3 — inpaint 服务（编码 / 九节点图 / 取回）  ⬜ 0/5


- **S1 `PngCodec`** — 新增 `src/paint/PngCodec.h/.cpp`（`EncodePng` / `DecodePng`，RGBA8 ↔ PNG）；**libpng 符号只允许出现在这里与 PngDecoder**；CMake 登记。判据：编一张 64×64 图再解码，像素往返一致。
- **S2 `BuildInpaintGraph`（九节点图）** — 照下表逐节点拼 API JSON（**照抄原实现，别换节点**），连线按 `←id:slot`。判据：JSON 结构可直接 POST `/prompt`。

**九节点图（原实现抄本）**

| id | class_type | 关键输入 |
|----|-----------|---------|
| 1 | `CheckpointLoaderSimple` | `ckpt_name` |
| 2 | `LoadImage` | `image` = 原图上传名 |
| 3 | `LoadImageMask` | `image` = 遮罩上传名，`channel` = `red` |
| 4 | `VAEEncodeForInpaint` | `pixels`←2:0, `vae`←1:2, `mask`←3:0, `grow_mask_by` |
| 5 | `CLIPTextEncode`(正) | `text`, `clip`←1:1 |
| 6 | `CLIPTextEncode`(负) | `text`, `clip`←1:1 |
| 7 | `KSampler` | `seed/steps/cfg/denoise`、`sampler_name=dpmpp_2m`、`scheduler=karras`、`model`←1:0、`positive`←5:0、`negative`←6:0、`latent_image`←4:0 |
| 8 | `VAEDecode` | `samples`←7:0, `vae`←1:2 |
| 9 | `SaveImage` | `filename_prefix` = `Settings::outputPrefix`, `images`←8:0 |
- **S3 `RunInpaint` 全流程** — 编码底图 + 遮罩 PNG → 文件名带时间戳 → 上传（`type=input`）→ 提交 → WS 等待（**静默 20s 退回 `/history`**）→ `/view` 取回 → 解码 → 替换画布底图；全程 worker 线程，`onDone` 后画布已被结果替换。判据：涂一块点生成能贴回画布。
- **S4 遮罩语义与失败处理** — `MaskMode::Protect` 导出时**白 = 可重绘**、`Editable` 反之；失败**保留原画布** + 中文错误；生成中 UI 不卡、进度可见。
- **S5 参数区与验收** — `App.cpp` 画布面板「生成」按钮与参数区（checkpoint / prompt / 负向 / steps / cfg / denoise / seed / growMaskBy / 输出前缀）；跑验收 4 条，第 4 条按 `Doc/RULES-COMFY.md` §12 复核（无"卡死"判定、有完整错误块），结果贴 `../PROGRESS.md`。

---

## P6.4 — 画布与图库 / 媒体联动  ⬜ 0/3


- **S1 发送到画布** — `PaintCanvasView.cpp` 接收拖拽 + 右键菜单；`App.cpp` 图库项与输出项加「发送到画布」→ 载入为底图并**自动对齐画布尺寸**。判据：图库右键发送后画布显示该图、尺寸正确。
- **S2 结果落盘与回流** — `PaintService.cpp` 结果写入 `Settings().paintOutputDir`；刷新后出现在图库 / 输出列表。判据：生成结果能在两个列表里看到。
- **S3 导出 PNG + 验收** — 画布「导出为 PNG」（复用 `PngCodec`）；跑验收 3 条（含透明通道用系统看图程序打开正常）并贴 `../PROGRESS.md`。

---
