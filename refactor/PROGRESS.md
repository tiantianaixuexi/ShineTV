---
id: refactor.progress
kind: status
status: current
source_of_truth:
  - refactor/phases.md
  - refactor/architecture.md
  - refactor/design-spec.md
  - CMakeLists.txt
  - src/ui/imgui/
last_verified: 2026-09-30
---

# 重构进度看板

> 最后更新：**2026-09-30**
> 步骤定义见 [`phases.md`](phases.md)。每完成一步就勾上并补验证记录。

图例：`[ ]` 未开始 · `[~]` 进行中 · `[x]` 已完成并通过验收 · `[!]` 阻塞

> ⚠️ **与 phases.md 的两处偏离**（用户明确要求，phases.md 未同步）：
> 1. 渲染后端用 **OpenGL**（`imgui_impl_opengl3` + `opengl32`），不是 D3D11。
>    `shine::gpu::AttachDevice` 注入的是自己解析的 GL 函数表（`Host.cpp:194`）。
> 2. P6.1 的抓图走 `glReadPixels`，不是 D3D11 后备缓冲。

---

## 总览

| 阶段 | 名称 | 状态 | 进度 | 关键验收 |
|---|---|---|---|---|
| **P0** | 底座与门禁 | `[x]` | 4/4 | 能开一个 ImGui 空窗口 |
| **P1** | 运行时骨架 | `[x]` | 6/6 | `gpu::AttachDevice` 生效 |
| **P2** | 主题与字体 | `[x]` | 7/7 | **中文无豆腐块** ✅ |
| **P3** | 组件套件 | `[x]` | 7/7 | 组件画廊 5 主题对齐 |
| **P4** | 应用外壳 | `[x]` | 11/11 | 外壳逐项对齐 |
| **P5** | 六个工作区 | `[x]` | 6/6 | 6 页 × 5 主题对齐 |
| **P6** | 验收取证 | `[x]` | 3/3 | 总回归入口可跑 ✅ |
| **P7** | 收尾 | `[x]` | 7/7 | **不装 Qt 也能构建** ✅ |

**整体：51 / 51 步**

---

## 基线（重构开始前已实测）

| 项 | 值 | 记录时间 |
|---|---|---|
| 基线 exe 体积 | **392,330,166 B**（Qt 前端） | 2026-09-30 |
| 当前 exe 体积 | **250,451,831 B**（ImGui 前端，未裁剪） | 2026-09-30 |
| `check-layers.ps1` | **PASS (0 violations)**，退出码 0 | 2026-09-30 |
| 工具链 | MSYS2 MinGW64（`C:/msys64/mingw64`），GCC，`CMAKE_CXX_STANDARD 26` | 2026-09-30 |
| ImGui 版本 | 1.93.0 WIP（`IMGUI_VERSION_NUM 19297`） | 2026-09-30 |
| 待删的 Qt 树 | `src/ui/qml` 52 文件 · `src/ui/kit` 55 · `src/ui/pages` 89 · `src/ui/layout` 2 | 2026-09-30 |

---

## P0 底座与门禁

- [x] **P0.1** `third/imgui`（`IMGUI_VERSION_NUM == 19297`）
- [x] **P0.2** CMake 前端开关（`SHINE_UI_IMGUI` / `SHINE_UI_QT`）+ `shine_imgui` 目标
  - `shine_imgui` 是 `GLOB_RECURSE CONFIGURE_DEPENDS "src/ui/imgui/*.cpp"`，**新增 .cpp 不用改 CMake**
  - 默认 `SHINE_UI_IMGUI=ON` / `SHINE_UI_QT=OFF`
- [x] **P0.3** `tools/check-layers.ps1` 三条规则反向
  - 记录：`PASS (0 violations)`，退出码 0
- [x] **P0.4** 最小宿主：Win32 窗口 + **OpenGL** + ImGui 一帧
  - 记录：取证跑通，37 张 PNG 全部 saved

---

## P1 运行时骨架

- [x] **P1.1** `Host.{h,cpp}`：窗口 / GL 上下文 / 深度缓冲 / `WM_SIZE`
  - MSAA 关掉，否则 `glReadPixels` 抓的图边缘发虚
- [x] **P1.2** `shine::gpu::AttachDevice(api)` ⚠️ 关键动作
  - 注入的是自己 `GetProcAddress` 解析的 GL 函数表（`Host.cpp:188-196`）
  - 缺函数时 `log::Error("gpu::AttachDevice 缺函数")`，不静默
- [x] **P1.3** 帧循环 + `async::DrainUiQueue()` / `gallery::Tick()`（`Host.cpp:35-36`）
- [x] **P1.4** CLI 分派 + `ConfigureAppDataSandbox()` + `SHINE_*` 自检（`AppEntry.cpp:32`）
  - 取证模式下 APPDATA 重定向到 `<OutDir>/_appdata`
- [x] **P1.5** 布局持久化（`layout.dat`，magic `shinetv-layout-1`）
- [x] **P1.6** DPI 感知（`ImGui_ImplWin32_EnableDpiAwareness()` 在建窗**前**调用）
  - framebuffer 固定 1:1，**不**按 DPI 放大

---

## P2 主题与字体

> ✅ **本阶段最硬的判据已达成**：5 套主题名与全部业务中文逐字可读，图集远低于 64 MB 上限。

- [x] **P2.1** `Tokens.h` 搬运（5 个 JSON 原地复用，格式未改）
- [x] **P2.2** `ColorToken` 31 项映射，一次写全
- [x] **P2.3** 派生色预混（`Derived`，12 组）
- [x] **P2.4** `ImGuiStyle` 生成
- [x] **P2.5** ⚠️ 字体图集 + CJK
  - 实测 **12 roots / 3948 codepoints / vram 0.2 MB**（上限 64 MB）
  - 字形集 = `GetGlyphRangesChineseSimplifiedCommon()`(2500 字) **∪** 源码扫描出的业务汉字
  - 根目录从 **exe 路径回溯**，不是 CWD（程序常从 `build/` 启动）
  - 等宽字体（`Cascadia/Consolas` 只有拉丁）挂雅黑回落链（`MergeMode`），中文副行不再变 `?`
  - `kUiSizes` 补齐 14.5 / 17 / 26 三档，`kMonoSizes` 补 11 / 12
  - `Shell::SetTheme` 用 `atlasIsSerif_` 守卫：**只有字体族真变了才重建图集**
    （只判「切到水墨」会漏切离水墨的回退；不记状态会连续重烘 5 次）
- [x] **P2.6** 主题切换 + 持久化 + `SHINE_THEME_SET` 自检
- [x] **P2.7** 减少动效开关（从顶栏设置键挪进**主题菜单**，见 P4.2）

---

## P3 组件套件

- [x] **P3.1** 绘制基建：圆角矩形 / 渐变 / 文本截断 / 阴影近似 / `AutoGridCols`
- [x] **P3.2** 基础控件：Button(4×3) / IconBtn / Input / TextArea / Select / Switch / Checkbox / Field
- [x] **P3.3** 数据展示：Tag(7 色调×2) / StatusDot / Kbd / KV / Progress
- [x] **P3.4** 容器与浮层：Card / Segmented / Tabs / **Spinner / Tooltip / Divider / DataTable / Tree / Menu** /
  `Overlays.{h,cpp}`（Scrim / Modal / Drawer / Toast）
  - `Card` 的 `hoverable` / `glow` 早先是死代码，已改成真 `HitTest` + hover 边色 + 上浮 2px
  - `IconButton` 的 `tip` 原走 `ImGui::SetTooltip`（错背景 / 错延迟 / 无法按设计稿右侧锚定），改走新 Tooltip
  - `Empty` 的正文框原写死 320px 宽且忽略传入 bounds，240px 侧栏里横穿面板 —— 改为夹在 bounds 内
  - **刻意不做** Skeleton / Avatar / Badge：全 `webui/src` 里没有这三个原语，
    凭空造尺寸等于凭空造设计
- [x] **P3.5** 画布与图像 ⚠️：`FlowCanvas` / `Art` / `ArtInk`
  - `FlowCanvas`（`Views.{h,cpp}`）：150×76 节点、5 态、9px 端口、24 段折线逼近三次贝塞尔、
    滚轮光标锚点缩放（0.35–2.0）、拖节点 / 拖背景平移、适应视图、右上玻璃工具条
  - 三处踩坑：fitInset 重复扣宽、居中未扣内边距、**满幅画布页误套外壳 2400px 撑高**
    （实测 `canvas=644x2400 → y=1112`，节点全被顶出视口）
- [x] **P3.6** 图标字体（`Icon.cpp`，与 `webui/src/components/Icon.jsx` 46 个一致）
- [x] **P3.7** 组件画廊页

---

## P4 应用外壳

- [x] **P4.1** 窗口框架布局（46/56/240/280/190/26/34）
- [x] **P4.2** 顶栏
  - 运行 / 停止**二选一**（`webui Shell.jsx:44-48`）
  - 「主题」是**幽灵按钮 + 文字**，弹**主题菜单**（5 套 + 色块 + 勾 + 减少动效）
  - 品牌标 / 项目胶囊 / 搜索框**都可点**（分别回项目中心 / 开命令面板）
  - Comfy 状态项读 `ComfySession` 真值，不是恒定 `Idle` 裸点
- [x] **P4.3** 导航栏 6 工作区 + 3 面板开关 + 画廊 + accent 选中条
  - 面板开关**无常驻高亮**：设计稿的 `.rail-btn.active` 在整个 CSS 里**没有规则**（死类），
    早先凭空多画了一层常驻 `fill-hover`
  - ⚠️ 修掉一个致命交互 bug：曾先 `Hovered(idA)` 再 `Clicked(idB)`，两个 InvisibleButton
    落同一矩形，ImGui 只让先注册者拿 `HoveredId` → **整个导航栏点不动**
- [x] **P4.4** 侧栏 + 折叠把手
  - 写死的假章节名（"第 1 章 · 雨夜"…）已换成诚实空态
- [x] **P4.5** 检查器（3 段可折叠）
  - 写死的假属性（`{代码:S012, 动作:转身, …}`）已换成诚实空态
- [x] **P4.6** 底栏 4 标签
  - 任务队列 ← `comfy::QueueModel` 真实快照（早先是写死的 `Progress(42%, run=true)`）
  - 日志 ← 运行期真实事件（早先是四条写死的 "T3 生成图 · …"）
  - 产物 ← 扫 `<project>/output`，**IO 在 worker**，按路径 + 3s 节流重扫
  - 校验报告 ← **诚实空态**（真值在 `novel.db` 的 T12/T15 产出，尚未接查询）
- [x] **P4.7** 状态栏
  - Comfy / LLM / 队列三项读真值；`T{n}/17 · {pct}%` 三态（未运行 / 运行中 / 已完成）
- [x] **P4.8** 面包屑
- [x] **P4.9** 命令面板（`Ctrl+K`）
- [x] **P4.10** 浮层全套：主题菜单 / 设置模态 / 校验报告模态 / `Esc` 逐层关闭
  - ⚠️ 全屏 scrim **不能**用 `kit::HitTest`：它会先拿到 `HoveredId`，
    把模态内部所有按钮变成死键。改用不注册 item 的 `ImGui::IsMouseHoveringRect`
- [x] **P4.11** 项目中心页
  - `DrawProjectHub` 早先**定义了但没人调用**（整页进不去）；已接成整屏替身（`hubOpen_`）

---

## P5 六个工作区

- [x] **P5.1** 总控（pipeline）— 接 `Runner::{Usage, LedgerLog, CurrentStage, RunNext}` +
  `Budget` + `Ledger::Entries` + `StopPolicy::Evaluate` + `Checkpoint::Load` + `AppSettings`
  - 删掉全部伪造 KPI / 哈希 / 停止条件
  - ⚠️ `BindOverviewProject` 曾落在**匿名 namespace** 里 → 外部链接不到
- [x] **P5.2** 小说（novel）— 8 模式 · `NovelGraph::ListChapters / ListEntities` 真实返回值
- [x] **P5.3** 资产（assets）— `NovelVisual::FindAssetByEntity / ListArtifacts`
- [x] **P5.4** 分镜（storyboard）— `NovelVisual::ListShotsByChapter / ListStageArtifacts` + `RunContinuityChecks`
  - `unverified` 显式报成「无不一致，但有数据不足项」，不报成「通过」
- [x] **P5.5** 出图（imageflow）— 含 `FlowCanvas`
- [x] **P5.6** 出片（videoflow）— 含 `FlowCanvas`
  - 三页共用一份工程快照（`BindNovelProject` 一处即可），IO 在 worker，generation 丢弃过期结果

---

## P6 验收取证

- [x] **P6.1** 后备缓冲 → PNG 抓图 + manifest（`shots-manifest.txt`，对齐 `scripts/run_reviews.ps1:56`）
  - `glReadPixels` 原点在左下，PNG 从左上起 → **逐行倒着写**
- [x] **P6.2** 保留全部 `SHINE_*` 环境变量名与分派点
- [x] **P6.3** `SHINE_IMGUI_REVIEW`：6 工作区 × 5 主题 + 全工作区基线
  - 覆盖面 **17 → 37 张**
  - manifest 含 `overall=PASS|FAIL` 行，退出码 FAIL 从 2 改 1
  - 最新一轮：**37/37 saved · `overall=PASS` · `identical-theme-pairs: 0` · 退出码 0**
  - ⚠️ 补了**判据二**：同一工作区在 5 套主题下的像素必须两两不同。
    只判「PNG 写出来了」的旧门禁是**假绿** —— 实测把 exe 放到没有 `themes/` 的目录里跑，
    37 张只有 13 张唯一、五套主题逐字节相同，manifest 依旧报 PASS。
    现在这种情况直接 `overall=FAIL` + 退出码 1（实测触发 `identical-theme-pairs: 36`）

---

## P7 收尾

- [x] **P7.1** 删 `src/ui/{app,kit,layout,pages,qml,verify}` —— **198 个文件**
  - 唯一还在被 ImGui 侧用到的 `AppEnvironment.{h,cpp}` 搬到 `src/ui/imgui/host/`
- [x] **P7.2** 删 `shine_kit` / `shine_qml` 目标与 `SHINE_UI_QT`
- [x] **P7.3** 移除 `find_package(Qt6)` / `AUTOMOC` / `AUTORCC`
  - `check-layers.ps1` 的「Qt 前端豁免目录」列表也一并删掉 —— 门禁现在对全 `src/` 零豁免
- [x] **P7.4** 门禁定型：`check-layers` / `check-theme` / `check-colors` 的主题路径改指 `src/ui/imgui/theme/Themes`
- [x] **P7.5** `package-qt.ps1` → `scripts/package-imgui.ps1`
  - 旧脚本传 `-DSHINE_QT_UI=ON` —— 这个开关**在 CMakeLists 里从来不存在**，
    所以它一直在静默配出一个 ImGui 版，再叫 `windeployqt` 去部署 Qt 运行库
- [x] **P7.6** 更新 `AGENTS.md`：删掉「双前端共存」描述，补上 `src/ui/imgui/**` 的 UTF-8 编码事实
- [x] **P7.7** 实测产物体积并填表
  - `RelWithDebInfo` 未裁剪：**252,530,560 B（240.8 MB）**
  - `strip --strip-all` 后：**12,435,968 B（11.9 MB）** —— 达标
  - 差距全部在调试信息：`.debug_info` 单节就 0x091f7a46 ≈ **146 MB**

---

## 关键指标

| 指标 | 基线 | 目标 | 实测 |
|---|---|---|---|
| exe 体积（未裁剪） | 392,330,166 B | — | 252,530,560 B |
| **exe 体积（strip 后）** | — | **< 30 MB** | **12,435,968 B = 11.9 MB** ✅ |
| `src/` 中 Qt 头引用 | 165 个 UI 文件 | **0** | **0**（门禁零豁免仍 PASS） |
| QML 文件 | 52 | **0** | **0** |
| `src/ui/` 下的前端树 | 6 棵 | **1 棵** | **1 棵（`imgui/`）** |
| 构建是否需要 Qt | 是 | **否** | **否**（全新 `build-p7` 目录零 Qt 配过 + 构建 + 取证通过） |
| `check-layers` | PASS | PASS | **PASS (0 violations)** |
| 字体图集 | — | < 64 MB | **0.2 MB / 3958 codepoints** |
| 取证图组数 | 17 | 35+ | **37（PASS）** |

---

## 风险状态

| # | 风险 | 状态 | 备注 |
|---|---|---|---|
| R1 | 中文字体 / TTC / 图集 | **已解决** | 0.2 MB / 3948 codepoints，无豆腐块 |
| R2 | 门禁自拦 | **已解决** | PASS (0 violations) |
| R3 | `FlowCanvas` | **已解决** | 见 P3.5 三处踩坑 |
| R4 | 毛玻璃无等价 | **已接受降级** | 不追求像素一致 |
| R5 | box-shadow 无等价 | **已接受降级** | 高光 + 描边近似 |
| R6 | `color-mix` 无合成 | **已解决** | `Derived` 预混 + `MixSrgb` 实色混合 |
| R7 | 取证代码全废 | **已解决** | 37 张 / PASS |
| R8 | 纹理异步未就绪 | **未处理** | P5.3 未完成 |
| R9 | cover 缩放锚点 | **已解决** | 满幅画布页改用可视高度 |
| R10 | 启动初始化顺序 | **已解决** | `AppEntry` 分派 + APPDATA 沙箱 |
| R11 | ImGui 叠放 item 抢 hover | **已解决** | 同一矩形只能 `HitTest` 一次；scrim 不能用 `HitTest` |
| R12 | `Runner` 无「跑一个 T 阶段」的服务接口 | **未解决** | 阶段**记账**真实，阶段**本体**是桩（executor 直接 return true） |
| R13 | `StopPolicy` 只判 5 组 | **未解决** | 缺可枚举的 S1–S12 规则表，同 `src/ui/pages/pipeline/StopReportView.h` 的结论 |
| R14 | `Ledger` 无章节维度 | **未解决** | 设计稿的章节×阶段双轴甘特无法完整实现 |
| R15 | 校验报告无查询源 | **未解决** | T12/T15 的产出在 `novel.db`，ImGui 侧未接 |

---

## 验证记录

> 每次追加一行：日期 / 阶段 / 做了什么 / 观测到什么 / 是否通过。
> **没跑的步骤写"未执行"，不要留空，更不要把推断写成通过。**

