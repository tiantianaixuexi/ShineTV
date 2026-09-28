---
id: design.p08-gallery-gates
kind: design-proposal
status: current
scope: ui-redesign
source_of_truth:
  - src/ui/verify/gallery/WidgetGalleryView.cpp
  - src/ui/verify/checks/P02Checks.cpp
  - tools/check-layers.ps1
  - tools/check-colors.ps1
  - tools/check-theme.ps1
  - tools/check-i18n.ps1
  - docs/10-modules/ui-kit.md
  - docs/30-engineering/checks.md
  - docs/40-operations/environment.md
  - docs/40-operations/verification.md
last_verified: 2026-09-28
---

# 08 · 设计检视台 + 新增门禁 + 文档回写

**工作量**：中
**风险**：低
**收益**：高——让前 7 步的效果可回归、可防止回退

## 目标

1. 把 `--widget-gallery`（已存在）升级为**完整设计检视台**：令牌色板、排版阶梯、间距阶梯、页面骨架、状态矩阵、外壳缩略、六态与动效对照。
2. 新增/扩展静态门禁，把本方案集里的约定变成机器可查的规则。
3. 建立"改动前后截图对照"的取证流程。
4. 把实现结果回写到 `docs/`（现有文档描述的是旧 UI）。

## 不做什么

- 不引入截图 diff 阈值判定（像素级回归易抖动，先只做人工对照 + manifest 清单）。
- 不把设计稿/PS/Figma 引入仓库。

---

## 现状证据

| 事实 | 位置 |
|---|---|
| 检视台已存在：`--widget-gallery` + `SHINE_GALLERY_SHOTS=<目录>` 逐页截图 | `src/ui/verify/checks/P02Checks.cpp:120-126` |
| 画廊已含控件五态页与四主题全览 | `src/ui/verify/gallery/WidgetGalleryView.cpp:224-228`、`:905-914` |
| 动效对照截图已生成 | `WidgetGalleryView.cpp:933-940` |
| 现有门禁 4 个 | `tools/check-layers.ps1`、`check-colors.ps1`、`check-theme.ps1`、`check-i18n.ps1` |
| `check-colors.ps1` 实际只拦 `setStyleSheet`/`QColor` + 主题数量 | `tools/check-colors.ps1:4-7` |
| 截图脚本的三个已知坑已写进注释 | `scripts/capture_window.ps1:9-14` |
| 文档描述的是旧 UI（22 个颜色 token、"七区布局"表述） | `docs/10-modules/ui-kit.md:24`、`:87` |

---

## 方案

### 8.1 检视台新增页

`WidgetGalleryView::BuildAll`（`WidgetGalleryView.cpp:224`）新增 6 页：

| 页 | 内容 | 判定用途 |
|---|---|---|
| `gallery-tokens` | 28 个颜色 token × 4 主题的色板矩阵，每格显示名字 + hex + 对比度 | 01 |
| `gallery-type` | 6 档 Role（Display/H1/H2/H3/Body/Caption）各一行，含中英混排样例 | 02 |
| `gallery-space` | space 12 档 + radius 5 档的可视标尺 | 01 |
| `gallery-scaffold` | PageScaffold 的三种形态（标准页 / 宽表页 / 空态页） | 03 |
| `gallery-matrix` | StateMatrix 6 态 × 灰度版本 + 悬停/选中态 | 06 |
| `gallery-shell` | 外壳缩略（活动栏 72px、分栏比例、右栏折叠三态） | 04 |

`GrabAllPages`（`WidgetGalleryView.cpp:191-220`）已按页名存图，无需改逻辑。

### 8.2 新增门禁

| 脚本 | 规则 | 拦截的问题 |
|---|---|---|
| `tools/check-emoji.ps1` | `src/` 下 C/C++ 源码字符串中的 emoji 区段（U+1F300–1FAFF、U+2600–27BF、U+FE0F），注释豁免 | 02 引入 |
| `tools/check-ui.ps1` | ① `src/ui/pages` 中 `new QTableWidget` / `new QTabWidget`；② 页面顶层 `setContentsMargins(0,0,0,0)`；③ `QMessageBox`；④ `QLabel(QStringLiteral("暂无` / `「P0x 接入`类占位文案 | 03/05/06/07 引入 |

两个脚本都遵循既有约定：`$ErrorActionPreference='Stop'`、命中即打印 `文件:行` 并 `exit 1`、**纯 ASCII 输出**（`scripts/capture_window.ps1:14` 记录了 PowerShell 5.1 的编码坑）。

接入方式：`pwsh -File tools/check-layers.ps1` 之后手动串行执行，或在 `docs/30-engineering/checks.md` 的表格里补两行并说明何时运行。

