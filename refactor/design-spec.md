---
id: refactor.design-spec
kind: contract
status: current
source_of_truth:
  - webui/src/styles/tokens.css
  - webui/src/styles/base.css
  - webui/src/styles/shell.css
  - webui/src/styles/ui.css
  - webui/src/styles/views.css
  - webui/src/components/UI.jsx
  - webui/src/components/Icon.jsx
  - webui/src/shell/Shell.jsx
  - src/ui/imgui/theme/Tokens.h
last_verified: 2026-09-30
---

# webui 1:1 视觉规范（ImGui 实现的唯一依据）

> 本篇是"1:1"的判据来源。每条数值都标了 webui 源文件行号。
> 实现时凡与本文冲突，以 webui 源码为准，本文是它的索引。

---

## 1. 主题无关几何（直接写进 `ImGuiStyle`）

| Token | 值 | 来源 |
|---|---|---|
| `--r-xs` | 4 | `tokens.css:9` |
| `--r-sm` | 6 | `tokens.css:10` |
| `--r-md` | 10 | `tokens.css:11` |
| `--r-lg` | 14 | `tokens.css:12` |
| `--r-xl` | 18 | `tokens.css:13` |
| `--r-pill` | 999 | `tokens.css:14` |
| `--sp-1…7` | 4 / 8 / 12 / 16 / 24 / 32 / 48 | `tokens.css:16-22` |
| `--dur-1/2/3` | 120 / 200 / 320 ms | `tokens.css:24-26` |
| `--ease` | `cubic-bezier(0.2, 0, 0, 1)` | `tokens.css:27` |
| `--ease-out` | `cubic-bezier(0.16, 1, 0.3, 1)` | `tokens.css:28` |

圆角用得很规矩，不是随手写的：控件/输入框/按钮 = `r-sm`(6)，卡片/面板 = `r-md`(10)，
弹窗/浮层 = `r-lg`(14)，胶囊（tag/chip/progress/switch）= `r-pill`，小构件（dot/kbd/树行）= `r-xs`(4)。
仅两处例外：品牌标 6px（`shell.css:41`）、色板 4px（`shell.css:151`）。

### 1.1 字号刻度（CSS 实测出现过的全部值）

| px | 用途 |
|---|---|
| 10.5 | kbd、`tag.sm`、`chip.cnt`、甘特格、表头标签 |
| 11 | 命令面板页脚、步骤序号 |
| 11.5 | tag、`th`、`dot-meta`、日志行、k-label |
| 12 | 等宽工具、面板标签、侧栏 meta |
| 12.5 | small、树、kv、toast、树行 |
| 13 | **btn / table body / input**（最常用） |
| 13.5 | card-title、浮层面板标题 |
| 14 | `btn.lg`、抽屉头 |
| 14.5 | 项目卡名 |
| 15 | 弹窗标题、命令面板输入 |
| 17 / 18 | 区块 h3 / 视图大标题 |
| 20 | 章节标题 |
| 24 | KPI 数值 |
| 26 | 项目中心标题 |

字重：500（kv 值）、600（多数控件标签）、700（卡片/弹窗标题）、
**800（视图大标题、中心标题、KPI 数值、章节标题）**。

行高：全局 1.6（`base.css:12-16`）；正文 `.draft` 1.9、`.md-view` 1.9、
`.pfoot` 1.7、`.json-view` 1.75、Beat 编辑器 1.8。

字距：`0.01em`（品牌/中心标题）、`0.02em`（面包屑/视图头）、
`0.03em`（表头）、`0.05em`（k-label）、`0.08em`（命令面板分组）。

> ⚠️ `Tokens.h:102-114` 的 `font::kSizes = {12,13,14,16,20,28}` 是**取整档**，
> 和 webui 的刻度**不是一一对应**。ImGui 侧直接用本节实测值，不走 `kSizes`；
> 真正建进字体图集的是 `kit/Fonts.cpp` 顶部那张字号表（只建常用档，
> 未建档由 `FontAt()` 向上取最近一档 ⇒ 表里缺的档会**画大**，见 §9 的已知偏差清单）。

---

## 2. 颜色

### 2.0 权威在哪 / 改哪里（先读这条）

⚠️ **`webui/` 不在版本控制里**（`.gitignore` 最后一行就是 `webui/`，它只是本地设计稿）。
新 clone 拿不到 `tokens.css`。因此**下面的表就是唯一可复现的色值记录** ——
`tokens.css` 丢了也不影响实现与验收。

改颜色只有三个入口：

| 改什么 | 改哪个文件 | 怎么验证 |
|---|---|---|
| 某主题的某个色值 | `src/ui/imgui/theme/Themes/<主题名>.json` 的 `colors`（恰好 31 项） | `SHINE_THEME_SELFTEST=1` 跑一次，看 `theme selftest: PASS` |
| 色 token 的**名字 / 数量 / 顺序** | `src/ui/imgui/theme/Tokens.h` 的 `struct ColorToken` + `kColorTokenNames` | `powershell -File tools\check-theme.ps1` |
| 由色值算出的淡底 / 淡边 / 投影 | `src/ui/imgui/theme/Theme_Derived.cpp` | `tools\check-theme.ps1` |

⚠️ 三处**不许动**：`kColorTokenNames` 的顺序 == `ColorToken` 字段顺序 == `ApplyTheme()` 的映射顺序，
中间插一项会让整表错位且**不报编译错**（`Tokens.h:8-10`）；JSON 少写一项该主题直接
`loaded=false` 回落默认主题而**不报错**；`Theme_Derived.cpp` 的 `gateBg`/`checkRowBg`
按纯 alpha 算的旧口径**已冻结**（`Theme.h:116-120`）。

原始色值在 `tokens.css`（深空 `:7-73`、薄暮 `:75`、纸墨 `:116`、水墨 `:157`、极夜 `:210`）。

⚠️ **token 数量以 `Tokens.h:71-79` 的 31 为准**。`tokens.css:3` 的注释写"28 项"已过时，
`webui/src/shell/Shell.jsx:791-798` 的样式编辑器也只列了 28 个（少 `shadow.1/2/accent`）。

### 2.0.1 CSS 变量名 ↔ token 名（31 项，逐条同序）

