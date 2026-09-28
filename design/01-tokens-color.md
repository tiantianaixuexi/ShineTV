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

> 行号已按 `374bd94` 核对；标 ⚠️ 的是本轮**改方案时发现与源码不符**的条目，已改。

| 事实 | 位置 |
|---|---|
| `accent.primary == status.busy`（**四套主题全撞**，不是三套） | `Themes/深空.json:16,24`、`Themes/薄暮.json`、`Themes/纸墨.json`、`Themes/极夜.json` |
| 背景四级明度差 < 2% L* | `Themes/深空.json:4-7` |
| 状态色只有 5 档，缺"排队中" | `src/ui/kit/theme/Token.h:38-42` |
| hover/selected 复用 `bg.elevated`/`bg.panel` | `src/ui/kit/theme/QssBuilder.cpp:194`、`:141` |
| token 数 22 硬编码 6 处 | `Theme.h:35`、`Theme.cpp:143-144`、`QssBuilder.cpp:446,461,470`、`StyleEditorDialog.h:30`、`StyleEditorDialog.cpp:65,78,147` |
| `%N` 是位置编号，插入即错位 | `QssBuilder.cpp:445-456` |
| ⚠️ **原写"`bg.overlay` 同时承担遮罩和浮层底"——不成立**：`%5` 在两张模板里**出现 0 次**；`bg.overlay` 的值是带 alpha 的黑（`#00000099`），语义上其实是遮罩，而遮罩没有任何 QSS 消费点 | `QssBuilder.cpp`（`%5` 计数 0） |
| ⚠️ **原写"`status.busy` 被 QSS 消费"——不成立**：`%21` 在 QSS 里**出现 0 次**，状态色走的是 C++ 侧 `theme::Current().statusBusy`（约 12 处）。加 `status.pending` 时不能照抄 busy 的做法 | `src/ui/pages/novel/*.cpp`、`src/ui/kit/data/*.cpp` |

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
- **新增 `theme::kColorTokenCount = kColorTokenNames.size()`**，`Theme.h` / `Theme.cpp` / `QssBuilder.cpp` / `StyleEditorDialog.{h,cpp}` 的所有 token 数组长度与循环上界一律改用它，魔数 `22` 全部消失（下次追加 token 不用再全仓搜）。
- 4 份主题 JSON 各补 6 个键（缺键会导致 `ColorTokenFromJson` 判定为不完整主题，`Theme.cpp` 的 `hit != kColorTokenCount`）。

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
- hover 填充一律 `%4` → `%24`（`fill.hover`）：`QPushButton` / `QToolButton` / `QHeaderView::section`、kit 的 `button` / `iconbutton` / `segment` / `selectbutton` / `selectitem`。
- 选中填充一律 `%4` → `%25`（`fill.selected`）：`QMenuBar::item:selected` / `QMenu::item:selected` / `QPushButton:checked` / `QToolButton:checked` / 两处 `selection-background-color`、kit `segment[selected="true"]` 与 `iconbutton[active="true"]`。
- 所有 `:focus` 的 `%13`（`accent.primary`）→ `%27`（`line.focus`），让焦点环在四套主题里都有足够对比。
- 只读区底色 `%2` → `%26`（`fill.muted`）：`QLineEdit[readOnly]` 与 kit `input[readOnly]`。
- **⚠️ 已改**：`bg.overlay` 原本"兼作遮罩"的说法与源码不符（见「现状证据」）。落地改为：`%5` 改成**不透明浮层底**并接进浮层类控件（`dialog` / `drawer` / `tooltip` / QSS `QToolTip` / `selectpopup`），遮罩交给新增的 `*[shineKind="scrim"] { background-color: %28; }`。这样 `%5` 与 `%28` 才都有真实消费点。
- 新增 `status.busy` / `status.pending` 的 QSS 消费点：`tag[tone="busy"|"pending"]`、`toast[tone=...]`、`toasticon[tone=...]`。（`status.*` 主要走 C++ 侧 token，QSS 里补的是 Tag / Toast 这类泛用色调位。）

### 1.7 几何 token 顺带收口（同文件，低风险）

| 项 | 现状 | 目标 | 理由 |
|---|---|---|---|
| `radius` | `{3,5,8,12,999}` | `{2,4,6,10,999}` | 按钮/输入 5→4、卡片 8→6，统一到两档 |
| `space` | `{0,2,4,8,12,16,24,32,48}`（9 档） | 补 `6 / 20 / 40` | 当前从 16 直接跳到 24 |
| `font::kSizes` | `{11,12,13,15,18,26}` | `{12,13,14,16,20,28}` | 见 `02-type-icons.md` |