### 8.3 取证流程（每步都走一遍）

```powershell
# 1) 静态门禁
pwsh -File tools/check-layers.ps1
pwsh -File tools/check-colors.ps1
pwsh -File tools/check-theme.ps1
pwsh -File tools/check-emoji.ps1
pwsh -File tools/check-ui.ps1

# 2) 构建
cmake --build build -j 8 --target ShineTVStudio

# 3) 检视台（令牌/排版/骨架/矩阵）
$env:SHINE_GALLERY_SHOTS = "$PWD\build\gallery"
& .\build\ShineTVStudio.exe --widget-gallery

# 4) 主窗口（真实交互路径）
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\capture_window.ps1 -Out build\_shot.png
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\crop_zoom.ps1 -In build\_shot.png -X 0 -Y 0 -W 900 -H 520 -Out build\_crop.png

# 5) 既有评审包（回归）
$env:SHINE_P09_REVIEW = "$PWD\build\p09"; & .\build\ShineTVStudio.exe
```

每次交付记录：`命令 / 工作目录 / 退出码 / 关键输出 / 是否启动真实 UI / 是否依赖外部服务`（`docs/40-operations/verification.md` 的格式）。

**对照清单**（人工过一遍，附在交付说明里）：

- [ ] 深色 + 浅色两主题下，卡片与画布可区分
- [ ] 主色与"运行中"不同色
- [ ] 6 态在灰度下可区分
- [ ] 7 个页面页头规格一致
- [ ] 活动栏不 hover 可读
- [ ] 空态都有下一步
- [ ] 焦点环可见
- [ ] 1280 / 1600 / 1900 三种宽度无溢出、无空洞

### 8.4 文档回写

| 文档 | 更新内容 |
|---|---|
| `docs/10-modules/ui-kit.md` | token 数 22 → 新数；新增 `fill.*`/`status.pending`/`line.focus`；控件清单补 `PageScaffold`/`StatusDot`/`Confirm`/`SectionHeader`/`StateMatrix`/`Glyph`；"已知重复实现"表按收敛结果更新 |
| `docs/30-engineering/checks.md` | 门禁表补 `check-emoji.ps1`、`check-ui.ps1` |
| `docs/40-operations/environment.md` | 若检视台新增 CLI 行为，同步；`--widget-gallery` 已记录（`:63`），核对无误 |
| `docs/00-overview/product.md:21` | 「顶栏、活动栏、侧栏、中央文档页、右侧检查器、底部队列和状态栏」→ 与 04 后的实际结构一致 |
| `design/` | 全部方案执行后在本目录顶部 README 标注「已实施 / 已废弃」，不留计划壳 |

> `AGENTS.md` 要求"删除文档后不留失效路径或兼容壳"。本方案执行完后，`docs/` 里凡引用旧 UI 结构、旧 token 数、旧栏宽的段落必须同步改，不保留"旧版说明"。

---

## 改动清单

```
修改  src/ui/verify/gallery/WidgetGalleryView.cpp   新增 6 页
修改  src/ui/verify/checks/P02Checks.cpp            如新增页需要额外参数
新增  tools/check-emoji.ps1
新增  tools/check-ui.ps1
修改  docs/10-modules/ui-kit.md
修改  docs/30-engineering/checks.md
修改  docs/00-overview/product.md
修改  design/README.md                               顶部加实施状态
```

## 验收

```powershell
pwsh -File tools/check-emoji.ps1    # 退出码 0
pwsh -File tools/check-ui.ps1      # 退出码 0
$env:SHINE_GALLERY_SHOTS = "$PWD\build\gallery"; & .\build\ShineTVStudio.exe --widget-gallery
```

判据：

1. 画廊输出 6 张新页，且 `gallery-tokens.png` 中 4 主题 × 28 token 全部有色块与 hex 标注。
2. 两个新门禁能**故意制造违规并被拦住**（手动塞一行 emoji / `new QTableWidget`，确认退出码 1，再撤销）。
3. `SHINE_QSS_PROBE`、`SHINE_P03_REVIEW`、`SHINE_P09_REVIEW`、`SHINE_P10_S1` 全部退出码 0。
4. `docs/` 中不存在与新 UI 矛盾的旧描述（人工 grep `240`、`56`、`22 个` 等旧值）。

## 风险

| 风险 | 缓解 |
|---|---|
| 新门禁误伤既有合规代码 | 先只报告不失败跑一轮，把清单贴出来再开启 `exit 1` |
| 画廊页越来越多、难找 | 按主题分组（令牌/排版/控件/数据/外壳），页名带序号前缀 |
| 截图目录堆在 `build/` 不入库，无法长期对照 | 每次交付把关键 3–5 张贴进交付说明；不提交二进制 |