`--bg-void`→`bg.void`、`--bg-surface`→`bg.surface`、`--bg-panel`→`bg.panel`、
`--bg-elevated`→`bg.elevated`、`--bg-overlay`→`bg.overlay`、
`--line-subtle`→`line.subtle`、`--line-normal`→`line.normal`、`--line-strong`→`line.strong`、
`--text-primary`→`text.primary`、`--text-secondary`→`text.secondary`、
`--text-muted`→`text.muted`、`--text-inverse`→`text.inverse`、
`--accent`→`accent.primary`、`--accent-h`→`accent.primary.hover`、
`--accent-fg`→`accent.primary.fg`、`--accent-2`→`accent.secondary`、`--info`→`accent.info`、
`--ok`→`status.ok`、`--warn`→`status.warn`、`--danger`→`status.danger`、
`--busy`→`status.busy`、`--idle`→`status.idle`、`--pending`→`status.pending`、
`--fill-hover`→`fill.hover`、`--fill-selected`→`fill.selected`、`--fill-muted`→`fill.muted`、
`--line-focus`→`line.focus`、`--scrim`→`shadow.scrim`、
`--shadow-1`→`shadow.1`、`--shadow-2`→`shadow.2`、`--shadow-accent`→`shadow.accent`。

### 2.0.2 31 项 × 5 主题全表

下表是 **`src/ui/imgui/theme/Themes/*.json` 的实际值**（= C++ 侧真正画出来的颜色），
顺序 == `kColorTokenNames`。`类型` 列标注 CSS 侧原式的形态：

- `hex` = CSS 也是不透明实色，**逐位可对**。
- `rgba` = CSS 是 `rgba(...)` 半透明，JSON 存的是**摊平到 `--bg-surface` 之后的等效实色**。
  ⚠️ 摊平只有效于一种底（见 §2.0.3），这是**已知偏差**。
- `阴影` = CSS 是一条完整 `box-shadow` 表达式，JSON 只存其中的**色调**；
  几何（dx/dy/blur/逐层 alpha）在 `src/ui/imgui/theme/Theme.cpp` 的 `kShadowSpecs` 里逐主题硬编码。
- `派生` = CSS 是 `color-mix()` / `linear-gradient()`，JSON 存预混结果。

| # | token | 类型 | 深空 | 薄暮 | 纸墨 | 水墨 | 极夜 |
|---|---|---|---|---|---|---|---|
| 1 | `bg.void` | hex | `#090C12` | `#140E1A` | `#EBE4D4` | `#E9E6DD` | `#000000` |
| 2 | `bg.surface` | hex | `#111825` | `#1F1729` | `#F7F3EA` | `#F4F1E8` | `#0C1016` |
| 3 | `bg.panel` | hex | `#1A2230` | `#2C2036` | `#FFFFFF` | `#FAF8F1` | `#151A21` |
| 4 | `bg.elevated` | hex | `#222B39` | `#362841` | `#FFFFFF` | `#FFFFFF` | `#1D242C` |
| 5 | `bg.overlay` | hex | `#28313F` | `#3D2D49` | `#FFFFFF` | `#FFFFFF` | `#232B34` |
| 6 | `line.subtle` | ⚠️ rgba | `#202734` | `#2E2637` | `#E6E2D9` | `#E1DED5` | `#1E2228` |
| 7 | `line.normal` | ⚠️ rgba | `#2D3440` | `#3A3244` | `#D7D3CB` | `#D0CDC5` | `#2B2F35` |
| 8 | `line.strong` | ⚠️ rgba | `#414853` | `#4D4556` | `#BFBCB4` | `#B4B1AA` | `#454A50` |
| 9 | `text.primary` | hex | `#EAF0F7` | `#F2E9F5` | `#211E19` | `#1F1C18` | `#E8EEF4` |
| 10 | `text.secondary` | hex | `#A7B3C4` | `#C7B6D2` | `#5B5346` | `#4A453D` | `#9BA8B5` |
| 11 | `text.muted` | hex | `#79859A` | `#8E7D99` | `#786E60` | `#7C766A` | `#66717F` |
| 12 | `text.inverse` | hex | `#05231E` | `#2A1608` | `#F7F3EA` | `#F4F1E8` | `#01251E` |
| 13 | `accent.primary` | hex | `#35D0B4` | `#F2A65A` | `#0C8577` | `#2F2C28` | `#3ECFB2` |
| 14 | `accent.primary.hover` | hex | `#4BDEC3` | `#FFB974` | `#0AA08E` | `#1C1A17` | `#5FE0C6` |
| 15 | `accent.primary.fg` | hex | `#05231E` | `#2A1608` | `#FFFFFF` | `#F4F1E8` | `#01251E` |
| 16 | `accent.secondary` | hex | `#F2A65A` | `#E86A8B` | `#C2502F` | `#B03A2E` | `#F2A65A` |
| 17 | `accent.info` | hex | `#6FA8FF` | `#9A8CFF` | `#3B6FD4` | `#4E6E8E` | `#6FA8FF` |
| 18 | `status.ok` | hex | `#6BCB8A` | `#7FCB9A` | `#2E9E5B` | `#4A7C59` | `#6BCB8A` |
| 19 | `status.warn` | hex | `#E8C56A` | `#F2C94C` | `#B7791F` | `#A8842C` | `#E8C56A` |
| 20 | `status.danger` | hex | `#F07178` | `#F07178` | `#D64550` | `#A83A32` | `#F07178` |
| 21 | `status.busy` | hex | `#A78BFA` | `#C39BFF` | `#7C5CD6` | `#7A6890` | `#A78BFA` |
| 22 | `status.idle` | hex | `#6E7A8A` | `#8A7E93` | `#8A8578` | `#8B8578` | `#5F6B7A` |
| 23 | `status.pending` | hex | `#8FBBFF` | `#9FB7FF` | `#3B6FD4` | `#4E6E8E` | `#8FBBFF` |
| 24 | `fill.hover` | ⚠️ rgba | `#1F2836` | `#3A2B47` | `#EAE6DD` | `#E7E4DC` | `#161D26` |
| 25 | `fill.selected` | ⚠️ rgba | `#1F2F39` | `#453353` | `#DBE6DC` | `#DAD7CF` | `#12222A` |
| 26 | `fill.muted` | ⚠️ rgba | `#17202D` | `#251B2F` | `#EEEAE2` | `#EAE7DF` | `#0F141B` |
| 27 | `line.focus` | hex | `#4BDEC3` | `#FFB974` | `#0AA08E` | `#1C1A17` | `#5FE0C6` |
| 28 | `shadow.scrim` | 阴影 | `#05070BB8` | `#0A060EB8` | `#1E180E73` | `#18151180` | `#000000C7` |
| 29 | `shadow.1` | 阴影 | `#0000004D` | `#00000052` | `#3C321E1A` | `#322C241A` | `#00000080` |
| 30 | `shadow.2` | 阴影 | `#00000073` | `#00000080` | `#3C321E29` | `#322C242E` | `#000000B3` |
| 31 | `shadow.accent` | 阴影 | `#35D0B447` | `#F2A65A47` | `#0C857740` | `#2F2C2838` | `#3ECFB242` |

