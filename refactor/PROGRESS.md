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

> 最后更新：**2026-09-30**（HEAD `091ea9c`）。步骤定义见 [`phases.md`](phases.md)，每完成一步就勾上并补验证记录。
> **本文只留四样东西**：当前状态 · 下一步 · 没做的 · 容易再踩的坑。
> 逐轮的长篇过程已进 `git log`（`git log -p -- refactor/PROGRESS.md` 可查历史全文）；
> 结论背后的机制大部分写在**源码注释**里，下文每条都给了去处 —— 改代码前先读那处注释，别只读本文。

> ⚠️ **与 phases.md 的两处偏离**（用户明确要求，phases.md 未同步）：
> 1. 渲染后端用 **OpenGL**（`imgui_impl_opengl3` + `opengl32`），不是 D3D11。
>    `shine::gpu::AttachDevice` 注入的是自己解析的 GL 函数表（`host/Host.cpp`）。
> 2. P6.1 的抓图走 `glReadPixels`，不是 D3D11 后备缓冲。

图例：`[x]` 已完成并通过验收 · `[ ]` 未做（全部列在「没做的」里）

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

**整体：51 / 51 步完成。** 重构主干已结束；剩下的是**产品缺口**，见「下一步」与「没做的」。

---

## 基线（重构开始前已实测）

| 项 | 值 |
|---|---|
| 基线 exe 体积 | **392,330,166 B**（Qt 前端） |
| 工具链 | MSYS2 MinGW64（`C:/msys64/mingw64`），**GCC 16.2.0**（`mingw-w64-x86_64-gcc 16.2.0-4`，2026-09-30 从 16.1.0-2 升上来），`CMAKE_CXX_STANDARD 26` |
| ImGui 版本 | 1.93.0 WIP（`IMGUI_VERSION_NUM 19297`） |
| 已删的 Qt 树 | `src/ui/{app,kit,layout,pages,qml,verify}` 共 **198 个文件** |

---

## 关键指标

| 指标 | 基线 | 目标 | 实测 |
|---|---|---|---|
| exe 体积（未裁剪） | 392,330,166 B | — | **290,667,311 B**（2026-09-30，GCC 16.2 全量重建实测；P7.7 当时 252,530,560 B，16.1 时代复测 290,689,357 B） |
| **exe 体积（strip 后）** | — | **< 30 MB** | **13,406,208 B = 12.8 MB** ✅（2026-09-30 对 16.2 二进制复测；P7.7 当时 12,435,968 B） |
| `src/` 中 Qt 头引用 | 165 个 UI 文件 | **0** | **0**（门禁零豁免仍 PASS） |
| QML 文件 | 52 | **0** | **0** |
| `src/ui/` 下的前端树 | 6 棵 | **1 棵** | **1 棵（`imgui/`）** |
| 构建是否需要 Qt | 是 | **否** | **否**（全新 `build-p7` 目录零 Qt 配过 + 构建 + 取证通过） |
| `check-layers` | PASS | PASS | **PASS (0 violations)** |
| 字体图集 | — | < 64 MB（**自定红线**，不是显存） | **7.8 MB**（`RasterizerDensity=2.0` 后；统计读 `GetSizeInBytes()`） |
| `src/ui/imgui` 文件数 | 45 | — | **132**（72 `.cpp` / 55 `.h`），最大 `.cpp` **818 行** |
| 取证图组数 | 17 | 35+ | **79–80**（见「判据清单」里的最新一轮读数） |

---

## 风险状态

| # | 风险 | 状态 | 备注 |
|---|---|---|---|
| R1 | 中文字体 / TTC / 图集 | **已解决** | 1.92 按需加载 + `RasterizerDensity=2.0`；图集 7.8 MB，无豆腐块 |
| R2 | 门禁自拦 | **已解决** | PASS (0 violations) |
| R3 | `FlowCanvas` | **已解决** | 见 P3.5 三处踩坑 |
| R4 | 毛玻璃无等价 | **已接受降级** | 不追求像素一致 |
| R5 | box-shadow 无等价 | **已解决** | 2026-09-30 已按 CSS 逐条接投影档（`ShadowTier` + `kShadowSpecs`） |
| R6 | `color-mix` 无合成 | **已解决** | `Derived` 预混 + `MixSrgb` 实色混合 |
| R7 | 取证代码全废 | **已解决** | 79–80 张 / 判据齐 |
| R8 | 纹理异步未就绪 | **未处理** | 界面里所有图像仍是程序化占位 `kit::Art()`，**真实纹理解码/上传路径尚未接入**；接入时必须走 worker + `async::PostToUi`（P5.3 早已完成，旧备注「P5.3 未完成」作废） |
| R9 | cover 缩放锚点 | **已解决** | 满幅画布页改用可视高度 |
| R10 | 启动初始化顺序 | **已解决** | `AppEntry` 分派 + APPDATA 沙箱 |
| R11 | ImGui 叠放 item 抢 hover | **已解决** | 同一矩形只能 `HitTest` 一次；遮罩只画不注册 |
| R12 | `Runner` 无「跑一个 T 阶段」的服务接口 | **未解决** | 阶段**记账**真实，阶段**本体**是桩；归因见「下一步」第一行 |
| R13 | `StopPolicy` 只判 5 组 | **界面侧已解决** | UI 改画 `shine::novelcore` 的 **S1–S12 真实规则表**（`StopCodeCondition` / `StopCodeHint`），不再借用 `pipeline/StopPolicy` 的编号；`StopPolicy` 那 5 组仍在但界面不用它。唯一限制：数字阈值只能拿到**文本**、逐条状态本页算不出 ⇒ 卡上**只列规则不标状态** |
| R14 | `Ledger` 无章节维度 | **一半已接** | `T1–T17` 链确实没章节轴（`LedgerEntry` 只有 4 字段，做不了）；`V1–V7` 走 `stage_artifacts` 的**章节 × 阶段矩阵已渲染** |
| R15 | 校验报告无查询源 | **已解决** | 真值是 `work/ch<NNN>/v08_continuity.json`，列表按章列三态、点行走模态看逐项 C1–C12 |
| R16 | 底栏「校验报告」曾长期是诚实空态 | **已解决** | 同 R15 |

---

## P0 底座与门禁

- [x] **P0.1** `third/imgui`（`IMGUI_VERSION_NUM == 19297`）
- [x] **P0.2** CMake 前端开关（`SHINE_UI_IMGUI` / `SHINE_UI_QT`）+ `shine_imgui` 目标
  - `shine_imgui` 是 `GLOB_RECURSE CONFIGURE_DEPENDS "src/ui/imgui/*.cpp"`，**新增 .cpp 不用改 CMake**
  - 默认 `SHINE_UI_IMGUI=ON` / `SHINE_UI_QT=OFF`
- [x] **P0.3** `tools/check-layers.ps1` 三条规则反向 —— PASS (0 violations)
- [x] **P0.4** 最小宿主：Win32 窗口 + **OpenGL** + ImGui 一帧（37 张 PNG 全部 saved）

---

## P1 运行时骨架

- [x] **P1.1** `Host.{h,cpp}`：窗口 / GL 上下文 / 深度缓冲 / `WM_SIZE`
  - MSAA 关掉，否则 `glReadPixels` 抓的图边缘发虚
- [x] **P1.2** `shine::gpu::AttachDevice(api)` ⚠️ 关键动作
  - 注入自己 `GetProcAddress` 解析的 GL 函数表；缺函数时 `log::Error`，不静默
- [x] **P1.3** 帧循环 + `async::DrainUiQueue()` / `gallery::Tick()`
- [x] **P1.4** CLI 分派 + `ConfigureAppDataSandbox()` + `SHINE_*` 自检
  - 取证模式下 APPDATA 重定向到 `<OutDir>/_appdata`
