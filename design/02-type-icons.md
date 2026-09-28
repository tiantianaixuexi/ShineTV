---
id: design.p02-type-icons
kind: design-proposal
status: current
scope: ui-redesign
source_of_truth:
  - src/ui/kit/theme/Token.h
  - src/ui/kit/theme/QssBuilder.cpp
  - src/ui/kit/controls/Controls.h
  - src/ui/kit/controls/Controls.cpp
  - src/ui/kit/controls/Inputs.cpp
  - src/ui/kit/controls/Surfaces.cpp
  - src/ui/kit/controls/WidgetCommon.h
  - src/ui/app/AppEntry.cpp
  - src/ui/pages/shell/TopBar.cpp
  - src/ui/pages/shell/ActivityRail.cpp
last_verified: 2026-09-28
---

# 02 · 排版层级 + emoji 换成矢量图标

**工作量**：中偏大（新增 `kit/icons`，改 15+ 处调用点）
**风险**：中——图标渲染要跨 DPI 验证；排版改动会让所有页面文字变宽，需逐页看截图
**收益**：高。图标是"廉价感"的最大来源；排版层级是"信息层级"的直接来源。

## 目标

1. 建立 6 档排版角色（display / h1 / h2 / h3 / body / caption），由 QSS + token 统一供给，页面不再自己 `setPixelSize`。
2. 用矢量图标替换全部 emoji（活动栏 6 个、顶栏 5 个、搜索框 1 个、Toast 4 个、空态/错误态）。
3. 新增门禁，防止 emoji 再回来。

## 不做什么

- 不引入外部图标库（Material/Codicon 等）及其字体依赖——离线包与授权不明确。
- 不改任何业务逻辑、不改数据流。

---

## 现状证据

### 排版

| 事实 | 位置 |
|---|---|
| 全局只有 `QFont` 10pt 一个基准 | `src/ui/app/AppEntry.cpp:87-93` |
| **QSS 里没有任何 `font-size`** | `src/ui/kit/theme/QssBuilder.cpp`（全文仅 `font-weight: 600`，见 `:184`） |
| 字号各自为政（≥10 处） | `Panels.cpp:56`、`ProjectHubView.cpp:117/171/380/392`、`NovelWorkspace.cpp:178`、`AutoRunPanel.cpp:78`、`ReviewView.cpp:161`、`StateDiffView.cpp:133`、`Flow.cpp:226/230` |
| `font::kSizes` 定义了但没被 QSS 消费 | `src/ui/kit/theme/Token.h:71` |
| 三行块（`QLabel` + `SetKind` + `SetSemibold`）重复 20 余处 | `src/ui/kit/controls/WidgetCommon.h:41-44`（注释自述） |

### 图标

| 位置 | 字形 | 证据 |
|---|---|---|
| 活动栏 6 个入口 | `⌂ 📖 🎭 🎬 🖼 🎞` | `ActivityRail.cpp:22-26` |
| 顶栏 5 个动作 | `🔍 ▶ ⏹ 🎨 ⚙` | `TopBar.cpp:50-92` |
| 搜索框前缀 | `🔍` | `Inputs.cpp:657` |
| Toast 四态 | `✔ ! ✕ ℹ` | `Surfaces.cpp:193-196` |
| 错误态 | `⚠` | `Feedback.cpp:88` |
| 已挂待办 | 「P03 换真图标」 | `Controls.h:67` 注释 |

---

## 方案

### 2.1 排版角色（6 档）

`Token.h` 的 `font::kSizes` 已在 `01-tokens-color.md` 调整为 `{12, 13, 14, 16, 20, 28}`，本方案让它真正生效。

新增 `src/ui/kit/controls/Typography.h`（header-only，与 `WidgetCommon.h` 同级）：

```cpp
namespace shine::type {
enum class Role { Display, H1, H2, H3, Body, Caption };

// 应用字号 + 字重 + 颜色（颜色走 token，零字面值）
void Apply(QWidget* w, Role r);

[[nodiscard]] inline int Px(Role r) { return theme::font::kSizes[static_cast<std::size_t>(r)]; }
[[nodiscard]] inline int Weight(Role r) {
    return (r == Role::Display || r == Role::H1 || r == Role::H2) ? theme::font::kSemibold
                                                                 : theme::font::kRegular;
}
} // namespace shine::type
```

| Role | 字号 | 字重 | 用途 | 出现位置 |
|---|---|---|---|---|
| Display | 28 | 600 | 项目封面字、对话框大标题 | `ProjectHubView.cpp:117/171` |
| H1 | 20 | 600 | **页面标题**（PageScaffold 页头） | 03 方案引入 |
| H2 | 16 | 600 | 区标题（甘特图 / 账本 / 停止报告） | `GanttView.cpp:17` |
| H3 | 14 | 600 | 卡片标题 | `ProjectHubView.cpp:380` |
| Body | 14 | 400 | 正文、表格单元格、按钮文字 | 全局默认 |
| Caption | 12 | 400 | 辅助说明、时间戳、状态副文案 | `ProjectHubView.cpp:392` |