**CSS 原式**（上表 ⚠️ 与 阴影 四类，供摊平口径复核；深空为参考，其余主题同结构换色）：

| token | CSS 原式（深空） |
|---|---|
| `line.subtle` | `rgba(234,240,247,0.07)` |
| `line.normal` | `rgba(234,240,247,0.13)` |
| `line.strong` | `rgba(234,240,247,0.22)` |
| `fill.hover` / `fill.selected` / `fill.muted` | `#1F2836` / `#1F2F39` / `#17202D`（深空是实色；**浅色三主题才是 rgba**：纸墨 `rgba(33,30,25,.06/.12/.04)`、水墨 `rgba(31,28,24,.06/.13/.045)`） |
| `shadow.scrim` | `rgba(5,7,11,0.72)` |
| `shadow.1` | `0 1px 2px rgba(0,0,0,.35), 0 4px 16px rgba(0,0,0,.30)` |
| `shadow.2` | `0 12px 40px rgba(0,0,0,.45)` |
| `shadow.accent` | `0 4px 20px rgba(53,208,180,.28)` |

### 2.0.3 已知偏差：`line.*` 的摊平口径（审计 #3，未修）

CSS 的 `--line-subtle` 是 `rgba(...,0.07)` —— 它**叠在谁身上，结果就随谁变**。
JSON 只能存一个不透明值，于是按「叠在 `--bg-surface` 上」摊平：

```
深空 0.07 × rgb(234,240,247) + 0.93 × rgb(17,24,37) = #202734   ← JSON 里的值
深空 0.07 × rgb(234,240,247) + 0.93 × rgb(26,34,48) = #29303E   ← 卡片（bg-panel）上应有的值
```

所以**同一根卡片描边在面板底与浮层底上亮度不同**，实现画出来是一律 `#202734`。
修法是让 `Derived` 按底预混多份（或恢复运行时 alpha 叠加），属于主题层改动；
在修之前，验收这类描边**按「有没有画出来 + 明暗顺序对不对」判，不按像素值判**。
另：`Theme_Derived.cpp` 已新增 `stageDoneBg` / `stageFailBg` / `gateFailBg` 三个**实色**预混
（`gateBg`/`checkRowBg` 的旧口径已冻结），新代码优先用它们。

### 2.1 必须预混的派生色（`color-mix` 的等价物）

webui 大量用 `color-mix(in srgb, var(--X) N%, transparent)` 做淡底/淡边。
ImGui 没有合成助手，**主题表必须预混出这些**：

| 派生色 | 配方 | 用在哪 |
|---|---|---|
| `tagBg[tone]` | tone 色 12% 透明 | `ui.css:139` |
| `tagBorder[tone]` | tone 色 35% | `ui.css:140` |
| `stageRunBg` | accent 14% | `views.css` stage node |
| `gateBg` | tone 色 10% | `views.css:658` |
| `ganttCell[tone]` | tone 色 12% | `views.css:496` |
| `jumpBtnBg` | accent 22% | `shell.css:647` |
| `checkRowBg[tone]` | tone 色 10% | `views.css` checklist |
| `accentDim` | accent 14% | `tokens.css:68` 等 |
| `accentGlow` | accent 30% | `tokens.css:65` |
| `inputFocusRing` | accent 14%（`box-shadow 0 0 0 3px`） | `ui.css:315` |
| `btnPrimaryShadow` | accent 28% | `ui.css:70` |
| `dangerBg` | danger 12% | `ui.css:77` |

**每套主题都要算一遍**，写进主题 JSON 的 `derived` 段。

### 2.2 阴影

| Token | 深空值 | 来源 |
|---|---|---|
| `--shadow-1` | `0 1px 2px rgba(0,0,0,.35), 0 4px 16px rgba(0,0,0,.3)` | `tokens.css:67` |
| `--shadow-2` | `0 12px 40px rgba(0,0,0,.45)` | `tokens.css:68` |
| `--shadow-accent` | `0 4px 20px rgba(53,208,180,.28)` | `tokens.css:69` |
| `--scrim` | `rgba(5,7,11,.72)` | `tokens.css:61` |
| `--glass` | `rgba(16,22,33,.82)` | `tokens.css:64` |

> ImGui **没有 box-shadow**。做法：卡片/浮层用「底色 + 1px 描边 + 上方 1px 高光线」近似；
> `--shadow-2` 的浮层额外加一圈 `shadowScrim` 描边。这是**已知降级**，验收时按"有没有浮起来"判，
> 不按阴影像素判。

---

## 3. 图标：46 个，24 网格 / 1.6 描边 / 圆头圆角

`webui/src/components/Icon.jsx:3-48`，每个图标是**一条 SVG `d` 路径字符串**，
`viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6"`。

全部名称：

```
gauge book masks clapper image film play stop search settings plus x
chevron chevdown check alert info layers text chip wave aperture encode
grid refresh folder palette terminal list sparkles wand dots download
upload eye compare link zap panel panelL dock moon target clock users bolt2
```