- [x] **P1.5** 布局持久化（`layout.dat`，magic `shinetv-layout-1`）
- [x] **P1.6** DPI 感知（`ImGui_ImplWin32_EnableDpiAwareness()` 在建窗**前**调用）
  - framebuffer 固定 1:1，**不**按 DPI 放大

---

## P2 主题与字体

> ✅ **本阶段最硬的判据已达成**：5 套主题名与全部业务中文逐字可读。
> 字体相关的硬事实另见「跨轮硬事实 · 字体与图集」。

- [x] **P2.1** `Tokens.h` 搬运（5 个 JSON 原地复用，格式未改）
- [x] **P2.2** `ColorToken` 31 项映射，一次写全
- [x] **P2.3** 派生色预混（`Derived`，12 组）
- [x] **P2.4** `ImGuiStyle` 生成
- [x] **P2.5** ⚠️ 字体图集 + CJK
  - 根目录从 **exe 路径回溯**，不是 CWD（程序常从 `build/` 启动）
  - 等宽字体（`Cascadia/Consolas` 只有拉丁）挂雅黑回落链（`MergeMode`），中文副行不再变 `?`
  - `kUiSizes` 补齐 14.5 / 17 / 26 三档，`kMonoSizes` 补 11 / 12
  - `Shell::SetTheme` 用 `atlasIsSerif_` 守卫：**只有字体族真变了才重建图集**
    （只判「切到水墨」会漏切离水墨的回退；不记状态会连续重烘 5 次）
  - 依据见 `kit/Fonts.cpp` 顶部注释
- [x] **P2.6** 主题切换 + 持久化 + `SHINE_THEME_SET` 自检
- [x] **P2.7** 减少动效开关（从顶栏设置键挪进**主题菜单**，见 P4.2）

---

## P3 组件套件

- [x] **P3.1** 绘制基建：圆角矩形 / 渐变 / 文本截断 / 阴影 / `AutoGridCols`
- [x] **P3.2** 基础控件：Button(4×3) / IconBtn / Input / TextArea / Select / Switch / Checkbox / Field
- [x] **P3.3** 数据展示：Tag(7 色调×2) / StatusDot / Kbd / KV / Progress
- [x] **P3.4** 容器与浮层：Card / Segmented / Tabs / Spinner / Tooltip / Divider / DataTable / Tree / Menu /
  `Overlays.{h,cpp}`（Scrim / Modal / Drawer / Toast）
  - `Card` 的 `hoverable` / `glow` 早先是死代码，已改成真 `HitTest` + hover 边色 + 上浮 2px
  - `IconButton` 的 `tip` 原走 `ImGui::SetTooltip`（错背景 / 错延迟 / 无法按设计稿右侧锚定），改走新 Tooltip
  - `Empty` 的正文框原写死 320px 宽且忽略传入 bounds，在 240px 侧栏里横穿面板
  - **刻意不做** Skeleton / Avatar / Badge：全 `webui/src` 里没有这三个原语，凭空造尺寸等于凭空造设计
- [x] **P3.5** 画布与图像 ⚠️：`FlowCanvas` / `Art` / `ArtInk`
  - `FlowCanvas`（`Views.{h,cpp}`）：150×76 节点、5 态、9px 端口、24 段折线逼近三次贝塞尔、
    滚轮光标锚点缩放（0.35–2.0）、拖节点 / 拖背景平移、适应视图、右上玻璃工具条
  - 三处踩坑：fitInset 重复扣宽、居中未扣内边距、**满幅画布页误套外壳 2400px 撑高**
    （实测 `canvas=644x2400 → y=1112`，节点全被顶出视口）
- [x] **P3.6** 图标字体（`Icon*.{h,cpp}`，与 `webui/src/components/Icon.jsx` 46 个一致）
- [x] **P3.7** 组件画廊页

---

## P4 应用外壳

- [x] **P4.1** 窗口框架布局（46/56/240/280/190/26/34，档位收在 `Shell_Layout.h`）
- [x] **P4.2** 顶栏
  - 运行 / 停止**二选一**（`webui/src/shell/Shell.jsx:44-48`）
  - 「主题」是**幽灵按钮 + 文字**，弹**主题菜单**（5 套 + 色块 + 勾 + 减少动效）
  - 品牌标 / 项目胶囊 / 搜索框**都可点**（分别回项目中心 / 开命令面板）
  - Comfy 状态项读 `ComfySession` 真值，不是恒定 `Idle` 裸点
- [x] **P4.3** 导航栏 6 工作区 + 3 面板开关 + 画廊 + accent 选中条
  - 面板开关**无常驻高亮**：设计稿的 `.rail-btn.active` 在整个 CSS 里**没有规则**（死类）
  - ⚠️ 修掉一个致命交互 bug：曾先 `Hovered(idA)` 再 `Clicked(idB)`，两个 InvisibleButton
    落同一矩形，ImGui 只让先注册者拿 `HoveredId` → **整个导航栏点不动**
- [x] **P4.4** 侧栏 + 折叠把手（写死的假章节名已换成诚实空态）
- [x] **P4.5** 检查器（3 段可折叠；写死的假属性已换成诚实空态）
- [x] **P4.6** 底栏 4 标签
  - 任务队列 ← `comfy::QueueModel` 真实快照（早先是写死的 `Progress(42%, run=true)`）
  - 日志 ← 运行期真实事件（早先是四条写死的 "T3 生成图 · …"）
  - 产物 ← 扫 `<project>/output`，**IO 在 worker**，按路径 + 3s 节流重扫
  - 校验报告 ← `v08_continuity.json`（原为诚实空态，现已接真数据 + 报告模态）
- [x] **P4.7** 状态栏（Comfy / LLM / 队列读真值；`T{n}/17` 三态，未接入时如实显示「阶段执行体未接入」）
- [x] **P4.8** 面包屑（第三段改成 `Shell::DerivedViewLabel()` 按工作区现算）
- [x] **P4.9** 命令面板（`Ctrl+K`；另注册 `Ctrl+N` / `Ctrl+O` / `Ctrl+T` / `Ctrl+Enter`）
- [x] **P4.10** 浮层全套：主题菜单 / 设置模态 / 校验报告模态 / `Esc` 逐层关闭
  - ⚠️ 全屏 scrim **不能**用 `kit::HitTest`：它先拿 `HoveredId`，把模态内部按钮变成死键。
    改用只画不注册的 `kit::ScrimPaint` + 手算 `Rect::contains`
- [x] **P4.11** 项目中心页（`DrawProjectHub` 早先**定义了但零调用**，已接成整屏替身 `hubOpen_`）

---

## P5 六个工作区

- [x] **P5.1** 总控（pipeline）— 接 `Runner::{Usage, LedgerLog, CurrentStage, RunNext}` + `Budget` +
  `Ledger::Entries` + `StopPolicy::Evaluate` + `Checkpoint::Load` + `AppSettings`
  - 删掉全部伪造 KPI / 哈希 / 停止条件
  - ⚠️ `BindOverviewProject` 曾落在**匿名 namespace** 里 → 外部链接不到
- [x] **P5.2** 小说（novel）— 8 模式 · `NovelGraph::ListChapters / ListEntities` 真实返回值
- [x] **P5.3** 资产（assets）— `NovelVisual::FindAssetByEntity / ListArtifacts` + kind 筛选树
- [x] **P5.4** 分镜（storyboard）— `ListShotsByChapter / ListStageArtifacts` + `RunContinuityChecks`
  - `unverified` 显式报成「无不一致，但有数据不足项」，不报成「通过」
- [x] **P5.5** 出图（imageflow）— 含 `FlowCanvas` + 4 个面板页签
- [x] **P5.6** 出片（videoflow）— 含 `FlowCanvas`
  - 三页共用一份工程快照（`BindNovelProject` 一处即可），IO 在 worker，generation 丢弃过期结果

---

## P6 验收取证

