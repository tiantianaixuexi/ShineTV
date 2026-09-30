---
id: root.agents
kind: instruction
status: current
scope: repository
source_of_truth:
  - CMakeLists.txt
  - Build.bat
  - src/
  - tools/
  - refactor/
last_verified: 2026-09-30
---

# ShineTV Studio AI 协作规则

> **现状（2026-09-30）**：前端已是 **Dear ImGui**（OpenGL 3.3 / WGL），配过与构建**零 Qt 依赖**。
> 旧的 Qt（QWidget + QML）前端已整体删除。
> 界面是 `webui/`（React + CSS 设计稿）的 **1:1 复刻** —— `webui/` 只做效果，不管功能。

## 开始工作前

1. 读 [`refactor/README.md`](refactor/README.md)（**文件地图：要改什么改哪个文件**）与本文件。
   动任何视觉之前，再读 [`refactor/design-spec.md`](refactor/design-spec.md) 与
   [`refactor/PROGRESS.md`](refactor/PROGRESS.md) 的「视觉 1:1 符合性审计」一节。
2. 先检查工作区已有改动。unexpected changes 属于用户，不能回滚、覆盖或顺手整理。
3. 用源码符号和路径定位事实。文档只做导航；源码、CMake 和可执行检查优先。
   **不要把推断写成通过** —— 没跑的验证写「未执行」。
4. 改任何视觉前先确认**归因在哪一层**（页面层 / 主题层 / 字体层 / 宿主层）。
   同样的现象在错层上改，会改成另一种错。

## 双端架构（唯一的心智模型）

| 端 | 位置 | 地位 |
|---|---|---|
| Web 设计稿 | `webui/`（React + CSS） | **视觉唯一基准**，只做效果、不管功能 |
| C++ 实现 | `src/ui/imgui/`（Dear ImGui） | 真正的界面；按 `webui` 1:1 复刻 |
| 业务层 | `src/` 其余（`shine_core`） | 零 Qt、零 ImGui，**一行不改** |

⚠️ **`webui/` 不在版本控制里**（`.gitignore` 最后一行就是 `webui/`）。
新 clone 拿不到它 ⇒ `refactor/design-spec.md` §2 内联了全部 31×5 色值，
那份表才是可复现的记录。要核对 CSS 原式前，先确认 `webui/` 还在本机。

## 构建

- **一键**：根目录 `Build.bat`

  | 命令 | 配置 | 产物 |
  |---|---|---|
  | `Build.bat` | Debug（默认） | `build\debug\ShineTVStudio.exe` |
  | `Build.bat --release` | Release | `build\release\ShineTVStudio.exe` |
  | `Build.bat --clean` | 先删该 build 目录再编 | 同上 |
  | `Build.bat --help` | 用法 | — |

- 两个配置**各用独立 build 目录**：`CMAKE_BUILD_TYPE` 是按 build 目录缓存的，
  塞同一个目录会导致每次切换配置把所有目标重编一遍。
- `Build.bat` 先检查 `SHINE_MINGW\bin\g++.exe`（默认 `C:\msys64\mingw64`，可用环境变量覆盖），
  找不到就 `exit 2`。让 CMake 自己挑编译器会挑出一个「看起来能编过、但不是你以为的那个」的二进制。
- 手动等价命令：

  ```powershell
  cmake -S . -B build\release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
        -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe
  cmake --build build\release --parallel
  ```

- ⚠️ **不要在同一个 build 目录上并发 `cmake --build`** —— 会撞 exe 锁。
- ⚠️ **构建失败后，不要读任何截图或日志** —— 它们描述的是上一个二进制。

## 验证顺序（改完 C++ 必走）

```powershell
powershell -File tools\check-layers.ps1      # 分层门禁
powershell -File tools\check-colors.ps1      # 硬编码颜色
powershell -File tools\check-theme.ps1       # 主题表完整性
Build.bat --release
.\build\release\ShineTVStudio.exe            # 实际启动，肉眼比对 webui 同页面
```

自动化 1:1 回归（**必须带 fixture**，否则判据的成立条件不成立）：

```powershell
out\_make_fixture.ps1                        # 首次 / fixture 丢了之后跑一次
powershell -File out\_run_review.ps1 -OutDir "$PWD\build\shots" -Theme deepspace
```

