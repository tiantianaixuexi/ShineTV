---
id: refactor.dpi-layout
kind: design
status: current
source_of_truth:
  - src/ui/imgui/host/Host.cpp
  - src/ui/imgui/kit/Layout.h
  - tools/check-contrast.ps1
last_verified: 2026-11-23
---

> **本页回答**：UI 的 DPI 缩放、布局管理、颜色体系怎么组织——三套机制的契约、入口与验收线。
> 背景是 2026-11-23 的用户报告「字看不清、有些太小、布局混乱重叠」，同日三件套全部落地。

# UI 体系：DPI · 布局 · 颜色

## 1. DPI 缩放

窗口使用 Per-Monitor V2。启动选择窗口所在显示器的 DPI，从 `WM_DPICHANGED` 更新比例，并应用 Windows 建议的外框。普通启动按该显示器的工作区约束窗口大小，取证允许固定逻辑尺寸及比例。

| 环节 | 坐标与行为 | 实现 |
|---|---|---|
| 客户区 | Win32 客户区用物理像素。最小尺寸以 760 × 500 逻辑单位为目标，受显示器工作区约束。 | `Host::Initialize`、`WM_GETMINMAXINFO` |
| 布局 | Win32 后端更新物理尺寸后，宿主以客户区除以比例，设置逻辑 `DisplaySize`。 | `Host::PumpFrames` |
| 输入 | Win32 的客户区、非客户区、按钮、滚轮与前台补位统一在入队前换算坐标。OpenGL3 消费同一份 `DisplayFramebufferScale`。 | `imgui_impl_win32` 的逻辑坐标选项 |
| 字体 | 输入控件和自绘传同一类逻辑字号；设备比例只参与字形位图密度，切屏时按需烘焙。 | `Fonts.cpp`、`Widget_Input.cpp`、`WM_DPICHANGED` |
| 页面 | 窄窗先尝试只显示一个侧面板，保留用户的面板偏好。矮窗压缩底栏并允许活动栏滚动。宽内容用独立横向视口。 | `Shell.cpp`、`Shell_Chrome.cpp`、`ScrollRegion` |

比例优先级为有效的 `SHINE_UI_SCALE`、取证缺省 100%、所在显示器 DPI。默认 1600 × 960 全量回归用于比较产品轨道。四档比例、900 × 560 客户区以及实际 Win32 命中使用 [交互规范中的快速取证](ui-interaction.md#dpi-与验收)。

## 2. 布局管理器（`kit::Layout`：游标 + 剩余空间 + 棘轮）

**病根**：手写 `y += 高 + 间距` 是「静默截断」「热区叠行」的共同源头——高度一改，推进值不会
自己跟着动。收编进游标后高度只有一处出处。

| 原语 | 语义 | 像素不变契约 |
|---|---|---|
| `Stack` | 竖向：`Next(h, gap)` 取整行矩形并推进 h+gap | 与手写两句逐位等价；裸段间距用 `Advance`，不硬分解 |
| `Row` | 横向：`Next(w, h, gap)` | 同上 |
| `Flow` | flex-wrap：放不下折行（判式与原手写逐字相同） | 折行次数是运行态，组间距挂 `Cur()` 行顶 |
| `Remain()` | 剩余高度/宽度（2026-11-23 补）：排不定高内容前先问，防「写死高度溢出到下一段」的静默重叠 | 只读，零绘制 |

**防回归 = 棘轮扫描器族**（改门禁单独一轮做、先跑自检）：

| 扫描器 | 抓什么 | 当前读数 |
|---|---|---|
| `find-manual-list` | `y += A + B` 与手算 `setContentHeight` | **0 处**（Gallery 大网格明文豁免） |
| `find-raw-text` | 页面层裸 `AddText` / 字面量字号 | **0** |
| `find-fixed-width` | 依赖文字宽度却写死数字 | **0** |
| `find-silent-truncation` | 列表按容器高度静默截断 | **0** |

四读数全绿即「三件套收编」判据面全绿（R7 已关账，见 `进度.md`「当前计划」）。

## 3. 颜色体系（主题表 + 对比度门禁）

- **唯一出处**：`src/ui/imgui/theme/Themes/*.json`（31 token × 5 主题）；调用点不写颜色字面量
  （`check-colors` 拦）。派生色在 `Theme_Derived` 算（主题无关叠层）。
- **对比度门禁 `tools/check-contrast.ps1`**（2026-11-23 立）：按「token 落在哪个 bg 上」成对算
  WCAG 对比度，17 对文字/前景组合全部 ≥4.5:1（小字口径；单看前景亮度没意义）。
  `-SelfTest` 4 项守判据本体与「带 alpha 的 token 不进审计表」的边界。
- **可读性修订批注**：2026-11-23 修 9 个主题值（`text.muted` 全主题通病最低 3.16 → ≥4.5），
  清单见 `design-spec.md` §2 顶部批注（表体未逐格重写，批注=补丁层；webui 原值可反向恢复）。

## 4. 三件套与既有纪律的关系

- **1:1 判据不动**：全量 DPI 回归缺省 k=1；布局迁移逐位等价（`Next` 分解契约）；颜色修订只动
  未被像素计数判据钉住的 token（判据数的是 fixture 纯色与 kpi-glow，text/status 不在内）。
- **减少动效**（Shell_Palette 开关）与**动画取证钉住**语义不受影响（补间 pinned 落终值）。
- 修门禁/扫描器的纪律照旧：单独一轮做、先跑 `-SelfTest`。

## 5. 证据索引

| 事实 | 落点 |
|---|---|
| k 的三级优先级与全链约定 | `src/ui/imgui/host/Host.cpp`（Initialize 顶部那段注释） |
| 字体烘焙乘 FramebufferScale | `src/ui/imgui/kit/Fonts.cpp:438` |
| 游标原语与像素不变契约 | `src/ui/imgui/kit/Layout.h`（文件头 + 各原语注释） |
| 对比度门禁与修订清单 | `tools/check-contrast.ps1` · `refactor/design-spec.md` §2 批注 |
| 回归基线 | `out/_run_review.ps1` 90/90 `overall=PASS`（2026-11-23 多轮） |
