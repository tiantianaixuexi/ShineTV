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

---

## 下一步

重构主干（51/51）已完成并通过验证。剩下的是**产品缺口**，不是重构缺口：

| 缺口 | 归因 | 说明 |
|---|---|---|
| 阶段执行体是桩 | `shine_core` | 没有「替前端跑一个 T 阶段」的服务接口。Runner 的**记账**（预算 / `work/*.json` / 账本）是真的，阶段**本体**不是。补它要动 `shine_core`，超出「重构期不改业务层」的边界。 |
| S1–S12 停止规则表 | `pipeline/StopPolicy` | 只判 5 组，且没有可枚举的规则表（同 `src/ui/pages/pipeline/StopReportView.h` 的旧结论）。页面当前显示 S1–S8，是**实有的 5 组 + 3 条 LLM 判据**，不补齐就不该声称是 S1–S12。 |
| 章节×阶段双轴甘特 | `pipeline::Ledger` | `LedgerEntry` 只有 `{stage, input_hash, output_path, degradation}`，`Flush` 写出的 `_manifest.json` 里也没有章节字段 —— **章节维度在数据层根本不存在**，不是 UI 少画了一根轴。补它要改 `Ledger`。 |
| 侧栏树 / 检查器的**卷层级** | `shine_core` | `NovelGraph` 只有 `UpsertVolume`，**没有 `ListVolumes`**（`src/novel/NovelGraph.h` 全文确认）。卷标题与卷序取不到，设计稿的「书 / 卷 / 章」三层因此只能画两层。 |
| 侧栏树 / 检查器主体 | 页面层 | **已实现**：`WorkspacePages.h` 开 `BookSide()` 只读视图（`BookSideView` 纯数据拷贝，`ApplyBook` 落地时重建），侧栏画「章 → 镜」树、点选换章换镜，检查器「属性」给九行真实字段。镜码 `ShotCode()` 三处共用一份实现。资产工作区的 kind 筛选树、节点「快捷跳转」、检查器「关联」段（伏笔 / 场 / 镜）也已于 2026-09-30 接上真数据。**仍缺**：镜与镜之间的关联（哪一对连续性不过）—— `ContinuityIssue` 没有 shot id，要归因必须改 `src/novel/`。 |

| 小说页「设定」模式的实体卡**不可点** | 页面层 | 2026-09-30 修好了这张卡**整张不画**的反向矩形（见验证记录），但它仍然只是只读展示：实体选择与属性在**资产工作区**那张网格上，两边没打通。设计稿里 `Assets.jsx` 的「设定集」是实体**详情页**里的一段，不是卡片网格，所以没有可直接对齐的交互定义 —— 要做点选-联动得先定「点了之后右栏显示实体属性还是仍显示章节属性」，属产品决策，本轮不猜。 |

另有一条**已接受降级**：`Derived::gateBg` / `checkRowBg` 当年按纯 alpha 算，
与设计稿的实色差一个底。字段按「只加不减」冻结，要 1:1 请用新增的
`stageDoneBg` / `stageFailBg` / `gateFailBg`。