**ImGui 落法**（推荐顺序）：
1. 把 46 条 `d` 路径转成**矢量字体**（FontForge / `svgtofont`），栅格化成 ImGui 字体图集，
   base size 16，灰度抗锯齿 → 一次 `ImFont::AddFontFromFileTTF` 解决全部图标。
2. 或：每条路径在 `24×24` 单位空间里直接用 `ImDrawList::PathLineTo/PathStroke2D` 画，
   线宽 1.6，颜色 = `ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_Text))`。
   好处是"随主题变色"天然成立（`currentColor` 语义）；代价是每次绘制有路径求值开销。

**尺寸档**（CSS 实测）：默认 16、导航栏 19、按钮内 15（小 13）、图标按钮 16（小 13）、
命令面板条目 15、空态大字 24、树/分隔 12、kbd/关闭 10。

⚠️ 两处注意：
- `Gallery.jsx:140` 传了未定义的 `icon="flow"`，被 `Icon.jsx:52` 的兜底 `P.info` 吞掉 →
  **不要照抄这个 bug**；补一个 `flow` 图标或改用 `chip`。
- `x` 被当作"减号"用（`FlowCanvas.jsx:135`、`Gallery.jsx:91`）→ **补一个真正的减号**。

---

## 4. 应用外壳尺寸（`shell.css` + `Shell.jsx`）

```
TopBar      h=46   玻璃 + 1px 下边框     shell.css:19
├─ Rail      w=56   --bg-surface + 右边框  shell.css:157
├─ SidePanel w=240  --bg-surface, padding 12px 10px  shell.css:572
├─ Center    flex:1
│  ├─ Crumbs  h=34                          shell.css:223
│  └─ View    flex:1 overflow:auto           shell.css:322
└─ Inspector w=280  --bg-surface + 左边框     shell.css:335
Dock         h=190  （CSS 默认 200，内联覆盖 190）  Shell.jsx:208
StatusBar    h=26                          shell.css:445
```

### 4.1 顶栏内容（`Shell.jsx:24-79`）

品牌标 22×22 / r6 / `grad-accent` + `shadow-accent` + 内嵌 12px play 图标；
文字 `ShineTV ` + 渐变 `Studio`（13.5/700）。
项目胶囊 h28 r6，含 8×8 渐变点（`0 0 6px accent-glow`）+ 名字（max-w 140）+ 10px 下箭头。
搜索触发器 260×28 胶囊，右侧 `Kbd` "Ctrl K"。
右侧：`运行`(primary + play) / `停止`(secondary + stop)、主题菜单（宽 224，每行带 26×16 渐变色板 +
分隔线 + "样式编辑器…" + "减少动效"）、设置图标钮、Comfy 状态点。

### 4.2 导航栏（`shell.css:169-202`）

按钮 40×40 / r10，图标 19px，`--text-muted`；
hover = `--fill-hover` + primary 文字；
选中 = `--fill-selected` + accent 文字 + **左侧 2.5px accent 竖条**
（`left:-8px`，上下内缩 9px，r3，`0 0 8px accent-glow`）。

| 序 | 项 | 图标 | 目标 |
|---|---|---|---|
| 1 | 总控 | `gauge` | pipeline |
| 2 | 小说 | `book` | novel |
| 3 | 资产 | `masks` | assets |
| 4 | 分镜 | `clapper` | storyboard |
| 5 | 出图 | `image` | imageflow |
| 6 | 出片 | `film` | videoflow |
| — | 分隔线 24×1 | | `Shell.jsx:99` |
| 7 | 侧栏开关 | `panelL` | Ctrl+B |
| 8 | 底栏开关 | `terminal` | Ctrl+J |
| 9 | 检查器开关 | `panel` | Ctrl+I |
| — | 弹性空隙 | | `Shell.jsx:109` |
| 10 | 组件画廊 | `grid` | `hidden: true`，只在这一栏出现 |

> ⚠️ `Shell.jsx:100` 的侧栏开关 `.active` 判断是反的（面板**关**时才点亮）——
> 别照抄，按正常语义做（开 = 点亮）。
> ⚠️ 本表是导航栏的**唯一依据**（旧的 Qt 前端已整体删除）。
> 落地在 `src/ui/imgui/pages/Shell_Chrome.cpp`，图标走 `kit::Icon.h` 的矢量路径（§3）。
> ⚠️ 「总控」这一个词在代码里有三份字符串，**别当重复去合并**：导航栏项 = `总控`
> （`Shell.cpp:82`）、视图大标题 = `全流程总控台`（`Shell.cpp:95`，§7.1 的 `.vw-title`）。
> 第三份在 `Shell_Chrome.cpp:482`（写的是「总控台」），**与导航栏不一致，待订正**。

### 4.3 面包屑（34px）

`项目名 › 工作区 › 标签`（`Shell.jsx:118-141`）。当前项 primary/600，分隔符 `›` 透明度 0.55。
右侧提示串：`Ctrl+B 侧栏 · Ctrl+J 底栏 · Ctrl+I 检查器 · Ctrl+K 命令`。

### 4.4 状态栏（26px，`Shell.jsx:312-336`）

左→右：Comfy 状态点 + 文字 · "LLM 就绪" · "队列 N" · 弹性空隙 · 运行中显示阶段名 ·
96px 细进度 + `T{n}/17 · {pct}%` · "主题：{name}" · "v0.2.0"。
每项 h20 r4 padding 0 8，hover `--fill-hover`。

### 4.5 底栏（`Shell.jsx:196-309`）

4 个标签：任务队列 / 日志 / 产物 / 校验报告 + 关闭钮。
日志行：等宽 11.5px，纵向 padding 1.5px，级别色 ok/warn/err，运行中尾部追加 7×12 的
闪烁 accent 光标块。产物行点开 `FileViewer`。校验报告点开 640×min(74vh,660) 弹窗。

---

## 5. 组件套件（`webui/src/components/UI.jsx` 20 个 + `StageFlow.jsx` 2 个）

> `UI.jsx:2` 的文件头就写着「UI 基础组件（对应 Qt 端 kit::widgets 语义）」——
> **这就是 ImGui 组件的实现契约**，不是 Qt 那 32 个 widget 逐个翻译。

