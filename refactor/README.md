---
id: refactor.readme
kind: overview
status: current
source_of_truth:
  - CMakeLists.txt
  - Build.bat
  - src/ui/imgui/**
  - refactor/design-spec.md
  - webui/            # 本地设计稿，**不在版本控制里**（.gitignore 末行）
last_verified: 2026-09-30
---

# ShineTV Studio 前端：ImGui 移植总览

> **双端架构**：`webui/`（React + CSS）是**视觉设计基准**，只做效果、不管功能；
> `src/ui/imgui/`（C++ / Dear ImGui）是它的 1:1 复刻，是真正跑起来的界面。
> 业务层 `shine_core`（`src/core util net db llm comfy gpu media flow visual novel paint mcp project pipeline`）
> 与本次移植无关，**一行不改**。

本文是**入口**：先读它定位「要改的东西在哪个文件」，再按需展开
[`design-spec.md`](design-spec.md)（视觉判据）、[`architecture.md`](architecture.md)（分层与线程契约）、
[`PROGRESS.md`](PROGRESS.md)（已验证的事实 + 未修偏差清单）。

---

## 0. 现状（2026-09-30）

| 项 | 状态 |
|---|---|
| 构建 | `Build.bat` 一键通过；Debug `build\debug\ShineTVStudio.exe`，Release `build\release\ShineTVStudio.exe`（17 MB） |
| 渲染后端 | **OpenGL 3.3 core（WGL）** + `imgui_impl_win32`；不用 D3D11 |
| 主题 | 5 套（深空 / 薄暮 / 纸墨 / 水墨 / 极夜），JSON 驱动，`%APPDATA%/ShineTVStudio/theme.json` 持久化 |
| 颜色 token | **31** 项（`Tokens.h:71-79`）；`tokens.css` 注释写的 28 已过时 |
| 组件 | `kit/` 下 15 族 `Widget_*.h` + 视图族 `Views / View_Stage / View_FlowCanvas` |
| 导航 | 6 个工作区 + 项目中心（整屏）+ 组件画廊（回归基准） |
| 验收 | `SHINE_IMGUI_REVIEW=<dir>` 跑一次总回归后退出；`overall=PASS` ↔ 退出码 0 |
| 未修偏差 | 见 [`PROGRESS.md` §「视觉 1:1 符合性审计」](PROGRESS.md) —— **改任何视觉前先读那一节** |

⚠️ **`webui/` 不在版本控制里**（`.gitignore` 最后一行）。它是本地设计稿，新 clone 拿不到。
所以 `design-spec.md` §2 内联了全部 31×5 色值 —— 那份表才是可复现的记录。

---

## 1. 分层与目录

```
shine_core (STATIC)              业务层：零 Qt、零 ImGui。本次移植不动一行
   core util net db llm comfy gpu media flow visual novel paint mcp project pipeline
        ▲  gpu::AttachDevice(GlApi) ← 纹理上传的注入点，宿主在启动时接上
        │
shine_imgui (STATIC)             src/ui/imgui/**，GLOB_RECURSE（排除 app/）
   theme/    主题表（31 token）· 派生色 · ImGuiStyle 生成 · 持久化
   kit/      组件库：15 族 Widget_* + Draw / Fonts / Icon / Anim / Scroll / Overlays / Views
   pages/    外壳 + 6 工作区 + 项目中心 + 组件画廊
   host/     Win32 窗口 + WGL/GL3 + 帧循环 + DPI + 取证注入点
   verify/   回归取证（抓图 / 悬停探针 / 浮层点击 / 快捷键 / 判定）
        │
ShineTVStudio (exe)              src/ui/imgui/app/main.cpp —— 只做 CLI 分派
```

三个第三方库直接编进 `shine_imgui`：`imgui`（含 `imgui_demo`，画廊做对照用）、
**`ImAnim`**（动画，只取 `im_anim.cpp`）、`VisualNodeSystem`。

---

## 2. 两条权威线（先定这个，否则会改错层）

| 维度 | 权威 | 理由 |
|---|---|---|
| **视觉 / 样式 / 布局 / 交互态** | `webui/` | 用户要求 1:1；它是 CSS 设计稿，值齐全且自洽 |
| **功能 / 数据 / 行为** | `src/`（`shine_core`） | 只有它真接了 `project` / `novel` / `flow` / `comfy` 等后端 |

两者的差集是**已知的、不补的**：

- `webui` 的 `.doctabs`（文档标签栏）是**死 CSS**，无任何 JSX 引用 → 不实现。
- `webui` 无主题持久化（全仓 0 处 `localStorage`）→ 用 C++ 侧的 `theme.json`。
- `webui` 的 `Ctrl+N` 只有 toast、样式编辑器的「另存为主题」只发 toast → 不当作行为契约。

---

## 3. 文件地图：**要改什么 → 改哪个文件**

### 3.1 主题与样式（改颜色、圆角、间距、字号）

| 要改的东西 | 文件 | 验证方式 |
|---|---|---|
| 圆角 / 间距 / 边框档（`r-xs…pill`、`sp-1…7`） | `theme/Tokens.h` 的 `radius::` `space::`；应用到 `ImGuiStyle` 的是 `theme/Theme.cpp` 的 `ApplyGeometry()` | 肉眼 + 抓图 |
| 某一套主题的某个色值 | `theme/Themes/<主题名>.json` 的 `colors`（恰好 31 项） | `SHINE_THEME_SELFTEST=1` |
| 色 token 的**名字 / 数量 / 顺序** | `theme/Tokens.h`：`struct ColorToken` + `kColorTokenNames`（⚠️ 顺序三方共用，中间插一项整表错位且不报错） | `tools\check-theme.ps1` |
| 由色值算出的淡底 / 淡边 / 投影（`color-mix` 的等价物） | `theme/Theme_Derived.cpp` | `tools\check-theme.ps1` |
| 阴影几何（dx/dy/blur/逐层 alpha，逐主题） | `theme/Theme.cpp` 的 `kShadowSpecs`（JSON 存不下，硬编码在此） | 抓图 |
| 主题的加载与解析 | `theme/Theme_Parse.cpp`（`LoadThemesFrom`） | `SHINE_THEME_SELFTEST=1` |
| 主题切换 / 持久化 | `theme/Theme.cpp` 的 `ApplyTheme()`；外壳入口 `pages/Shell.h` 的 `Shell::SetTheme()` | 顶栏主题菜单 |
| 每套主题的 `ImGuiStyle` 控件色 | `theme/Theme.cpp` `ApplyTheme()`（31 项必须**一次写全**，不许循环凑） | 抓图 |
| 字体族 / 字号档 / 图集 | `kit/Fonts.cpp`（`BuildFontAtlas` / `FontAt` / `FontBoldAt` / `MonoAt`） | 抓图 |
| 颜色运算与取色出口 | `kit/Widget_Color.h`（`ColorOf/ColorText/…/WithAlpha/LerpColorTo`） | `tools\check-colors.ps1` |
| 减少动效开关 | `kit/Draw.h` 的 `SetReduceMotion`；`[data-reduce-motion]` 对应 `kit/Anim.h` 的 `Pinned` 短路 | 抓图 |

### 3.2 组件库 `kit/`（改控件外观就改这里）

| 族 | 文件 | 覆盖的设计稿 |
|---|---|---|
| 地基：绝对矩形 + 命中测试 | `kit/Widget_Core.h`（`Rect` / `HitTest` / `Clicked`） | — |
| 取色 + 颜色运算 | `kit/Widget_Color.h` | — |
| 按钮 / 图标钮 | `kit/Widget_Button.h` | `UI.jsx` Button、IconBtn |
| 标签 / 状态点 / 按键提示 | `kit/Widget_Badge.h` | Tag、StatusDot、Kbd |
| 卡片 | `kit/Widget_Card.h` | Card（含 `hoverable` / `glow`） |
| 分段控件 / Chip / 页签 | `kit/Widget_Choice.h` | Segmented、chip、Tabs |
| 表单（Field/Input/TextArea/Select/Switch/Checkbox） | `kit/Widget_Input.h` | 同名列 |
| 进度 / 空态 / KV / 转圈 / 分隔线 | `kit/Widget_Status.h` | Progress、Empty、KV、Spinner |
| 浮层（Tooltip / Menu / Scrim / Modal / Drawer / Toast） | `kit/Overlays.h` + `kit/Widget_Overlay.cpp` | `.menu-pop`、`.modal`、`.drawer`、`.toast`、`[data-tip]` |
| 列表行 / 双行卡 | `kit/Widget_List.h` | `.list-row`、项目中心卡 |
| 数据表（可排序） | `kit/Widget_Table.h` | `.table` |
| 树 | `kit/Widget_Tree.h` | `.tree` |
| 自绘原语 + 文本居中 + 网格列数 | `kit/Draw.h` | `draw` 层 |
| 动画（ImAnim 封装） | `kit/Anim.h` | `--dur-1/2/3`、`--ease`、`--ease-out` |
| 滚动区 | `kit/Scroll.h` | `overflow:auto` |
| 复合视图：`Art` / `ArtInk` | `kit/Views.cpp` | `UI.jsx` Art、ArtInk |
| 阶段流 / 阶段表 / Steps | `kit/View_Stage.cpp` | `StageFlow.jsx`、`UI.jsx` Steps、`StageList` |
| 节点画布 | `kit/View_FlowCanvas.cpp` | `FlowCanvas.jsx` |
| 图标 | `kit/Icon.h`（门面）→ `Icon_Path.cpp`（SVG `d` 解析）/ `Icon_Registry.cpp`（名→路径）/ `Icon_Draw.cpp` | `Icon.jsx` |

### 3.3 外壳 `pages/`

| 要改的东西 | 文件 |
|---|---|
| 外壳**固定档**（顶栏 46 / 导航 56 / 侧栏 240 / 检查器 280 / 底栏 190 / 状态栏 26 / 面包屑 34） | `pages/Shell_Layout.h` |
| 顶栏 / 导航栏 / 状态栏 / 面包屑 | `pages/Shell_Chrome.cpp` |
| 侧栏（卷→章→镜树 / 资产 kind 树）+ 检查器三段 | `pages/Shell_Panels.cpp` |
| 底栏（任务队列 / 日志 / 产物） | `pages/Shell_Dock.cpp` |
| 底栏「校验报告」页 + 详情模态 | `pages/Shell_Reports.cpp` |
| 命令面板 / 主题菜单 / 设置模态 / 浮层分发 | `pages/Shell_Palette.cpp` |
| 工作区分发 / 快捷键 / 布局持久化 / 项目打开 | `pages/Shell.cpp`、`pages/Shell.h` |
| 页面公共件（`kGap=16`、`ViewHeader`、占位串、视口高通道） | `pages/PageCommon.h`、`pages/WorkspacePages.h` |
| KPI 卡 | `pages/PageKpi.h` |
| 通用弹窗外壳三段 | `pages/PageModal.h` |

### 3.4 各页面

| 页面 | 文件 |
|---|---|
| 总控（P5.1） | `pages/Overview.h` + `OverviewState/Flow/Kpi/Tables/StopRules/RightRail.cpp` |
| 小说（P5.2） | `pages/Page_Novel.cpp` + 数据层 `BookData.*` / `BookQuery.*` |
| 资产（P5.3） | `pages/Page_Assets.cpp` |
| 分镜（P5.4） | `pages/Page_Storyboard.cpp` |
| 出图 / 出片（P5.5/5.6） | `pages/Page_ImageFlow.cpp`、`pages/Page_VideoFlow.cpp` + 共用的 `pages/FlowGraph.*` |
| 项目中心（整屏） | `pages/ProjectHub.cpp` + `Hub.h` / `HubState` / `HubCard` / `HubWizard` / `HubDialogs.cpp` |
| **组件画廊（回归基准）** | `pages/Gallery.h` / `Gallery.cpp` |

### 3.5 宿主与验收

| 要改的东西 | 文件 |
|---|---|
| 窗口 / GL 上下文 / 帧循环 / DPI | `host/Host.h`、`host/Host.cpp` |
| 启动顺序 / CLI 分派 / 环境变量 | `host/AppEntry.cpp`、`host/AppEnvironment.*` |
| 取证注入（鼠标 / 按键 / 点击） | `host/Host.h` 的 `SetFrame*Override`（⚠️ 必须在 `NewFrame` **之前**注入） |
| 抓图 / 探针 / 判定 | `verify/*.h/.cpp`（`Review.cpp` 是总入口，`ReviewVerdict.cpp` 是唯一判定处） |
| 造验收用工程 fixture | `verify/ReviewFixture.cpp` |

### 3.6 门禁与脚本

| 工具 | 抓什么 |
|---|---|
| `tools\check-layers.ps1` | 分层越界（`src/` 里出现 Qt 头、页面层直连底层） |
| `tools\check-colors.ps1` | 硬编码颜色字面量（只认 `IM_COL32` / `ImVec4(数字…)` / `"#rrggbb"` / `0xAARRGGBB`） |
| `tools\check-theme.ps1` | 主题表完整性（token 数与顺序） |
| `tools\check-i18n.ps1` | 文案规范 |
| `tools\find-rect-wh-misuse.ps1` | `kit::Rect` 四参 `(minX,minY,maxX,maxY)` 被当成 `(x,y,w,h)` |
| `tools\find-silent-truncation.ps1` | 列表按容器高静默截断 |
| `tools\find-draw-arg-order.ps1` | `AddRectFilled` 的颜色/圆角参数错位 |
| `scripts\capture_window.ps1` | 截当前窗口 |
| `scripts\run_reviews.ps1` | 批量跑 P03…P10 回归并核对「manifest ↔ 退出码」 |
| `scripts\studio.ps1` / `comfy.ps1` | 运行辅助 |

---

## 4. 两个固定动作（加控件 / 加图标）

### 4.1 加一个控件

1. 在 `webui/src/styles/ui.css`（或 `shell.css` / `views.css`）找到对应选择器，
   **抄下**：`height`、`padding`、`border-radius`、`font-size`、`font-weight`、
   `background`、`border`、`:hover` / `:active` / `:focus` / `:disabled` 各态。
2. 归到 `kit/Widget_<族>.h` 里加声明。约定（`kit/Widgets.h` 顶部写全）：
   - 吃**绝对矩形**（`Rect`），不走 ImGui 线性布局 —— 外壳是自绘栅格；
   - 命中测试统一走 `InvisibleButton` 占位（保 ImGui 的 ID 栈与焦点语义）；
   - 颜色**一律**从 `kit/Widget_Color.h` 取，**不许**在调用点写 `ImVec4(...)` 字面量。
3. 实现在同族 `.cpp`。数值**只允许**来自 `design-spec.md` §5 或 `Tokens.h` 的档位，
   硬编码裸数字要在注释里写出 CSS 出处（`ui.css:123`）。
4. **加进组件画廊** `pages/Gallery.cpp` —— 每一种状态都要有（不只是默认态）。
5. 验证：`check-layers.ps1` → `check-colors.ps1` → `Build.bat` → 抓图。

### 4.2 加一个图标

1. 从 `webui/src/components/Icon.jsx` 抄那条 SVG `d` 路径字符串。
2. 在 `kit/Icon_Registry.cpp` 注册「名字 → 路径」。未知名字**回落 `info`**，不会崩。
3. 画：`kit::DrawIcon(draw, center, size, name, color)` / `DrawIconCentered(...)`，
   24 网格 / 1.6 描边，`currentColor` 由 `color` 参数承担（换主题不用重建图集）。
4. ⚠️ 两处设计稿自身的坑，**别照抄**：`Gallery.jsx:140` 传了未定义的 `icon="flow"`
   （被兜底吞掉）、`x` 被当作减号用（应补一个真减号）。

---

## 5. 构建 / 运行 / 门禁 / 验收

```powershell
# 构建（一键；两个配置各用独立 build 目录，别塞进同一个）
Build.bat                 # Debug   -> build\debug\ShineTVStudio.exe
Build.bat --release       # Release -> build\release\ShineTVStudio.exe
Build.bat --clean         # 先删该 build 目录

# 门禁
powershell -File tools\check-layers.ps1
powershell -File tools\check-colors.ps1
powershell -File tools\check-theme.ps1

# 跑起来
.\build\release\ShineTVStudio.exe

# 自动化 1:1 回归（跑完直接退出；overall=PASS ↔ 退出码 0）
out\_make_fixture.ps1                        # 首次 / fixture 丢了之后跑一次（判据的成立条件）
powershell -File out\_run_review.ps1 -OutDir "$PWD\build\shots" -Theme deepspace
```

⚠️ **回归必须带 `novel.db` fixture**。`SeedReportFixture()` 不建它，少了它侧栏树与资产树
退化成诚实空态，`hover-tree-node` / `hover-asset-card` 两个探针永远不可能过 ——
同一份代码缺库 `9/11 + FAIL`、有库 `11/11 + PASS`。
⚠️ `scripts\run_reviews.ps1` 的 `-Exe` 曾写死在 `build\`（那里躺着一个更旧的二进制，
而它照样报 PASS）—— 现已按 `build/release → debug → build` 解析并在开头打印
`exe: <路径> (<时间戳>)`。纪律不变：**核对那一行**。首选上面这条 `out\_run_review.ps1`。

常用环境变量：

| 变量 | 作用 |
|---|---|
| `SHINE_IMGUI_REVIEW=<dir>` | 跑一次总回归抓图后退出 |
| `SHINE_IMGUI_WINDOW=1600x960` | 锁窗口尺寸（跨进程比对才有意义） |
| `SHINE_THEME_SET=<ascii id>` | 启动即切主题（`deepspace`/`dusk`/`paperink`/`inkwash`/`polarnight`） |
| `SHINE_THEME_DIR=<dir>` | 换主题 JSON 目录 |
| `SHINE_THEME_SELFTEST=1` | 主题读写自检，输出 `theme selftest: PASS/FAIL` |
| `SHINE_APPDATA_OVERRIDE=<dir>` | 沙箱 `%APPDATA%`，不污染真实用户数据 |

验收判据与方法见 [`design-spec.md` §9](design-spec.md)。

---

## 6. 文档地图

| 文件 | 内容 | 什么时候读 |
|---|---|---|
| `README.md`（本文） | 现状、分层、**文件地图**、两个固定动作 | 每次开工先读 |
| [`design-spec.md`](design-spec.md) | webui 1:1 视觉规范：几何/字号、31×5 色表、图标、外壳尺寸、22 组件、6 页逐页布局、降级清单、验收方法 | **动任何视觉之前** |
| [`architecture.md`](architecture.md) | 分层、线程契约、纹理路径、门禁规则 | 动宿主 / 线程 / 纹理时 |
| [`PROGRESS.md`](PROGRESS.md) | 已验证的事实、已修的真缺陷、**未修偏差清单**、跨轮硬事实 | 改之前先查这里，避免重复踩 |
| [`phases.md`](phases.md) | P0–P7 历史实施步骤与验收判据 | 想知道某个 P 编号指什么时 |
| `../AGENTS.md` | 协作规则、构建、门禁、常用入口 | 每次开工 |
| `../webui/` | 设计稿本体（**不在版本控制里**） | 需要核对 CSS 原式时 |

---

## 7. 硬约束（违反即返工）

1. **不改 `shine_core`**。业务层零 Qt、零 ImGui 已成立；宿主只需要接
   `gpu::AttachDevice(GlApi)` 这一处注入。
2. **颜色只能从主题来**。调用点写 `ImVec4(...)` / `IM_COL32(...)` 字面量会被
   `check-colors.ps1` 拦。「不画」用 `kit::ColorTransparent()`，别写 `IM_COL32(0,0,0,0)`。
3. **同帧同一矩形只能 `HitTest` 一次**。ImGui 先注册者独占 `HoveredId`；
   浮层遮罩**只画不注册**（`kit::ScrimPaint`），否则面板里的控件会点不动，
   而且**哪一处坏取决于绘制顺序**。
4. **`kit::Rect` 四参是 `(minX,minY,maxX,maxY)`**，不是 `(x,y,w,h)`。写错**不报编译错**，
   控件整块不画也不可点。宽高式构造用 `kit::RectAt(x,y,w,h)`。
5. **不要调 `ImGui::IsMouseHoveringRect`**（本工程会 `0xC0000005`），
   用 `Rect::contains(io.MousePos)` 手算。
6. **`ScrollRegion` 不调 `setContentHeight()` 就是滚不动**（不是「滚不顺」）。
7. **浮层用 `GetForegroundDrawList()`**；child 的裁剪与层级按 draw list 生效，不按代码嵌套。
8. **合成鼠标/按键必须在 `NewFrame` 之前注入**（`Host::SetFrame*Override`），
   在帧回调里写 `io.MousePos` 活不过 `NewFrame`。
9. 两处**必须接受**的降级：`backdrop-filter` 毛玻璃、`box-shadow`。
   不要为它们做像素级死磕（`design-spec.md` §9.3）。