- [x] **P6.1** 后备缓冲 → PNG 抓图 + manifest（`shots-manifest.txt`，对齐 `scripts/run_reviews.ps1`）
  - `glReadPixels` 原点在左下，PNG 从左上起 → **逐行倒着写**
- [x] **P6.2** 保留全部 `SHINE_*` 环境变量名与分派点
- [x] **P6.3** `SHINE_IMGUI_REVIEW`：6 工作区 × 5 主题 + 全工作区基线
  - 覆盖面 17 → 37 张（后续轮次加到 79–80）
  - manifest 含 `overall=PASS|FAIL` 行；退出码**只读** `ReviewResult::pass`（见「跨轮硬事实」）
  - ⚠️ 补了**判据二**：同一工作区在 5 套主题下的像素必须两两不同。旧门禁只判「PNG 写出来了」是**假绿**
    —— 实测把 exe 放到没有 `themes/` 的目录里跑，37 张只有 13 张唯一、五套主题逐字节相同，manifest 依旧 PASS

---

## P7 收尾

- [x] **P7.1** 删 `src/ui/{app,kit,layout,pages,qml,verify}` —— **198 个文件**
  - 唯一还被用到的 `AppEnvironment.{h,cpp}` 搬到 `src/ui/imgui/host/`
- [x] **P7.2** 删 `shine_kit` / `shine_qml` 目标与 `SHINE_UI_QT`
- [x] **P7.3** 移除 `find_package(Qt6)` / `AUTOMOC` / `AUTORCC`
  - `check-layers.ps1` 的「Qt 前端豁免目录」一并删掉 —— 门禁现在对全 `src/` 零豁免
- [x] **P7.4** 门禁定型：`check-layers` / `check-theme` / `check-colors` 的主题路径改指 `src/ui/imgui/theme/Themes`
- [x] **P7.5** `package-qt.ps1` → `scripts/package-imgui.ps1`
  - 旧脚本传 `-DSHINE_QT_UI=ON` —— 这个开关**在 CMakeLists 里从来不存在**，
    所以它一直在静默配出一个 ImGui 版，再叫 `windeployqt` 去部署 Qt 运行库
- [x] **P7.6** 更新 `AGENTS.md`：删掉「双前端共存」描述，补上 `src/ui/imgui/**` 的 UTF-8 编码事实
- [x] **P7.7** 实测产物体积并填表（见「关键指标」，2026-09-30 已复测）
  - 差距全在调试信息：`.debug_info` 单节曾测得 ≈146 MB

---

## 判据清单

### 取证指标（`SHINE_IMGUI_REVIEW=1`，入口 `out/_run_review.ps1`）

| 指标 | 读的是什么 | 抓什么 |
|---|---|---|
| `# shots` / `failed` | PNG 写出数 / 失败数 | 编码、落盘失败 |
| `identical-theme-pairs` | 同工作区 5 主题两两像素比对 | `themes/` 没落地 ⇒ 五套主题逐字节相同 |
| `identical-driven-pairs` | 受控图两两比对 | 「证明性」截图没拍到它承诺的状态 |
| `inverted-rects` | 运行时反向矩形计数 | `kit::Rect` 四参当 (x,y,w,h) ⇒ **整块不画也不可点** |
| `duplicate-hits` | 同帧重叠热区计数 | 画得出但**点不动**（先注册者独占 `HoveredId`） |
| `hover-probes` | 悬停三拍（静息 A / 静息 B / 目标 C）+ 命中自检 | hover 链路断；分 `unstable` / `broken` |
| `shortcuts` | 7 个快捷键逐个注入 | 空动作；按 `Layout`/`Palette`/`Theme`/`Hub` 四类分别读产品自己的状态 |
| `overlay-clicks` | 浮层 × / 点遮罩 / 不穿透 / 同帧自毁守卫 | 浮层 item 提交在根窗口而 hover 恒在 child |
| `scroll-failed` | 粘性滚动请求有没有被核销 | 区域压根不能滚 |
| `*-snapshot` | 收敛 / TIMEOUT | fixture 没落地 ⇒ 页面走空态，分支从未被执行 |
| `overall` + 退出码 | 判据的 `pass` **原样**传出 | — |

**最新一轮（`build/rv23-gcc162/`，GCC 16.2 全量重建后）**：
80/80 saved · failed 0 · `scroll-failed 0` · `identical-theme-pairs 0` · `identical-driven-pairs 0` ·
`inverted-rects 0` · `hover-probes 11/11` · `shortcuts 7/7` · `overlay-clicks 7/7` ·
**四个 snapshot 全部 converged**（`chapters=3 shots=5` / `assets=3` / `artifact rows=6`）·
**`duplicate-hits: 815`** → **`overall=FAIL`**。

⚠️ **这个红项怎么读（2026-09-30 实测盘上每一份 manifest 得出）**：`duplicate-hits` 在此前**每一轮**
都是 0，但那些轮次（`rv14` / `rv16`~`rv22` / `rv15-baseline`）的 `book-snapshot` 与 `asset-snapshot`
**全是 TIMEOUT** ⇒ 侧栏树 / 检查器 / 资产树全退化成诚实空态，**这个指标一直在测空气**。
rv23 是第一轮**页面里有真实数据**的取证，所以 815 是**首次可观测值**，不是「从 0 涨到 815」的回归。
它**不是** GCC 16.2 引入的（同一份源码，编译器不改命中注册）。究竟是「嵌套热区被设计性命中」
（整卡 `HitTest` + 卡内 footer 按钮 / 滚动区自身热区）还是真缺陷，**尚未判定**，见「没做的」。

**基线对照**（`build/rv15-baseline-31b56a0/`，提交 `31b56a0`，manifest 在盘上可复核）：
`overall=FAIL`，成因同样是 `book-snapshot` / `asset-snapshot=TIMEOUT`（那个年代 fixture 没落地），
hover 8/11 / overlay 4/4。证据目录 `build/rv14` ~ `build/rv23-gcc162` **全部保留在盘上，不要当 build 垃圾清掉**。
⚠️ 反过来说：验证记录里 r74 / r88 / r92 那几轮「四个 snapshot converged · `overall=PASS`」的
manifest **已不在盘上**，所以它们现在只能当二手结论引用；要看数字就得重跑。

### 硬门禁（失败退出码 1）

| 脚本 | 抓什么 | 自检 |
|---|---|---|
| `tools/check-layers.ps1` | 分层：`shine_core` 不得含 Qt 头 / ImGui 头 | `-SelfTest`（6/6） |
| `tools/check-theme.ps1` | 主题 token 与 JSON 一致 | 无 |
| `tools/check-colors.ps1` | 硬编码颜色字面量（豁免必须写 `// theme-ok <理由>`） | `-SelfTest`（11/11） |
| `tools/check-i18n.ps1` | 文案 i18n 覆盖 | 无 |

### 人工复核扫描器（只打印疑似，不改退出码）

| 脚本 | 抓什么 | 自检 |
|---|---|---|
| `tools/find-silent-truncation.ps1` | 列表按容器高度静默截断（含 `> body.max.y - 20.0f` 变体） | `-SelfTest`（6/6） |
| `tools/find-draw-arg-order.ps1` | 绘制原语的**颜色 / 圆角**参数错位 | `-SelfTest`（10/10） |
| `tools/find-rect-wh-misuse.ps1` | `kit::Rect` 四参当成 (x,y,w,h) | **无 `-SelfTest`**（带上会被静默忽略后照常报 `0`，与真通过字面相同）；用 `-Root <样本目录>` |

**一条纪律**：改完扫描器**先跑它自己的自检**。一个「改完报 0」的扫描器和一个坏掉的没区别，
而假绿比没有门禁更坏。

---

## 验证记录

> 每轮记「日期 / 做了什么 / 观测到什么 / 是否通过」。没跑的写「未执行」，**不要把推断写成通过**。
> 逐轮长篇过程在 `git log -p -- refactor/PROGRESS.md`；下表只留结论。