产物 = 每张 PNG + `shots-manifest.txt`。**manifest 结尾的 `overall=PASS|FAIL`
必须与进程退出码一致**（PASS↔0 / FAIL↔1）。判据说明见 `design-spec.md` §9。

⚠️ **截图不可跨轮逐字节比对**：同一个二进制连跑两轮，80 张里有 48 张字节不同
（时钟 / 动画相位 / 日志时间戳）。要验证**无损重构**时，取两轮之间稳定的那 32 张做子集比
（实测「32/32 未变」即足够）；想做真正的跨轮像素回归，得先把不稳定来源钉死。

两个最容易骗自己的假信号：

| 假信号 | 真相 |
|---|---|
| **缺 `novel.db` fixture** | `SeedReportFixture()` 不建它。少了它侧栏树 / 资产树退化成诚实空态，`hover-tree-node` / `hover-asset-card` 永远命中不到 —— 同一份代码缺库 `9/11+FAIL`、有库 `11/11+PASS`。「图照常 saved、判据照常红」就是它 |
| **默认跑了一个旧产物** | `Build.bat` 的产物在 `build\debug\|release\`。`scripts\run_reviews.ps1` 曾把 `-Exe` 写死在 `build\`（那里躺着一个更旧的二进制，而它照样报 PASS）—— **现已修成按 `build/release → debug → build` 解析并打印时间戳**。纪律不变：核对日志里那行 `exe:`（**编译失败时脚本仍会跑起来**，那两轮数字全是旧二进制的） |
| **以为「异步的图」拍得到** | 真设定图是 worker 解码 → `PostToUi` → 上传 GPU 的**异步**链，只泵一两帧抓到的必然是占位画 —— 而「链断了」与「拍得太早」在截图上完全一样。`ReviewScene.cpp` 在抓资产网格之前等到 `kit::PendingImageCount()==0`，并把 `sheet-images=settled requested=N pending=0 after M frames` 写进 manifest（**它进 overall**）。实测同一个二进制曾出现一轮 33848 像素、一轮 0 |

**数某个具体颜色的像素**（`out\_colorcount.ps1`）是这一族唯一可靠的取证手段：截图跨轮不可复现
（80 张里 48 张会变），而**数某个色值免疫**。真设定图在 fixture 里是两张纯色 PNG
（`#FF2D55` / `#7CFF00`，刻意选在五套主题调色板之外），上没上屏一数便知。

## 门禁与扫描器

| 工具 | 抓什么 | 自检 |
|---|---|---|
| `tools\check-layers.ps1` | 分层越界（`src/` 出现 Qt 头、页面层直连底层） | — |
| `tools\check-colors.ps1` | 硬编码颜色字面量 | — |
| `tools\check-theme.ps1` | 主题表 token 数与顺序 | — |
| `tools\check-i18n.ps1` | 文案规范 | — |
| `tools\find-silent-truncation.ps1` | 列表按容器高度静默截断 | `-SelfTest` |
| `tools\find-draw-arg-order.ps1` | 绘制原语的**颜色 / 圆角**参数错位 | `-SelfTest` |
| `tools\find-rect-wh-misuse.ps1` | `kit::Rect` 四参当成 `(x,y,w,h)` | **无 `-SelfTest`**（带上会被静默忽略后照常报 `0`）；用 `-Root <样本目录>` |

三个扫描器抓的典型形态：

| 形态 | 症状 |
|---|---|
| `if (y + 24.0f > body.max.y) break;`（含 `> body.max.y - 20.0f` 这种「给页脚留位」的变体） | 列表按容器高度静默截断，用户看不到「还有第 N+1 条」 |
| `Rect{x, y, cardW, 192.0f}` ⇒ `max.x < min.x` | 控件**整块不画也不可点**，编译不报错 |
| `AddRectFilled(a, b, radius + grow, col)` | 颜色位收到几何量，**全透明黑、画了等于没画**，编译不报错也不崩 |