同时改 `AppEntry.cpp:87-93`：`setPointSize(10)` → `setPixelSize(theme::type::Px(Role::Body))`（14px），保证默认就是正文档。

**清理清单**（改完必须逐个删掉）：

```
src/ui/kit/data/Panels.cpp:55-57          setPointSize(26)          → type::Apply(v, Role::Display)
src/ui/pages/project/ProjectHubView.cpp:116-119 / 169-172 / 378-381 / 390-393
src/ui/pages/novel/NovelWorkspace.cpp:177-180   setPointSize(+2)     → Role::H1
src/ui/pages/novel/AutoRunPanel.cpp:77-79      setBold(true)        → Role::H2
src/ui/pages/novel/StateDiffView.cpp:132-134   setBold(true)        → Role::H3
src/ui/pages/novel/ReviewView.cpp:160-162       pointSizeF(-1)      → Role::Caption
src/ui/kit/data/Flow.cpp:226 / :230             QFont{.., 11} / {.., 9}  → Role::Caption / Role::Body
src/ui/kit/layout/QtLayout.h:27-33             SectionLabel DemiBold  → type::Apply(.., Role::H3)
```

QSS 侧同步补字号（`QssBuilder.cpp` 的 `kTemplate` / `kKitTemplate`）：

```css
*[shineKind="dialogtitle"] { font-size: 20px; font-weight: 600; }
*[shineKind="statetitle"]  { font-size: 20px; font-weight: 600; }
*[shineKind="statesub"]    { font-size: 13px; }
*[shineKind="statedetail"] { font-size: 13px; }
*[shineKind="fieldlabel"]  { font-size: 13px; }
```

> Qt 的 QSS `font-size` 只对 `QLabel`/按钮等部分控件生效，`QTableView` 单元格仍走 item delegate。表格字号由 `type::Apply(table, Role::Body)` 设置控件默认字体解决。

### 2.2 矢量图标

**先决检查（已核实）**：`CMakeLists.txt:17` 只有 `find_package(Qt6 REQUIRED COMPONENTS Widgets)`，**没有声明 `Svg`**。

- **推荐（零依赖）**：用 `QPainterPath` 在 `Glyph.cpp` 里直接画路径。24 网格、1.6px 描边，20 个图标约 300 行，风格统一且不引入新组件。
- **备选**：若确认本机 Qt 安装带 `Qt6Svg`，则新增 `src/ui/kit/icons/svg/*.svg` + `QSvgRenderer`，并把 `CMakeLists.txt:17` 改成 `COMPONENTS Widgets Svg`。代价是打包体积与字体风格漂移风险。

```cpp
// src/ui/kit/icons/Glyph.h
namespace shine::icons {
enum class Glyph {
  Overview, Novel, Asset, Storyboard, Image, Video,   // 活动栏 6 个
  Search, Play, Stop, Palette, Settings, Close,        // 顶栏/通用
  ChevronDown, ChevronRight, Warning, Check, Info, Error,
  Queue, Log, Artifact, Report, Refresh, Add, Run, Pause
};
[[nodiscard]] QIcon Make(Glyph g, int px = 20);   // 24 网格，1.6px 描边，圆头圆角
}
```

设计规则（与 Token 对齐）：

- 网格 24×24，线宽 1.6px，端点/拐角圆角（`Qt::RoundCap` / `RoundJoin`）；
- 描边色 = `theme::Current().textSecondary`；**选中态用 `accent.primary`**；
- 不使用彩色填充（保持与整套单色 Token 体系一致）；
- 图标本身不表达状态，颜色由调用方决定。

### 2.3 接入点替换

| 现有 | 替换为 |
|---|---|
| `IconButton("⌂", …)`（`ActivityRail.cpp:22-26`） | `IconButton::SetGlyph(Glyph::Overview)` 或构造重载 `IconButton(Glyph, tooltip, size)` |
| `Button("🔍  搜索命令")`（`TopBar.cpp:50`） | `Button(Glyph::Search, "搜索命令", …)` |
| `Button("▶ 运行")`（`TopBar.cpp:61`） | `Button(Glyph::Play, "运行", …)` |
| `Button("⏹ 停止")`（`TopBar.cpp:68`） | `Button(Glyph::Stop, "停止", …)` |
| `Button("🎨 主题")`（`TopBar.cpp:77`） | `Button(Glyph::Palette, "主题", …)` |
| `Button("⚙ 设置")`（`TopBar.cpp:86`） | `Button(Glyph::Settings, "设置", …)` |
| `SearchBox` 的 `QLabel("🔍")`（`Inputs.cpp:657`） | `icons::Make(Glyph::Search, 16)` |
| Toast 的 `✔ ! ✕ ℹ`（`Surfaces.cpp:193-196`） | `Check / Pause / Error / Info` |
| `ErrorState` 的 `⚠`（`Feedback.cpp:88`） | `icons::Make(Glyph::Warning, 24)` |
| `EmptyState(icon, …)` 第一参数（`Feedback.h:37`） | 参数类型 `icons::Glyph` |
| 文档标签的 `＋`（`MainWindow.cpp:609`） | `icons::Make(Glyph::Add, 14)` |
| 抽屉关闭的 `×`（`Surfaces.cpp:125`） | `Glyph::Close` |
| `MainWindow.cpp:622` 的 `▾` | `Glyph::ChevronDown` |
| `RightPanel.cpp:22/27` 的 `▾`/`▸` | `Glyph::ChevronDown` / `Glyph::ChevronRight`（旋转 90°） |