| # | 组件 | 关键尺寸 | ImGui 落法 |
|---|---|---|---|
| 1 | `Button` | h30 / pad 0 14 / r6 / 13px / 600；sm h24 pad 0 10 12px；lg h36 pad 0 20 14px r10；图标 15（sm 13）；`:active scale(.97)`；disabled opacity .45；4 变体 primary/secondary/ghost/danger | `ImGui::Button` + 自绘背景圆角 + 状态偏移 |
| 2 | `IconBtn` | 28×28 r6（sm 22×22），图标 16（sm 13）；hover fill-hover；`.active` fill-selected+accent | 同上，纹理用第 3 节图标字体 |
| 3 | `Tag` | h20 pad 0 8 **r-pill** 11.5/600 1px 边；sm h17 pad 0 6 10.5；7 色调；可选 7px `currentColor` 圆点；busy 调加脉冲 | 自绘 |
| 4 | `StatusDot` | 7×7 圆；7 色调；`run` 时 1.6s 脉冲环 | `ImDrawList::AddCircleFilled` |
| 5 | `Kbd` | min-w18 h18 pad 0 5 r4，1px 边 + **下边 2px** | 自绘 |
| 6 | `Card` | bg-panel + 1px line-subtle + r10；头 padding 12/16 + 下边框；体 padding 16；`hover` = line-strong + 上浮 2px；`glow` = accent 边 + accent 辉光 | `ImGuiChildFlags_Borders` + 自绘 |
| 7 | `Segmented` | 容器 pad3 gap2 fill-muted r6 1px；项 h26 pad 0 13 r4 12.5/600；选中 bg-elevated + shadow + **4px accent 圆点** | 自绘（ImGui 原生没有分段控件） |
| 8 | `Tabs` | 项 pad 8/12 13/600；选中 accent + **2px 下划线（左右各内缩 10px）** | `ImGuiTabBar` 需改样式，或自绘 |
| 9 | `Field` | 竖排 gap6；标签 12px/600 secondary；help 12px muted | 自绘 |
| 10 | `Input` | h30 pad 0 10 r6 fill-muted 1px line-normal；focus = line-focus 边 + **`0 0 0 3px accent-dim` 外环** | `ImGui::InputText` + 改样式 |
| 11 | `TextArea` | 同 Input，自适应高，pad 8/10，行高 1.6 | `ImGui::InputTextMultiline` |
| 12 | `Select` | 原生 select + 自绘 10×6 箭头 | `ImGui::Combo` |
| 13 | `Switch` | **34×19 r-pill**，旋钮 13×13 位移 15px | 自绘 |
| 14 | `Checkbox` | 盒 15×15 r4 1.5px 边 + 内联对勾 | `ImGui::Checkbox` 改样式 |
| 15 | `Progress` | 轨 h6（thin 4）r-pill；填充 **`grad-accent`**；`run` 时叠 100° 白 35% 微光扫过 | 自绘（渐变填充 ImGui 原生不支持） |
| 16 | `Empty` | 居中 gap10 pad 40/20；字形 **52×52 r14 虚线边** + 24px 图标 + 4s 上下浮动；标题 13/600；正文 max-w 320 | 自绘 |
| 17 | `KV` | 网格 `auto 1fr` gap `6px 14px` 12.5；键 muted，值 primary/500 | 自绘 |
| 18 | `Art` | **程序化 SVG 占位画**，12 组调色板按 `|seed| % 12`；viewBox 160×100；竖直渐变天空 + 日轮 r13（+r20 光晕）+ 3 层山形 | 可用 `ImDrawList` 复刻（同为纯几何绘制） |
| 19 | `ArtInk` | 800×260，4 层水墨山形用 `--text-primary` 透明度 .08/.13/.22/.42，远两层高斯模糊 7；小船 + 26×26 印泥印章 | 同上；模糊用多层半透明叠 |
| 20 | `Steps` | 步 12/600 + 20×20 序号圆；当前 = accent 底 + accent-fg 字；完成 = accent 边 + 10px 对勾；连接线 24×1.5 | 自绘 |
| 21 | `StageFlow` | 节点 h30 pad 0 11 **r-pill** 1px 边 + 等宽 `.code` 10.5/700；5 态 todo/run/done/fail/skip；连接线 18×1.5，`done` 时 accent 填充 | 自绘 |
| 22 | `StageList` | 行网格 `44px 1fr auto 88px` gap10 pad 7/10 r6；等宽码 11.5/700 按态着色 | `ImGui::BeginTable` |

### 5.1 纯 CSS 组件（views 直接用，无 JSX 封装）

`chip`（h26 r-pill，`.on` = accent-dim + accent-glow，`.cnt` 内层胶囊）、`dlist/drow`、
`kpi`（90px accent 圆模糊溢出右上角）、`gantt`、`tl-card`、`film-cell`、`fnode`、
`checklist/ck`、`gates/gate`、`rubric/r-row`、`vsec`、`plist/prow/plabel/pfoot`、
`table`、`sheet/sheet-grid/sheet-cell`、`md-view/json-view`、
`fv-stage/fv-lens/fv-tools`、`float-toolbar/float-panel/float-strip`、`compare`、`derive`、`tl`。

**这些也要实现**——每个页面都由它们拼出来。

---

## 6. 浮层

| 浮层 | 尺寸 | 来源 |
|---|---|---|
| 抽屉 Drawer | **390px**，右侧通高，`--bg-overlay` + 左 1px 边 + shadow-2；`translateX 50px→0` 入场 320ms | `Overlays.jsx:17-150` |
| 确认弹窗 | 420px，无头，danger/warn 图标 + 15px/700 标题 + 12.5px 正文（行高 1.7） | `Overlays.jsx:152-179` |
| 文件查看器 | **720 × min(78vh,700)**；类型图标 + 等宽 14px 文件名 + 类型 Tag + 翻页 + 复制/另存 | `FileViewer.jsx:155` |
| 首启向导 | 520px，3 步（主题 / Comfy 地址 / LLM 供应商+Key） | `Shell.jsx:721-788` |
| 样式编辑器 | 620px | `Shell.jsx:800-839` |
| 校验报告 | 640 × min(74vh,660) | `Shell.jsx:272` |
| 新建项目向导 | 600px，4 步（模板/命名/创意/确认），体最小高 280 | `ProjectHub.jsx:26` |
| 打开项目 | 520px | `ProjectHub.jsx:217` |
| 编辑镜头 | 460px（动作/情绪/时长/备注） | `Storyboard.jsx:186` |