### 分轮结论

| 轮次 | 结论 | 关键读数 |
|---|---|---|
| P0–P7 收尾 | ✅ | 37 张 · `overall=PASS` · 退出码 0 |
| 报告页 / 侧栏树 / 资产树三轮 | ✅ | 45 → 52 → 64 张，md5 全唯一 |
| 悬停 + 动作判据 | ✅ | 77 张 · `hover-probes 11/11` · `shortcuts 7/7` |
| 章节 × V 矩阵 + 滚动修复 | ✅ | 79 张 · `scroll-failed 0` |
| 8 处截断 + 滚动条 + 镜码（r74） | ✅ | 80 张 · 四个 snapshot converged · `overall=PASS` |
| 投影落地（r88） | ✅ | 80 张 · `overlay-clicks 4/4` · `overall=PASS`（不靠 diff、靠读像素剖面证明） |
| 鼠标注入修好后（r92） | ✅ | 80 张 · `hover-probes 11/11` · `overall=PASS` |
| kit 收编（rv13/14） | ⚠️ | `overall=FAIL`，成因 = fixture 两个 snapshot TIMEOUT；已与基线 `31b56a0` 对照（同样红） |
| 大文件拆分（rv20–22） | ⚠️ | 同上；manifest 的 `# shots:` 指标行与拆分前**逐字节相同** ⇒ 行为等值 |

⚠️ **一次未复现的失败，如实记**：r85 在第 47 张图（`overlay-palette`）之后窗口塌成 0×0
（`capture: bad frame 0x0`），剩 36 张全 FAILED，且崩溃前有几帧报出 x 翻转的反向矩形。
r86/r87/r88 连续三轮同参数**均未复现**，无残留进程。**原因未定论，不写成「已修复」。**

### 已修的真缺陷（按类，供「为什么有这条门禁」用）

**假数据 / 死代码**（静息截图上看着最正常的一类）
- 底栏「任务队列」写死 `Progress(42%, run=true)`、日志四条写死事件 → 接 `comfy::QueueModel` 真实快照
- 侧栏假章节名、检查器假属性（`{代码:S012, 动作:转身}`）、出片「视频任务」4 条算术级数进度 → 诚实空态 / 真值源
- 校验报告页写死「诚实空态」而真值就在 `v08_continuity.json` → 接上；`DrawReportModal` 的驱动字段
  `reportDetail_` 全代码只有 `= -1` 初始化 ⇒ 模态一次都没开过
- toast 是空函数（`DrawOverlays` 是 `(void)draw;`）→ 补 `.toast` 原语（fixed right 16 / bottom 40 / 左 3px 色调条 / 0.4s 淡出）
- 面包屑第三段恒为「总览」（`SetWorkspace` 无条件写死）→ `DerivedViewLabel()` 按工作区现算
- `loading` 标志在派发瞬间就翻、视图没跟上 ⇒ 调用方「等 !loading」一帧都不等（上一轮据此把上一章画面当成新章拍了）
- `runStageIndex_` / `runPercent_` 全文件只有「写 0」与「读出来显示」⇒ 状态栏的 0% 进度条**结构上不可能前进**
- `pipeline::Runner` 注入空 lambda（`return true`）⇒ 17 阶段完成 / ¥0.17 / 100% 全是**真数据通路算出的假结果**；
  `Runner::RunNext` 先扣预算（`:40`）后调执行体（`:46`），所以「返回 false」也留真·假账 ⇒ UI 侧改成压根不调 `RunNext`
- 图评审页 5 行假复选框：返回值丢弃 + `on` 传字面量 `i < 3` + 五行同一个 label
- 组件画廊的 `Switch` / `Checkbox` / `Segmented` / `Tabs` 返回值直接丢弃（kit 的 `bool on` 是**按值**传）⇒ 画廊里按不动的控件
- 「3 秒节流」是死计时器（`*RefreshAt_` 只被赋 `0.0f`）⇒ 除了换工程没有第二个触发点
- 「批量出图」是丢了返回值的 primary 主按钮 → 点了弹 toast 说明为什么没接
- `Art()` 程序化占位在三处漏标注 → 抽出全仓共用常量 + 可见标注（`Shell.cpp` 早先已标，其余三处漏了）
- 两个浮动面板头的「运行中 / 出片中」是字面量 → 只在 `ComfyHasRunning()` 为真时画
- `IsMouseHoveringRect` 在本工程 **0xC0000005**，共 7 处在用 → 全部改 `Rect::contains(io.MousePos)` 手算

**命中与叠放**（画得出、点不动）
- 同一矩形注册两次 InvisibleButton（`Hovered(idA)` + `Clicked(idB)`）⇒ ImGui 先注册者独占 `HoveredId`，**整个导航栏点不动**
- 四个浮层全被工作区盖住：都用 `GetWindowDrawList()`，而 child 在父窗口那条 list **之后**渲染 → 全改 `GetForegroundDrawList()`
- 浮层的 item 提交在**根窗口**，`FindHoveredWindowEx` 从 `g.Windows` 末尾往前扫 ⇒ `g.HoveredWindow` 恒是工作区 child，
  而 `ItemHoverable` 第一句就是 `if (g.HoveredWindow != window) return false;` ⇒ **根窗口里所有按钮恒死键**
  （80 张静息绿图零覆盖：它们不点击；hover 探针也抓不到：它们只打 chrome 与 child 内部控件）
- 模态遮罩注册成全屏热区（`kit::Scrim`）⇒ 报告模态「外壳先画」时遮罩先拿 `HoveredId`、**全部按钮按不动**；
  项目中心「对话框最后画」时**不会坏**。危害方向取决于绘制顺序，两种顺序结论相反
- 项目中心卡片先注册整卡热区再注册页脚 4 个按钮 ⇒ 按钮死键，点「…」会直接打开项目（等价于 webui 的 `stopPropagation`）
- 检查器三段折叠箭头是纯装饰（每帧新建的 `const` 局部数组，没人写回展开态）⇒「关联」段永远打不开
- 浮层开着时外壳 chrome 照常提交 item ⇒ 吃掉「点遮罩关闭」那次点击（症状：工作区被切走、浮层还开着）
- 「点面板外关闭」把刚打开的对话框当场关掉（同一帧 `IsMouseClicked()` 仍为真、鼠标仍在工具条上）⇒ `HubState::dismissArmed`
- 设置模态与主题菜单可以同开（互斥只写在主题那一侧）→ 两处入口都走 `Shell::ToggleSettingsModal`
- `SettingsCloseRect()` 是一份独立的 `560×452` 复算 ⇒ 改高度就分叉（× 停在老位置，判据只会报「没关掉」）

**滚动、裁剪、视口**（自绘控件没有 DOM 兜底）
- 自绘控件只往 `ImDrawList` 加 draw call，**全程没给 ImGui 提交过任何 item** ⇒ 三个 `ScrollRegion` 全部从来不能滚
- `ScrollRegion` 不调 `setContentHeight()` = **滚不动**（不是「滚不顺」）；上报值恰好等于视口高也等于滚不动
- 嵌套 child 的裁剪矩形只写进**它自己那条** list 的 `CmdBuffer` ⇒ 画到外层 list 的 draw call 完全不受裁剪
  （症状不是「卡片消失」而是**盖住上层**：总控页右栏滚上去后「停止条件」浮到 y=93 压住 KPI 与页头按钮）
- 页面把外壳给的 2400px **布局区**当视口 ⇒ 右栏高 1978 而内容只有 654；一张 **1750px 高只有 6 行**的账本卡
- `SetScrollHereY` 在 child 构造期（BeginChild 之后、内容之前）拿不到 item（读到的是上一帧残留）⇒ 实测**滚不动**，
  改用 `SetScrollY(自报内容高 − 可视高)`