| 日期 | 阶段 | 内容 | 观测 | 通过 |
|---|---|---|---|---|
| 2026-09-30 | 基线 | `check-layers.ps1` | PASS (0 violations) | ✅ |
| 2026-09-30 | 基线 | 现有 exe 体积 | 392,330,166 B | ✅ |
| 2026-09-30 | 侦察 | 确认 `third/imgui` 在 `aa61046`（1.93.0 WIP） | 与 `src/gpu` 配套 | ✅ |
| 2026-09-30 | 侦察 | 确认 `gpu::AttachDevice` 全仓无调用 | 注入点确实空着 | ✅ |
| 2026-09-30 | 侦察 | 确认 `msyh.ttc` 存在（19.7 MB，TTC） | 走 `ImFontConfig::FontNo` | ✅ |
| 2026-09-30 | 侦察 | 确认 `-DSHINE_QT_UI` 在 CMakeLists 中不存在 | `package-qt.ps1:18` 死开关，P7.5 修 | ⚠️ |
| 2026-09-30 | P0–P2 | 首次 `SHINE_UI_IMGUI=ON` 全量构建 | 链接通过 | ✅ |
| 2026-09-30 | P2.5 | 字体图集实测 | 12 roots / 3948 codepoints / 0.2 MB | ✅ |
| 2026-09-30 | P3.5 | `FlowCanvas` 接进出图 / 出片 | 3 处踩坑已修（见上） | ✅ |
| 2026-09-30 | P6.3 | 取证 37 张（5 主题） | 37/37 saved · `overall=PASS` · 退出码 0 · md5 全唯一 | ✅ |
| 2026-09-30 | P5.1 | 总控页接 `Runner` 真数据 | 链接失败 2 处（`BindOverviewProject` 在匿名 ns + `kit::HitTest` 只声明未定义），已修 | ✅ |
| 2026-09-30 | P4.3 | 导航栏点不动 | `Hovered` + `Clicked` 双 InvisibleButton 抢 `HoveredId` | ✅ |
| 2026-09-30 | P4.11 | 项目中心进不去 | `DrawProjectHub` 定义了但零调用点 | ✅ |
| 2026-09-30 | P6.3 | 底栏「任务队列」出图复核 | 写死 42% 进度条 + 标签叠印 | ✅ |
| 2026-09-30 | 编码 | `src/ui/imgui/**` 实为 **UTF-8**，非 GBK | 往返转换吞掉 8 个 `⚠️`；已无损还原 | ✅ |
| 2026-09-30 | P3 | `AddText` 字面量长度 6 处写死字节数 | 每处少画 1 个字；一处 `+82` 比实际多 5 字节（越界读） | ✅ |
| 2026-09-30 | P2.5 | 字形集补 20 个符号兜底基线 | 3948 → 3958 codepoints | ✅ |
| 2026-09-30 | **P7.1** | 删 `src/ui/{app,kit,layout,pages,qml,verify}` | 198 文件进回收站；`AppEnvironment` 外迁到 `imgui/host/` | ✅ |
| 2026-09-30 | **P7.2/3** | CMake 去 Qt | 全新 `build-p7` 目录零 Qt 配过 + 全量构建通过 | ✅ |
| 2026-09-30 | **P7.4** | 三个检查脚本主题路径改指 `imgui/theme/Themes` | check-layers / check-theme / check-colors 全 PASS | ✅ |
| 2026-09-30 | **P7.5** | `package-qt.ps1` → `package-imgui.ps1` | 旧脚本的 `-DSHINE_QT_UI=ON` 是**从未存在**的开关 | ✅ |
| 2026-09-30 | **P7.7** | 产物体积 | strip 后 **12,435,968 B = 11.9 MB**（目标 < 30 MB） | ✅ |
| 2026-09-30 | P6.3 | 全新零 Qt 构建跑取证 | 37/37 saved · `overall=PASS` · 退出码 0 | ✅ |
| 2026-09-30 | P6.3 | **取证门禁假绿** | exe 放在没有 `themes/` 的目录 → 37 张只有 13 张唯一，旧门禁仍报 PASS；补判据二后 `identical-theme-pairs: 36` → FAIL | ✅ |
| 2026-09-30 | 缺口 3 | **底栏「校验报告」页接真数据** | 真值源是 `RunContinuityChecks` 落盘的 `work/ch<NNN>/v08_continuity.json`；列表按章列三态，点行走模态看逐项 C1–C12。关闭了「校验报告页」缺口 | ✅ |
| 2026-09-30 | 缺陷 | **`DrawReportModal` 是永远打不开的空壳** | 驱动它的 `reportDetail_` 全代码只有 `= -1` 初始化与复位，从无任何地方设为 ≥0 —— 模态一次都没出现过，Esc 逐层关闭里那一层也永远走不到。列表行点击补上这条路径 | ✅ |
| 2026-09-30 | 缺陷 | **四个浮层全被工作区盖住** | 报告 / 设置 / 命令面板 / 主题菜单都用 `GetWindowDrawList()`，而页面与底栏跑在 `ScrollRegion`(BeginChild) 里、child 在父窗口那份 list **之后**渲染。症状：遮罩与面板都画了，工作区 KPI 卡片压在模态上，「模态」只剩一条表头带，manifest 仍记 `saved`。全部改走 `GetForegroundDrawList()`（本仓 `Tooltip` 早已用同一招并记了原因） | ✅ |
| 2026-09-30 | 缺陷 | **命令面板列表溢出面板外** | 16 条目 + 3 组标题 = 546px，输入框下面只有 358px，且无裁剪无滚动 —— 末尾几行画到面板外压在工作区上。补 `PushClipRect` + 选中跟随滚动 + 滚动条 | ✅ |
| 2026-09-30 | 缺陷 | **「3 秒节流」是死计时器** | 产物页与报告页的 `*RefreshAt_` 只被赋 `0.0f`，`aged` 恒为假 —— 除了换工程再没有第二个触发点，跑完阶段后页面会一直停在旧内容。改成真时点 | ✅ |
| 2026-09-30 | 验证 | 浮层与报告页取证 | **42/42 saved · md5 42 张全唯一 · `identical-theme-pairs: 0` · `report-scan: converged` · `overall=PASS`**；`report-scan` 结论进 manifest 且进 overall 判据 | ✅ |
| 2026-09-30 | 缺口 5 | **侧栏树 + 检查器接真数据** | `WorkspacePages.h` 开 `BookSide()` 只读视图（外壳不持有数据也不做 IO）。侧栏「章 → 镜」树、点选换章换镜；检查器「属性」给镜的九行真实字段或章的四行。取不到的空串一律显示「—」 | ✅ |
| 2026-09-30 | 缺陷 | **只读视图在选中项变化时不重建** | `RebuildBookSide` 只在落地 / 换工程时跑，故事板页直接读 `BookState`（新鲜）而侧栏与检查器读缓存 —— 点了 S002，故事板换面了、侧栏高亮和检查器还钉在 S001。`SelectBookShot` / 选章 / 点故事板镜卡片三条路径都补上重建 | ✅ |
| 2026-09-30 | 缺陷 | **`loading` 标志在派发瞬间就翻，视图却没跟上** | 视图里的 `loading` 永远停在上一轮的 false ⇒ 调用方「等 !loading」立刻成立、一帧都不等。上一轮就因此把上一章的画面当成新章拍下来（`side-tree` 与 `side-tree-empty-chapter` 逐字节相同） | ✅ |
| 2026-09-30 | 门禁 | **加判据三：受控图不许重样** | 第三/四段的图都是显式驱动出来的，彼此重样就说明有一张没拍到它承诺的状态。只对这批做判据 —— `ws-*` 与 `theme-<base>-*` 拍的是同一工作区同一主题，只因累积界面状态不同才没撞上，拿它们互比就成了碰运气 | ✅ |
| 2026-09-30 | 一致性 | 镜码与时长两处口径不一 | 侧栏 `S001` / 故事板 `镜 #1`、检查器 `5s` / 故事板 `4.5s` —— 同一个镜三个名字、同一时长两个值。镜码提成 `WorkspacePages.h` 的 `ShotCode()` 三处共用，时长统一 `%.1fs` | ✅ |
| 2026-09-30 | 验证 | 侧栏 / 检查器取证 | **45/45 saved · md5 45 张全唯一 · `identical-driven-pairs: 0` · `chapter-switch: converged` · `overall=PASS`**；fixture 是一份 76 张表的 `novel.db`（DDL 从 `NovelDb.cpp` 程序化抽取，见 `out/_fixture_schema.sql`） | ✅ |
| 2026-09-30 | 缺口 6 | **资产工作区 kind 筛选树** | `BookSideView` 增 `assets`（全量实体镜像）+ `relation`；`BookKindFilter()` 是侧栏与主区**共享**的工作区 UI 态。侧栏画「全部 + 实际 kind」chip 行 + 「kind 分组 → 实体」两层树；主区总览网格吃同一份筛选。⚠️ 设计稿的 chip 写死 5 个候选（`Shell.jsx:522`）而分组名从数据推 —— `entities.kind` 有 30 个英文取值且无 CHECK 约束，照抄那 5 个会让绝大多数 kind 筛不出来，所以 chip 也从数据推 | ✅ |
| 2026-09-30 | 缺口 7 | **检查器「关联」段接真数据** | 设计稿是一行平铺三个 Tag（`伏笔 #3` / `场景 12` / `镜 S05`，`Shell.jsx:173-179`），是**内联字面量**、无数据结构、不可点。换成真数据：伏笔走 `ListSceneForeshadows` + `ListOpenForeshadows`、场走已在内存的 `scenes`、镜走当前选中镜。取不到的组**不出 tag** —— 「伏笔 #3」这种假 tag 比没有更糟 | ✅ |
| 2026-09-30 | 缺陷 | **检查器三段的折叠箭头是纯装饰** | `const Section sections[] = {{"属性",true},…}` 是**每帧新建的 const 局部数组**，没有任何东西写回展开态、段头也没有点击处理 ⇒「关联」段永远打不开。和上一轮那个死模态同一类。展开态提到 `Shell::sectionOpen_[3]`，初值照设计稿 `{a:true,b:true,c:false}` | ✅ |
| 2026-09-30 | 缺口 8 | **小说侧栏「快速跳转」按钮组** | 2×2（资产 / 分镜 / 出图 / 出片），`.jump-btn` 视觉：h24 r6 + accent-glow 边 + accent-dim 底，hover 换 `jumpBtnBg`。点击 = `SetWorkspace(ws)` + 一条 toast，与设计稿 `setWorkspace` 同语义（不带 tab、不带选中项，章节上下文靠全局选中态自然带过去）。⚠️ `ButtonVariant` 没有 jump 这一档，自绘不去硬凑 Ghost | ✅ |
| 2026-09-30 | 缺陷 | **toast 是空函数** | `toastTimer_` 是死字段、`DrawOverlays` 是 `(void)draw;` —— 设计稿到处在用的 `notify(...)` 一次都没出现过。补 `.toast`（fixed right 16 / bottom 40 / min-w 260 / 3px 左色调条 / 尾部 0.4s 淡出），走 `GetForegroundDrawList()`（页面在 `BeginChild` 里，同一个 z 序坑） | ✅ |
| 2026-09-30 | 缺陷 | **资产总览网格的卡片整张没画、也点不到** | `Rect{x, y, cardW, 192.0f}` —— `kit::Rect` 的四参构造是 **(minX, minY, maxX, maxY)** 不是 (x, y, w, h)。`max.x = cardW(208) < min.x = 296` ⇒ `DrawRoundRect` 的 `max.x <= min.x` 直接 return，thumb 用了 `card.max.x` 一起消失，界面上只剩名字与状态两行文字浮在背景上。编译不报错、运行不崩。改走 `RectAt` | ✅ |
| 2026-09-30 | 缺陷 | **底栏页签条的选中下划线画到了屏幕顶栏** | 同型：`Rect{area.min.x+12, area.min.y, 400.0f, 32.0f}` ⇒ `bounds.height()` = `32 - 745` = **-713**。`Tabs` 按 `bounds.height()` 排每个页签，文字位置碰巧还对，但选中页签那条 2px accent 线被画到 y≈31 | ✅ |
| 2026-09-30 | 门禁 | **`tools/find-rect-wh-misuse.ps1`** | 上面两个缺陷同型且**都不报编译错**，靠肉眼翻不出来。加一个括号配对扫描器：取出每个 `Rect{...}` 的顶层实参，命中「第 3 参像尺寸、第 4 参是裸字面量」就报出来（保守，宁可多报）。全树扫出 2 处，都已修 | ✅ |
| 2026-09-30 | 缺陷 | **资产工作区的检查器显示上一段的镜属性** | 资产页点实体，右侧却还显示小说侧栏选中的镜（S001 / 场 / 动作…）—— 两边说的不是一回事。`属性` 段按工作区分流：资产工作区给实体的八行真实字段（名称 / 类别 / 实体 ID / 摘要 / 视觉资产 / 生产状态 / 形象层 / 降级产物） | ✅ |
| 2026-09-30 | 一致性 | 资产状态词表只留一份 | `AssetTone()` 在 `WorkspaceB.cpp` 的匿名命名空间里，Shell 够不着；让检查器自己再映射一遍就是第二份状态词表。改为在 `RebuildBookSide` 里把 `tone` **和** `statusLabel` 一起算好存进 `BookAssetView` | ✅ |
| 2026-09-30 | 门禁 | **取证流程不自愈** | `SeedReportFixture()` 只写 `work/ch<NNN>/v08_continuity.json`，**不建 novel.db**；上一轮的库是手工拷进取证输出目录的。缺库时侧栏 / 检查器 / 资产树全部退化成诚实空态，而旧门禁只查「图写出来了」，整轮照样 PASS，只是图是空的。改由 `out/_run_review.ps1` 拷库 + 校验自检文件，缺库直接失败；新增 `out/_make_fixture.ps1` 与 `out/_fixture_seed.sql`（伏笔 3 条，其中 1 条 `REVEALED` 用来验「取不到就不出 tag」） | ✅ |
| 2026-09-30 | 已知小瑕疵 | **`.ps1` 必须带 UTF-8 BOM** | PowerShell 5.1 读无 BOM 的 `.ps1` 会按系统 ANSI 码页（中文 Windows = GBK/936）解码，每行中文注释都被拆成错字节；某些组合会凑出引号 / 括号把后面的代码吃掉，报 `ParserError: Unexpected token ')'`，**报错行号与真正的原因毫无关系**。同源问题还有：自检里个别行回显为空（值是对的，门禁比字符串能证明），以及 `Get-Content` 必须显式 `-Encoding UTF8` 才不乱码。写 `.ps1` 一律带 BOM | ✅ |
| 2026-09-30 | 小修 | `WorkspaceB.cpp` 去 BOM | 全树 37 个源文件里**只有它**带 UTF-8 BOM（HEAD 里本来就有，非本轮引入）。纯字节去掉 3 字节，diff 只有第 1 行 | ✅ |
| 2026-09-30 | 验证 | 资产树 / 关联段 / 跳转按钮取证 | **52/52 saved · md5 52 张全唯一 · `identical-driven-pairs: 0` · `asset-snapshot: converged` · `relation=ok tags=4` · `overall=PASS`**；新增第五段 6 张（`assets-kind-tree` / `assets-leaf-selected` / `assets-kind-filtered` / `assets-grid-filtered` / `assets-grid-all` / `inspector-relations` / `side-jump-buttons`）。**四道门禁全过**：check-layers / check-theme / check-colors / check-i18n | ✅ |
| 2026-09-30 | 已知小瑕疵 | ~~`WorkspaceB.cpp` 带 BOM~~ | 已于 2026-09-30 清除，见上方变更记录。 | ✅ |
| 2026-09-30 | 门禁 | **判据四：运行时反向矩形计数** | `kit::Rect` 的四参是 (minX,minY,maxX,maxY)，写错不报编译错、只是 `max < min` ⇒ `DrawRoundRect` / `HitTestImpl` 整块丢弃控件（不画、不可点），日志与 manifest 全绿。`Draw.h` 加 `NoteInvertedRect` / `InvertedRectCount`，`DrawRoundRect` 的 early-return 与 `HitTestImpl` 同时挂钩（只记**严格**反向，退化 `max == min` 是合法用法）。比 `find-rect-wh-misuse.ps1` 的启发式强：扫描靠猜第 3/4 参像不像尺寸，运行时兜底不猜 —— 谁真传反了自己举手。52 张覆盖 7 工作区 × 5 主题实测 `inverted-rects: 0`，进 `overall` 判据 | ✅ |
| 2026-09-30 | 门禁 | **第六段取证：悬停态 + toast** | 前 52 张全是静息态，而悬停是最容易整条链路断掉、**静息态截图完全看不出来**的状态。新增 8 个悬停探针 + 2 张 toast 抓图，`hover-probes` 进 `overall` 判据。取证里必须**帧内注入 `ImGui::GetIO().MousePos`**：后台作业里窗口不是前台，`ImGui_ImplWin32_UpdateMouseData` 的 `is_app_focused` 补位分支整个跳过，实测 `io.MousePos` 恒为初值 `-FLT_MAX`（ImGui 的「无鼠标」哨兵，从不被逐帧重置）。根因在宿主：`PumpFrames` 走 `PumpUi()` 而 `PumpUi` **不泵 Windows 消息**，已补 `PumpMessages()`（与 `RunLoop` 同形） | ✅ |
| 2026-09-30 | 缺陷 | **`PumpFrames` 不泵 Windows 消息** | `Host::PumpUi()` 只有 `DrainUiQueue` + `gallery::Tick`，没有 `PeekMessageW`；`RunLoop` 自己有，所以真实交互下正常，只有后台/离屏路径裸奔。补 `Host::PumpMessages()`，`PumpFrames` 每帧 `PumpMessages(); PumpUi();` | ✅ |
| 2026-09-30 | 验证 | 悬停态 + toast 取证（第一轮） | 54 张 / 8-8 悬停探针 / `overall=PASS` | ❌ 假绿，见下 |
| 2026-09-30 | 门禁 | **悬停探针必须三拍，且整段钉住动画时钟** | 只拍「有鼠标 vs 无鼠标」两张时，资产页与总控页的**永不静止**指示器（Progress 微光 / StatusDot 呼吸 / Tag busyPulse）让「hover 生效」和「页面正好在闪」分不开 —— 首轮 8 个探针里 4 个是这种假信号（assets 2 + overview 2，novel 上的 4 个因为恰好没有运行态指示器才过）。改成三拍（无鼠标 A / 无鼠标 B / 目标 C），**A 必须等于 B** 才说明页面静止，否则记 unstable 并让整轮红掉。另加 `kit::PinAnimation` / `UnpinAnimation`：钉住 `g_now` 后帧间唯一变量就是鼠标位置（可用前提是本工程所有 hover 样式都是 `hit.hovered ? A : B` 的直接状态切换，没有基于时间的插值）。52 张静息态截图**不钉**，它们该看到的就是带动画的真实界面 | ✅ |
| 2026-09-30 | 缺陷 | **资产总览网格的卡片整条 hover 链路缺失** | 三拍判据稳定后剩下的一条真缺陷：网格卡片只有 `Clicked(card, …)`，**没有任何 hover 绘制**，鼠标划过去一点反应都没有；同族的项目中心 `DrawHubCard` 一直有（`hit.hovered ? ColorAccentGlow() : ColorLineSubtle()`，即设计稿 `.card:hover`）。补一次 `HitTest` 取齐 `hovered`/`clicked` | ✅ |
| 2026-09-30 | 取证纪律 | **悬停探针的前置动作也要显式写全** | 资产卡片探针没切视图时，上一条 `assets-leaf-selected` 已把资产页留在**详情**态 —— 那个坐标上根本没有卡片，判据报「悬停前后相同」，症状与真缺陷一模一样（差点把「探针没摆好状态」当成「卡片 hover 断了」去改产品）。`HoverTarget` 提为文件级结构并带 `assetsOverview` / `clearKindFilter` 两个前置开关 | ✅ |
| 2026-09-30 | 门禁 | **悬停那 8 张不计入 `captured`/`failed`** | `ProbeHover` 自己存图、自己写 manifest 行，返回值在调用点被 `(void)` 丢开 ⇒ `# shots: 54` 而盘上其实 62 张，编码失败也不会进 `failed`。取证的数字必须和盘上的文件数对得上，改成直接传 `captured` / `failed` 指针进去 | ✅ |
| 2026-09-30 | 工具 | **`find-rect-wh-misuse.ps1` 三处修正** | ① **会把自己的注释报出来**：源码里那些解释这个坑的注释（`// 这里原来写 Rect{x, y, 400.0f, 32.0f}`）被判成命中 —— 只跳字符串不够，**单趟扫描要让状态从 `Rect{` 落点就贯通**，内层跟不到「落点本身在注释里」这件事。② **只认裸 `Rect{`，不认声明式 `Rect name{`**：本仓两种写法 67 / 97 处，漏掉的恰是更主流的那种。③ 加 `-Root` 参数 + 一份含 3 真 3 假的临时样本做**自检**（改完扫描逻辑必须证明还抓得到真样本 —— 改完报 0 的扫描器和坏掉的没区别） | ✅ |
| 2026-09-30 | 缺陷 | **第三处反向矩形：小说页「设定」网格整张不画** | 上一条工具扩覆盖面后立刻报出来的真缺陷：`const Rect card{x, y, cardW, 76.0f}`（**声明式**，所以上一轮的扫描器看不见）。`max.x = cardW(~250) < min.x(~296)` ⇒ `DrawShadowed` 整块 return，「设定集」的实体卡片**一张都没画、也点不到**，界面上只剩三行字浮在背景上 | ✅ |
| 2026-09-30 | 取证覆盖 | **「覆盖 7 个工作区」不等于「覆盖每个工作区的每个视图」** | 小说页有 8 个模式标签（章节/设定/初始化/流水线/评审/模型/状态/自动），而 `mode_` 只能靠点标签切换、取证**根本到不了** —— 于是「设定」「流水线」两个**有内容**的模式从来没被截过图，上一条那个反向矩形就是这么活下来的：不是没人修，是没人拍到过。补 `Shell::SetNovelMode` 入口 + `novel-mode-world` / `novel-mode-pipeline` 两张图 | ✅ |
| 2026-09-30 | 门禁 | **`ImGui::IsMouseHoveringRect` 在本工程导致 `0xC0000005`** | 退出码 `-1073741819`、fault offset `0x8b8597`。已改成手算比较，不依赖 ImGui 的窗口/命中查询 | ✅ |
| 2026-09-30 | 验证 | 悬停态 + toast + 多模式覆盖取证（终轮） | **64/64 saved · 盘上 64 张 md5 全唯一 · manifest saved 行数 = PNG 数 = `# shots` · `identical-driven-pairs: 0` · `inverted-rects: 0` · `hover-probes: 8/8` · `unstable=0` · `overall=PASS`**；**四道门禁全过**（check-layers / check-theme / check-colors / check-i18n）；Rect 扫描器：真实树 0 命中、自检 3 命中 | ✅ |
| 2026-09-30 | 覆盖审计 | **系统枚举全应用的多视图面** | 「7 个工作区全拍了」听起来够了，但一个工作区里还有 4 个 dock 页签、3 个检查器段、2–4 个右侧面板页签、8 个小说模式标签。逐个枚举后发现：底栏 **0/1/2 三个页签从没被拍过**、检查器「预览」段没拍过、出图/出片的第二个面板页签没拍过、分镜选中镜没法驱动。补 `SetImageFlowPanel` / `SetImageFlowFolded` / `SetVideoFlowPanel` / `SelectStoryboardShot` 四个入口 + 第七段 9 张图 | ✅ |
| 2026-09-30 | 缺陷 | **`SelectStoryboardShot` 是死入口** | 链路 `Shell::SelectStoryboardShot` → `StoryboardPage::setSelected` → 写 `selectedShot_`，而该字段**全仓零读点** —— 真正被读的是 `BookState::selectedShot`。于是「换到第 2 张镜」那张图与默认态**逐像素相同**，而 manifest 照样记 saved（取证集里没有可比对象，判据也抓不到）。已删 `selectedShot_`/`setSelected`，改走点击同一条路径 `SelectBookShot`（顺带 `RebuildBookSide`）；故事板时间轴的点击原来是把那两行**复制**了一份，现在也收敛过去 | ✅ |
| 2026-09-30 | 取证纪律 | **「证明性」截图必须成对** | 只拍「换到第 2 张」那一张时，它跟任何别的图都不重样，于是「受控图不许重样」也抓不到「这个入口是死的」。补一张默认态 `storyboard-shot-1`，两张一旦相同就当场变红。**单独一张只用来证明的截图，证明不了任何东西** | ✅ |
| 2026-09-30 | 缺陷 | **出图页页签 1 落进 `else`，与页签 3 渲染同一内容** | 页签有 4 个（绑定 / 批量出图 / 图评审 / 结果），分发却只有 `if 0 / else if 2 / else` ⇒ 点「批量出图」看到的是**「结果」页的尺寸/步数/种子/耗时**。补 1 号分支：候选镜用 `BookSide().shots`（本章真实镜表，与侧栏树/故事板同一份）+ 镜自己的 `canon_status`，**不编批次号不编张数**；批量提交明说没接（业务层没有替前端提交 V4 的只读投影），而不是放一个点了什么都不做的按钮 | ✅ |
| 2026-09-30 | 缺陷 | **图评审页是 5 行假复选框** | 三处同时是假的：① `Checkbox` 返回值被丢弃 ⇒ **点不动**；② `on` 传字面量 `i < 3` ⇒ 勾选态**每帧重建**，永远是「前 3 个勾上」；③ 五行 label 都是同一个字符串「构图稳定」⇒ 叠印成一坨。「勾上 3 个、5 个同名、点不动」在静息态截图上看着还挺像个评审页。改成诚实列表：只列**真有视觉资产**的实体（名字 + 生产状态），一条都没有就说明为什么没有，并注明逐项评分没有只读投影 | ✅ |
| 2026-09-30 | 缺陷 | **出片页「视频任务」是 4 条写死进度** | `Progress(20/40/60/80, running = i == 0)` 是算术级数，与真实运行态毫无关系 —— 界面上永远显示「第一条在跑、其余到 80%」。而底栏任务队列早就改用 `QueueModel::Row::progress` 真值了，**同一个队列两个口径**。改接 `comfy::ComfySession::Queue().Snapshot()` 同一份；队列空就显示空态并写出真值源，不补 4 条占位 | ✅ |
| 2026-09-30 | 覆盖审计 | **fixture 缺数据也会造成覆盖洞** | 「图评审」页签改成「只列真有视觉资产的实体」之后，fixture 里一条 `visual_assets` 都没有 ⇒ 那一页**只跑得到诚实空态**，列表分支从来没被执行过。和「多态视图进不去」是同一类洞，只是更隐蔽：**页面拍到了，分支没跑到**。给 fixture 补两条视觉资产，且 `status` 取**两个不同的值**（`READY` / `STALE`）—— 只放一条的话另一种状态映射同样没被跑到。同理，产物页扫 `<root>/output` 而 fixture 没有产物文件 ⇒ 底栏第 3 个页签只拍到「output/ 目录是空的」；`SeedReportFixture` 补 6 个产物（png / mp4 / json 各若干），并在取证里**等扫描收敛**（`artifact-snapshot` 进 manifest 与 overall 判据） | ✅ |
| 2026-09-30 | 门禁 | **悬停探针加命中自检，从「一个信号」变「三个信号」** | 「悬停前后像素相同」有两种**完全不同**的原因：坐标点空了（探针的问题）或 hover 分支什么都不画（产品的缺陷）。只看像素差分不开 —— 我一度把前者当成后者去改产品。`kit::NoteHoveredItem` / `HoveredItemCount` / `LastHoveredItem` 挂在 `HitTestImpl` 上，探针据此分成三档：`rest-hovered>0` 哨兵失效 / `hit==0` 坐标落在热区外（**探针**问题）/ `hit>0` 且像素没变（**产品**缺陷，判 `broken`）。日志还会打出命中的 widget id（`if-tabs##1`、`asset-card-0`…），失败时直接知道是哪个控件 | ✅ |
| 2026-09-30 | 缺陷 | **鼠标注入点错了一帧，诊断信号整个是噪声** | 原先在 `onFrame` 里写 `io.MousePos`，那只改到 `IsMouseHoveringRect` 用的那份；`g.HoveredId` 早在此前的 `NewFrame` 里按**真实光标**算好了 ⇒ 每帧恒有 1 个 item（真实光标所在那个）报 hovered，静息帧的「命中数」永远是 1。加 `Host::SetFrameMouseOverride` 把注入挪到**所有后端 `NewFrame` 之前**（用两个 float 而非 `ImVec2`，`Host.h` 刻意不引 imgui 头），实测 `rest-hovered` 全 0 | ✅ |
| 2026-09-30 | 缺陷 | **`Segmented` 的 hover 多画了一层设计稿里没有的底** | 设计稿 `ui.css`：` .seg > button { color: text-muted }`（无 background）/ `.seg > button:hover { color: text-primary }`（**也没有 background**）/ `.seg > button.on { background: bg-elevated; color: text-primary }`。我们给 hover 加了一层 fill-hover 底。连带后果：选中项的 hover 因此毫无变化（color 本来就是 text-primary），而**设计稿本来就不承诺选中项有 hover 反馈** —— 探针据此把目标改成非选中项，而不是去改产品 | ✅ |
| 2026-09-30 | 缺陷 | **小说页模式页签：hover 背景漏了选中项** | 设计稿 `views.css`：`.novel-modes .ntab:hover { color: text-primary; background: fill-muted }` 与 `.novel-modes .ntab.on { color: accent; border-bottom-color: accent }` 特异度相同、后者胜出，但 **`.on` 没有声明 background**，所以 hover 的 fill-muted 底**照样作用在选中项上**。我们写的是 `hit.hovered && i != mode_` ⇒ 选中页签悬停毫无反应。悬停探针判 `broken` 抓到的 | ✅ |
| 2026-09-30 | 验证 | 悬停判据加固轮取证 | **77/77 saved · 盘上 77 张 md5 全唯一 · manifest saved 行数 = PNG 数 = `# shots` · `identical-driven-pairs: 0` · `inverted-rects: 0` · `hover-probes: 11/11`（新增产物行 / 出图面板页签 / 小说模式页签三条）· `artifact-snapshot: converged` · `overall=PASS`**；四道门禁全过；Rect 扫描器 0 命中 | ✅ |
| 2026-09-30 | 缺陷 | **命令面板 14 条里 8 条是空操作** | 列表用一个 `int workspace` 同时表达「切到第 N 个工作区」与「别的命令」（-1 命令 / -2 切主题 / -3..-7 五个主题），而 Enter 的执行体只有 `if (picked.workspace >= 0) SetWorkspace(...)` 一句。主题组那 5 条尤其扎眼 —— 面板上明明白白列着「深空/薄暮/纸墨/水墨/极夜」，按 Enter 毫无反应。改成显式 `enum class PaletteAction`（提到 `Shell.h` 命名空间级），新增动作必须同时写执行体；`Item` 带上 `action + arg` | ✅ |
| 2026-09-30 | 缺陷 | **执行体只存一份：抽出 `RunPaletteAction`** | 同一个动作被写两遍时，迟早只改得动一边 —— 本仓已经吃过一次（`Ctrl+T` 早先被注册成「打开命令面板」，面板上那条又只能开关面板，套娃）。Enter 与 `Ctrl+N` / `Ctrl+O` / `Ctrl+T` 现在都走 `Shell::RunPaletteAction`。另把「切换主题」从 `Theme(index 1)` 改成真正的 `ThemeNext`（轮换到下一个）—— 早先两者共用一个 `Theme`，「切换」实际只是切到 index 1，第二轮点回同一个主题，看着像没生效 | ✅ |
| 2026-09-30 | 缺陷 | **`Ctrl+N` / `Ctrl+O` 根本没注册** | 命令面板上写着「新建项目 / Ctrl+N」「打开项目 / Ctrl+O」，但 `ApplyShortcuts` 里只有 B / J / I / K / T ⇒ 这两行提示**两处都是死的**。补上注册并走 `RunPaletteAction`。另加 `Ctrl+Enter` = 运行（`phases.md:286` 的契约），同样走 `RequestRunStart` | ✅ |
| 2026-09-30 | 缺陷 | **`Ctrl+K` 同帧开又关 ⇒ 永远打不开命令面板** | `ApplyShortcuts()` 在帧首跑、`DrawCommandPalette()` 在帧尾跑，同一帧里 Ctrl+K 先置 `paletteOpen_=true`，走到关闭判定时**同一个按键沿**又置 false。早先只把关闭条件从裸 `K` 收窄成 `Ctrl+K`（治了「输入含 k 的查询把面板关掉」这个副症状），主症状原样。真正的原因是打开与关闭读到同一个按键沿，判据要在**消费** `paletteJustOpened_` 之前把它抓住，跳过「刚打开那一帧」的关闭判定。动作判据 r53 打出的 `palette=closed → palette=closed`（且 `saw-ctrl` / `saw-pressed` 都为真）抓到的 | ✅ |
| 2026-09-30 | 缺陷 | **命令面板输入框丢了自动聚焦** | `SetKeyboardFocusHere` 被一个「条件成立、里面空的」`if` 掏掉了。症状是「面板能开、列表能滚，但打不了字」—— 必须先用鼠标点一下输入框。命令面板的基本用法就是「打开就打字」，整段过滤逻辑是真的，所以这个洞很难从界面上看出来 | ✅ |
| 2026-09-30 | 缺陷 | **面包屑第三段恒为「总览」** | `SetWorkspace` 里无条件 `lastViewLabel = "总览"`，写死就永远盖住真状态。改成 `Shell::DerivedViewLabel()` 按工作区现算（小说取真章、资产取真实体名，其余用结构性标签），字段保留只为 `layout.dat` 向后兼容 | ✅ |
| 2026-09-30 | 验证 | **动作判据（第八段）** | 7 个快捷键逐个注入按下 / 抬起两帧，比对**该动作真正会改的那个状态**前后变没变；帧内自检 `sawCtrl` / `sawPressed` 把「注入没到位（探针问题）」与「产品有空动作」分开。注入必须在 `NewFrame` **之前**（`g.HoveredId` 早按真实光标算好），且 `io.KeyCtrl` 还需在 `NewFrame` **之后**再钉一次（win32 后端 `UpdateKeyModifiers()` 用自己空的键数组会写回 false） | ✅ |
| 2026-09-30 | 门禁 | **判据自己也会骗人：覆盖面不足与产品缺陷在判据眼里长得一样** | 第八段最初让 7 个动作**共用一个** `LayoutStateHash()`，r52 报 3/7：B/J/I 改的是 `layout_` 里落盘的字段，命中；K/T/N/O 改的是**命令面板开态 / 当前主题 / 项目中心开态** —— 这三个**本来就不该持久化**，所以进不了布局哈希。判据把「哈希没覆盖到」误报成「产品有空动作」，差点去改本来正确的代码。改成按语义分四类（`Layout` / `Palette` / `Theme` / `Hub`），每类读**产品自己的状态读数**（为此给 `Shell` 补了 `commandPaletteOpen()`），并读成**字符串**而不是 `uint64` —— 出错时能直接打出 `0 → 1` 或 `deepspace → dusk` | ✅ |
| 2026-09-30 | 门禁 | **「再按一次当复位」对四条动作是错的** | 复位原本是无条件再注入一次同一个快捷键。B/J/I/K 恰好是纯 toggle 所以成立，但 `Ctrl+N` / `Ctrl+O` 的执行体是 `if (!hubOpen_) ToggleProjectHub()` —— **幂等**，再按一次什么也不发生，项目中心一直开着，后面所有截图都拍错内容；`Ctrl+T` 是**轮换**，再按一次等于把主题真的换走，连带后面 30 张 `theme-*` 图全部拍成另一个主题而图名还写着原来的。改成按语义显式复位 + 跑完整体还原主题 | ✅ |
| 2026-09-30 | 缺陷 | **`pipeline::Runner` 注入的是空 lambda，整页跑出一套**看起来完全正常**的假数据** | `BindOverviewProject` 注入 `[](StageId, const std::string&, std::string&) { return true; }` —— 什么都不做、直接报成功。后果：账本落下 17 条 `work/T*.json` 占位、预算扣成「17 次 LLM / ¥0.17」、进度条走到 100%、状态写「全流程完成：产物与账本已落盘」，**而一个阶段都没真跑过**。这比旧版的字面量假数据（12480 / 3/17）更坏：字面量一眼看得出是假的，这一套是**真数据通路算出来的假结果**，验不出来。真执行体在 `novel/`（`NovelDirector::GenerateChapter`、`novelcore::GenerateOneChapter`、`NovelContinuity`），签名是 `expected<...>` / `ChapterGenOutcome`，与 `StageExecutor = bool(StageId, string&, string&)` **不同形状且无适配层** | ✅ |
| 2026-09-30 | 缺陷 | **「让执行体返回 false」也还是会留下假数字** | `Runner::RunNext` 在 `Runner.cpp:40` 先 `budget_.ConsumeLlm(...)`，**才**在 `:46` 调执行体 —— 所以「执行失败」这条路照样留下一条「1 次 LLM / ¥0.01」的真·假账。正解是 UI 侧**根本不调 `RunNext`**：`StartRun` 加前置判断，未接入时一个数字都不产生。`Configure` 也从空 lambda 改成传 `nullptr` | ✅ |
| 2026-09-30 | 缺陷 | **「测出来的 0」与「从来没测」在界面上长得一样** | 执行体未接入时账本/预算恒为全 0，于是总控页五行 KPI 显示「0/17 · LLM 0 次 · 估算成本 ¥0.00 · 镜头 0 个 · 阶段产物 0 条」—— 看着像**测出来的零**，其实是**从来没测**。统一改成「未接入」；状态卡从「等待运行 · 从 T1 开始」（在**许诺一件做不到的事**，用户会一直等）改成「未接入阶段执行体 · 本页不产生任何运行数据」；两个运行按钮 disabled 并在**按钮下方**写出禁用原因（tooltip 抓不到截图，而原因必须在那张图上看得见）。`OverviewPipelineWired()` 是**唯一一份**读数，顶栏「运行」与本页两个按钮都靠它 | ✅ |
| 2026-09-30 | 缺陷 | **状态栏的「T1/17 · 0%」是一个结构上不可能前进的进度条** | `runStageIndex_` / `runPercent_` 全文件**只有「写 0」与「读出来显示」**，没有任何自增点，注释却写着「这里是真实运行态」。那不是「跑到一半卡住」，是从设计上就不可能动。未接入时如实显示「阶段执行体未接入」（warn 色），并连空轨道一起不画 —— 画一条 0% 的轨道是在暗示「进度是 0」而不是「没有进度可言」。另：那个 `17` 是硬编码字面量，全仓库没有 `kStageCount`；`StartRun` 的 `AllStages().size()` 是 **28**（T1–T17 + V0–V11）而界面只呈现 17 段，分母已按界面改 | ✅ |
| 2026-09-30 | 缺陷 | **「批量出图」是个点了没反应的 primary 主按钮** | 工具条上 `Button(...)` 的**返回值直接丢弃** —— 画了个 primary 主按钮，点了什么也不发生，而同一页 `panelTab_ == 1` 分支里明明写着「批量提交未接」。改成点了弹 toast 把原因说清楚：面板是**可折叠**的（`folded_`），禁用了按钮用户就只能看到「按不动」，而按不动的原因写在可能被折起来的面板里。为此给页面层加了一条 `pages::SetWorkspaceToast` 通道（由 Shell 注入自己的 `Notify`，页面不该认识 Shell） | ✅ |
| 2026-09-30 | 缺陷 | **组件画廊的开关与复选框点不动** | `Switch(draw, …, on, …)` / `Checkbox(draw, …, checked, …)` 的**返回值直接丢弃**。kit 的 `bool on` 是**按值**传进去的，返回值是「是否被翻转」这个信号，所以点一百次都停在 on。这是组件画廊页 —— 它存在的意义就是演示这些控件可用，一个按不动的开关在这里比在业务页更不能接受 | ✅ |
| 2026-09-30 | 缺陷 | **三段编出来的「参数」** | ① 出图页「结果」页签 `KeyValues({{尺寸,1024x576},{步数,28},{种子,1289471},{耗时,12.4s}})` —— 业务层没有出图结果的只读投影（`visual_assets` 只有 status），四个数字没有一个是真的；② 出片页「成片」`{分辨率,1920x1080},{帧率,24fps},{帧数,112},{缺失镜头,S013}` —— 连「缺失镜头 S013」这种具体断言都是假的；③ 胶片条 `const bool ready = i < 3;` 无条件循环 6 次，前 3 格画缩略图 + 编出来的 `S010..S012` + 编出来的 `24fps` + 一个**会被读成「这一格出好了」**的绿点，后 3 格写「待出片」—— 而同一文件里另一处注释还写着「队列空就是空，不补 6 条占位」。全部改接真值源（`BookSide().shots` / `.assets`），没有就写明为什么没有 | ✅ |
| 2026-09-30 | 缺陷 | **出片页「首尾帧链」编了 3 行镜号与一个「断链」** | 循环 3 次造 `"S0"+(10+i)+" -> S0"+(11+i)`，连接状态写死 `i == 1 ? "断链" : "已连接"` —— 镜号是编的，断链也是编的（与本工程无关）。改列 `BookSide().shots` 里**相邻两镜**，右侧标签显示后一个镜自己的 `canonStatus`（真字段）；「断链」需要判定帧链连通性，业务层没有这个只读投影，所以不编，并写明这一点 | ✅ |
| 2026-09-30 | 缺陷 | **设计稿的 mock 字符串 `分镜图_v3` 被照抄进工具条标题** | `"出图流程 · 分镜图_v3"` —— 「分镜图_v3」是设计稿的假资产名（与「实体 · 林晚」同一类错误），照抄等于把一个不存在的名字写死在界面上。换成结构性标签「出图流程 · 本章 N 镜」 | ✅ |
| 2026-09-30 | 缺陷 | **设置模态与主题菜单可以同开** | 「设置」只 `settingsOpen_ = !settingsOpen_`，不清 `themeMenuOpen_`（「主题」那侧早就清了，只有这侧漏了）⇒ 开着主题菜单点「设置」，菜单盖在模态上面。两处开设置模态的入口（齿轮键 + 连接状态行）现在都走 `Shell::ToggleSettingsModal`，互斥只写一份。刻意**不**复用取证用的 `SetSettingsOpen` —— 那要能独立控制浮层态，塞进互斥会让截图前置动作互相干扰 | ✅ |
| 2026-09-30 | 缺陷 | **三个 `ScrollRegion` 全部从来不能滚** | 自绘控件只往 `ImDrawList` 加 draw call，**全程没给 ImGui 提交过任何 item** ⇒ child 的 `ContentSize` 恒为 0、`ScrollMaxY` 恒为 0 ⇒ 视口以下的内容被裁掉且**滚轮够不着**。`Shell::DrawWorkspace` 里那个 `view` 撑到 2400px 的注释写着「这样超出视口时能被外壳滚到」，但 2400 从没告诉过 ImGui，只是个自说自话的局部变量 —— **注释描述的机制根本没接上**。`ScrollRegion` 加 `setContentHeight()`（析构前补一个 `ImGui::Dummy`），三个调用点都上报 | ✅ |
| 2026-09-30 | 缺陷 | **嵌套 child 的裁剪矩形被绕过**（总控页右栏） | `Shell::DrawWorkspace` 把**外层**（工作区 child）的 draw list 传进 `DrawOverview`，右栏又开了一个嵌套 child —— 而 `BeginChild` 的裁剪矩形只写进**它自己那条** list 的 `CmdBuffer`，画到外层 list 上的 draw call **完全不受裁剪**。症状不是「卡片消失」而是**盖住上层**：右栏滚上去后「停止条件」浮到 y=93，压在 KPI「镜头/未接入 ↑」和页头「运行」按钮上，看着像布局算错。`ScrollRegion` 加 `drawList()`，右栏整块改用它 | ✅ |
| 2026-09-30 | 缺陷 | **右栏按 2400 的布局区高收口，成了布局容器而不是视口** | `rightCol` 的下边界取 `content.max.y`（= Shell 给的 2400），于是右栏 child 高 1978px、内容只有 654px ⇒ 期望滚动上限 0，「右栏自己滚」不存在，裁切全靠外层。要建**自己的视口**的页面必须知道真正能看见多少：新增 `pages::WorkspaceViewportHeight()`（Shell 每帧写入），右栏下边界改成 `min(content.max.y, area.min.y + 视口高)`，正文流（左列甘特+账本）仍按布局区高排 | ✅ |
| 2026-09-30 | 门禁 | **滚动取证：加一个正交信号，别信「两张图不一样」** | 第一版 `overview-vstages` 判据全绿（`identical-driven-pairs: 0`）而右栏**压根没滚** —— 两张图只差 326px、位置在左上角一块无关区域（`SetScrollHereY` 在构造期没有 item 可依）。**「图不一样」有二义性**：「滚到位了」和「别处在动」分不开。加 `ScrollRegion::LastApplied()`：读产品自己的 `scrollY` / 期望上限 / ImGui 侧上限，不从像素反推。⚠️ 判据自己的三个分支必须**平级**写 —— 第一版把 `maxScrollY > 0` 塞进 `if` 里，于是「内容压根没超出」和「请求没被核销」都落进 `else` **静默通过**，而那恰恰是最该红的覆盖洞。失败时 `scroll-failed: 1` 直接把 `overall` 拉成 FAIL | ✅ |
| 2026-09-30 | 验证 | 章节 × V 阶段矩阵渲染 + 滚动修复（终轮） | **79/79 saved · 盘上 79 张 md5 全唯一 · manifest saved 行数 = PNG 数 = `# shots` · `scroll-failed: 0` · `identical-theme-pairs: 0` · `identical-driven-pairs: 0` · `inverted-rects: 0` · `hover-probes: 11/11` · `shortcuts: 7/7` · 四个 snapshot converged · `overall=PASS`**；**四道门禁全过**；Rect 扫描器：真实树 0 命中、自检 3 命中。像素证据：`overview-bound` vs `overview-vstages` 差 36,578px / bbox `150,143..1319,743`（对比修之前的 326px / `150,143..202,196`，那个 bbox 正是「差异来自别处」的实证）；矩阵逐格与 fixture 一致（第 1 章 V1–V7 全 5、第 2 章到 V5、第 3 章空） | ✅ |
| 2026-09-30 | 缺陷 | **`hub-scroll` 的内容高度上报成了视口高（自己引入的回归）** | 上一轮修「ScrollRegion 不滚」时给三个调用点都补了 `setContentHeight`，项目中心那处写的是 `setContentHeight(region.content().height())` —— 上报值恰好等于视口高 ⇒ `ScrollMaxY` 恒为 0 ⇒ 项目超过一屏就够不着（1080 高窗口约 6 张，第 7 张起被裁掉且滚不到）。**看着加了、实际等于没加**，是「改了但没生效」最典型的形态：编译器不报错、像素看不出来，只有真去滚才发现。改成由 `DrawProjectHub` 自报栅格实际高度，Shell 侧加了**只报一次的错误哨兵**（没上报就明写进日志，而不是静默返回） | ✅ |
| 2026-09-30 | 缺陷 | **小说侧栏「快速跳转」整组被空态吞掉** | `DrawJumpButtons` 排在「这本小说还没有章」的 `Empty()+return` **之后** ⇒ 没章节 / 没绑定 / 读取中 / 读取失败四种状态下四个按钮一个都不画，而它跳的是资产 / 分镜 / 出图 / 出片四个工作区，与「有没有章节」无关。界面看着正常（有空态提示），只有 `hover-jump-btn` 探针知道那里本该有东西。挪到所有空态 `return` 之前 | ✅ |
| 2026-09-30 | 门禁 | **「点空了」分不出三种原因，加热区扫描** | `hit=0` 至少对应：注入没到位 / 视口塌了 / **坐标落在热区外**。第三种是布局一改就全失效的（坐标是照旧截图量的），而日志只说「点空了」，不告诉你控件现在在哪 —— 只能靠猜。上一条那个缺陷就是这样定位不到的：探针报「坐标偏了，别改产品」，可产品明明有 bug。`SHINE_SCAN=<ws>:<x0>:<y0>:<x1>:<y1>:<step>` 打出每格命中的控件 id，扫一遍就看见 `jump-*` **在那个工作区压根不存在**。**诊断工具，不参与 pass/fail**；必须跑在探针**之后**（前面扫到的是 fixture 没就绪那一帧） | ✅ |
| 2026-09-30 | 门禁 | **同帧重叠热区自检**（抓「画得出但点不动」） | ImGui 同窗口内先注册者独占 `HoveredId`（`imgui.cpp:5161`）⇒ 重叠矩形上的第二个 `InvisibleButton` 永远 `clicked=false`，而它上面的控件外观画得好好的。`kit::NoteDuplicateHit` 按帧分桶比对矩形（不比 id —— 漏写的人通常还换了个 id），计数进 `overall`。与 `InvertedRectCount` 同一族：那个抓「画不出」，这个抓「画得出但点不动」。迁移报告行时我自己犯了一次（`spec.id` 之后又补了一次 `HitTest`），靠读代码看不出来，是判据逼我回去看出来的 | ✅ |
| 2026-09-30 | 重构 | **kit 小组件收编：7 处重复实现 → 2 个组件** | 抽 `kit::ListRow`（4 处单行）+ `kit::ListCard`（3 处双行卡，**刻意不合并**：hover 一个走填色一个走投影档，选中一个换底一个换描边环，视觉形态不同）。4 个「已实现、算得对、全树零调用」的组件全部接线：`ModalFrameRect` / `Toast`（补 `alpha` 保住 0.4s 淡出）/ `Menu` / `DataTable`（补 `tag` / `mono` 两个形态）。**归因修正**：原表记「8 处列表行」，实际只有 4 处是；WorkspaceA 产物 / 向导模板 / 打开列表是双行卡，胶片条是缩略图格，**硬塞进一个函数就是造第二份假抽象**。⚠️ 收编范围比第一轮估的**多两处**：`Shell.cpp` 的报告模态与设置模态也各有自己的外壳 —— 只收 WorkspaceB 那 3 处的话，`kit::ModalFrameRect` 本体**仍然是零调用**，目标没真正达成。⚠️ 本行的「4 处 / 3 处」在初版曾误写成 5 / 2（迁移时把 WorkspaceA 那一处从 `ListRow` 改成了 `ListCard` 却没回头改表），已按调用点复核修正 | ✅ |
| 2026-09-30 | 缺陷 | **模态遮罩注册成全屏热区（自己引入的）** | 收编模态外壳时让 `ModalFrameRect` 的遮罩走 `kit::Scrim`（= `HitTest`）。危害方向**取决于绘制顺序且两种相反**：报告模态外壳先画、按钮后画 ⇒ 遮罩先拿 `HoveredId`、**全部按钮按不动**；项目中心对话框最后画 ⇒ 遮罩排在按钮之后，**不会坏**。我第一版以为两者都坏，**判断错了**。改成遮罩只画不注册；「点外面关闭」由调用方拿 frame 手算（既不抢 `HoveredId`，也避开本工程会 0xC0000005 的 `IsMouseHoveringRect`）。判据自检：把遮罩改回 `Scrim` ⇒ `duplicate-hits` 0 → **6232**、overall FAIL | ✅ |
| 2026-09-30 | 缺陷 | **「点面板外关闭」把刚打开的对话框当场关掉（自己引入的）** | 点工具条「打开项目」的那一下点击，**同一帧**里 `openDlg` 才置真、对话框才被画出来，而 `IsMouseClicked()` 那一帧仍为真、鼠标仍在工具条上（对话框矩形之外）⇒ 刚开的对话框当场被关。症状「点『打开项目』→ 闪一下就没了」：编译过、截图正常、打开之前一切正常，**只有真点一下才看得见**。修法 `HubState::dismissArmed`（只在本帧之前对话框就已开着时，点外面才真的关） | ✅ |
| 2026-09-30 | 门禁 | **浮层判据加第 5 条：项目中心控件收得到鼠标** | 前两条回归都因为「打开之前一切正常」而漏过。这条判据：全屏扫出 `hub-open` 坐标（**量**出来，不猜）→ 点击打开对话框 → **先验 `hubDialogOpen()` 前置条件** → 再全屏扫一遍断言有 `hub-` id。当场抓到上面第 2 条。⚠️ 它**第一版是假绿**：不检查「对话框真的开了」，会在没开的状态下扫一片没有遮罩的界面照常通过 —— 补 `pages::HubDialogOpen()` 读数后才有效。⚠️ 它**不是**遮罩顺序的检测器（自检时仍 5/5），那是 `duplicate-hits` 的活。**判据自己也会骗人**：一个「报通过的原因」和「它本该抓的缺陷」长得一样的判据，比没有判据更坏 | ✅ |
| 2026-09-30 | 验证 | kit 收编后取证（`overall=FAIL`，原因已定位） | **79/79 saved · 0 failed · `duplicate-hits: 0` · `identical-theme-pairs: 0` · `identical-driven-pairs: 0` · `inverted-rects: 0` · `scroll-failed: 0` · `overlay-clicks: 4/4` · `shortcuts: 7/7` · `hover-probes: 9/11`（基线 8/11，本轮修好 `hover-jump-btn`）**；四道门禁 + 三个扫描器（含 `-SelfTest`）全过。**`overall=FAIL` 的原因是 fixture 没落地**：`book-snapshot=TIMEOUT` / `asset-snapshot=TIMEOUT`（`assets=0`）⇒ 资产侧栏走 `Empty()`、`hover-tree-node` 与 `hover-asset-card` 坐标上没有控件。`git stash` 对照跑过基线，**同样红**，与本轮改动无关，属环境/fixture 缺口，未在本轮修 | ⚠️ |
| 2026-09-30 | 门禁 | **工作区主滚动区也要有探针** | 上一轮只验了 `ov-right`，而**工作区才是主滚动区** —— 修好 `setContentHeight` 之后它才第一次真的能滚，却没有任何探针。一个没人验的滚动区等于没有。补 `workspace-scroll` 的四分支判据（没核销 / 上限为 0 / 没滚到位 / ImGui 侧不认识这个高度），并打出实测数字：`自报内容高=1142 可视高=664 上限=478` | ✅ |
| 2026-09-30 | 缺陷 | **2400 的布局区高被页面当视口用，卡片被撑成巨型空盒子** | 总控页「账本」卡的 rect 一直拉到 `left.max.y`（= Shell 给的 2400）⇒ 实测一张 **1750px 高、只有 6 行**的卡，6 行浮在顶上、下面 1450px 全是空背景，还让工作区多出约 1450px 只能滚到空白的范围。同一族的还有小说页 `timelineTop = content.max.y - 118` 与撑到 2200 的 `StageList`。解法两条：① 卡片高度按**内容**算；② 新增 `pages::SetPageContentHeight()`，页面自报实际画到的最底，Shell 用它替代 2400 作为滚动区高度（没自报的页面退回 2400，可逐页迁移） | ✅ |
| 2026-09-30 | 门禁 | **`tools/find-silent-truncation.ps1`：找「按容器高度静默截断列表」** | 形如 `if (y + 24.0f > body.max.y) { break; }` —— 自绘列表不像 DOM 自动给滚动条，`break` 之后用户看到的是一个「就这么多」的列表，**看不出还有第 N+1 条**。实测出图页「绑定」页签里 Comfy 队列有 50 个任务时只画前 6 个。全树 **10 处**（含 `maxRows = (body.height() - 4)/18` 这种反推式砍行）。带 `-Root` 自检：3 真 + 4 假样本恰好命中 3；豁免必须写 `// scan:allow-silent-truncation <理由>`，**无理由的豁免无效**，且豁免项单列一节照常打印（不静默） | ✅ |
| 2026-09-30 | 缺陷 | **底栏「任务队列」按容器高度砍行** | `DrawDockQueue` 里 `if (y + 30.0f > body.max.y) break;`，50 个任务只画 6 个、无任何提示。改 `kit::ScrollRegion` 画**全部**条目并报内容高度，脚注写明「共 N 个任务」。顺带：底栏「日志」是**跟随最新**的流，保留尾部窗口（否则每来一行就把用户拽到底部、与往上翻历史打架），但补上「共 N 行 · 这里只显示最近 M 行」—— 原来只有一句注释，界面上看不出更早的行存在过 | ✅ |
| 2026-09-30 | 门禁 | **S1–S12 放在左列整宽，不放 300px 右栏** | 12 条判据句子长（「Comfy 不可用且本章需要出图：探活失败 >= 3 次（间隔 5s）」），塞进右栏被裁掉后半句 —— **规则表被截断等于没写**。左列整宽后每行两栏：左「什么时候停」（`StopCodeCondition`，含数字阈值）、右「停下之后怎么办」（`StopCodeHint`）。判据说明停的条件、提示说明停之后怎么办，缺一条这条规则就没法用 | ✅ |
| 2026-09-30 | 缺陷 | **8 处列表按容器高度静默截断**（`WorkspaceB.cpp`） | 扫描器自报 8 处：小说页连续性卡的 `issue` / `note` 两个循环（`if (cy + 22.0f > body.max.y) break;`）+ 出图/出片 **6 个面板列表区**。全部改成面板自己当视口（`kit::ScrollRegion`）画**全部**条目 + `setContentHeight` 上报，列表本体之外的卡片标题与汇总行留在滚动区**外面** —— 滚的只有条目行。`DrawProjectHub` 末尾补 `pages::SetPageContentHeight` 自报栅格实际高度（`ViewportBottom` 统一算下边界）。真实树归 0，剩 1 处带理由豁免 | ✅ |
| 2026-09-30 | 缺陷 | **滚动区把滚动条关了，于是「还有更多」没有任何视觉提示** | `ScrollRegion` 一直带 `ImGuiWindowFlags_NoScrollbar`。内容能滚了（内容高度已上报）之后，用户看着**和不能滚时完全一样** —— 一块被裁到看不见底的平铺区域，没有任何线索说下面还有东西。改用 ImGui 自带竖条（自带拖拽与 hover/active 态，手搓一版没拖拽的滑块等于假控件），颜色走 `ColorLineStrong()` / `ColorAccent()`，背景透明；`PopStyleColor` 从 2 改 6。ImGui 只在 `ScrollMax > 0` 时才画条，所以不用手动判要不要画 | ✅ |
| 2026-09-30 | 缺陷 | **连续性 issue 报的是库主键，不是镜码** | `ContinuityIssue::detail` 里写的是 `fmt::format("镜 #{}→#{}…", cur.id, next.id)`（`NovelContinuity.cpp` 五处），界面上原样显示成「镜 #128→#131」—— 那是数据库自增主键，用户在分镜表 / 侧栏树里**对不上任何一个镜**。`ShotPairToCodes` 用已在内存里的 `s.shots` 把主键换回镜码（`镜 S004 → S005`），解析不出就原样显示 `detail`（不猜、不吞）。走内存快照不搬 IO | ✅ |
| 2026-09-30 | 验证 | 8 处截断 + 滚动条 + 镜码（终轮，r74） | **80/80 saved · 盘上 80 张 md5 全唯一 · manifest saved 行数 = PNG 数 = `# shots: 80` · `scroll-failed: 0` · `identical-theme-pairs: 0` · `identical-driven-pairs: 0` · `inverted-rects: 0` · `hover-probes: 11/11` · `shortcuts: 7/7` · 四个 snapshot converged · `overall=PASS`**；四道门禁全过；Rect 扫描器真实树 0 / 自检 3；截断扫描器真实树 **0 待修** + 1 带理由豁免、自检 3；`src/ui/imgui/**` 无 BOM 无 U+FFFD。工作区滚动实测 `自报内容高=1142.0 可视高=664.0 上限=478.0` | ✅ |
| 2026-09-30 | 门禁 | **`check-colors` 是一条永远不会失败的死门禁** | 它只匹配 `setStyleSheet` / `QColor` —— **Qt 时代**的模式。P7 把整棵 Qt 树删掉之后这两个词在全仓一个都不存在，于是门禁恒 PASS，而它报的却是「0 hard-coded style colors」，让人以为「颜色这条线有机器守着」。重写成守**当前**规则：`IM_COL32(数字)` / `ImVec4(数字×3~4)` / `"#rrggbb"` / `0xAARRGGBB`，豁免 `// theme-ok <理由>`（**必须写理由**，无理由的豁免无效）+ 两个指名到文件的豁免区（`theme/Theme.cpp` 是调色板本身、`kit/Views.cpp` 是设计稿 UI.jsx:184 的插画调色板数据表）。扫描范围收到 `src/ui/imgui`（规则只对 UI 成立；`check-layers` 已保证 shine_core 里不可能有 ImGui 颜色代码。顺带记录了为什么不是「缩小范围来通过」：`src/media/VideoThumb.cpp` 的 `static_cast<HRESULT>(0x8000000A)` 是个恒定假阳性，而那个文件属 shine_core，本轮不能动）。**内建 11 份样本自检**，含「有标记但没理由照样算命中」与「取最近的那条 theme-ok」 | ✅ |
| 2026-09-30 | 缺陷 | **浮层的按钮全是死的：设置模态 ×、报告模态按钮、命令面板输入框、主题菜单行** | 本轮最严重的一个。浮层的 item 提交在**根窗口**，而工作区是 `BeginChild`：`FindHoveredWindowEx` 从 `g.Windows` **末尾往前**扫、取第一个命中（`imgui.cpp:6573` / `6607`）⇒ `g.HoveredWindow` 恒是工作区 child；`ItemHoverable` 第一句就是 `if (g.HoveredWindow != window) return false;`（`:5156`）⇒ 根窗口里那些 item 恒 `hovered=false / clicked=false`。**按钮画得出来、点不动**，只能按 Esc 或点遮罩。**80 张静息绿图对它零覆盖**（不点击），hover 探针也抓不到（它们只打根窗口控件与 child 内部控件）。三处修法：① `ScrollRegion` 加 `noMouseInputs`，浮层开着时给工作区 child 加 `ImGuiWindowFlags_NoMouseInputs`（顺带得到模态语义：底下点不动）；② `Shell::ChromeHit()` —— 浮层开着时外壳 chrome **一个 item 都不提交**，因为同窗口内是「**先注册者独占** HoveredId」（`:5161`），外壳先注册会吃掉「点遮罩关闭」那次点击（症状：工作区被切走、浮层还开着）；③ 主题菜单的「点外面关闭」热区提到 `DrawFrame` 开头、chrome 之前注册 | ✅ |
| 2026-09-30 | 门禁 | **浮层点击探针，并做反向验证证明它有鉴别力** | 只注入鼠标**位置**测不出「点不动」—— `IsItemClicked()` 还要按下沿，给 `Host` 补 `SetFrameMouseButtonOverride`。判据读**产品自己的开态**（`settingsOpen()` / `workspace()`）与**产品自己的按钮矩形**（`SettingsCloseRect()`，不在判据里复算布局——布局一改就会打在一个已不存在的位置上而仍报「通过」）。4 条：× 按钮 / 点遮罩关闭 / 模态开着点导航不许穿透切工作区 / 模态开着点工作区正文不许穿透。进 `overall` 判据（只打日志而绿灯就是假绿）。<br>**反向验证**：把修复临时关掉重跑 → `overlay-clicks=2/4` + `overall=FAIL`，而其余 78 项全绿 —— 证明这条判据是**唯一**能抓住该缺陷的信号。恢复后 3/4，第三条失败是**判据自己期望写错**（我多要求了「模态还开着」，而点外面关闭本就是模态的既定行为）—— 一条永远红的判据和一条坏掉的判据没有区别 | ✅ |
| 2026-09-30 | 门禁 | **`check-layers` 的 ui-leak 规则匹配不到真实写法** | 正则是 `#include\s*[<"](imgui\|ImDraw\|ImGui)[>"]`，要求头名后面**紧跟** `>`。真实写法是 `#include "imgui.h"` —— 一个 `.h` 就让它永远不匹配，**shine_core 引 ImGui 这个本仓最重要的分层规则从来就没生效过**。改成先取 include 的路径、再对**最后一段**（basename）判定。**是自检发现的，不是读代码发现的** | ✅ |
| 2026-09-30 | 门禁 | **`check-layers` 会把自己的注释报成硬编码颜色** | 逐行匹配不区分是不是注释，于是我写进注释的崩溃码 `0xC0000005` 被判成 8 位十六进制颜色，门禁直接 FAIL 4 条。注释里的说明文字改不掉（那正是后来人需要的），所以修门禁：加块注释状态机跳过整行注释 / 块注释（`find-rect-wh-misuse` 早先已经为同类问题做过一次）。补 **13 份样本自检**，含裸路径与嵌套路径两种 ImGui 泄漏、跨行块注释、`theme-ok` 豁免。保持该文件自述的「pure ASCII on disk」 | ✅ |
| 2026-09-30 | 门禁 | **截断扫描器长期漏掉「给下边界留页脚」那种写法** | 正则右边写成 `\.max\.y\s*\)`，于是 `if (y + 26.0f > body.max.y - 20.0f) { break; }` 不匹配 —— **扫描器在有真缺陷的树上报 0**，而「报 0」和「没有截断」在结果里长得一模一样。右边放宽成 `[^;)]*`、顺带认 `>=`，真实树立刻从 0 抓到 **2** 处（底栏产物列表、底栏报告列表），两处都改成 `ScrollRegion` 画全部 + 脚注写明「共 N 个」。同时把**自检内建进脚本**（早先要人手工在 `%TEMP%` 铺样本，等于没有），6 份样本里 `true-footer` 那份就是这次漏掉的写法 | ✅ |
| 2026-09-30 | 缺陷 | **`ImGui::IsMouseHoveringRect` 在本工程会崩，而它还剩 6 处在用** | PROGRESS 早先记着「已改成手算比较」，实际只改了一处，剩下 6 处原样保留：`kit/Views.cpp` 画布滚轮、命令面板列表滚轮、报告模态（点遮罩关闭 + 列表滚轮 + 行 hover）、设置模态点遮罩关闭。全部换成 `Rect::contains(io.MousePos)` 手算。顺带删掉设置模态里那个一直没用的 `const Hit scrim;` | ✅ |
| 2026-09-30 | 缺陷 | **项目中心卡片的页脚四个按钮是死键，点「…」会直接打开项目** | `DrawHubCard` 先注册整卡热区再注册页脚四个按钮，而 ImGui 是**先注册者独占**（`imgui.cpp:5161`）。原注释断言「后命中的 item 优先」，**与 ImGui 的规则正好相反**，所以这个 bug 一直没人看出来。改成把整卡热区挪到按钮**之后**注册 —— 落在页脚像素的点击归按钮、落在卡片正文的归整卡，等价于 webui 的 `e.stopPropagation()`，而且是 ImGui 自己做的。边框高亮改手算鼠标位置（登记顺序变了，登记时还没有 `hit.hovered`） | ✅ |
| 2026-09-30 | 缺陷 | **程序化占位画 `Art()` 在三处没有任何标注** | `Art()` 是纯程序化绘制（调色板只按 `seed % 12` 取、形状固定几何），与 `novel.db` / Comfy 任何字段无关。`Shell.cpp` 里同一个 `Art()` **已被标注**「示意插画 · 非本工程出图结果」—— 本仓知道要标注，出图页「结果」/「图评审」两个页签与出片页胶片条这三处漏了。抽出全仓共用的常量（逐字照抄已有那句），每张图下方加可见标注；胶片条一格只有 56×48px，整句放不下，改为格内标「示意」两字 + 条脚写完整那句，容器高 76→92 给页脚腾位 | ✅ |
| 2026-09-30 | 缺陷 | **两个浮动面板头的「运行中」/「出片中」是写死的** | 文本与色调都是字面量，绘制位置在折叠提前 return 之前、页签分发之前，上下文 30 行内没有任何队列或 Runner 读数 —— Comfy 队列为空、甚至根本没绑工程时，面板仍在断言「现在正在出图」。真值源 `ComfyHasRunning()` 就在同一个文件里。改成**真在跑才画**；没在跑时什么都不画（不用「空闲」：面板头右侧就是折叠按钮，槽位 60px，「空闲」比空着更抢眼，而折叠态下这个词还会被读成「折叠了所以空闲」，凭空多一层歧义） | ✅ |
| 2026-09-30 | 缺陷 | **组件画廊的 Segmented / Tabs 点不动** | `Card_SegmentedTabs` 里 `Segmented(...)` / `Tabs(...)` 是裸语句，value 传的是字面量 `"a"` / `"1"`；`Widgets.cpp` 的实现确认返回值必须由调用方存回。同一文件 154-164 行已把 `Switch` / `Checkbox` 的同款问题判为「画廊里按不动的开关比在业务页更不能接受」并修好 —— 这张卡是同一张画布里**漏改的另一半**。照同一写法各加一个 `static` 跨帧状态，存回时经临时 `std::string` 拷贝，避开未点击时 `string_view` 指向自身变量的自引用赋值 | ✅ |
| 2026-09-30 | 归因纠正 | **「V 链只画 8 个阶段、真实是 12 个」不成立** | 审计报 `WorkspaceB.cpp:1173` 的 `i < 8` 写死、而 `src/pipeline/StageMachine.cpp` 的视觉链是 V0–V11 共 12 个，应当改成 12。**这是编号撞名的第三例**：`src/novel/NovelVisualStages.h` 明确「影视化链的阶段化实现（V1–V7）」，`VisualStageCode` 返回 `"V1"…"V7"`，`RunAllVisualStages` 跑 V1→V7。小说侧的真实视觉链就是 **V1–V7**，而 **V8 是另一件事** —— 连续性阶段，落 `work/ch<NNN>/v08_continuity.json`，代码注释写得很清楚。**代码是对的，没改** | ✅ |
| 2026-09-30 | 缺陷 | **整个 ImGui 前端一个投影都没画** —— 28 个 `DrawShadowed` 调用点全是平面矩形 | 本阶段最大的视觉偏差，且**没有任何一处判据能看见它**：`DrawShadowed()` 的旧实现只调 `DrawRoundRect(..., topHighlight=true)`，压根不画阴影；`Tokens.h` 里的 `struct Shadow` 与 `shadow::kSm/kMd/kLg` 是**零引用的死常量**，`theme-ok` 门禁管不到它们，因为它们不是颜色字面量。设计稿里 `.modal`（ui.css:947）、`.toast`（:998）、`.drawer`（:655）、`.menu-pop`（shell.css:101）、`.cmdk`（:494）、`.float-panel`（views.css:216）、`.float-toolbar`（:195）、`.float-strip`（:261）、`.fnode`（:1035）全都有 box-shadow。<br>改成 `ShadowTier{None,Card,Overlay,Accent}` + `ShadowSpec{layers[2],count}`（`Tokens.h`），几何与**逐层绝对 alpha** 记在 `Theme.cpp:412` 的 `kShadowSpecs`（五主题 × 三档，逐条抄 `tokens.css:67/108/149/190/243`，含浅色主题更小的模糊半径 14/36 vs 深色 16/40）。色相取主题 JSON（已逐条核对五套与 CSS 一致，误差 ≤1/255），**alpha 只从表里取** —— JSON 的 `shadow.1` 存的是 CSS 第二层的值，再乘一遍只剩 45% | ✅ |
| 2026-09-30 | 缺陷 | **`AddRectFilled` 的「颜色」与「圆角」参数写反**（本轮根因，且是修复过程中自己引入的） | ImGui 签名是 `AddRectFilled(p_min, p_max, col, rounding)` —— **颜色在圆角前面**。写反**编译不报错、不崩、当帧也照常出图**：col 位收到一个半径（14）于是颜色变成 `0x0000000E` 全透明黑；rounding 位收到颜色被截成巨大值；半径为 0 的那次最彻底，`col` 恰好为 0 被 `if (col == 0) return;` 整块丢掉。<br>排查花了几轮，因为**三个信号全是骗人的**：① 像素 diff 显示 r77→r84 只变了 180px，而那 180px 其实是模态里印着的**工程路径**（`r77`→`r78`），纸墨图没印路径所以是 0 —— diff 从来就没测过阴影；② 加了一整块**不透明纯黑哨兵**，画在模态左边 300px 处，**仍然拍不到**；③ 一次探针改坏了 `ImVector::Size`（这版是字段不是方法）导致构建失败，而我**在构建失败的情况下继续跑了取证并据此下结论** —— 那轮跑的是旧二进制。<br>真正的判据是「`AddRectFilled` 之后 `_VtxCurrentIdx` 有没有推进」：135 次调用**一次都没推进**，直接坐实「画调用发出了但被丢弃」。之后才顺藤摸到参数顺序 | ✅ |
| 2026-09-30 | 门禁 | **`tools/find-draw-arg-order.ps1`：找 ImGui 绘制原语的参数错位** | 与已有的静默截断、`Rect` 四参属于同一类**编译干净、不崩、只有读渲染结果才发现**的失效。判据只有一条：`AddRectFilled` / `AddRect` / `PathRect` 的**第 3 个实参里找不到任何颜色特征**就报出来（`AddRectFilledMultiColor` 第 3~6 个都是颜色，排除）。<br>⚠️ 判据先写错过一版：本来想用「像不像几何表达式」的正则提高置信度，结果**只制造漏报** —— 真正咬人的那行是 `radius + grow`（带加号、带第二个标识符），三条正则全不匹配；单行的 `radius` 反而被抓到。**工具漏报自己该抓的东西，比误报危险得多**，所以只留「有没有颜色特征」一条。<br>**反向验证**：拿修复前的真实两行代码当样本，两处**都**被抓出来。真实树 0。自检内建 10 份样本（含那条真实的跨行算式、行注释、块注释） | ✅ |
| 2026-09-30 | 视觉 | 按 CSS 逐个接投影档位，接了 20 处 | 常驻 9 处（模态 / toast / 抽屉 / 菜单 / 命令面板 / 浮动面板 / `.fnode` / 浮动工具条 / 胶片条）+ hover 7 处 + accent 3 处。`.fnode`（views.css:1035）是**常驻不是 hover**；`.asset-card` 的 class 是 `card asset-card hoverable`（Assets.jsx:151），所以 hover 走 shadow-1、`.on` 走 shadow-accent。<br>只读代理回读 11 个「没接档」的调用点，结论是**3 处本来就不该接**（`pages/Gallery.cpp:23`、`WorkspaceB.cpp:1340`、`WorkspaceB.cpp:3329` 在设计稿里都是裸 `.card`，不带 `hoverable`/`glow`，基线规则 `ui.css:175` 无 box-shadow），1 处**设计稿里没有对应元素**（`WorkspaceB.cpp:1571`，设计稿该页是表格不是卡片栅格），其余接上。<br>**代理还纠正了我 briefing 里的一条错**：`views.css:117` 的 `.proj-card:hover` 是 `--shadow-2`（不是 shadow-1），而且它在 `ProjectHub.jsx:167` 的元素上是**死规则** —— `.card.glow:hover`（特异度 0,3,0）压过它（0,2,0），与 CSS 引入顺序无关 | ✅ |
| 2026-09-30 | 缺陷 | 资产卡 hover 边框用的是 accent-glow，设计稿写的是 line-strong | `ui.css:197` 是 `border-color: var(--line-strong)`，代码里写的是 `ColorAccentGlow()` —— 那是当初为了让 hover「看得出来」自己挑的颜色，差得很远。hover 投影与边框是**同两条规则**给的（`ui.css:196-199` / `201-203`），这次把投影和边框一起按 CSS 接 | ✅ |
| 2026-09-30 | 验证 | 投影落地（终轮，r88） | **80/80 saved · 盘上 80 张 md5 全唯一 · `scroll-failed: 0` · `identical-driven-pairs: 0` · `inverted-rects: 0` · `hover-probes: 11/11` · `shortcuts: 7/7` · `overlay-clicks: 4/4` · 四个 snapshot converged · `overall=PASS`**；四道门禁全过；`check-colors -SelfTest` 11/11、`check-layers -SelfTest` 6/6、`find-silent-truncation -SelfTest` 6/6、`find-draw-arg-order -SelfTest` 10/10；三个扫描器真实树 0（截断剩 1 处带理由豁免）。<br>**不靠 diff、靠读像素证明投影真的在**：设置模态左缘在 x=520，往左 40px 的剖面从 `(8,12,18)` 单调渐暗到 `(5,8,11)`，正是 Overlay 档 `0 12px 40px rgba(0,0,0,.45)` 的形状；资产卡 hover 的卡片下缘之后有 ~18px 的衰减带，与 `--shadow-1` 两层（blur 2 + blur 16）吻合；**未 hover 的卡旁边完全干净** —— 对应 `.card` 基线无 box-shadow。修前 → 修后的像素差：`overlay-settings` 180 → **69853**，`theme-paperink-image` 0 → **308739**。<br>⚠️ **一次未复现的失败，如实记**：r85 在第 47 张图（`overlay-palette`）之后窗口塌成 0×0，`capture: bad frame 0x0`，剩下 36 张全 FAILED、`overall=FAIL`，且崩溃前有几帧报出 `DrawRoundRect min=(20,0) max=(-20,420)` 这类 x 翻转的反向矩形。r86 / r87 / r88 连续三轮同参数**均未复现**，无残留进程。**原因未定论**，不写成「已修复」 | ⚠️ |
| 2026-09-30 | 过程事故 | **字节归位脚本把 `0A 0D` 写进了源码** | 修 `Overlays.cpp` 的行尾时，归位器写成「先 append 原字节、再补 CR」—— 产出 `0A 0D` 而不是 `0D 0A`，把 137 处行尾全写坏了，git 随即把该文件判成二进制（`w/-text`）。这就是「输出缓冲与输入游标必须独立推进」那条：**统一形态的损坏好救**，回退到 HEAD 再重打那 3 处改动即可。第二次写归位器时先在 4 份合成样本上自检（LF 串 / CRLF 串 / 混合 / 已损坏的 LF CR，四种都要归到 CRLF 且内容不变）才允许它碰真实文件 —— 上一版就是没验证就上手 | ✅ |
| 2026-09-30 | 归因纠正 | 「投影偏淡是因为 alpha 被乘了两次」**不完整** | 真实有两个独立成因，第二个即使修好第一个也仍然不对。① **乘了两次**：主题 JSON 的 `shadow.1` 存的已经是 CSS 那一层的 alpha（深空 0x4D ≈ 0.30 对应 CSS 0.3），再乘表里的 `layer.alpha` 就只剩 45%。② **环画成互不重叠的窄圈**（每层只覆盖 `[g_i, g_{i+1}]`、各带一份 alpha）：各环不累加，边缘处可见 alpha 反而只有 `w_0/Σw ≈ 1/3.2 = 31%`。正确形态是第 i 环**铺满 `[本体边, g_i]`、只带增量 `alpha·(w_i − w_{i+1})`**，由外向内叠，任意距离 d 处累计正好是 `w_{i(d)}·alpha`。② 与 ① 无关，只修 ① 得到的仍然是错的 | ✅ |
| 2026-09-30 | 门禁缺陷 | **取证退出码用错了量：判据红、进程却退 0** | `AppEntry.cpp` 写的是 `std::_Exit(result.failed == 0 ? 0 : 1)`，而 `failed` 只数「图片没写出来」。判据红（hover 探针没过 / 快照 TIMEOUT）时 79 张图照样全部 `saved`，`failed` 是 **0** ⇒ manifest 写 `overall=FAIL`、进程退 **0**。<br>**这轮是它自己咬人**：拆 `WorkspaceB.cpp` 之前拿退出码当「这轮绿了吗」，读到 0 就准备往下走 —— 是 manifest 的 `overall=FAIL` 与退出码互相矛盾才暴露的。`Review.cpp:1550` 的注释写着「overall 行必须存在且与退出码一致」，**断言在，实现没跟上**。<br>修法：`ReviewResult` 加 `bool pass`，`RunReview` 把 `pass` 写进去，调用方只读 `result.pass` —— 判据的结论**原样**传出去，不许调用方拿 `captured`/`failed` 二次推算。修后同一份代码退出码为 **1**。`run_reviews.ps1` 本来能靠 agree=False 抓到，但它只能报「不一致」，分不清是判据红还是判据自身坏了 | ✅ |
| 2026-09-30 | 重构 | **`Shell.cpp` → `Shell.cpp` + `Shell_Reports.cpp`** | 校验报告那一摊（解析 `v08_continuity.json`、`BuildCheckRows`、报告页与报告模态、`SeverityTone`/`ReportTone`/`ReportSummary`）整段 539 行搬进独立 TU。切在**功能边界**上而不是按行数对半砍：搬走之后剩下的 `Shell.cpp` 里「外壳本体」更完整，而报告那一摊本来就只被外壳的底栏页签与模态调用 | ✅ |
| 2026-09-30 | 重构 | **`WorkspaceB.cpp` 3522 行 → 11 个模块** | 这是全树最大的一个「上帝文件」：六个工作区的页面实现 + novel.db 快照 + 项目中心 + 三套状态词表 + 页面运行时通道全在一个 .cpp 里，共用逻辑住在顶部匿名命名空间，谁也够不着谁。按**三个真实边界**切，不按行数对半砍：<br>① **线程边界**（最硬的一条）—— `BookQuery.cpp` 是 worker 侧开库查询、只往 `BookState` 里填；`BookData.cpp` 是 UI 侧快照持有 / 视图重建 / 绑定选中。分错就会出现「UI 线程里开着库」这种明令禁止的形状。<br>② **页面边界** —— `Page_Novel` / `Page_Assets` / `Page_Storyboard` / `Page_ImageFlow` / `Page_VideoFlow` / `ProjectHub` 一个一页，公共 API 全在 `WorkspacePages.h` 不变，Shell 零改动。<br>③ **共用件边界** —— `PageCommon.h`（版式常量 + `LabelWidth` + `ViewportBottom` + 页面↔外壳运行时通道）与 `FlowGraph.h`（出图/出片共用的节点图内容）。`FlowGraph` 只导出三样（`FlowDataKey` / `ComfyRunningForBadge` / 两个 fit 标志），`Make*FlowNodes` 留在 .cpp 内部 —— 导出节点表就等于允许第二处定义它。<br>**顺带删掉一处死代码**：`StageStateFromStatus()`（原 94–104 行）全树零调用。<br>**顺带修掉两句被拆分作废的注释**：`Page_ImageFlow.cpp` 里「真值源：**同文件**的 Comfy 队列读数」已经不同文件了，改成指名 `FlowGraph.cpp`。注释里的断言与实现脱节时，先怀疑注释。 | ✅ |
| 2026-09-30 | 验证 | 拆分的行为等值证明（**含对照实验 + 一条工具纪律**） | 构建 `exit=0`；七道门禁全 `exit=0`（check-layers / check-colors / check-theme / check-i18n / find-silent-truncation / find-draw-arg-order / find-rect-wh-misuse）。取证：manifest 的 **`# shots:` 指标行在拆分前后逐字节相同**（`79 saved / 0 failed`、`hover-probes 9/11`、`shortcuts 7/7`、`overlay-clicks 7/7`、`duplicate-hits 0`、`inverted-rects 0`、`identical-driven-pairs 0`），`overall=FAIL` 成因不变（`book-snapshot` / `asset-snapshot` TIMEOUT，fixture 缺口，与本轮无关）。<br>**像素这一层不能当下划线的证据 —— 量的结果是这个 harness 本身不可复现**：同一个二进制连跑两轮，79 张里只有 **14~15** 张 md5 相同（rv20 是唯一一次离群，与其余每一轮都只共享 14 张）。差异是**同一个共享元素**上的 175px / 34×10 小块（x=1336..1369, y=213..222），裁出来读是**一个字形描边的亚像素抗锯齿**差异，肉眼不可分。⇒ **md5 逐张比对在跨运行场景下不是可用信号**。<br>仍然成立的是：拆分后各轮与拆分前那轮（rv16）的共享数（74/75/72/75）与**拆分后各轮彼此之间**的共享数（74/72/73/74/75）**同一量级**，没有任何一轮显示回归。另有 5 张（`dock-artifacts` / `hover-artifact-row` / `overlay-settings` / `overview-vstages` / `toast-warn`）**每轮都不同** —— 裁出来读是界面里印着的**输出目录名**（`build\rv16\_review_reports_pn…` vs `rv17`），与 2026-09-30 那次「投影差 180px 其实是模态里印着的工程路径」同型。<br>⚠️ **本轮自己踩了两次工具坑，都记下来**：① 写像素比对脚本时用 `W` / `Sl` 当函数名 —— 它们是 `Set-Location` 的**内置别名**，PowerShell 会优先解析别名，报的却是「位置参数无法找到」，与真实原因毫无关系。② 归一化变量名用了 `$A` / `$a`、`$X` / `$x`，**PowerShell 变量名大小写不敏感**，大写赋值把路径变量覆盖了。更严重的是第三版脚本**对 79 张全部报 0 差异**——而同一对目录我手工量过确有 28px 差异：**一个恒报 0 的判据和一个坏掉的判据长得一模一样**。判据必须先在**已知有差异**的样本上证明会红（拿 rv17 vs rv18 跑一遍，报出 88 字节差异才算数），再去读它的 0。 | ✅ |


