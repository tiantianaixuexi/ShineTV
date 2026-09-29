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
| **P3** | 组件套件 | `[~]` | 6/7 | 组件画廊 5 主题对齐 |
| **P4** | 应用外壳 | `[~]` | 10/11 | 外壳逐项对齐 |
| **P5** | 六个工作区 | `[~]` | 4/6 | 6 页 × 5 主题对齐 |
| **P6** | 验收取证 | `[x]` | 3/3 | 总回归入口可跑 ✅ |
| **P7** | 收尾 | `[ ]` | 0/7 | **不装 Qt 也能构建** |

**整体：40 / 51 步**

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
- [x] **P3.4** 容器：Card / Segmented / Tabs
  - `Card` 的 `hoverable` / `glow` 早先是死代码，已改成真 `HitTest` + hover 边色 + 上浮 2px
  - Tooltip / Dialog / Drawer / Toast / DataTable / Tree / Skeleton / Menu → `[~]`
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
- [~] **P5.2** 小说（novel）— 8 模式 · 进行中
- [~] **P5.3** 资产（assets）— 进行中
- [~] **P5.4** 分镜（storyboard）— 进行中
- [x] **P5.5** 出图（imageflow）— 含 `FlowCanvas`
- [x] **P5.6** 出片（videoflow）— 含 `FlowCanvas`

---

## P6 验收取证

- [x] **P6.1** 后备缓冲 → PNG 抓图 + manifest（`shots-manifest.txt`，对齐 `scripts/run_reviews.ps1:56`）
  - `glReadPixels` 原点在左下，PNG 从左上起 → **逐行倒着写**
- [x] **P6.2** 保留全部 `SHINE_*` 环境变量名与分派点
- [x] **P6.3** `SHINE_IMGUI_REVIEW`：6 工作区 × 5 主题 + 全工作区基线
  - 覆盖面 **17 → 37 张**
  - manifest 含 `overall=PASS|FAIL` 行，退出码 FAIL 从 2 改 1
  - 最新一轮：**37/37 saved · `overall=PASS` · 退出码 0 · 37 张 md5 全唯一**

---

## P7 收尾

- [ ] **P7.1** 删 `src/ui/kit/`(55) + `src/ui/qml/`(52) + `src/ui/pages/`(89) + `src/ui/layout/`(2)
- [ ] **P7.2** 删 `shine_kit` / `shine_qml` 目标与 `SHINE_UI_QT`
- [ ] **P7.3** 移除 `find_package(Qt6)` / `AUTOMOC` / `AUTORCC`
- [ ] **P7.4** 门禁定型
- [ ] **P7.5** `package-qt.ps1` → `package-imgui.ps1`（顺带修掉不存在的 `-DSHINE_QT_UI=ON`）
- [ ] **P7.6** 更新 `AGENTS.md` 分层规则
  - ⚠️ `AGENTS.md` 现在写「着色源码是 GBK 编码中文注释」，**对 `src/ui/imgui/**` 是错的**（那棵树是无 BOM 的 UTF-8）
- [~] **P7.7** 实测产物体积并填表
  - 当前 **250,451,831 B** vs 目标 < 30 MB。**未裁剪**，差距主要在静态库未 strip

---

## 关键指标

| 指标 | 基线 | 目标 | 实测 |
|---|---|---|---|
| exe 体积 | 392,330,166 B | < 30 MB | **250,451,831 B**（P7.7 未完成） |
| `shine_core` 中 Qt 头引用 | 165 个 UI 文件 | **0** | **0**（`check-layers` 门禁保证） |
| QML 文件 | 52 | **0** | 52（P7.1 未做） |
| 构建是否需要 Qt | 是 | **否** | **否**（`SHINE_UI_IMGUI=ON` 默认） |
| `check-layers` | PASS | PASS | **PASS (0 violations)** |
| 字体图集 | — | < 64 MB | **0.2 MB / 3948 codepoints** |
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
| 2026-09-30 | P7.7 | 当前 exe 体积 | 250,451,831 B | ⚠️ 未达目标 |

---

## 下一步

**P7.1 删 Qt 树** —— 唯一能同时解掉体积指标与「构建是否需要 Qt」两个判据的一步。

```
src/ui/qml  52 文件   src/ui/kit  55   src/ui/pages  89   src/ui/layout  2
```

删完再跑 `cmake -S . -B build-imgui -DSHINE_UI_IMGUI=ON -DSHINE_UI_QT=OFF`，
不装 Qt 也能配过 = P7 的验收判据。