- 10 处列表按容器高度 `break`（含 `> body.max.y - 20.0f` 这种「给页脚留位」的变体）⇒ 50 个任务只画前 6 个、无任何提示
- 滚动区带 `ImGuiWindowFlags_NoScrollbar` ⇒ 内容能滚了之后用户看着**与不能滚时完全一样**
- 页面要自建视口必须知道「真正能看见多少」⇒ `pages::WorkspaceViewportHeight()`（外壳每帧写入）/ `SetPageContentHeight()`（页面自报）
- 命令面板 16 条目 + 3 组标题 = 546px，输入框下只有 358px 且无裁剪 ⇒ 末尾几行画到面板外压在工作区上

**布局与度量**
- `kit::Rect` 四参是 **(minX, minY, maxX, maxY)**；当 (x,y,w,h) 用 ⇒ `max < min` ⇒ 整块不画也不可点，编译不报错。
  两处（资产总览网格卡、底栏页签下划线画到屏幕顶栏）+ 一处小说页「设定」网格整张不画
- `AddRectFilled(p_min, p_max, col, rounding)` —— **颜色在圆角前面**。写反则 col 收到半径变成全透明黑、
  rounding 位收到颜色被截成巨大值、半径为 0 那次 col 恰为 0 被 `if (col == 0) return` 整块丢掉。
  编译过、不崩、同一帧别的图元照常出图（见 `kit/Draw.cpp` 的 ⚠️⚠️ 注释与 `find-draw-arg-order.ps1`）
- 整个 ImGui 前端**一个投影都没画**：28 个 `DrawShadowed` 全是平面矩形，`Tokens.h` 的 shadow 常量零引用
  → 引入 `ShadowTier` + `kShadowSpecs`，按 CSS 逐条接 20 处（常驻 9 + hover 7 + accent 3）
- 投影两层 alpha 被乘两次（JSON 里存的就是 CSS 那层 alpha）**且**每层画成互不重叠的窄圈（累计只剩 31%）
  ⇒ 第 i 环要铺满 `[本体边, g_i]` 且只带**增量** alpha；只修前者得到的仍然是错的
- 资产卡 hover 边框用 `accent-glow`，设计稿写的是 `line-strong`（当初为了让 hover「看得出来」自己挑的颜色）
- 检查器段头「偏上 6.2px」是**我自己算错的数**：按正确口径重算是 Δ = −0.75px（在接受带内），
  真毛病是**整段高度差 15.2px**（24 vs 39.2）—— 是布局短，不是字没居中
- 居中偏差最大两处：`kit::Chip()` 低 5.25~6.0px（kit 自身，且被资产侧栏 chip 实际调用）、队列行文字高 5.75~6.0px
  → 统一走 `kit::CenterTextY`（度量口径见「跨轮硬事实」）

**重构（零行为变化，等价性有判据）**
- `WorkspaceB.cpp` 3522 行 → 11 个模块，切在**线程边界**上：`BookQuery.cpp`（worker 开库查询）/
  `BookData.cpp`（UI 侧快照与视图重建）—— 分错就会出现「UI 线程里开着库」这种明令禁止的形状
- `Shell.cpp` 2797 行 → 6 个文件，切在「**外壳的哪一族**」上（Chrome / Panels / Dock / Palette / Layout / 本体）
- `kit/Widgets.cpp` 1578 → 12 个控件族 + 34 行门面 · `verify/Review.cpp` 1533 → 8 个验收族 + 55 行入口 ·
  `pages/WorkspaceA.cpp` 1014 → 7 · `pages/ProjectHub.cpp` 1069 → 5 · `theme/Theme.cpp` 750 → 3 模块 ·
  `kit/Views.cpp` + `kit/Icon.cpp` 1102 → 3 个视图族 + 4 个图标族
- 拆出的共用件：`PageKpi` / `PageModal`（`ModalBox` / `ModalFooterHint`）/ `kit::detail::HitTestItem` /
  `kit::detail::UniqueId` / `BezierAt` / `DrawLink`
- 删死代码：`kit::Modal`（零调用，且头部绘制与 `ModalFrameRect` 逐行重复）、`StageStateFromStatus()`、
  `selectedShot_` / `setSelected`（写的是它、真读的是 `BookState::selectedShot` ⇒ 「换到第 2 张镜」那张图与默认态逐像素相同）
- kit 小组件收编：7 处重复实现 → `kit::ListRow`（**4 处**）+ `kit::ListCard`（**3 处**，**刻意不与 ListRow 合并**：
  hover 一个走填色一个走投影档，选中一个换底一个换描边环）；4 个「已实现、算得对、全树零调用」的组件全部接线
  （`ModalFrameRect` / `Toast` / `Menu` / `DataTable`）；新原语 `kit::ScrimPaint`（只画不注册）+ `kit::OverlayPanel`
- 字形集扫描整段删除（`GlyphRanges` 在 `RendererHasTextures` 后端下只被 `imgui_draw.cpp:3568` 读一次且到不了）

**取证基建**
- fixture 由 `out/_make_fixture.ps1` + `out/_fixture_seed.sql` 播种（`novel.db` + `stage_artifacts` + `visual_assets`
  + 6 个产物文件），缺库/缺自检文件**直接失败**，不再「跑出一轮空图还报 PASS」
- 覆盖度靠枚举得出：底栏 3 个页签、检查器「预览」段、出图/出片第二个面板页签、8 个小说模式都曾从没被拍过
  ⇒ 补 `SetNovelMode` / `SetImageFlowPanel` / `SetImageFlowFolded` / `SetVideoFlowPanel` / `SelectStoryboardShot` 等入口
- `git stash` 基线对照要**留下证据目录**（`build/rv15-baseline-31b56a0/`），
  只写「已对照跑过基线」而把目录清掉 = 结论只在提交信息里，工作区查无实据
- ⚠️ **取证脚本的默认 `-Exe` 曾经指向 `build-p7\ShineTVStudio.exe`**（2026-09-30 修）。那个目录里躺着一个
  两小时前、体积小 26 MB 的旧二进制，而默认跑**照样 PASS** —— 那个 PASS 与当前代码毫无关系。
  **默认跑错产物是「判据通过」最难发现的一种假绿**：判据只认产物，不认产物的出处。
  已改指规范的 `build\ShineTVStudio.exe`；跑之前仍要核对 `-Exe` 与 exe 时间戳。

---

## 下一步

重构主干（51/51）已完成并通过验证。剩下的是**产品缺口**，不是重构缺口：

| 缺口 | 归因 | 说明 |
|---|---|---|
| **阶段执行体是桩**（页面已不再假装） | `shine_core` | **归因成立，且已复核修正三处事实**：① `pipeline::StageExecutor` 是 `std::function<bool(StageId, const std::string&, std::string&)>`（`src/pipeline/Runner.h:23`），第二参是 **`const std::string&`**；② `GenerateOneChapter` 在命名空间 **`shine::novel`**（`src/novel/NovelPipeline.h:45-49`）；③ **`NovelContinuity` 根本不是执行体** —— 它是 V8 连续性校验模块，形状上无法充当 T 阶段执行体。<br>保留这个缺口的理由比「签名不同」更硬：`RunNext` **每个阶段调一次**、共 29 个阶段（T1–T17 + V0–V11），且 `Runner.cpp:40` 先扣预算、`:46` 才调执行体、`:51-55` 成功后还要落 `work/T*.json` 并记账 —— 把「生成一章」包成 29 次调用会稳定产出假账本。两套 V 阶段枚举互不兼容（`pipeline::StageId` 是 V0–V11，`src/novel/NovelVisualStages.h:55,64-65` 的 `VisualStageId` 是 V1–V7），`pipeline/` 全树对 novel 零引用。<br>**界面侧已做完**：`Configure(root, mode, nullptr, nullptr)`、`StartRun` 压根不调 `RunNext`、KPI 全 0 改成「未接入」、状态栏改成「阶段执行体未接入」、两个运行按钮 disabled 且**在按钮下方**写出原因（tooltip 抓不到截图）。`OverviewPipelineWired()` 是**唯一一份**读数。 |
| **侧栏树 / 检查器主体** | 页面层 + 一处越界 | **已实现**：`WorkspacePages.h` 开 `BookSide()` 只读视图，侧栏画「卷 → 章 → 镜」三层树、点选换章换镜，检查器「属性」给九行真实字段，镜码 `ShotCode()` 三处共用一份。资产 kind 筛选树、节点「快捷跳转」、检查器「关联」段（伏笔 / 场 / 镜）均已接真数据。<br>**仍缺**：把「哪一对镜连续性不过」做成**可点的**结构化关联。`ContinuityIssue` 只有人类可读的 `detail`（两个 `ShotRow::id` 在里面），已用 `ShotPairToCodes` 在内存快照里换回镜码显示，但要跳转只能正则解析字符串 —— 应在 `src/novel/` 补 `shotAId/shotBId`，属越界，**本轮不猜**。 |