| 2026-09-30 | 重构 | **`Shell.cpp` 2797 行 → 6 个文件** | 第二个上帝文件。按**「外壳的哪一族」**切，不按行数对半砍：<br>· `Shell_Layout.h` —— design-spec §4 的七个固定档（顶栏 46 / 导航 56 / 侧栏 240 / 检查器 280 / 底栏 190 / 状态栏 26 / 面包屑 34）。原先是文件顶部的 `constexpr`，**七块外壳每一块都要用**；各写一份就是七份可能打架的尺寸，留一个 .cpp 里则那个文件永远拆不开。收录标准只有一条：**换一块外壳照样成立**（命令面板行高、侧栏树扁平序号、layout.dat 的 magic 都不算版式）。<br>· `Shell_Chrome.cpp` 顶栏 / 导航栏 / 状态栏 / 面包屑 —— 同一族：贴边常驻、尺寸全来自那份固定档。<br>· `Shell_Panels.cpp` 侧栏 + 检查器 —— **放一起的理由不是「都在左右」**：它们是同一份选中态的两个视图（侧栏点一下改的就是检查器读的那份）。分成两个文件之后「谁写谁读」会看不明白，而「侧栏高亮和检查器属性不是同一个东西」正是这一层最容易出的缺陷。<br>· `Shell_Dock.cpp` 底栏页签条 / 任务队列 / 日志 / 产物 —— 同一套 `ScrollRegion` 视口口径与 `SetPageContentHeight` 高度口径。P4.6c 产物从文件中部搬到末尾附近，与它的绘制住在一起。<br>· `Shell_Palette.cpp` 命令面板 + 浮层分发 + 主题菜单 + 设置模态 —— 共用 `OverlayPanel` / `ScrimPaint` 与「浮层开着时 chrome 不提交 item」那条规则，放在一个文件里那条规则才有唯一读者。<br>· `Shell.cpp` 只留**本体**：帧循环 / 工作区分发 / 快捷键 / layout.dat 持久化 / 工程打开 / 项目中心整屏。<br>**一个必须记的 C++ 约束**：`Shell::` 成员函数定义只能出现在**包住** `shine::pages` 的命名空间里，匿名命名空间是**嵌在**里面、不包住 —— 所以切分时成员函数那一段绝不能顺手包进 `namespace { }`，否则报的是「声明与定义不匹配」这类与真实原因无关的错误。共享常量（`kPaletteGroupH` 等）放 `shine::pages` 直接可见即可：`constexpr` 在命名空间作用域本来就是内部链接，不需要匿名命名空间来兜。 | ✅ |
| 2026-09-30 | 验证 | `Shell.cpp` 拆分的行为等值 | 构建 `exit=0`；七道门禁全 `exit=0`；取证 manifest 的 `# shots:` 指标行与拆分前那一轮（`rv21`）**逐字节相同**，`overall=FAIL` 成因不变（book/asset snapshot TIMEOUT）。像素层按上一条已定的纪律不下结论（同一二进制连跑两轮只有 14~15 张 md5 相同，md5 逐张比对不是可用信号） | ✅ |