**通用弹窗外框**（`ui.css:943-976`）：`--bg-overlay` + 1px line-normal + **r14** + shadow-2，
宽 `min(560px, 100vw-48px)`，高 `min(640px, 100vh-64px)`；头 padding 14/18 + 下边框 + 标题 15/700；
体 padding 18 滚动；脚 padding 12/18 + 上边框。

**Toast**：右下固定（right 16 / bottom 40），列 gap8，z300；
min-w 260 max-w 380，pad 10/14，`--bg-overlay` + 1px 边 + **左侧 3px 色调条** + r10 + shadow-2；
12.5px；`pop-in` 入场；**存活 3400ms，最多 4 条**（`store.jsx:34-38`）。

**Tooltip**：`--bg-overlay` + 1px 边 + r6，11.5/500，pad 4/9，z90，shadow-1；
**默认在锚点右侧**（`left: calc(100% + 10px)`），`tipDir="b"` 才是下方；延迟 200ms。

**命令面板**：z400，scrim `blur(6px)`；宽 `min(560px, 100vw-40px)`；
三组（页面 / 命令 / 主题），中文标签；过滤 = label+hint+group 的小写 `includes`；
空组丢弃；选区循环；`↑↓` 移动 `Enter` 执行 `Esc` 关闭。

---

## 7. 六个工作区布局

公共骨架 `.vw` = `padding 20px 24px 26px`，竖排，gap 16。
头 = 20px accent 图标 + `.vw-title`(18/800) + `.vw-sub`(12.5 muted) + 弹性空隙 + 控件。

### 7.1 总控 `Overview`
1. 头：`gauge` + "全流程总控台" + 运行下一阶段(secondary) + 一键全流程(primary)/停止(danger)
2. 运行状态卡：状态行 + 百分比 + `Progress` + `StageFlow`(T1–T17)；运行中整卡 `.glow`
3. KPI 行：`repeat(auto-fit, minmax(210px,1fr))` gap14；每卡 pad 14/16，
   **右上角 90px accent 圆模糊 2px 溢出**；标签 11.5/700 字距 .05em；
   **数值 24px/800 等宽数字** + 单位 + 涨跌（涨 ok 跌 danger）；脚注 11.5；卡片 50ms 错峰入场
4. `.grid-3-1`（`1fr / 300px` gap16）：
   - 左：甘特卡（网格 `88px + 8 × minmax(64px,1fr)`，min-width 640，表头吸顶；
     格 h22 r4 10.5/600 按态着色，hover `scale(1.08)`；运行中转圈、完成 ✓、中止 ✕）
     + 账本表（阶段/产物表头可排序，等宽哈希列，降级 Tag，合计脚注）
   - 右：停止条件 S1–S12（StatusDot 列表）、最近产物（40×28 缩略图行 → 文件查看器）、运行信息（KV）

### 7.2 小说 `Novel`
骨架 `.novel-shell` = 网格 `minmax(0,1fr) / 280px`，`height:100%` overflow hidden。
- 中：8 个模式标签（章节/设定/初始化/流水线/评审/模型/状态/自动，min-h 44，横向滚动，
  `.on` = accent + 2px accent 下边框），右侧状态 Tag + 生成本章/停止生成。
  下方 `.novel-body`(pad 16/18/24 滚动)：
  - **章节**：20px/800 章名 + 状态 Tag + meta；`.chap-summary`（fill-muted + **3px accent 左边框** +
    pad 10/14）；`.draft` 正文 **14px / 行高 1.9 / max-w 720 / 首行缩进 2em / `**粗体**`→accent**，
    尾部闪烁光标；未写章节显示 `Empty` + "去流水线生成 T1–T17"
  - **流水线**：`StageFlow` T1–T17 + `StageList`（T3/T11/T12/T16 有产物标签）
  - **设定**：世界 `Segmented` + 5 列表 + 关系图谱（`Tag ← 关系（强度）→ Tag`）
  - **初始化**：`StageFlow` I1–I16 + `.grid-3-1`（门禁 N1–N14 + 前情导入 Checkbox）
  - **评审**：结论卡（`.glow` + 量表分 Tag 簇 + `Progress`）+ 8 维量表条
    （`.r-row` 网格 `76px 1fr 44px`，分数 <75 标红）+ K01–K29 门禁 + 动作按钮
  - **模型**：角色/模型表（内联 `Select` 200px + 查看/复制/另存）+ API Key 卡 + 自动规则开关
  - **状态**：G1–G5 门禁 + StateDiff 表（Before danger / After ok）+ 提交卡（全过才启用）+ 回滚（确认弹窗）
  - **自动**：无人值守配置（4 个 90px 数字域 + 手动/半自动/全自动 `Segmented` + 启动/停止）
    + 前置检查 1–6 + 策略 `Switch`
- 右 280px：属性 KV / 预览 110px `Art` / 本章产物行

### 7.3 资产 `Assets`
`Segmented` 切「详情」（默认）/「总览」。
- **详情**（紧排 pad `10px 18px 18px`，gap 0，三个 `.vsec` 块，14px 2px padding，发丝分隔线，无卡片外框）：
  1. **设定集**：头 = accent 图标 + 实体名 + 状态 Tag + meta + `Segmented` +
     生成完整链(primary sm) + 导出整版(secondary sm) + 刷新(ghost sm)；
     体网格 `minmax(240px,320px) / minmax(280px,1fr)`；左 220px 可点 `Art` 基线；
     右 `KV`(类别/别名/出处/降级策略) + 3 按钮 + **V0 派生链**
     （`.derive`：92px 宽节点 = 50px 缩略图 + 状态点 + 标签；22px 连接线，状态 ≠ todo 时填 accent）
  2. **一致性对比**：`.compare` 16:10 max-w 640，`clip-path` 分割，
     2px accent 滑块 + 22px accent 旋钮（shadow-accent），可拖（限 4–96%），
     两个 Tag（基线/当前）；右侧 `KV` + 4 行差异（一致 ok / 变化 warn）+ 仅重跑差异项
  3. **关联时间线**：`.tl` 92px 高轴（2px 线 + 6 个章节刻度）+ 3 个可点 pin
     （10px 圆 + 3px bg-void 环 + 4px 线环；`hot` = accent + 10px 辉光）+ 引线；
     下方绑定镜头 `.chip` 云 + 4 张 52×36 参考图