已闭环的旧缺口（原归因多为「要动 `shine_core`」，复核后都不需要）：

| 旧缺口 | 结论 |
|---|---|
| ~~S1–S12 停止规则表~~ | **已接上，零 `shine_core` 改动**。`src/novel/NovelRunLoop.h:43-48` 的 `StopCode`(S1…S12) + `StopCodeName`/`StopCodeCondition`/`StopCodeHint`，`NovelRunLoop.cpp:102-138` 逐条落地。旧界面照 `pipeline::StopPolicy` 的编号把「LLM 调用 0/1000」标成 S1 —— **编号撞名**，已拆成「停止条件 · S1–S12」（规则表）与「流水线停止规则」（预算/前置，**去掉 S 编号**）两张卡 |
| ~~侧栏树的卷层级~~ | **已接上**。`ChapterRow::volume_id` 早就有，`ListChapters` 的 SELECT 也带了它 —— 缺的只是 UI 侧**两行没搬的字段**（`BookChapter` 少 `volume_id`、搬运循环漏了），加一条 `SELECT id,title FROM volumes` |
| ~~章节 × 阶段双轴甘特（V 链）~~ | **已接上并渲染**。`T1–T17` 链确实没章节轴（做不了）；`V1–V7` 走 `stage_artifacts`(chapter_id + stage + 镜序 + created) 已有只读接口，矩阵逐格与 fixture 一致 |
| ~~小说页「设定」模式的实体卡不可点~~ | **已接上**。点实体就是换 `BookState::selectedAsset`（右栏与资产总览网格读的是同一个），设计稿里没有「点完右栏显示什么」的歧义 |

另有一条**已接受降级**：`Derived::gateBg` / `checkRowBg` 当年按纯 alpha 算，与设计稿的实色差一个底。
字段按「只加不减」冻结，要 1:1 请用新增的 `stageDoneBg` / `stageFailBg` / `gateFailBg`。

---

## 没做的

### 视觉 1:1 符合性审计（2026-09-30 起）

投影已按 CSS 接完。这一轮留下一批**已核实、已排序、尚未动**的 token 偏差（归因一律先独立核实，默认假设是「我错了」）。

| # | 偏差 | 归因 | 说明 |
|---|---|---|---|
| 1 | ~~`--dur-1/2/3` 零读点~~ | — | ✅ **已接**：`kit/Anim.{h,cpp}`（third/ImAnim）+ `TransitionTo` / `TransitionColorTo`。另订正 `kEaseOut` 对齐 `webui/src/styles/tokens.css:28`、删掉凭印象编的 `kExit`。CSS 里实际是 43 处 `transition`（不是 60+） |
| 2 | ~~面包屑整条底色 + 下边框缺失~~ | — | ✅ **已接**：底色 + 下边框 + padding 24→16 + 字号 12.5→12.0（12.5 不在 `kSizes` 里 ⇒ 实际渲染 13px）+ flex `gap:7px` |
| 8 | 面包屑左内边距、顶栏 padding、活动栏行距与选中底、dock 左右内边距、检查器段头、底栏页签字号 | 页面层 | ⚠️ 面包屑与检查器段头已改；**仍欠**：顶栏左内边距 +16→12、品牌→胶囊 +20→10、活动栏行距 +48→44、选中底 44×44→40×40、dock 左右内边距各 −2px、底栏页签字号 13px→12px |
| 3 | 15 个 `line.*` / `fill.*` 的 alpha 被压平成不透明实色 | 主题层 | JSON 存的是 CSS `rgba` 叠在 `--bg-surface` 上的结果，算法自洽，但叠在哪个底上都一样（例：深空卡片边应为 `#29303E`，实现画的是 `#202734`，暗 9/9/10） |
| 4 | 15 处字号取不到档：`FontBoldAt(9.5f)` / `(16.0f)` 等 | 字体层 | 两侧都无此档，向上取档到 10.5 / 17px。设计稿有 9.5 / 12.5 / 14.5 / 16px |
| 5 | `font-weight: 500` 无字面 | 字体层 | `Fonts.cpp` 只加载 400 / 600 两档 |
| 6 | `.card.hoverable:hover` 的 `translateY(-2px)` 未接 | 页面层 | **这条记录已被订正**：`kit::Card()` 早就接了。真正缺的是**没走 `kit::Card()` 的页面级卡片**（资产卡 / 项目卡 / 向导行等手写 `DrawShadowed` 的地方） |
| 7 | `[data-theme="inkwash"]` 下 `.brand .mark` / `.hub-logo` 被 `webui/src/styles/tokens.css:206` 的 `box-shadow: none` 覆盖 | 页面层 | 水墨主题下这两个 logo 应当**没有**投影，ImGui 侧未按主题特判 |
| 9 | `.tl-card` 宽 96→128、圆角 8→10；项目中心卡片名 17.0→14.5px | 页面层 | 同上 |

**未接投影档位、且需要先补 `HitTest` 的两处**（不是漏接，是顺序问题）：
`pages/WorkspaceA.cpp` 总览 KPI 卡（`.card.kpi.hoverable`，`KpiCard` 整个没有命中测试）、
`pages/ProjectHub.cpp` 向导确认卡（`.card.glow`；只读汇总卡，hover 反馈价值低，建议只补 `HitTest` 不接档）。
**`pages/Page_Assets.cpp` 的总览网格卡在设计稿里找不到对应元素**（设计稿该页是表格而非卡片栅格），
其真实原型是 `webui/src/views/Assets.jsx:151`；按纪律不硬套档位，注释里的原型出处待订正。

### kit 小组件覆盖缺口

收编后仍欠：**#9**「图标 + 文字」居中胶囊 3 处仍未封装；**#10** 分段页签条 3 处、2 套实现，`kit::Tabs` 在但调用点没统一。
已判定**不该收**：#3 缩略图格（1 处，`hoverable=false` 的 `ListCard` 形态不对，1 处不值得抽）、
#8 甘特 8 列（没有表头带、单元格是色块，套 `DataTable` 会硬造一个设计稿里不存在的表头）。
`kit::Drawer` 至今零调用，但页面上确实没有抽屉式交互，**不算缺口**。

### 门禁自身的盲区（只记录未改）

1. `find-rect-wh-misuse.ps1` **没有 `-SelfTest` 参数**：带上会被静默忽略后照常扫默认根目录并打印 `0` —— 与真通过字面相同。
   它的自检方式是 `-Root <样本目录>`：2026-09-30 实测，3 个真样本 + 3 个假样本里**只抓到 1 个真样本**
   （见下条），证明扫描器还活着但覆盖面确实有洞。