---

## 下一步

重构主干（51/51）已完成并通过验证。剩下的是**产品缺口**，不是重构缺口：

| 缺口 | 归因 | 说明 |
|---|---|---|
| 阶段执行体是桩 | `shine_core` | **归因成立（2026-09-30 复核并修正了三处事实）**。① 签名写错了：`pipeline::StageExecutor` 实为 `std::function<bool(StageId, const std::string&, std::string&)>`（`src/pipeline/Runner.h:23`），第二个参数是 **`const std::string&`**，不是 `string&`。② `GenerateOneChapter` 在命名空间 **`shine::novel`**（`src/novel/NovelPipeline.h:45-49`），不是 `novelcore`（`NovelPipeline.h:18` 只是给别的类型做别名）。③ **`NovelContinuity` 根本不是执行体** —— 它是 V8 连续性校验模块，唯一入口 `RunContinuityChecks` 对相邻镜对做 C1–C12 机器校验，形状上无法充当 T 阶段执行体；把它列为「真执行体」是概念错位。<br>结论保留的理由也比「签名不同」更硬：`Runner::RunNext` **每个阶段调一次**执行体、共 29 个阶段（`StageMachine.h:10-11` 的 T1–T17 + V0–V11），`Runner.cpp:40` 先扣预算、`:46` 才调执行体、`:51-55` 成功后还要落 `work/T*.json` 并记账 —— 把「生成一章」包成 29 次调用会稳定产出假账本。且两套 V 阶段枚举互不兼容（`pipeline::StageId` 是 V0–V11，`src/novel/NovelVisualStages.h:55,64-65` 的 `VisualStageId` 是 V1–V7），`pipeline/` 全树对 novel 零引用。`src/ui/**` 对这些执行体零调用属实（唯一入口是 `src/ui/imgui/host/AppEntry.cpp:132-133` 的 `--novel-` → `RunNovelCli`）。<br>界面已不再假装：原先注入的空 lambda 让整页跑出一套看起来正常的假数据（17 阶段完成 / ¥0.17 / 100%），现在改成 `Configure(root, mode, nullptr, nullptr)` 且 `StartRun` 压根不调 `RunNext`；KPI 五行的全 0 改成「未接入」，状态栏的 `T1/17 · 0%` 改成「阶段执行体未接入」。 |
| ~~S1–S12 停止规则表~~ | —— | **已接上（2026-09-30），原归因是错的**。缺口曾记为「`pipeline/StopPolicy` 只判 5 组、没有可枚举的规则表接口，补它要动 `shine_core`」。**错因是编号撞名**：`src/pipeline/StopPolicy.cpp:7-11` 的字面量也叫「S1–S4 预算」/S5/S6/S7/S8，那是**另一套** 5 组判定。真实的 S 编号在 `shine::novelcore`：`src/novel/NovelRunLoop.h:43-48` 的 `StopCode`（S1…S12）与三个公开 noexcept 自由函数 `StopCodeName` / `StopCodeCondition` / `StopCodeHint`，实现 `NovelRunLoop.cpp:102-138` 逐条落地（阈值 >=2 次、>2×、>=5 次、>=500 条… 都写在那十二条中文判据里）。**零 shine_core 改动。**<br>接上时发现旧界面**在误导**：它照 `StopPolicy` 的编号把「LLM 调用 0/1000」标成 **S1** —— 同一个代号，领域文档里指「机器校验连续失败 >= 2 次」，界面上却是 LLM 调用预算。已拆成两张卡：「停止条件 · S1–S12」画真实规则表，「流水线停止规则」画预算/前置且**去掉 S 编号**。<br>**唯一真实限制**（不构成改 core 的理由）：S1/S2/S3/S4/S6/S7/S9/S10 的数字阈值是 `EvaluateStop` 里的字面量，结构化取值只能拿到 `StopCodeCondition` 的**文本**（S5 的阈值另有公开的 `RunLimits`）。且逐条判定要一整轮跑出来的现场数字，本页没有 ⇒ 卡上**只列规则、不标状态**（画成「已触发/未触发」就是编状态）。 |
| ~~侧栏树 / 检查器的**卷层级**~~ | —— | **已接上（2026-09-30），原归因是错的**。缺口曾记为「`NovelGraph` 只有 `UpsertVolume`、没有 `ListVolumes`，补它要动 shine_core」。实际不需要新接口：`ChapterRow::volume_id` 早就有（`src/novel/NovelTypes.h:115`），`ListChapters` 的 SELECT 也带了它（`src/novel/NovelGraph.cpp:410,421`），独立的 `volumes` 表也一直在。缺的只是 UI 侧**两行没搬的字段** —— `BookChapter` 结构体漏了 `volume_id`，`LoadBook` 的搬运循环也漏了。`LoadBook` 本来就持有一个只读 db 句柄（`db.Open({.readOnly=true})`），一条 `SELECT id,title FROM volumes` 就拿到卷名。侧栏树改成「卷 → 章 → 镜」三层（`FlatTreeIndex` / `ResolveTreeIndex` 同步改成三层先序，卷展开态存 `collapsedVolumes_`）。**一个被记成「要改业务层」的缺口，其实是 UI 侧没读两列。** |
| ~~章节×阶段双轴甘特（V 链）~~ | —— | **已接上并渲染（2026-09-30）**。`T1–T17` 文本链**确实**没有章节轴（`LedgerEntry` 只有 4 个字段、`output_path` 形如 `work/T1.json`），这一半归因成立、这一半做不了。但 **`V1–V7` 视觉链有**：表 `stage_artifacts` 带 `chapter_id` + `stage` + `scene_ord/shot_ord` + `created`，只读接口 `ListStageArtifacts(chapterId, stage)` 已存在（`src/novel/NovelVisual.h:242-243`、`.cpp:604-613`），**零 shine_core 改动**。`BookSideView::chapterVStages` 接 worker + 视图，fixture 补 45 行 `stage_artifacts`（第 1 章 5 镜跑满 V1–V7、第 2 章 2 镜只到 V5、第 3 章无镜）。<br>渲染先前「写完又删掉」，理由是右栏放不下 —— 那个理由**本身建立在一个没生效的机制上**：右栏的 `ScrollRegion` 当时根本不能滚（见下一行的 ScrollRegion 缺陷），所以「放不下」是必然的而不是设计的。改成真滚动后矩阵直接画出来，r70 的 `overview-vstages` 拍到且数据与 fixture 逐格一致。 |
| 侧栏树 / 检查器主体 | 页面层 | **已实现**：`WorkspacePages.h` 开 `BookSide()` 只读视图（`BookSideView` 纯数据拷贝，`ApplyBook` 落地时重建），侧栏画「卷 → 章 → 镜」三层树、点选换章换镜，检查器「属性」给九行真实字段。镜码 `ShotCode()` 三处共用一份实现。资产工作区的 kind 筛选树、节点「快捷跳转」、检查器「关联」段（伏笔 / 场 / 镜）也已于 2026-09-30 接上真数据。**仍缺**：把「哪一对镜连续性不过」做成可点的结构化关联（点一下跳到故事板并高亮那一对）。原记的归因「`ContinuityIssue` 没有 shot id，要归因必须改 `src/novel/`」**不准确**：两个 `ShotRow::id` 就在 `detail` 文本里（`镜 #128→#131`），2026-09-30 已用 `ShotPairToCodes` 在内存快照里换回镜码显示（`镜 S004 → S005`）。真正缺的**只是结构化字段** —— 现在要跳过去只能正则解析人类可读的 `detail`，而那是把一条契约绑在字符串格式上；要做成可点的关联，应在 `src/novel/` 给 `ContinuityIssue` 补 `shotAId/shotBId`，属越界，本轮不猜。 |