- **总览**：`.vw` 头 + `.asset-grid` = `repeat(auto-fit, minmax(210px,1fr))` gap14；
  卡片 150px `Art` + 名称 + 状态 Tag + 类别·角色 meta；hover 缩放 1.07；选中 = accent 边 + 辉光

### 7.4 分镜 `Storyboard`
1. 头：`clapper` + 当前镜头码·动作 + 副行 + 状态 Tag（运行中带点）
   + V9/V10 落库(secondary) + V10 重生成(secondary→确认弹窗) + 运行 V1–V8(primary，运行中禁用)
2. V1–V8 状态卡 + `StageFlow`（运行中 `.glow`）；每 650ms 打点；完成发 ok toast
3. `.shots-wrap` = `minmax(0,1.2fr) / minmax(0,1fr)` gap16：
   镜头详情卡（`KV` 动作/空间/表演/机位/光线/时长/情绪 + 等宽 8 行 Beat JSON 文本域
   + 保存/批量情绪；额外槽：编辑镜头 + 送去出图）
   + 连续性 C1–C12 卡（通过的检查用 ok 色 chip 云 + 4 行列表）
4. **故事板时间线**：横向滚动 128px `.tl-card`（72px 缩略图或"待出图"占位 +
   accent 等宽码 + 动作 + 时长比例条，按 6s 满格）；**HTML5 拖拽排序**（拖放后 "Prompt 版本 +1"）；
   选中卡 = accent 边 + accent-dim 外环
5. 编辑镜头弹窗 460px

### 7.5 出图 `ImageFlow`
**满幅画布页**（`.canvas-page` 绝对定位 inset 0），**不走 `.vw`**。
- `FlowCanvas` 填满，`fitInset=380`；点阵背景
  `radial-gradient(circle at 1px 1px, line-normal 1px, transparent 0) 0 0 / 22px 22px` 铺在 `--fill-muted` 上
- **浮动工具条**（左上，玻璃 blur14，r10，shadow-1，pad 8/12）：
  图标 + "出图流程 · 分镜图_v3" + 1px 分隔 + 导入/导出(ghost sm) + 提交前校验(secondary sm) + 批量出图(primary sm)
- **浮动面板**（右 348px，内缩 16，r14，blur16，shadow-2）：
  头 = 链接图标 + 镜头码·动作(max 210) + 状态 Tag + 折叠钮；
  体 = 全宽 `Segmented` 4 标签 + 内容；脚 = ComfyUI 健康条；`.folded` 折成只有头高
  - **绑定**：两行 `.dlist`（from → to，✔可用 / ✘缺数据）+ 缺 `KSampler.seed` 的 danger 提示
  - **批量出图**：全部入队(primary，未连 Comfy 禁用) / 中断·清队列(secondary)；
    每行序号 + 码 + 章节 + 可选告警图标 + 进度 + 百分比 + 状态 Tag（带点）
  - **图评审**：评审当前图(primary) / 仅重跑⚠项(secondary)；
    150px 画框（未评审时 25% 不透明 + prompt 浮层）；评审后 5 项 `.checklist` + 过/⚠/✘/待评审 Tag
  - **结果**：190px 可点画（打开整章画廊）+ `KV` + `.mp4` 与评审清单行 + 重跑本镜出图
- **画布工具**（左下竖排）：`+` / `x`(减) / 适应视图；有节点 `run` 时加转圈

### 7.6 出片 `VideoFlow`
同画布页骨架。
- 浮动工具条：影片图标 + "出片流程 · H3 视频" + 分隔 + 提交前参数校验(secondary sm)
- 浮动面板：头显示 已出片/出片中/待出片 Tag；体 = `Segmented` 首尾帧链 / 视频任务 / 成片：
  - chain：`.dlist` 行 `Sxx → Sxx` + 策略文字 + ✔已连接 / ⚠断链 Tag
  - task：全部入队/停止 + 每行（码 30px，首帧就绪/缺失 62px，进度，百分比，状态 Tag）
  - cut：系统播放器连播(primary) / 导出一场(secondary) + `KV`（缺失镜头值染 danger）
  - 脚：H3/RIFE/Encode 状态行 + 等宽 `24fps · 112 帧`
- **浮动条**（底，left16 / right384，玻璃 r14 横向滚动）：固定"成片"标签 + 6 个 118px `.film-cell`
  （56px 缩略图 + meta 行（等宽码 + fps + StatusDot））；就绪格可点、播放时叠转圈；未就绪 0.5 透明 + "待出片"

### 7.7 项目中心 `ProjectHub`（全屏，不套外壳）
居中列 max-w 1080，pad 40/32/60，底为两层径向渐变
（accent-dim 18%/-10% 与 info 9% 95%/0%，60% 衰减）+ 一条 240px 高 `ArtInk` 绝对定位带（透明度 0.3）。
- 英雄区：52px logo（r14，grad-accent，shadow-accent，26px play，5s 浮动）+ 26px/800 渐变标题
  + 副标题 + 3 个 Tag（右对齐）
- 工具条：280 搜索 + `Segmented[最近打开|名称]` + 弹性 + 打开…(secondary) + 新建项目(primary)
- 计数行 + `.proj-grid` = `repeat(auto-fill, minmax(240px,1fr))` gap14；
  卡片：120px `Art` 封面（hover `scale(1.06)`）+ 模糊模板 Tag 浮层 + 14.5/700 名称 + 描述
  + meta（章节/镜头/更新）+ 页脚（打开 primary sm / 在资源管理器中显示 ghost / ⋮ ghost / 换主题 icon-btn sm，
  上方一条发丝线）；hover = accent 边 + shadow-2
- 页脚：3 个 `Kbd` 快捷键提示

---

## 8. 响应式

