---
id: design.p01-tokens-color
kind: design-proposal
status: current
scope: ui-redesign
source_of_truth:
  - src/ui/kit/theme/Token.h
  - src/ui/kit/theme/Theme.cpp
  - src/ui/kit/theme/QssBuilder.cpp
  - src/ui/kit/theme/Themes/深空.json
  - src/ui/kit/theme/Themes/薄暮.json
  - src/ui/kit/theme/Themes/纸墨.json
  - src/ui/kit/theme/Themes/极夜.json
  - src/ui/pages/settings/StyleEditorDialog.cpp
  - tools/check-colors.ps1
  - tools/check-theme.ps1
last_verified: 2026-09-28
---

# 01 · 颜色语义重排 + 背景层级 + 四套主题重调

**工作量**：中（约 300–400 行改动，集中在 `kit/theme`，加 4 个 JSON）
**风险**：中——改了 token 语义会影响所有页面的观感；硬编码的 `22` 必须一次改全
**收益**：最高的一步。后面每一张截图都会立刻变干净。

## 目标

1. 主色只表达"主动作 / 选中 / 焦点"，不再与状态色重叠。
2. 状态轴补齐 6 档，每档一个独立色相，色盲下也能靠形状/文字区分。
3. 背景四级色阶拉开明度差，卡片真的"浮"起来。
4. 补齐交互态专用色（hover / selected / muted 填充），斑马行与悬停不再共用卡片底色。
5. 四套主题按同一套语义表重调，保证换肤后语义不翻转。

## 不做什么

- 不改控件结构、不改布局（那是 02–05）。
- 不改 QSS 的选择器组织方式，只改它引用的 token 编号与语义。
- 不引入渐变、玻璃拟态、阴影堆叠等新风格。

---

## 现状证据

| 事实 | 位置 |
|---|---|
| `accent.primary == status.busy`（三套主题都撞） | `Themes/深空.json:16,24`、`Themes/薄暮.json`、`Themes/纸墨.json` |
| 背景四级明度差 < 2% L* | `Themes/深空.json:4-7` |
| 状态色只有 5 档，缺"排队中" | `src/ui/kit/theme/Token.h:41` |
| hover/selected 复用 `bg.elevated`/`bg.panel` | `src/ui/kit/theme/QssBuilder.cpp:194`、`:141` |
| token 数 22 硬编码 6 处 | `Theme.cpp:143-144`、`QssBuilder.cpp:446,461,470`、`StyleEditorDialog.cpp:65,78,147` |
| `%N` 是位置编号，插入即错位 | `QssBuilder.cpp:447-448` |

---

## 方案

### 1.1 token 表：22 → 28（**只在末尾追加，不改 1–22 的编号**）

`ColorToken` 字段顺序 = `kColorTokenNames` 顺序 = QSS `%N` 顺序（`Theme.cpp:25-30` 的反射）。在中间插入会让整张 QSS 模板的 `%N` 全部错位，因此：

**保留 1–22 原样编号，只追加 23–28。**

| # | 新 token | 用途 | 深空建议值 |
|---|---|---|---|
| 23 | `status.pending` | 排队中（Comfy 队列 / 等待前置阶段） | `#8FBBFF` |
| 24 | `fill.hover` | 行/卡片/列表项悬停底 | `#1A2230` |
| 25 | `fill.selected` | 选中行/当前 tab/选中项底 | `#1B2B31` |
| 26 | `fill.muted` | 斑马行、只读区、次要分区底 | `#141B25` |
| 27 | `line.focus` | 焦点环（可与 accent 不同，浅色主题下更清楚） | `#4BDEC3` |
| 28 | `shadow.scrim` | 浮层遮罩（替代 `bg.overlay` 兼作遮罩的现状） | `#05070BCC` |

改动点：
- `src/ui/kit/theme/Token.h`：结构体尾部加 6 个 `std::uint32_t`；`kColorTokenNames` 尾部加 6 个名字（`"status.pending"`、`"fill.hover"`、`"fill.selected"`、`"fill.muted"`、`"line.focus"`、`"shadow.scrim"`）。
- `Theme.cpp:143-144`：`22` → `28`（两处）。
- `QssBuilder.cpp:446` / `:461` / `:470`：`22` → `28`。
- `StyleEditorDialog.cpp:65` / `:78` / `:147`：`22` → `28`。**更好的做法**：把这三处的魔数换成 `theme::kColorTokenNames.size()`，避免下次再踩。
- 4 份主题 JSON 各补 6 个键（缺键会导致 `ColorTokenFromJson` 判定为不完整主题，`Theme.cpp:200-202`）。