| ~~小说页「设定」模式的实体卡不可点~~ | —— | **已接上（2026-09-30），原判断是错的**。先前记「实体选择与属性在资产工作区那张网格上，两边没打通」，并说「要做点选-联动得先定『点了之后右栏显示实体属性还是仍显示章节属性』，属产品决策」。实际设计稿里没有这个歧义：点实体就是换**被选中的实体**，右栏读的是同一个 `BookState::selectedAsset` —— 资产总览网格那张卡走的也是它。补 `HitTest`（一次取齐 hovered + clicked，**不要** `Hovered(idA)` 叠 `Clicked(idB)`，本仓踩过）→ `SelectBookAsset(i)`，该函数会顺带 `RebuildBookSide`，所以侧栏高亮跟着走。描边规则照抄资产总览网格那张卡（`.card:hover` 把边框提到 accent-glow），不另发明一套选中样式。**「另存一份局部选中态」就是第 N 个「点这边亮那边」**，这里没犯 | ✅ |

另有一条**已接受降级**：`Derived::gateBg` / `checkRowBg` 当年按纯 alpha 算，
与设计稿的实色差一个底。字段按「只加不减」冻结，要 1:1 请用新增的
`stageDoneBg` / `stageFailBg` / `gateFailBg`。

## 视觉 1:1 符合性审计（2026-09-30 起）