**`webui/src` 里没有任何 `@media` 查询**（全仓 0 处）。适配全靠容器驱动：

| 机制 | 出现处 |
|---|---|
| `repeat(auto-fill, minmax(240px,1fr))` | 项目网格 |
| `repeat(auto-fit, minmax(210px,1fr))` | KPI 行、资产网格 |
| `repeat(auto-fit, minmax(340px,1fr))` | 组件画廊 |
| `min-width: 640px` + 横向滚动 | 甘特、阶段流、时间线、胶片条、派生链、命令面板 |
| 网格比例 | `.grid-3-1` 1fr/300、`.shots-wrap` 1.2fr/1fr、`.flowwrap` 1.15fr/1fr、`.novel-shell` 1fr/280 |

> **ImGui 含义**：这些 `auto-fill/auto-fit` **不能继承**，要在 C++ 里按
> `ImGui::GetContentRegionAvail().x` 显式算列数。写一个
> `int AutoGridCols(float avail, float min_col, float gap)` 工具函数，全套网格共用。

---

## 9. 1:1 验收方法

### 9.1 先从自动化判据拿结论

**推荐走脚本，它会先把 `novel.db` fixture 摆好**（这一步不是可选的，见下）：

```powershell
Build.bat --release                          # 编（Debug 太慢，取证用 release）
out\_make_fixture.ps1                        # 首次 / fixture 丢了之后跑一次
powershell -File out\_run_review.ps1 -OutDir "$PWD\build\shots" -Theme deepspace
```

⚠️⚠️ **`<输出目录>\_review_reports_project\db\novel.db` 是判据的成立条件，不是装饰**。
`SeedReportFixture()` 只写 `work/ch<NNN>/v08_continuity.json` 与 `output/` 里的假产物，
**不建 novel.db**；少了它，侧栏「卷→章→镜」树、检查器属性、资产 kind 树全部退化成**诚实空态**，
于是 `hover-tree-node` / `hover-asset-card` 两个探针**永远不可能命中**。
实测同一份代码：缺库 `hover-probes 9/11 · overall=FAIL`，有库 `11/11 · overall=PASS`。
**「图照常 saved、判据照常红」是这类前提丢失的唯一症状**，别把它读成产品退化。
这套输入（`out\_fixture_{schema,data,seed}.sql` + `out\_make_fixture.ps1`）由 `.gitignore`
明写为「提交的」，**不要删**。

手动等价做法（不走脚本时）：

```powershell
$env:SHINE_IMGUI_REVIEW = "$PWD\build\shots"      # 触发总回归抓图后直接退出
$env:SHINE_IMGUI_WINDOW = "1600x960"              # 锁窗口尺寸
.\build\release\ShineTVStudio.exe
```

产物在输出目录：每张 PNG + `shots-manifest.txt`。
manifest 结尾的 `overall=PASS|FAIL` **必须与进程退出码一致**（PASS↔0 / FAIL↔1）。
manifest 里的四族判据：

| 行 | 判什么 | 归零意味着 |
|---|---|---|
| `hover-probes` | 注入鼠标后**悬停前后必须像素不同** | 控件有 hover 样式但没接线，或压根不可点 |
| `overlay-clicks` | 浮层开着时点按钮**状态真的变了** | 「画得出、点不动」（`HoveredId` 被遮罩抢走） |
| `shortcuts` | 注入 Ctrl+B/J/I/K 后状态与路由真的变 | 快捷键只注册了没接 |
| `duplicate-hits` | 同帧还有几个控件叠着抢命中 | 计数高**不一定是缺陷**，见 §9.3 |

批量跑 8 组环境（P03…P10）用 `scripts\run_reviews.ps1`。
⚠️ 那个脚本的 `-Exe` 曾写死在 `build\ShineTVStudio.exe`（而 `Build.bat` 的产物在
`build\debug|release\`）—— 那里躺着一个两小时前、体积小 26 MB 的旧二进制，**而默认跑照样 PASS**。
现已改成按 `build/release → debug → build` 解析，并在开头打印 `exe: <路径> (<时间戳>)`。
**纪律不变：跑之前核对那一行。**「默认跑了一个旧产物」是这类判据最难发现的假绿。

### 9.2 再拿 webui 目视比对

1. `cd webui && npm run dev` 起设计稿，逐页逐主题截图 → **基准图**。
   ⚠️ `webui/` **不在版本控制里**；换机器前先确认它还在这台机器上（否则见 §2.0）。
2. 同一台机器、同分辨率启动 `ShineTVStudio.exe`（`SHINE_IMGUI_WINDOW=1600x960` 锁尺寸），
   走同一工作区截图 → **对照图**。
3. 5 套主题各来一轮，共 7 个页面（项目中心 + 6 工作区）= **35 组**；
   另外**组件画廊页是每加/改一个组件的回归基准**（§5 全部组件 × 全部状态）。
4. 逐组目视比对：区域位置、间距档、圆角档、字号档、状态色、层级。
5. **像素差不作判据**（跨进程约 2000px、跨构建约 40000px 噪声）。内容有没有丢用结构推理：
   元素个数、是否重叠、是否居中、行高是否取该行最高。

### 9.3 已知必须接受的降级（不要为它们死磕像素）

| 降级 | 原因 | 验收改成怎么判 |
|---|---|---|
| `backdrop-filter` 毛玻璃 | ImGui 直绘没有背景合成 | 用 `--glass` 实色叠加；按「不透明底 + 有没有浮起来」判 |
| `box-shadow` | ImGui 没有 blur API | 同心圆角矩形多层逼近；按「浮层有没有浮起来」判 |
| `line.*` / `fill.*` 的半透明摊平 | JSON 只能存不透明值，见 §2.0.3 | 按「明暗顺序对不对」判 |
| 字号半点档缺失（9.5/12.5/14.5/16） | 图集只建整数档，缺档向上取 | 按「相对大小关系」判 |

完整、逐条、带归因的未修偏差清单在
[`PROGRESS.md` §「视觉 1:1 符合性审计」](PROGRESS.md) —— **改之前先读那一节**，
里面每条都标了「归因在页面层 / 主题层 / 字体层」，避免改错层。