2. 该扫描器漏掉 8 种真误用里的 6 种：`$sizeLike` 只认 `width|height|cardW|panelW|boxW`，裸 `w`/`h` 与
   `colW`/`cellW`/`iconW`/`thumbH` 全部放过（与它自己头部注释矛盾）。**2026-09-30 实测**：
   `Rect{10, 20, cardW, 192.0f}` 抓到（`a3='cardW' a4='192.0f'`），而 `Rect{x, y, w, 120.0f}` 与
   `Rect card{x, y, colW, 76.0f}` **双双漏网** —— 正是那两类形态。真实树报 0 不代表没有。
3. `check-colors.ps1` 抓不到 `ImU32(0x…)` 形态（只认 `IM_COL32` / `ImVec4(数字×…)` / `"#rrggbb"` / `0xAARRGGBB`），
   `kit/Widget_Status.cpp:38` 的 `ImU32(0x59FFFFFFu)` 就是漏网的真硬编码颜色。

**改门禁会让本轮的「绿」失去可比基线，应单独一轮做，并且改完必须先跑它自己的自检证明仍能抓。**

### 真缺陷（留给后续）

- **`duplicate-hits: 815` 尚未判定**（`build/rv23-gcc162`，2026-09-30，GCC 16.2 全量重建后的第一轮**有数据**取证）。
  此前它恒为 0 是因为页面是空的（两个 snapshot 都 TIMEOUT），所以这既不是回归也不是「已修」。
  下一步要分清「设计性嵌套」与「真缺陷」：整卡 `HitTest` + 卡内 footer 按钮、滚动区自身热区、列表行套在
  `ScrollRegion` 里都天然重叠。可行做法是给 `kit::NoteDuplicateHit` 加一个**豁免/归类**通道
  （如 `kit::HitTestNested(parentId, …)` 声明「我故意嵌在 parent 里面」），让计数只报**没声明过的**重叠；
  在那之前不要为了让它变 0 而改产品。
- `kit::ResetInvertedRectCount()` / `ResetDuplicateHitCount()` **全仓零调用点**。今天还不是缺陷
  （`RunReview` 在 `RunLoop` 之前跑），但只要将来有任何代码在取证前泵帧，判据会把交互会话的违规算进取证 ⇒ 整轮假红。
- manifest 的 `overlay-clicks` 只有 `7/7` 一个数，**没有分项也没有原因**；`hover-probes` 至少有 `unstable=`/`broken=` 后缀。两者不对等。
- `unstable=N` 与 `dead=N` 各自二义：同时表示「注入没到位」和「产品真的坏了」，manifest 层面没解。
- `tools/check-colors.ps1` 把 `kit/Views.cpp` 列为**按文件**的豁免区（`$Zones`），且文件不存在时直接 `exit 1`。
  ⇒ **`Views.cpp` 这个文件名是承重结构**：删掉或改名门禁就红（`check-colors.ps1:48` / `:229-232`）。
- 本文早期按 `文件名:行号` 引用旧位置（`Review.cpp:1550`、`WorkspaceA.cpp` / `ProjectHub.cpp` 的半像素表与 ListCard 计数），
  大文件拆分后已失效；本文已改用文件名 + 符号名，历史条目在 `git log` 里。

---

## 跨轮硬事实（别再踩）

### 字体与图集（ImGui 1.92 字体系统）

- **中文发糊是结构性的**，三条根因缺一仍糊，依据全写在 `kit/Fonts.cpp` 顶部注释：
  ① `ImFontConfig::RasterizerDensity` 缺省 1.0，而它是 1.92 起**唯一**能给字形光栅化加分辨率的旋钮（本工程 `DisplayFramebufferScale=(1,1)`、WGL 后端报不上更高密度 ⇒ `CurrentPixelDensity` 恒 1）；
  ② `PixelSnapH=true` 把 `OversampleH` 压成 1，而 `OversampleV` 在 auto 模式下**恒为 1**，且 CJK 糊的正是纵向笔画；
  ③ 本工程字号大量是**半点**（10.5/11.5/12.5/13.5/14.5，来自设计稿 CSS px），stb_truetype 按整数高度出位图，1:1 光栅化后缩到半点必然重采样。
  **修法**：`kRasterizerDensity = 2.0f`，四处 `ImFontConfig` 全部设上。A/B 实测平均边缘梯度 22.55 → **39.26（+74.1%）**、墨迹像素只 +1.9%（目击「变粗」被测量否掉）。
- **刻意不设 `ImFontConfig::GlyphRanges`**：1.92 起那是 legacy 路径，只有不声明 `RendererHasTextures` 的后端才读
  （`imgui_draw.cpp:3514` 跳过、`:3568` 唯一读点）。换回 legacy 后端时**必须重新接上**，否则界面全是 `'?'` —— 那不是回归，是换了一种加载协议。
- **别开 `ImTextureFormat_Alpha8`**：core 支持，但上游 `imgui_impl_opengl3.cpp` 把格式硬编码成 RGBA
  （`:712` / `:733` 以 `GL_RGBA` 上传，`:739` 却按 `BytesPerPixel` 算行跨度）⇒ 开了图集直接烘不出来。**这是 vendored 1.93 WIP 的缺口，不是本侧问题。**
- 图集统计用 `ImTextureData::GetSizeInBytes()`（读 `BytesPerPixel`），**不要**硬编码 `Width*Height*4` —— 旧代码求和后打成 `vram=`，
  标签是错的（`Pixels` 是 CPU 堆缓冲，GPU 侧只有 `TexID`）。64 MB 是项目自定红线、不是显存预算，告警已改成一次性快照 + 单帧增量 + 增长率三档 `Warn`。
- 判定「字体尺寸」时别读反：`ImDrawList::AddText(font, size, …)`（自绘路径，绝大多数调用点）**不取整**；
  `ImGui::PushFont` 走 `GetRoundedFontSize` 取整，会落到另一个 baked 尺寸并触发 on-demand 重烘焙。预烘焙清单 `kUiSizes` 保持半点正是为了配合自绘路径，**不要「顺手」改成整数**。
- `BuildGlyphRanges()` 曾把**局部** `ImVector<ImWchar>` 的指针交出去就返回（悬垂，1.92 之前的老契约）。**该函数已随字形集扫描整段删除**，但形状仍要认：凡是把临时容器的指针交给「后续帧才回读」的 API，就是悬垂。

### 绘制与命中

- `AddRectFilled(p_min, p_max, col, rounding)` —— **颜色在圆角前面**。写反的三种形态全是「画了等于没画」，编译期完全沉默
  （`kit/Draw.cpp` 有 ⚠️⚠️ 注释 + `tools/find-draw-arg-order.ps1` 门禁）。
  **判据是 `_VtxCurrentIdx` 有没有推进**，不是像素 diff：实测 135 次调用一次都没推进，直接坐实「发出但被丢弃」。
- `kit::Rect` 四参是 `(minX, minY, maxX, maxY)`（`kit/Widget_Core.h:20`），当 (x,y,w,h) 用 ⇒ `max < min` ⇒ 整块不画也不可点。
  要 (x,y,w,h) 就用 `RectAt`。
- 遮罩**只画不注册**（`kit::ScrimPaint`，`kit/Overlays.h:21` 有说明）：注册成全屏热区会跟面板内容抢同一个 `HoveredId`，
  而 ImGui 里**先注册者独占**（`imgui.cpp:5161`）⇒ 后果不是「遮罩没生效」而是「面板内某些控件点不动」，且**哪一处坏取决于绘制顺序**。
- **child 的裁剪与层级按 draw list 生效，不按代码嵌套**：要建视口的区域整块改用容器自己的 `drawList()`；
  浮层/模态用 `GetForegroundDrawList()`，否则会被后渲染的 child 盖住。
- 「不画」用 `kit::ColorTransparent()`，别写 `IM_COL32(0,0,0,0)` —— 它是硬编码颜色里最常见的一种，豁免一多门禁等于没有。
- `kit::PinAnimation` 语义（接过渡后的前置条件）：钉住时**连续动画**（脉冲/旋转/呼吸）冻结在给定时刻，
  **过渡**（`TransitionTo` / `TransitionColorTo`）**直接落终值** —— 冻在半路会让静息截图每轮 md5 都变。
  另调一次 `iam_pool_clear()`，避免解绑瞬间闪一下。