投影已按 CSS 接完（见验证记录）。这一轮还留下一批**已核实、已排序、尚未动**的 token 偏差。归因一律先独立核实过，默认假设是「我错了」而不是「它不存在」。

| # | 偏差 | 归因 | 说明 |
|---|---|---|---|
| 1 | ~~`--dur-1/2/3` 三个常量**零读点**~~ **已接** | — | `kit/Anim.h` + `kit/Anim.cpp`（third/ImAnim）已建，`kit::TransitionTo` / `kit::TransitionColorTo` 走 ImAnim 补间。另订正：`kEmphasized{0.3,0.05,0.15,1}` 与设计稿对不上（`tokens.css:28` 是 `--ease-out: cubic-bezier(0.16,1,0.3,1)`），已按 CSS 改成 `kEaseOut`；凭印象编的 `kExit` 已删。**注**：CSS 里实际是 43 处 `transition`（不是 60+），其中 56 个槽位是颜色类、18 个 transform、仅 1 处 width |
| 2 | ~~面包屑条**整条底色 + 下边框缺失**~~ **已接** | — | `Shell.cpp` `DrawBreadcrumbs` 已按 `shell.css:223-234` 补齐底色（`theme::CurrentDerived().crumbBg`，这个派生色自 `Theme.cpp:99` 算好却**零绘制消费点**，只有 check-theme 自检在读）、下边框、padding 24→16、字号 12.5→12.0（12.5 不在 `kSizes` 里，`LookupNearest` 向上取档 ⇒ 实际渲染 13px）、分隔符按 flex `gap:7px` 真量字宽 |
| 8 | ~~面包屑 12.5→12px、左内边距 +24→16~~；顶栏左内边距 +16→12、品牌→胶囊 +20→10；活动栏行距 +48→44、选中底 44×44→40×40；dock 左右内边距各 −2px；~~检查器段头 12.5→12px 且 `ColorText()`→`ColorTextSecondary()`、箭头 10→11px~~；底栏页签字号 `Tabs` 13px→`.dock-tabs > button` 12px | 页面层 | 面包屑与检查器段头本轮已改。检查器段头另外三处：高度 24→**39.2**（`padding:10px 14px` + 行盒 `12×1.6`，见下节）、分隔线从段头下方移到**整段底部**（`.sect` 的 `border-bottom`）、hover 去掉底色只改文字色 |
| 3 | 15 个 `line.*` / `fill.*` 的 alpha 被压平成不透明实色 | 主题层 | JSON 存的是 CSS `rgba` 叠在 `--bg-surface` 上的结果，算法自洽，但叠在哪个底上都一样（例：深空卡片边应为 `#29303E`，实现画的是 `#202734`，暗 9/9/10） |
| 4 | 15 处字号取不到档：`FontBoldAt(9.5f)` / `(16.0f)` 等 | 字体层 | 两侧都无此档，向上取档到 10.5 / 17px。设计稿有 9.5 / 12.5 / 14.5 / 16px |
| 5 | `font-weight: 500` 无字面 | 字体层 | `Fonts.cpp` 只加载 400 / 600 两档 |
| 6 | ~~`.card.hoverable:hover` 的 `translateY(-2px)` 未接~~ **这条记录是错的，已订正** | — | `kit::Card()` 早就接了（`kit/Widgets.cpp:393-396`，`lift = hovered && hoverable ? -2.0f : 0.0f`）。真正缺的是**没走 `kit::Card()` 的页面级卡片**：资产卡、项目卡、向导行等手写 `DrawShadowed` 的地方。要补的是「把手写卡片换成 `kit::Card()`」，不是「给 `kit::Card` 加位移」 |
| 7 | `[data-theme="inkwash"]` 下 `.brand .mark` / `.hub-logo` 被 `tokens.css:206` 的 `box-shadow: none` 覆盖 | 页面层 | 水墨主题下这两个 logo 应当**没有**投影，ImGui 侧未按主题特判 |
| 9 | `.tl-card` 宽 96→128、圆角 8→10；项目中心卡片名 `FontBoldAt(17.0f)`→14.5px | 页面层 | 同上 |