⚠️ 门禁自身有已知盲区（记录在 `PROGRESS.md` §「门禁自身的盲区」）：
`find-rect-wh-misuse.ps1` 只认 `width|height|cardW|panelW|boxW`，裸 `w`/`h` 与
`colW`/`cellW`/`iconW`/`thumbH` 全部漏网；`check-colors.ps1` 抓不到 `ImU32(0x…)` 形态。
**扫描器报 0 不等于没有。** 改门禁会让当轮「绿」失去可比基线，
应单独一轮做，且改完必须先跑它自己的自检证明仍能抓。

## 地雷（都已踩过，别再踩）

- **颜色**：「不画」用 `kit::ColorTransparent()`，别写 `IM_COL32(0,0,0,0)` ——
  它是硬编码颜色里最常见的一种，豁免一旦多了门禁就等于没有。
- **`AddRectFilled` 的签名是 `(p_min, p_max, col, rounding)`**，颜色在圆角**前面**。
  写反的三个信号全是骗人的：编译过、不崩、同一帧别的图元照常出图。
  判据是 `_VtxCurrentIdx` 有没有推进，或直接读渲染结果。
- **ImGui 里先注册者独占 `HoveredId`**：同一矩形只能 `HitTest` 一次；浮层遮罩
  **只画不注册**（`kit::ScrimPaint`），否则遮罩会跟面板内容抢同一个 hover，
  症状是「面板里某些控件点不动」，而且**哪一处坏取决于绘制顺序**。
- **`kit::Rect` 四参是 `(minX,minY,maxX,maxY)`**。宽高式用 `kit::RectAt(x,y,w,h)`。
- **child 的裁剪与层级按 draw list 生效**，不按代码嵌套：浮层用 `GetForegroundDrawList()`；
  要建视口的区域整块改用容器自己的 `drawList()`。
- **`kit::ScrollRegion` 不调 `setContentHeight()` 就是滚不动**（不是「滚不顺」）；
  上报值恰好等于视口高等于滚不动。
- **`ImGui::IsMouseHoveringRect` 在本工程会 `0xC0000005`**，用 `Rect::contains(io.MousePos)` 手算。
- **合成鼠标位置必须走 `Host::SetFrameMouseOverride()` / `io.AddMousePosEvent()`，
  且排在所有后端 `NewFrame` 之前**；直接写 `io.MousePos` 字段活不过 `NewFrame`
  （win32 后端在窗口有焦点时一定塞真实光标），症状是「能不能复现取决于取证窗口当时有没有焦点」。
- **中文发糊是结构性的**（`kit/Fonts.cpp` 顶部注释写了三条根因）：
  `RasterizerDensity` 必须设 2.0，设计稿字号大量是半点（10.5/11.5/12.5/13.5/14.5），
  图集按整数档光栅化后缩到半点必然重采样。
- **两个降级不要死磕**：`backdrop-filter` 毛玻璃、`box-shadow`（见 `design-spec.md` §9.3）。

## 常用入口

- 构建：`Build.bat`（Debug 默认）/ `Build.bat --release`
- 门禁：`powershell -File tools\check-layers.ps1` / `check-colors.ps1` / `check-theme.ps1` / `check-i18n.ps1`
- 静默失效扫描：`powershell -File tools\find-silent-truncation.ps1` / `find-draw-arg-order.ps1` / `find-rect-wh-misuse.ps1`
- 截图取证：`scripts\capture_window.ps1` · 逐像素差异 `out\_pixdiff.ps1` ·
  **指定颜色的像素计数** `out\_colorcount.ps1`（`_pixdiff` 只说差了多少像素、不说变成了什么色，
  而这个空档正是「算式算出来了、但没人验过真到没到像素」长期混过去的地方）
- **1:1 回归（首选）**：`out\_make_fixture.ps1` → `out\_run_review.ps1`（自带 fixture 步骤）
- 回归（备选）：`scripts\run_reviews.ps1`（⚠️ 默认 `-Exe` 指向 `build\`，见上文假信号表）
- 运行辅助：`scripts\studio.ps1` · `scripts\comfy.ps1`
- 第三方库：`third/`（`imgui` **`ImAnim`** `VisualNodeSystem` `libhv` `sqlite` `yyjson`
  `spdlog` `fmt` `function2` `glaze` `mimalloc` `stdexec` `hiredis` `libpng` `libjpeg-turbo`
  `libwebp` `zlib`）
- 运行时数据：`runtime/`（**不入库**）