### 1.2 状态轴：6 态 × 6 色相

| 状态 | 语义 | 深空 | 薄暮 | 纸墨（浅色） |
|---|---|---|---|---|
| `status.idle` | 未开始 / 未连接 | `#6E7A8A` | `#8E7F99` | `#8A8071` |
| `status.pending` | 排队中 | `#8FBBFF` | `#9C8CE8` | `#2F6FD0` |
| `status.busy` | 运行中 | `#A78BFA` | `#8FA8FF` | `#5B4FD0` |
| `status.ok` | 完成 | `#6BCB8A` | `#7FCB9A` | `#2E8B57` |
| `status.warn` | 降级 / 需人工 | `#E8C56A` | `#E8C56A` | `#B8860B` |
| `status.danger` | 失败 | `#F07178` | `#F07178` | `#C0392B` |

关键变化：**`status.busy` 从 `#3ECFB2`（= accent）改为紫**，主色青绿 `#35D0B4` 只留给主动作与焦点。

### 1.3 背景四级：拉开明度差

以深空为例，目标相邻层 ΔL* ≥ 4：

| token | 现状 | 目标 | 说明 |
|---|---|---|---|
| `bg.void` | `#0B0E14` | `#090C12` | 窗口底 / 画布外 |
| `bg.surface` | `#121A24` | `#0F141C` | 画布（页面内容底） |
| `bg.panel` | `#182230` | `#161C27` | 卡片 / 表格 / 侧栏 |
| `bg.elevated` | `#1C2733` | `#1F2733` | 弹层 / 悬浮 / hover 兜底 |

纸墨（浅色）走反方向，目标是**面板比画布更亮**：

| token | 现状 | 目标 |
|---|---|---|
| `bg.void` | `#F4F1EA` | `#EFE9DC` |
| `bg.surface` | `#FFFFFF` | `#F7F3EA` |
| `bg.panel` | `#FBF9F4` | `#FFFFFF` |
| `bg.elevated` | `#FFFFFF` | `#FFFFFF` + `shadow.scrim` |

> 浅色主题当前 `bg.surface` 比 `bg.panel` 更亮（`#FFFFFF` vs `#FBF9F4`），层级是反的，务必一起改。

### 1.4 文字三档：拉开次级文字对比

现状 `text.secondary #B7C2D2` 与 `text.primary #E8EEF6` 太接近，次级文字读起来和正文一样重。

| token | 深空现状 | 深空目标 | 规则 |
|---|---|---|---|
| `text.primary` | `#E8EEF6` | `#EAF0F7` | 正文/标题 |
| `text.secondary` | `#B7C2D2` | `#A7B3C4` | 次级说明，ΔL* ≥ 12 |
| `text.muted` | `#8B96A8` | `#79859A` | 占位/禁用，**只在 13px 以下使用** |

### 1.5 主色微调

| token | 深空现状 | 目标 |
|---|---|---|
| `accent.primary` | `#3ECFB2` | `#35D0B4`（降一点饱和，避免大面积按钮刺眼） |
| `accent.primary.hover` | `#57DCC2` | `#4BDEC3` |
| `accent.primary.fg` | `#06231D` | `#05231E` |
| `accent.secondary` | `#F2A65A` | 保持 `#F2A65A`（图表第二序列，不承担状态语义） |
| `accent.info` | `#6FA8FF` | `#6FA8FF` 保持 |

### 1.6 QSS 消费新 token

- `QssBuilder.cpp:141` `alternate-background-color: %3` → `%26`（`fill.muted`），斑马行与卡片底解耦。
- `QssBuilder.cpp:223`、`:230` 等 iconbutton hover 的 `%4`（`bg.elevated`）→ `%24`（`fill.hover`）。
- `QssBuilder.cpp:281` segment `[selected="true"]` 的 `%4` → `%25`。
- 所有 `:focus` 的 `%13`（`accent.primary`）→ `%27`（`line.focus`），让焦点环在四套主题里都有足够对比。
- `bg.overlay` 当前同时承担"遮罩"和"浮层底"，改用 `%28`（`shadow.scrim`）做遮罩，`bg.overlay` 只做浮层底。