**未接投影档位、且需要先补 `HitTest` 的两处**（不是漏接，是顺序问题）：`pages/WorkspaceA.cpp:36` 总览 KPI 卡（`.card.kpi.hoverable` → hover 该走 Card 档，但 `KpiCard` 整个没有命中测试）、`pages/ProjectHub.cpp` 的向导确认卡（`.card.glow` → hover 该走 Accent 档，同理无 `HitTest`；且它是只读汇总卡，hover 反馈价值低，建议只补 `HitTest` 不接档）。**`pages/Page_Assets.cpp:70` 的总览网格卡在设计稿里找不到对应元素**（设计稿该页是表格而非卡片栅格），代码注释自称抄「资产总览网格」，其真实原型是 `Assets.jsx:151`。按纪律不硬套档位，注释里的原型出处待订正。

> 这三处的行号原本指向 `WorkspaceB.cpp` / `WorkspaceA.cpp` 的旧坐标；`WorkspaceB.cpp` 已于 2026-09-30 拆成 11 个模块（见上方变更记录），行号已按新文件重指。**`ProjectHub.cpp` 的向导确认卡没有给行号** —— 拆分后它的位置会随任何一次编辑漂移，而这里要记的是「它还没接 `HitTest`」这个事实，不是某一行。


---

## 文字垂直居中（2026-09-30）

用户报「很多按钮的字不在中间」。全树 `AddText` / `DrawTextClipped` 共 **216 处**，逐个归类。

### 度量口径（先定死，否则每次量出来的数都不一样）

`ImFont::RenderText` 里 `const float line_height = size;` —— **ImGui 的行盒高度就等于请求字号**，行盒顶就是 `pos.y`。所以

```
行盒中心 = pos.y + fontSize / 2
Δ = (pos.y + fontSize/2) − 容器中心Y
```

正确写法 `pos.y = 容器中心Y − fontSize/2`，已封装成 `kit::CenterTextY(font, size, centerY)`（`kit/Draw.h`）。

⚠️ **两条已踩过的坑，别再走**：

1. **不要**改成「按字体真实 Ascent/Descent 算」。这一版 ImGui 的 `Descent` 是**负数**（本机 size=13 时 asc=11 / desc=−3，合计只有 8），照它算会把字往下推 2.5px —— 方向与「字偏高」正好相反。已撤回过一次，`Draw.h` 里留了记录。
2. `min.y + (h − size) * 0.5f` 与 `center().y − size * 0.5f` **代数恒等**。曾经有一条注释说前者「假设 Ascent+Descent==fontSize、逐字号各偏各的」—— **那句是错的**，两式数值一样，注释在骗人。已撤回。

残留的 −0.45px 光学偏差（CJK 墨迹盒中心在基线上方 0.38em）**已知且接受**。量级判据：`|Δ| ≤ 0.5` 不动，`|Δ| ≥ 1` 才算缺陷。

### 推翻我自己写错的一个数

上一轮我在 `Shell.cpp` 注释和本文档里都写过「检查器段头偏上 **6.2px**」。**这个数是错的**，按上面的口径重算：文字 `min.y+5.0f`、字号 12.5 ⇒ 行盒中心 `min.y+11.25`，框中心 `min.y+12`，**Δ = −0.75px**，落在已接受的光学偏差带里。段头真正的毛病是**整段高度差 15.2px**（24 vs 39.2），三段合计检查器比设计稿短 45.6px —— 是**布局短**，不是字没居中。注释已改。

### 本轮修掉的（按偏差排序）

| 位置 | 内容 | 偏差 | 说明 |
|---|---|---|---|
| `kit/Widgets.cpp` `Chip()` | 主标签画在 `cy`（中心线，一个字都没减）、`.cnt` 同 | **低 5.25~6.0px** | 全树最严重的一处，而且是 **kit 小组件自身**；被资产侧栏筛选 chip 实际调用 |
| `pages/Shell.cpp` 队列行 | 任务名 `y+3.0f`、进度 `y+4.0f` | **高 5.75~6.0px** | 同一行里居中的 Tag 是正的，旁边两段字比它高 6px，最扎眼 |
| `pages/Shell.cpp` 报告模态页脚 | 说明句 `max.y−12−14` | **低 6.25px** | 同一页脚里左边说明、右边按钮差 6px |
| `kit/Widgets.cpp` `Tabs()` | `item.min.y + 8.0f` | 高 1.5px | 这个 8 只在容器高 29 时成立，实际调用给 32（dock 四个页签） |
| `kit/Widgets.cpp` `Tree()` | `-7.5f` / `-7.0f` | 高 1.0~1.25px | 两侧栏在用 |
| `kit/Overlays.cpp` `Modal`/`Drawer` 标题 | `top + 14.0f` | 高 2.0~2.5px | **潜伏缺陷**：`Modal`/`Drawer`/`Toast`/`Menu`/`DataTable` 全树零调用 |
| `kit/Widgets.cpp` `DataTable()` | 表头 / 单元格写死 `+7`~`+9` | 高 1.5~2.75px | 同样零调用 |
| `pages/Page_Novel.cpp` `Page_Storyboard.cpp` `ProjectHub.cpp` | 模式页签 / 章选择 chip / 最近项目计数 ×2 | 高 1.0~1.75px | 原 `WorkspaceB.cpp`，2026-09-30 拆分后重指 |
| `pages/WorkspaceA.cpp` | 甘特行标 | 高 0.75px | |
| `pages/Shell.cpp` 检查器段头 | 高度 24→39.2、文字改 `CenterTextY`、分隔线移到整段底部、箭头 10→11、颜色→`text-secondary`、hover 去掉底色 | — | 见上 |

### kit 小组件覆盖缺口（2026-09-30 两轮收掉 1~7 与 11~13，#8~#10 判定为不该收）

用户要求「小组件请封装，不要重复写」。这一轮**归因修正了**：原表把 8 处全记成同一族「列表行」，那是错的 —— 按实际几何只有 **4 处**是列表行，另 4 处是另一种东西，强行合并只会造第二份假抽象。

> ⚠️ 这张表的第一版把第 1 项写成「5 处」、把 WorkspaceA 产物算进 `ListRow`。**实际代码是 4 `ListRow` / 3 `ListCard`**（`Shell.cpp` 队列 / palette / 产物 / 报告 四处 `ListRow`；`WorkspaceA.cpp` 产物 + `ProjectHub.cpp` 向导模板 / 打开列表 三处 `ListCard`）。总数 7 对，分法错。起因是迁移时把 WorkspaceA 那一处从 `ListRow` 改成了 `ListCard`（它要投影档 + accent 描边环，`ListRow` 给不了），但表格没跟着改。**数字以调用点为准，不以当时的打算为准。**
>
> ⚠️ 同理，「`WorkspaceB.cpp` 向导模板 / 打开列表」是**拆分前**的坐标：2026-09-30 `WorkspaceB.cpp` 拆成 11 个模块后，向导与打开列表都在 `ProjectHub.cpp`，WorkspaceA 那一处仍在 `WorkspaceA.cpp`。计数 4/3 未变（以调用点为准，与文件拆分无关）。


| # | 重复模式 | 实际重复处 | 处置 |
|---|---|---|---|
| 1 | 列表行（行框 + hover 底 + 左图标 + 主文字 + 右侧次要文字 + chevron） | **4 处**（队列 / palette / 产物 / 报告） | ✅ 抽 `kit::ListRow`（`kit/Widgets.h`），4 处全改 |
| 2 | 双行卡（标题 + 描述，hover 走**投影档**、选中走 accent 描边环） | **3 处**（WorkspaceA 产物 / 向导模板 / 打开列表） | ✅ 抽 `kit::ListCard`，**刻意不与 ListRow 合并** |
| 3 | 缩略图格（60px 固定格，无 hover 无命中） | 1 处（胶片条） | **原表归因错了**：它既不是列表行也不是卡（`hoverable=false` 的 `ListCard` 也不能用 —— 它要的是 `fill-muted + 1px 边` 而不是 panel + 投影）。维持现状，1 处不值得抽 |
| 4 | 对话框外壳（scrim + 圆角 + 投影 + 头 + 体 + 脚） | 3 处（向导 / 打开 / 确认） | ✅ 走 `kit::ModalFrameRect`（`Overlays.h`），删掉页面层私有 `DrawModal` |
| 5 | Toast | 1 处，偏低 2.25px | ✅ 走 `kit::Toast`，给 kit 补了 `alpha` 参数保住 0.4s 淡出 |
| 6 | 下拉菜单 | 1 处（主题菜单） | ✅ 走 `kit::Menu`。**代价是主题色块没有了**（`.swatch` 是 24×14 渐变方块不是图标字形），选中态改由 accent 字 + accent-dim 底表达 |
| 7 | 数据表格 | 1 处（报告表 4 列） | ✅ 走 `kit::DataTable`，给 `TableColumn` 补 `tag` / `mono` 两个形态 |
| 8 | 甘特 8 列 | 1 处 | **不是表格**：没有表头带、单元格是色块、列是等分网格。套 `DataTable` 会硬造一个设计稿里不存在的表头 |
| 9 | 「图标 + 文字」居中胶囊 | 3 处 | 仍未做 |
| 10 | 分段页签条 | 3 处、2 套实现 | 仍未做（`kit::Tabs` 在，调用点没统一） |
| 11 | 模态外壳（设置模态，560×452 + 图标/标题/×/分隔线） | 1 处 | ✅ 第二轮收进 `kit::ModalFrameRect`；顺带消掉 `SettingsCloseRect()` 里一份**独立的 560×452 复算**（详见下节） |
| 12 | 浮层壳（命令面板：顶部下拉、**无** `.modal-h`、非居中） | 1 处 | ✅ 用新原语 `kit::ScrimPaint` + `kit::OverlayPanel` 拼；**刻意不折进 `ModalFrameRect`** —— 它强制居中且带标准头部，折进去两边都不对 |
| 13 | `kit::Modal`（只返回内容区的简版模态） | **0 调用** | ✅ 第二轮**删除**。它与 `ModalFrameRect` 的头部绘制逐行重复（图标 17px@+15 / 标题 15px 按头高中心落字 / 头底 1px 分隔 / body 内缩 18），而 `ModalFrameRect(...).body` 就是它返回的矩形 —— **同一个模态在 kit 内部写了两遍**。零调用点下没有保留理由。需要「按内容自适应高度」时把 `height` 传 0 |

**原表说「零调用的 5 个最优先」这个判断是对的**，但其中 `Modal`/`Toast`/`DataTable` 三个**本身不需要改**（Y 居中上一轮已经改成 `CenterTextY` 了，Δ 已经是 0）—— 要做的是**接线**，不是修组件。`Drawer` 至今零调用，页面上确实没有抽屉式交互，**不算缺口**。

### 顺带修掉的三个真缺陷（不是重构噪声）

1. **「打开…」列表行的「打开」按钮是死按钮**：整行 `HitTest` 先注册，按钮后注册，而 ImGui 同窗口内**先注册者独占** `HoveredId`（`imgui.cpp:5161`）⇒ 按钮永远 `clicked=false`。当时没暴露，只因为外面还写着 `|| hit.clicked` 兜底，整卡点击把功能兜住了 —— 按钮画得出来、按下去没反应。`ListCard` 的 footer 回调在整卡命中**之后**调用，顺序对了。
2. **小说侧栏的「快速跳转」整组被空态吞掉**：`DrawJumpButtons` 排在「这本小说还没有章」那个 `Empty()+return` **之后**，于是没章节 / 没绑定 / 读取中 / 读取失败四种状态下这四个按钮一个都不画 —— 而它跳的是资产 / 分镜 / 出图 / 出片四个工作区，与「有没有章节」毫无关系。挪到所有空态 `return` 之前。
3. **同帧重叠热区自检**（`kit::NoteDuplicateHit` → `DuplicateHitCount` → 进 `overall` 判据）：上面第 1 条那类 bug 的失败模式是**静默**的 —— 控件画得出来、编译过、截图正常、manifest 记 `saved`，只有那个控件点不动。现在它会自己举手。这与 `InvertedRectCount` 同一族兜底，区别是那个抓「画不出」，这个抓「画得出但点不动」。