### 度量

- **文字垂直居中的口径**（先定死，否则每次量出来的数都不一样）：`ImFont::RenderText` 里 `const float line_height = size;`
  ⇒ **行盒高 = 请求字号**、行盒顶 = `pos.y`，所以 `Δ = (pos.y + fontSize/2) − 容器中心Y`，
  正确写法 `pos.y = 中心Y − fontSize/2`，已封装成 `kit::CenterTextY`（`kit/Draw.h:137`）。
- ⚠️ **两条已踩过、别再走的坑**：① **不要**改成按真实 Ascent/Descent 算 —— 这版 `Descent` 是**负数**
  （size=13 时 asc=11 / desc=−3），照它算会把字往下推，方向与「字偏高」正好相反；
  ② `min.y + (h − size) * 0.5f` 与 `center().y − size * 0.5f` **代数恒等**，曾有一条注释说前者「逐字号各偏各的」，**那句是错的**，已撤回。
- 残留的 −0.45px 光学偏差（CJK 墨迹盒中心在基线上方 0.38em）**已知且接受**。判据：`|Δ| ≤ 0.5` 不动，`|Δ| ≥ 1` 才算缺陷。

### 取证与判据纪律

- **构建失败就别接着跑取证**：改坏一个 API 会让编译挂掉，而取证脚本照样对**旧二进制**跑出一份数字正常的报告。判据只认「构建成功**且**取证 PASS」，构建退出码要单独判。
- **像素差不是内容指标**：离屏渲染跨进程约 2000px 噪声、跨构建约 40000px；小于它不必当回事，大于它**也不能**直接判成回归。
  判「内容有没有丢」用**同一次运行内**的对照页 + 读像素剖面（沿一条线采样 RGB 看渐变）。
  本仓两次「投影差 180px / 0px」的真相都是**界面里印着的工程路径**（`rv77`→`rv78`），与投影无关。
- **md5 逐张比对在跨运行场景下不是可用信号**：同一个二进制连跑两轮，79 张里只有 14~15 张 md5 相同
  （差异是同一个共享元素上一个 175px / 34×10 小块的字形亚像素抗锯齿）。另有 5 张每轮都不同 —— 裁出来读是**输出目录名**。
  拆分类重构的等值证明改用「**manifest 的 `# shots:` 指标行逐字节相同**」。
- **判据自己也会骗人**：「报通过的原因」若与「它本该抓的缺陷」同形，它比没有判据更坏。注释必须写**事实描述**（谁抓谁），不能写意图。
- **反向验证是自检的最低要求**：故意把修复关掉重跑，那条判据必须变红（`overlay-clicks` 关掉修复后 2/4 + `overall=FAIL`，而其余 78 项全绿 ⇒ 证明它是唯一能抓住该缺陷的信号）。
  恢复后仍红 = **判据自己期望写错**，一条永远红的判据和一条坏掉的判据没有区别。
- **一个信号至少对应三种原因**：注入没到位 / 环境塌了（视口 0×0、坐标落在热区外）/ 目标真的没变。
  只看命中数必然改错地方，**两个方向都错**。做法是在探针里记下「系统实际看到什么」，并在没到位时**先于**其余分支短路。
- `ScrollRegion` 判据要打出**期望上限**和 **ImGui 侧上限**（`GetScrollMaxY()`）：两者不等说明根本没修好（粘性请求能到位 ≠ 用户滚轮能滚）。
  判据的多个分支必须**平级**写 —— 把 `maxScrollY > 0` 塞进 `if` 里，「内容压根没超出」和「请求没被核销」会一起静默通过。
- 「覆盖 N 个工作区」不等于「覆盖每个工作区的每个视图面」；fixture 缺数据也会造成覆盖洞（**页面拍到了，分支没跑到**）。
- 「证明性」截图必须**成对**（默认态 + 目标态）：单拍一张时它跟谁都不重样，判据抓不到「这个入口是死的」。
- 判据的产物是**证据不是 build 垃圾**：基线对照目录、取证目录、manifest 都要留在盘上可复核。
  只写「已对照跑过基线」而把目录清掉，等于结论只在提交信息里。

### 工具与环境

- **`.ps1` 必须带 UTF-8 BOM**：PowerShell 5.1 对无 BOM 文件按系统 ANSI 码页（中文 Windows = GBK/936）解码，
  中文注释被解成乱码，某些字节序列恰好凑出引号/括号 ⇒ 报 `ParserError` 而**行号与真实原因毫无关系**。
  判据：`[System.Management.Automation.Language.Parser]::ParseInput([IO.File]::ReadAllText($f), …)` 报零错而 `powershell -File $f` 报 ParseError ⇒ 就是 BOM 问题。
  用 `write` 工具生成的 `.ps1` 没有 BOM，交付前一律前置 `EF BB BF`。
- **改 `src/ui/imgui/**` 只用 `read`/`edit`/`write`**，不要 `Set-Content` / `Out-File` / `[IO.File]::WriteAllText` ——
  它们按 ANSI 重编码，会静默把 `⚠️`、`→` 这类 GBK 编不了的字符换成 `?`，编译照过、中文照在，标记没了。
  必须用脚本改字节时全程只走 `ReadAllBytes` / `WriteAllBytes`，**输出缓冲与输入游标必须独立推进**（共用游标时「替换完跳过差值」一旦算错就会吞掉紧跟标识符的那一个字节）。
  改行尾要归到 CRLF 时先在 4 份合成样本上自检（LF 串 / CRLF 串 / 混合 / 已损坏的 LF CR），否则会写出 `0A 0D` 让 git 把源文件判成二进制。
- 判别文件编码别看「高位字节里 0xC2-0xDF 多还是 0x81-0xFE 多」（GBK 汉字首字节本来就落在 0xB0-0xF7，会把 UTF-8 误判成 GBK）。用严格解码试一次：
  `$s = New-Object System.Text.UTF8Encoding($false,$true); try { $null = $s.GetString([IO.File]::ReadAllBytes($f)); 'UTF-8' } catch { '非 UTF-8' }`
- PowerShell 里**函数名不要用 `W` / `Sl`**（是 `Set-Location` 的内置别名，PowerShell 优先解析别名，报的却是「位置参数无法找到」）；
  变量名不要只靠大小写区分（`$A` / `$a` 不敏感，大写赋值会覆盖小写那个）。
- 「看着加了、实际等于没加」比没加更危险：编译器不报错、像素看不出来、只有真去滚才发现。给被修的量加**只报一次的哨兵** +
  一次**主动探测**，并且哨兵与判据**都要接到最终 verdict**（只写日志而 `overall=PASS` 等于「报错但绿灯」）。

### 三条归因纠正（别再拿旧结论去改代码）

1. **「V 链只画 8 个阶段、真实是 12 个」不成立** —— `pipeline::StageId` 是 V0–V11（连续性链），
   小说侧的真实视觉链是 `VisualStageId` V1–V7（`src/novel/NovelVisualStages.h`），**V8 是另一件事**（连续性校验）。**代码是对的，没改。**
2. **S 编号撞名** —— `pipeline::StopPolicy.cpp:7-11` 的「S1–S4 预算」与 `novelcore::StopCode` 的 S1–S12 是**两套**编号，
   旧界面拿前者当后者标，才需要拆成两张卡。同源撞名还有 `chapter-vstage` 曾经被读成 8 列甘特。
3. **「`ContinuityIssue` 没有 shot id，要改 `src/novel/`」不准确** —— 两个 `ShotRow::id` 就在 `detail` 文本里。
   真正缺的只是**结构化字段**（现在得正则解析人类可读文本，把一条契约绑在字符串格式上）。