### 1.7 几何 token 顺带收口（同文件，低风险）

| 项 | 现状 | 目标 | 理由 |
|---|---|---|---|
| `radius` | `{3,5,8,12,999}` | `{2,4,6,10,999}` | 按钮/输入 5→6、卡片 8→10，统一到两档 |
| `space` | `{0,2,4,8,12,16,24,32,48}`（9 档） | `{0,2,4,6,8,12,16,20,24,32,40,48}`（12 档） | 补 6/20/40，当前从 16 直接跳到 24 |
| `font::kSizes` | `{11,12,13,15,18,26}` | `{12,13,14,16,20,28}` | 见 `02-type-icons.md` |

> **注意**：`space::kSteps` 加长后，现有按索引取值的调用点（`TopBar.cpp:20`、`StatusBar.cpp:55`、`MainWindow.cpp:605` 等）索引不变、值不变，安全；但**新增的 5 档必须追加在末尾**，否则所有 `kSteps[3]` 之后的取值全部漂移。

---

## 改动清单

```
src/ui/kit/theme/Token.h            ColorToken 尾部 +6 字段；kColorTokenNames 尾部 +6 名；radius/space/font 常量
src/ui/kit/theme/Theme.cpp          22 → 28（2 处）；如改用 kColorTokenNames.size() 则一并去魔数
src/ui/kit/theme/QssBuilder.cpp     22 → 28（3 处）；引用新 token 的规则改写（见 1.6）
src/ui/kit/theme/Themes/深空.json    补 6 键 + 按 1.2/1.3/1.4/1.5 重调
src/ui/kit/theme/Themes/薄暮.json    同上（紫/靛系状态色）
src/ui/kit/theme/Themes/纸墨.json    同上（浅色：面板比画布亮）
src/ui/kit/theme/Themes/极夜.json    同上
src/ui/pages/settings/StyleEditorDialog.cpp   22 → kColorTokenNames.size()（3 处）
```

`CMakeLists.txt` 无需改（无新增源文件）。

## 验收

```powershell
pwsh -File tools/check-colors.ps1      # 4 主题齐全 + 无硬编码色值
pwsh -File tools/check-theme.ps1       # JSON 可解析
cmake --build build -j 8 --target ShineTVStudio

# 主题往返自检：token 数、JSON 往返、QSS 生成
$env:SHINE_THEME_SELFTEST = "1"; & .\build\ShineTVStudio.exe

# 四主题对照截图
$env:SHINE_GALLERY_SHOTS = "$PWD\build\gallery"; & .\build\ShineTVStudio.exe --widget-gallery
```

判据：

1. `SelfTestRoundTrip`（`Theme.cpp:207-232`）报告中每套主题 `tokens=28 round-trip=OK qss=OK`。
2. 四张 `gallery-<主题>-full.png` 中：卡片底色与画布底色**肉眼可区分**；主按钮颜色与"运行中"状态**不再同色**；次级文字比正文明显轻。
3. 切到纸墨（浅色）时，卡片比画布**更亮**（不是更暗）。
4. 表格斑马行与卡片底色可区分。
5. `SHINE_QSS_DUMP=1` 导出的 QSS 中 `%23`–`%28` 均被实际使用（不允许占位符空占）。

## 风险

| 风险 | 缓解 |
|---|---|
| 漏改某处 `22` → 样式编辑器少几行 / QSS 只替换到 22 | `SelfTestRoundTrip` 会因 `qss` 自检失败暴露；改完必须跑一次 |
| 在中间插入 token → 整张 QSS 编号错位 | 只允许尾部追加 |
| 浅色主题对比度不足 | 纸墨主题每对"底/文字"按 ≥ 4.5:1 校验；截图逐项过一遍 |
| 用户已存的自定义主题缺新键 → 加载失败 | 自定义主题在 `AppData/ShineTVStudio/themes/`（`ThemeService.cpp:114-118` + `Theme.cpp:285-289`），`ColorTokenFromJson` 对缺键判不完整（`Theme.cpp:200-202`）。需在 `LoadCustomThemes` 加一次迁移：缺键时用**当前内置主题的值补全**（可保留用户原有的 22 个键），或提示用户重建 |