**迁移过程中被自检抓到的一次**：报告行我先写了 `spec.id` 又在下面补了一次 `kit::HitTest` —— 正是上面第 1 条那个形状，靠读代码看不出来（新旧两段都「看起来对」），是 `DuplicateHitCount` 判据逼我回去看出来的。修完实测 `duplicate-hits: 0`。

### 判据基建：热区扫描（`SHINE_SCAN`）

排查上面第 2 条时加的：`SHINE_SCAN=<ws>:<x0>:<y0>:<x1>:<y1>:<step>` 把鼠标沿一片网格挨个挪过去，打出每格命中了哪个控件 id，并返回出现过的**全部 id 集合**。

**为什么需要**：`hit=0` 这一个信号至少对应三种原因（注入没到位 / 视口塌了 / **坐标落在热区外**），而第三种是「布局一改就全失效」的：坐标是照着某个旧截图量的。布局改完之后探针报红，日志只说「点空了」，**不告诉你这个控件现在在哪** —— 于是要么去改本来正确的产品代码，要么凭感觉挪两个数字再跑一轮 300 秒。扫一次把这个信息补齐：`jump-*` 在小说侧栏**根本不存在**这件事，是扫描一眼看出来的，日志读不出来。

⚠️ 要的是**集合**而不是「某一点命中了什么」：全屏遮罩若注册成热区，会在每一格都命中并盖住所有真控件 —— 单点读数看不出被盖住了，整片集合才看得出。

### 判据自检逮到的两个回归（2026-09-30，本轮自己引入又自己抓回）

**这一节是本轮最重要的部分**：下面两条**都是我在「收编模态外壳」时自己引入的**，而**每一条在引入时都是全绿的**。

1. **遮罩注册成全屏热区** ⇒ 浮层里按钮可能被抢走 `HoveredId`。危害方向**取决于绘制顺序**，而两种顺序结论相反：
   - 项目中心：卡片与工具条在前、对话框最后画 ⇒ 遮罩排在按钮**之后**，「先注册者独占」时按钮照样拿到 `HoveredId`，**不会坏**；
   - 报告模态：外壳先画、「导出 / ×」按钮后画 ⇒ 遮罩**先**拿到，**全部按钮按不动**。

   我第一版以为两者都会坏，**判断错了**。判据自检（把遮罩故意改回 `kit::Scrim`）实测：`duplicate-hits` 从 0 变 **6232**、overall FAIL，而 hub 那条浮层判据**仍然 5/5**。结论：**抓这个的是 `duplicate-hits`，不是浮层点击判据** —— 后者验的是另一件事。
2. **「点面板外关闭」把刚打开的对话框当场关掉**：点工具条「打开项目」的那一下点击，在**同一帧**里 `openDlg` 才置真、对话框才被画出来，而 `IsMouseClicked()` 那一帧仍为真、鼠标仍在工具条上（对话框矩形之外）。症状是「点『打开项目』→ **闪一下就没了**」。修法：`HubState::dismissArmed`（只在本帧之前对话框就已经开着时，点外面才真的关）。

**为什么这两条值得单列**：共同形态是**编译过、截图正常、manifest 记 saved、当时所有判据全绿**，只有真的点一下才看得见。第一条靠「判据自检」（故意改坏看门禁会不会红）才暴露；第二条靠新加的 hub 浮层判据当场抓到 —— 而**那条判据自己第一版是假绿**：它不检查「对话框真的开了」，会在没开的状态下扫一片没有遮罩的界面照常通过。补上 `HubDialogOpen()` 前置条件后才真正有效。

**判据自己也会骗人**：一个「报通过的原因」和「它本该抓的缺陷」长得一样的判据，比没有判据更坏 —— 它会让人以为这块验过了。

### 第二轮：最后两份浮层副本，和三段「注释说了但没接上」

这一轮把「同一份浮层壳写了 4 遍」收到 1 遍。顺带挖出三处**只有逐行读才看得见**的东西，其中两处是产品缺陷。

**1. 命令面板的遮罩是一次完全被覆盖的死绘制**（产品缺陷，已修）

```cpp
DrawRoundRect(draw, bounds.min, bounds.max, 14.0f, ColorScrim());   // ← bounds 是面板自己
DrawShadowed(draw, bounds.min, bounds.max, 14.0f, ...);            // ← 下一行整个盖住
```

`.scrim` 是 `position: fixed; inset: 0` —— 铺满**屏幕**。这里传的是面板自己的矩形，于是这张遮罩被面板底板 100% 覆盖，**命令面板从来没有遮罩**，背后该压暗的界面一点没暗，和其它所有浮层都不一样。编译过、截图正常、manifest 记 `saved`，唯一的症状是「和别的浮层不太一样」。

**2. 同一处有一段注释描述了一个从没实现的机制**

`ChromeHit` 的注释写着「点击由浮层自己的遮罩处理（遮罩走 `IsMouseClicked` + `IsMouseHoveringRect`）」，而 `DrawCommandPalette` 里**根本没有点外面关闭的代码**，只有 Esc / Ctrl+K。**注释里的断言与实现脱节时先怀疑注释** —— 它是这条需求的唯一来源，错了就会照着它去改。

**3. `SettingsCloseRect()` 是一份独立的 `560 × 452` 复算**（产品缺陷，已修）

「绘制与判据共用同一份几何」这句话只对了一半：判据确实没在 `Review.cpp` 里复算，但**产品内部早就分叉成两处** —— `DrawSettingsModal` 算一遍 `w/h` 得 `bounds`，`SettingsCloseRect` 又照着 `560 × 452` 重算一遍 × 的位置。把 452 改成 480 就分叉：× 停在老位置，点它什么也不会发生，而判据只会报「没关掉」。现在几何只有 `kit::ModalFrameRect` 一处算出来，写进 `settingsFrame_` / `settingsClose_`，绘制与判据都读它。

**kit 侧两个新原语**

- `kit::ScrimPaint` —— 遮罩**只画不注册**。`Modal` / `ModalFrameRect` / `Drawer` / 命令面板共用。后两个顺带从 `Scrim`（会注册全屏热区）换过来，并把只服务于那个热区的 `id` 形参删掉 —— 留着就是一个没人用的参数，正是「看着加了、实际等于没加」那一族。
- `kit::OverlayPanel` —— 面板底板（`bg-overlay` + `1px line-normal` + 常驻 `shadow-2`）。原本那三行字面量在四个文件里各写一遍。

**命令面板没有折进 `ModalFrameRect`**：它顶部下拉、不居中、**没有** `.modal-h`（输入框直接顶在顶部）。`ModalFrameRect` 强制居中并带标准头部，折进去两边都不对。判定依据是形态，不是「能不能凑合」。

### 判据侧：两条新判据 + 一次「报错信息自相矛盾」

**新判据**（`overlay-clicks` 5/5 → **7/7**）：

1. **命令面板 点遮罩关闭** —— 开着是前置条件，点面板外必须关掉。
2. **同帧自毁守卫** —— 面板在**打开的那一帧**就注入一次「面板外」点击，必须仍然开着。真实触发路径是点顶栏搜索框打开面板，而那一次点击位置就在面板之外；少了 `!openedThisFrame` 守卫，搜索框就是死按钮，症状是「点了没反应」，而**截图序列里完全看不出来**（面板开出来又当场关掉，只多一张正常画面）。

第 2 条**刻意不扫顶栏找 `tb-search`**，改用 `SetCommandPaletteOpen(true)` 走同一个 `ToggleCommandPalette()`（同样置 `paletteJustOpened_`）—— 验的是同一个守卫，但不依赖坐标。

**判据自己出的一个 bug，值得单列**：第一轮 `设置模态 ×` 红了，报错是「位置注入没到位（要 1051,277，帧内看到 1051,277）」—— **两条消息一模一样，却说没到位**。原因是新几何把 × 的中心算到了 **277.5 半像素**（头部高 47 减 22 除 2），而判据按 `< 0.5f` 判注入到位，差正好卡在边界上；同时报错用 `static_cast<int>` 打印，把 277.5 和 277.0 都印成「277」，于是自相矛盾。产品侧把 × 的纵向偏移取整（不制造半像素），判据侧容差放宽到 1.0f、打印改一位小数。

**报错信息自相矛盾时，先怀疑信息本身** —— 我第一反应是「判据在骗我」，实际错的是容差与打印精度。

**一条没查清的观察（如实记下，不写成结论）**：`FindHotspot` / `ScanHotspots` 扫顶栏 `y 0~64 × x 0~1600` 始终扫不到 `tb-search`（step 6 与 12 都返回 `(空)`），而同一函数在项目中心里找 `hub-open` 正常。一次性诊断在同一轮、同一状态下打点显示 `注入(300,23) -> hoverCount=1 id=tb-search` —— **顶栏热区注册是好的，产品没问题**。差异只在「判据那一刻的状态」，根因未查清。判据 ② 因此改成不依赖坐标。

### 取证结果（2026-09-30 第二轮）

| 指标 | 第一轮之后 | 第二轮之后 |
|---|---|---|
| `overlay-clicks` | 5/5 | **7/7**（新增命令面板那两条） |
| `duplicate-hits` | 0 | **0**（遮罩改走 `ScrimPaint` 后仍为 0） |
| `inverted-rects` | 0 | 0 |
| `hover-probes` | 9/11 | 9/11（同上，fixture 缺口） |
| `shortcuts` | 7/7 | 7/7 |
| shots / failed | 79 / 0 | 79 / 0 |
| `overall` | FAIL | **FAIL**（成因不变：`book-snapshot` / `asset-snapshot` TIMEOUT） |

构建 `exit=0`；`check-layers` / `check-colors` / `check-theme` / `check-i18n` / `find-silent-truncation` / `find-draw-arg-order` 全 `exit=0`，后两者 `-SelfTest` 亦 0。

证据留档：取证目录 `build/rv14/`（`shots-manifest.txt` + `rv14.log`）与基线目录 `build/rv15-baseline-31b56a0/` **都保留在盘上未删**，可复核。

### 基线对照（`overall=FAIL` 不是本轮引入的）

早先这里写的是「已用 `git stash` 对照跑过基线，基线同样红」—— 结论对，**但那个基线目录跟 `rv13` 一起被我当临时垃圾清掉了**，等于结论只在提交信息里，工作区查无实据。重跑一次并**把证据留下**。

基线取 `31b56a0`（= `90b8b9f` 的父，整个 kit 收编工作的**前**一个提交）。做法是 `git checkout 31b56a0 -- src/ui/imgui refactor/PROGRESS.md` 临时回退、增量构建、跑取证，再 `git checkout HEAD --` 原样还原并重建 —— **不碰工作区里用户自己的 `Plugins/` `docs/` `design/` 删除**。

| 指标 | 基线 `31b56a0` | 当前 `eea33a6` |
|---|---|---|
| `book-snapshot` / `asset-snapshot` | **TIMEOUT / TIMEOUT** | **TIMEOUT / TIMEOUT** |
| `hover-probes` | 8/11（红 `hover-jump-btn` / `tree-node` / `asset-card`） | **9/11**（红 `tree-node` / `asset-card`） |
| `overlay-clicks` | 4/4 | **7/7** |
| `duplicate-hits` | 该指标当时**还不存在** | 0 |
| `inverted-rects` | 0 | 0 |
| shots / failed | 79 / 0 | 79 / 0 |
| `overall` | **FAIL** | **FAIL** |

**结论有两层，都要分开说**：
- `overall=FAIL` 的**成因逐字相同**（那两个 TIMEOUT），且基线**同样红** ⇒ 失败是环境/fixture 缺口，与 kit 封装无关。
- 但基线不是「一模一样」：基线红**三个** hover 探针，当前红**两个** —— `hover-jump-btn` 那一条是被本轮修掉的（小说侧栏「快速跳转」被空态 `Empty()+return` 吞掉）。所以「基线同样红」只能支撑**成因没变**这一句，**不能**拿来支撑「本轮没有改善」。

证据文件：`build/rv15-baseline-31b56a0/shots-manifest.txt`、`build/rv15-baseline-31b56a0.log`。

**为什么值得单列**：这是同一句「已对照跑过基线」在两种写法下的差别 —— 一种只有结论，一种有可复核的产物。而**基线「同样红」很容易被读成「本轮没变化」**，实际上基线更红。

**删除 `kit::Modal` 后的复核**：`Modal` 全树零调用，且其头部绘制与 `ModalFrameRect` 逐行重复。删除后重跑取证，指标与删除前逐项一致（`overlay-clicks 7/7`、`duplicate-hits 0`、`shots 79/0`），门禁仍全 0 —— **这次删的是纯冗余 API，没有任何行为依赖它**。

### 取证结果（2026-09-30 第一轮）

| 指标 | 改动前基线 | 本轮之后 |
|---|---|---|
| `hover-probes` | 8/11 | **9/11** |
| `overlay-clicks` | 4/4 | **5/5**（新增项目中心那条） |
| `duplicate-hits`（新判据） | — | **0**（自检时改坏遮罩会变 6232，证明它能红） |
| `shortcuts` | 7/7 | 7/7 |
| `identical-theme-pairs` / `identical-driven-pairs` | 0 / 0 | 0 / 0 |
| `inverted-rects` | 0 | 0 |
| shots / failed | 79 / 0 | 79 / 0 |
| `overall` | FAIL | **FAIL**（见下） |

**`overall=FAIL` 的准确原因**：`book-snapshot=TIMEOUT` / `asset-snapshot=TIMEOUT`（`assets=0`），fixture 的 `novel.db` 没落地 ⇒ 资产侧栏走 `Empty()` 分支、`hover-tree-node` 与 `hover-asset-card` 两个探针的坐标上**没有控件**。已对照跑过基线（证据见下节），**同样红**，不是本轮引入。这是环境/fixture 缺口，与 kit 封装无关，未在本轮修。

---

## 取证基建：鼠标坐标注入（2026-09-30）

**症状**：r89 / r90 / r91 连续三轮 `hover-probes 0/11`、`overlay-clicks 0/4`、`unstable=11`，而 r88 同样这套代码是 11/11 与 4/4。

**归错了两次**：

- r89 我归因为「`CenterTextY` 里的一次性自检日志刷屏冲掉了驱动协议」。**错的** —— 删掉那条日志重跑，11/11 照旧红。
- 浮层探针同时报「位置注入没到位」，这条信号当时就在日志里，是我没去读。

**真因**（加自证读数后一次定位）：`Host::PumpFrames` 用 `io.MousePos = 覆盖值` 这个**字段赋值**来注入坐标，但 `ImGui::NewFrame()` 里的 `UpdateMouseData` 会用 `g.InputEventsMouse.MousePos[source]` **覆盖这个字段** —— 字段上的值活不过 `NewFrame`。而 win32 后端**一定**会往队列里塞一个事件：`ImGui_ImplWin32_UpdateMouseData`（`imgui_impl_win32.cpp:389-398`）在「窗口有焦点 + `bd->MouseTrackedArea == 0`」时调 `AddMousePosEvent` 把**真实光标**推进去。

实测帧内读数恒为 **`(3146, 50)`**（用户真实光标在第二块屏上），视口 `1600×960` 正常，11 个探针坐标各不相同却全都 `hit=0`。r88 能过只是因为当时取证窗口**恰好没拿到焦点**。

**修法**：排在后端 `NewFrame` **之后**调 `io.AddMousePosEvent(覆盖值)` —— 走事件队列而不是字段，同一 source 上后写的覆盖先写的。这与下面键盘 / 鼠标按键覆盖一直用的方式一致；唯独鼠标位置当初写成了字段赋值，于是**只有窗口恰好没有焦点时**才生效。

**判据自证（这条更重要）**：悬停探针现在每条都打 `landed` / `saw` / 视口三个读数。`hit = 0` 至少对应三种完全不同的原因 —— 注入没落地、视口塌成 0×0、坐标落在热区外 —— 只看命中数必然改错地方（我前面两次就都是这么错的）。浮层探针的报错也改成打出**实际看到**的坐标，而不是只说「没到位」。

修完 r92：`overall=PASS`，80/80 saved、`hover-probes 11/11`、`overlay-clicks 4/4`、`identical-driven-pairs 0`。

---

## 第三方库注册（2026-09-30）

| 库 | 目标 | 接法 | 备注 |
|---|---|---|---|
| `third/ImAnim` | `shine_imanim` | **只编 `im_anim.cpp`**，链 `shine_imgui_third` | 它的 `CMakeLists.txt` 是 CI 脚本，依赖 `examples/extern/ImPlatform`（会 fetch SDL3/Vulkan/Metal）并建 9 个 demo exe —— 照搬进来配置阶段就要外网 |
| `third/VisualNodeSystem` | `shine_vns` | 6 个根 `.cpp` + 3 个 jsoncpp `.cpp`，链 `shine_imgui_third` | 它的 `CMakeLists.txt` 要两个 CACHE 变量 + SHARED/STATIC 分支，是给独立仓库用的。源码侧有**一处本地补丁**（变量遮蔽，GCC 判 redeclaration，MSVC 接受），见 `third/VisualNodeSystem/LOCAL_PATCHES.md` |

**选 ImAnim 的理由**：本工程控件全自绘（只往 `ImDrawList` 塞 draw call，不产生 ImGui item），基于 item 生命周期的动画引擎完全用不上。ImAnim 的 C 接口 `iam_tween_float(id, channel, target, dur, ease, policy, dt)` 直接返回当前值、不依赖任何 item，正好对得上。

### `PinAnimation` 语义变更（接过渡的前置条件）

`kit/Draw.h` 原来断言「本工程所有 hover 样式都是直接状态切换，没有基于时间的插值（这是钉时钟能用的前提）」。**接了 ImAnim 之后这句不成立了**，改成：

- 钉住时，**连续动画**（`Now()` 驱动：脉冲 / 旋转 / 呼吸）冻结在给定时刻；
- 钉住时，**过渡**（`Anim.h` 的 `TransitionTo` / `TransitionColorTo`）**直接落终值**。

为什么不能冻过渡：冻在半路 ⇒ 52 张静息态截图拍到的是随机中间色，每轮 md5 都变；落终值 ⇒ 截图确定，且 hover 探针照样测得到（终态色 ≠ 静息态色）。`PinAnimation` 另外调一次 `iam_pool_clear()`，让在飞的补间重建时以终值起步，避免解钉瞬间闪一下。