### 2.4 emoji 门禁

新增 `tools/check-emoji.ps1`，规则：

- 扫描 `src/` 下 `*.h/*.hpp/*.cpp`；
- 正则匹配 Unicode 区段：`[\uD83C-\uDBFF\uDC00-\uDFFF]`（增补平面，覆盖 U+1F300–U+1FAFF）+ `[\u2600-\u27BF]` + `\uFE0F`；
- **允许注释行**（以 `//` 或 `/*` 开头）；
- 输出命中文件与行号，`exit 1`。

> ⚠️ `⌂ ▾ ▸ ＋ ✓ ＋` 也在 `\u2600-\u27BF` 内，需一并替换；`✔ ✕ ℹ ⚠ ⚙` 同理。

---

## 改动清单

```
新增  src/ui/kit/icons/Glyph.h / Glyph.cpp            （或 svg/ + CMake 资源登记）
新增  src/ui/kit/controls/Typography.h                 （header-only）
新增  tools/check-emoji.ps1
修改  src/ui/kit/theme/QssBuilder.cpp                  字号规则（见 2.1）
修改  src/ui/app/AppEntry.cpp:87-93                   基准字号 → 14px
修改  src/ui/kit/controls/Controls.h/.cpp             IconButton/Button 增加 Glyph 重载
修改  src/ui/kit/controls/Inputs.cpp:657               搜索图标
修改  src/ui/kit/controls/Surfaces.cpp:125,193-196    关闭/Toast 图标
修改  src/ui/kit/controls/Feedback.cpp:41,88           EmptyState/ErrorState 图标
修改  src/ui/pages/shell/{TopBar,ActivityRail,RightPanel,MainWindow}.cpp
修改  src/ui/kit/data/{Panels,Flow}.cpp
修改  src/ui/pages/project/ProjectHubView.cpp
修改  src/ui/pages/novel/{NovelWorkspace,AutoRunPanel,StateDiffView,ReviewView}.cpp
修改  src/ui/verify/gallery/WidgetGalleryView.cpp      画廊补「图标」「排版」两页
修改  CMakeLists.txt                                   仅当新增文件或 Qt Svg
```

## 验收

```powershell
pwsh -File tools/check-emoji.ps1                        # 0 命中
pwsh -File tools/check-colors.ps1
cmake --build build -j 8 --target ShineTVStudio
$env:SHINE_GALLERY_SHOTS = "$PWD\build\gallery"; & .\build\ShineTVStudio.exe --widget-gallery
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\capture_window.ps1 -Out build\_shot.png
```

判据：

1. `gallery-typography.png`：6 档 Role 各一行，字号/字重差异明显，无"同一段文字三种大小"的混用。
2. `gallery-icons.png`：所有图标单色描边，在深色/浅色两套主题下都清晰；100% / 150% / 200% DPI 下无锯齿、无错位。
3. 主窗口截图：活动栏与顶栏无彩色 emoji；图标与文字基线对齐；顶栏按钮不再"图标 + 两空格 + 文字"的拼接感。
4. `check-emoji.ps1` 退出码 0。
5. 抽 3 个页面（总控台 / 小说 / 资产）确认 `setPixelSize` 已全部消失（`grep setPixelSize src/ui/pages` 仅剩 `ProjectHubView` 之外的零星自绘）。

## 风险

| 风险 | 缓解 |
|---|---|
| 基准字号 10pt → 14px 后布局溢出（表格列宽、顶栏按钮） | 逐页截图；`DataTable` 列宽持久化会吸收大部分变化 |
| `Qt6::Svg` 未声明 | 已用 `QPainterPath` 主方案，零依赖（见 2.2） |
| 图标路径手绘风格不统一 | 一次性画完并放进画廊页对照，先定稿再替换调用点 |
| emoji 门禁误伤中文文案 | 只扫 `src/` 下的 C/C++ 字符串，且注释豁免；先跑一次看命中清单再修 |