> **⚠️ 已改（与原方案不同，按源码事实）**：原方案写"新增的 5 档必须追加在末尾，否则所有 `kSteps[3]` 之后的取值全部漂移"。核对源码后没有照做 —— 那样会得到 `{0,2,4,8,12,16,24,32,48,6,20,40}`，**非单调**，后续任何二分/追加都会踩坑。
> 实际做法：`kSteps` **原样不动**（9 个既有下标的取值全部不变，`kSteps[3]` 仍是 8），补的 3 档用**命名常量** `space::kXs=6` / `space::kXl=20` / `space::kXxl=40`。既满足"补 6/20/40"和"既有取值零漂移"，又不破坏序列单调性。
>
> `radius` 与 `font::kSizes` 是**纯值调整**，既有调用点（`TopBar.cpp:20`、`StatusBar.cpp:55`、`MainWindow.cpp:605`、`ProjectHubView.cpp:117` 等）按索引取值，下标不变故安全。
>
> **遗留**：QSS 模板里的 `border-radius: 5px / 8px / 12px` 等仍是字面值，本步只改 C++ 常量，QSS 圆角未跟着改（属于"不改 QSS 组织方式"的范围外）。留给 02/03 统一。

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

# 主窗口四主题对照（输出目录必须先建好，否则 pixmap.save 静默失败）
New-Item -ItemType Directory -Force build\tour
$env:SHINE_THEME_TOUR = "$PWD\build\tour"; & .\build\ShineTVStudio.exe

# 检视台按主题各跑一次（SHINE_THEME_SET 会持久化，跑完记得切回 deepspace）
$env:SHINE_THEME_SET = "paperink"; $env:SHINE_GALLERY_SHOTS = "$PWD\build\gal-paperink"
& .\build\ShineTVStudio.exe --widget-gallery
```

判据：

1. `SelfTestRoundTrip` 报告中每套主题 `tokens=28 round-trip=OK qss=OK`，且 `未被消费的 token: none`。
2. 四张主窗口主题图（`build\tour\<主题>.png`）与检视台图中：卡片底色与画布底色**肉眼可区分**；主按钮颜色与"运行中"状态**不再同色**；次级文字比正文明显轻。
3. 切到纸墨（浅色）时，卡片比画布**更亮**（不是更暗）。
4. 表格斑马行与卡片底色可区分。
5. 生成的 QSS 中 28 个占位符**全部被消费**（`SelfCheck` 的"未被消费的 token"为空即通过，不必再手工 grep）。

> ⚠️ 原方案写"四张 `gallery-<主题>-full.png`"——**不存在这种产物**。检视台只按控件出图、且一次只跑当前主题；已改为上面的 `SHINE_THEME_TOUR`（主窗口四主题）+ 按 `SHINE_THEME_SET` 分目录跑检视台。

## 实施结果（2026-09-28 已落地）

- token 表 22 → **28**（尾部追加 6 个，1–22 编号与既有取值未动）；新增 `theme::kColorTokenCount`，全仓 token 魔数清零。
- 四套主题 JSON 各补 6 键并按 1.2–1.5 重调；调色板门禁 `R1–R8` 全通过（相邻背景层 ΔL* ≥ 4、浮层底 ≥ 5、交互填充可见性、文字对比、主按钮前景对比、主色≠运行中色、六态色相互不重复）。
- **`status.busy` 由主色改为紫**（`#A78BFA` / 纸墨 `#5B4FD0`），主色青绿只留给主动作与焦点 —— 四套主题原先 `accent.primary == status.busy` 全部撞色。
- `bg.overlay` 改为**不透明浮层底**并接进 `dialog` / `drawer` / `tooltip` / `selectpopup`；遮罩改用新 token `shadow.scrim`（新增 `*[shineKind="scrim"]`）。
- 自定义主题缺新键不再整体加载失败：`LoadCustomThemes` 以当前内置主题为底做缺键补全，并 `log::Warn` 报缺了几个键。
- `SelfCheck` 加了两条硬门禁：**占位符残留扫描 1..N 全区间**（原来只看 `%1`/`%2`）+ **未被消费的 token 清单**。第二条当场查出 `status.idle` 其实从未被 QSS 消费（`%22` 计数为 0），已补 `tag[tone="idle"]` 与 `progressbar[state="idle"]`。
- 偏离原方案之处均已在正文用 ⚠️ 标出：`space` 补档方式、`bg.overlay` 语义、QSS 圆角字面值未同步（留给 02/03）。

## 风险

| 风险 | 缓解 |
|---|---|
| 漏改某处 `22` → 样式编辑器少几行 / QSS 只替换到 22 | 已用 `kColorTokenCount` 从根上消除魔数；`SelfTestRoundTrip` 的 token 数与"未被消费"检查双保险 |
| 在中间插入 token → 整张 QSS 编号错位 | 只允许尾部追加（已写进 `Token.h` 注释） |
| 浅色主题对比度不足 | 调色板门禁逐项量化（`R4`/`R5`），纸墨主按钮前景对比 4.16 → 4.91 已达标 |
| 用户已存的自定义主题缺新键 → 加载失败 | 已在 `LoadCustomThemes` 落地缺键补全（见「实施结果」） |
| 新 token 没有消费点 → 静默空占 | `SelfCheck` 的"未被消费的 token"检查，本轮已实际拦下一次 |
