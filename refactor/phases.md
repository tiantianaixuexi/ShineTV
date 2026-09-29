---
id: refactor.phases
kind: plan
status: current
source_of_truth:
  - refactor/architecture.md
  - refactor/design-spec.md
  - CMakeLists.txt
  - tools/check-layers.ps1
last_verified: 2026-09-30
---

# 实施步骤 P0–P7

每一步都写清：**目标 / 改动 / 验收**。验收不通过不进下一步。
进度记录在 [`PROGRESS.md`](PROGRESS.md)。

**贯穿全程的验证顺序**：

```powershell
powershell -File tools\check-layers.ps1            # 门禁
cmake --build build -j 8 --target ShineTVStudio    # 编译
.\build\ShineTVStudio.exe                          # 实际启动，肉眼比对 webui
```

⚠️ **构建目录单写**：`cmake --build` 不要并发，同一 build 目录并发会撞 exe 锁。
⚠️ **Qt 6 从 MSYS2 MinGW64 来**（`C:/msys64/mingw64`）；ImGui 用同一套 GCC，版本没问题。

---

## P0 底座与门禁

### P0.1 取回 ImGui 库

```powershell
git checkout aa61046 -- third/imgui
```

`aa61046` 是初始导入，含 **268 个文件**，`IMGUI_VERSION 1.93.0 WIP`，
含 `backends/imgui_impl_win32.*`、`backends/imgui_impl_dx11.*`、`misc/cpp/imgui_stdlib.*`。
另有 `third/imgui-backup-nodock`（`imgui_impl_win32` + `imgui_impl_dx11` + `imconfig.h`），
是去 dock 分支的备份 —— **本次不用它**。

只编译需要的 8 个 `.cpp`（与初始导入一致）：
`imgui / imgui_draw / imgui_tables / imgui_widgets / imgui_demo`（demo 可选）
+ `backends/imgui_impl_win32` + `backends/imgui_impl_dx11` + `misc/cpp/imgui_stdlib`。

**验收**：`third/imgui/imgui.h` 存在，`IMGUI_VERSION_NUM` == 19297。

### P0.2 CMake 前端开关

```cmake
option(SHINE_UI_IMGUI "ImGui 前端" ON)
option(SHINE_UI_QT    "Qt 前端（迁移期共存）" OFF)
```

- `ON` → 新建 `shine_imgui` 静态库 + 新的 `ShineTVStudio` 目标
- `OFF` → 走现有 Qt 路径（**迁移期随时能回退对照**）

初始导入的链接项直接抄回来：
`d3d11 dxgi d3dcompiler dwmapi user32 gdi32`，
编译定义 `IMGUI_DEFINE_MATH_OPERATORS UNICODE NOMINMAX WIN32_LEAN_AND_MEAN`。

**验收**：`cmake -S . -B build -DSHINE_UI_IMGUI=ON` 配置通过，无 Qt 报错即算成功
（`SHINE_UI_QT=OFF` 时不必 `find_package(Qt6)`）。

### P0.3 分层门禁反向

改 `tools/check-layers.ps1`：

| 规则 | 改法 |
|---|---|
| 1 | 从「`shine_core` 目录不得含 Qt 头」→ **全 `src/` 不得含 Qt 头**（`shine_imgui` 目录豁免 `third/imgui`） |
| 2 | **删除** `imgui\|ImVec\|ImDraw\|VisNodeSys\|ImAnim\|VisualNode` 这条 |
| 3 | `setStyleSheet`/`QColor` 那条改成「禁止硬编码颜色字面量」：`0x[0-9A-Fa-f]{6,8}` 与裸小数字符串 |

**验收**：`check-layers.ps1` 仍输出 PASS，退出码 0。

### P0.4 空窗口

`src/ui/imgui/host/` 下建最小宿主：`Win32Host.cpp` 建窗口 + D3D11 设备/交换链 +
ImGui 初始化 + 一帧 `ImGui::Begin("Hello")`。

**验收**：出窗口，显示一个 ImGui 窗口，Alt+F4 正常退出，无残留错误。

---

## P1 运行时骨架

### P1.1 Win32 宿主 + D3D11

`src/ui/imgui/host/Host.{h,cpp}`：
- `CreateWindowExW`（`WS_OVERLAPPEDWINDOW`，初始 1600×960）
- `D3D11CreateDeviceAndSwapChain`（`DXGI_SWAP_EFFECT_DISCARD`，与 ImGui dx11 后端匹配）
- 深度模板缓冲（ImGui dx11 后端需要）
- 窗口过程：`WM_SIZE` 重建交换链、`WM_SYSCOMMAND` 拦最小化、`WM_DESTROY` → `PostQuitMessage`
- 垂直同步：按初始导入的做法（`Present(1, 0)`）

### P1.2 接上 `gpu::AttachDevice()`

```cpp
shine::gpu::AttachDevice(device, context);   // 补上这一行
```

**这是整个重构的关键动作**：`src/media` 的纹理上传链从此有宿主。
立刻用探针验证 —— `gpu::Textures().Upload(2,2,bytes)` 能拿到非空 `GpuTextureHandle`。

### P1.3 帧循环 + 线程泵

```cpp
while (...) {
    PeekMessage / TranslateMessage / DispatchMessage
    if (WM_QUIT) break
    shine::async::DrainUiQueue();     // 原 15ms QTimer，改每帧
    gallery::Tick();
    ImGui::NewFrame(); ... ImGui::Render();
    Present();
}
```

保持 `std::_Exit` 收尾、**不 join worker**（照搬 `AppEntry.cpp:148-151` 的理由）。

### P1.4 CLI 分派与自检

照搬 `AppEntry.cpp:98-109`：
- `--mcp-stdio` → `shine::mcp::RunStdioServerMain()`
- `--novel-*` → `shine::novel::RunNovelCli()`
- `ConfigureAppDataSandbox()` 必须在最前（它改 `APPDATA`，影响所有路径解析）

`RunStartupChecks()` / `RegisterChecks()` 改成返回 `std::optional<int>` 的普通函数
（不再依赖 `QApplication`/`QTimer`）。

### P1.5 布局持久化

`%APPDATA%/ShineTVStudio/layout.dat`，magic 保留 `shinetv-layout-1`。
持久化：几何、三个面板可见性与宽度、当前工作区、底栏标签。
损坏文件 → 默认布局 + 提示，不崩（照 `MainWindow.cpp:323-326` 的行为）。

### P1.6 DPI

`ImGui_ImplWin32_EnableDpiAwareness()` + `io.DisplayFramebufferScale`。
目标面板宽（240/280/348/390）按 `DisplayScaledFactor` 缩放，
不要写死像素 —— 1:1 指的是 **100% 缩放下** 与 webui 一致。

**验收**：能开窗、能切页、能存布局、`gpu::AttachDevice` 生效、退出无残留进程。

---

## P2 主题与字体

> ⚠️ **P2 必须一次做完再进 P3**。字体没解决就做组件，所有截图全是豆腐块，判据全部作废。

### P2.1 Token 搬运

`src/ui/kit/theme/Token.h` → `src/ui/imgui/theme/Tokens.h`。
零 Qt、纯 std，**近乎原样搬**（去掉 QSS 字体族串）。
5 个 `Themes/*.json` **原地复用，格式不变**。

### P2.2 颜色表

`ColorToken` 31 项 → 结构体。⚠️ `ApplyTheme()` 里**一次写全 31 项映射**，
不许用循环凑 —— 漏一项就是某个控件在某主题下颜色错，而且不报错。

### P2.3 派生色

按 `design-spec.md` §2.1 的 12 组预混（`tagBg/tagBorder/gateBg/ganttCell/…`），
每套主题各算一遍，写进主题表的 `derived` 段。

### P2.4 `ImGuiStyle` 生成

主题无关几何进 `ImGuiStyle`：
`WindowRounding = rMd(10)`、`FrameRounding = rSm(6)`、`GrabRounding = rSm`、
`TabRounding = rXs(4)`、`WindowPadding = (16,16)`、`ItemSpacing = (8,8)`、
`FramePadding = (10,0)`（对应 `Input` h30）、`ScrollbarRounding = rPill`。

### P2.5 字体与 CJK ⚠️

1. 加载 `C:\Windows\Fonts\msyh.ttc`：
   - ⚠️ TTC → 必须设 `ImFontConfig::FontNo` 选子字体（Microsoft YaHei UI）
   - 字形范围用 `GetGlyphRangesChineseSimplifiedCommon()`（约 2500 常用字）
   - 尺寸档 10.5/11.5/12.5/13/13.5/14/15/18/24 → **按需建多个 `ImFont*`**，不逐档全建
2. 等宽字体 `Cascadia Code` / `Consolas`（`--font-mono`），取不到就退回雅黑
3. **水墨主题**是唯一换字体的（衬线族，`tokens.css:195`）→ 加载 `simsun.ttc`，
   或主题切换时重建图集
4. DPI 缩放时重建图集

**验收（硬判据）**：
- 主题菜单 5 个中文名**逐字可读，无豆腐块**
- 命令面板里搜「主题」能命中
- 5 套主题来回切，文字不糊
- 图集显存占用打印到日志，**不许超过 64 MB**

### P2.6 主题切换与持久化

- 保留 Qt 侧行为：QSS 预生成 → 200ms 淡入 → 持久化（`ThemeService.h:23` 的链）
  → ImGui 版改成「切 `ImGuiStyle` + 纹理/着色器在 D3D11 下不变」→ 只换 Style，**更快**
- 持久化到 `theme.json`（**webui 没有持久化，Qt 有，保留 Qt 的**）
- `SHINE_THEME_SET=<ascii id>` 自检开关保留

### P2.7 减少动效

`reduceMotion` 开关保留；开启后所有过渡时长 → 1ms（对齐 `tokens.css:250-257`）。

**验收**：5 套主题各截一张图，色值与 `Themes/*.json` 逐项一致；中文全部正常。

---

## P3 组件套件

> 目标：`webui/src/components/UI.jsx` 的 20 个导出 + `StageFlow.jsx` 的 2 个
> + 纯 CSS 组件清单（见 `design-spec.md` §5.1）。
> **按 `UI.jsx` 实现，不按 Qt 的 32 个 widget 逐个翻译。**

### P3.1 绘制基建

先把这些抽出来，后面全靠它们：
- `DrawRoundRect`（圆角 + 描边 + 可选顶部高光线）
- `DrawVGradient` / `DrawDiagGradient`（`grad-accent` 是 120° 斜渐变）
- `DrawTextClipped`（长文本省略号，对应 CSS `text-overflow: ellipsis`）
- `DrawShadow`（用描边 + 顶部高光近似 box-shadow，**已知降级**）
- `AutoGridCols(avail, min_col, gap)` —— 替换 CSS `auto-fill/auto-fit`（见 `design-spec.md` §8）

### P3.2 基础控件
`Button`（4 变体 × 3 尺寸 + loading + disabled + `:active scale(.97)`）、
`IconBtn`、`Input`、`TextArea`、`Select`、`Switch`、`Checkbox`、`Field`、
`SearchBox`、`Slider`、`NumberInput`

### P3.3 数据展示
`Tag`（7 色调 × 2 尺寸 + dot + busy 脉冲）、`StatusDot`（7 色调 + run 脉冲）、
`Kbd`、`KV`、`Progress`（渐变填充 + run 微光）、`Steps`、`Badge`

### P3.4 容器与导航
`Card`（普通/hover 上浮/glow）、`Segmented`、`Tabs`、`Splitter`、
`Dialog`（`design-spec.md` §6 的通用外框）、`Drawer`（390px 右侧通高）、
`Toast`（右下、最多 4 条、3400ms）、`Tooltip`（200ms 延迟、默认右侧）
+ CSS-only：`.chip` `.dlist/.drow` `.plist/.prow` `.vsec` `.table` `.gates/.gate`
`.checklist/.ck` `.rubric/.r-row` `.kpi` `.gantt` `.md-view` `.json-view`

### P3.5 画布与图像
- `FlowCanvas`：`ImDrawList` 自绘节点图（节点 180×120，5 态，端口连线，
  滚轮以光标为锚缩放，拖拽平移，选中/框选/删除/复制，浮动缩放工具条 + 转圈）
  —— **整个重构最难的单个控件**，Qt 版参考 `kit/canvas/FlowCanvas.cpp`
- `ImageViewer`：0.1–16× 锚点缩放 + 拖拽平移 + 可选 3× 圆形放大镜（160px）
- `CompareView`：A/B 分割对比（`clip-path` 等价：`ImDrawList::PushClipRect` + 滑块）
- `Art` / `ArtInk`：程序化占位画（`Art` 是 12 组调色板 + 日轮 + 3 层山形；
  `ArtInk` 是 4 层水墨山形 透明度 .08/.13/.22/.42 + 小船 + 印章）—— 都是纯几何，`ImDrawList` 能复刻

### P3.6 图标

46 个 SVG 路径 → 矢量字体，base 16。
保留 `gauge book masks clapper image film play stop search settings plus x
chevron chevdown check alert info layers text chip wave aperture encode
grid refresh folder palette terminal list sparkles wand dots download
upload eye compare link zap panel panelL dock moon target clock users bolt2`
+ **补 `minus`（真正减号）** + **补 `flow`**（补掉 `Gallery.jsx:140` 的兜底 bug）。

### P3.7 组件画廊（自证页）

**先做这一页再进 P4**。照 `webui/src/views/Gallery.jsx` 铺全部组件 × 全部状态
（normal/hover/pressed/disabled/focus × 数据/空/错误），
5 套主题各截一轮。**它是 P3 的验收判据，也是后续每个组件的回归基准。**

**验收**：组件画廊 5 张主题图与 `webui` 画廊逐项对齐；
`--sm/--md/--lg` 三档字号、中文、等宽数字、渐变进度全部正常。

---

## P4 应用外壳

> 尺寸全部照 `design-spec.md` §4（100% 缩放下）。

| 步骤 | 内容 |
|---|---|
| P4.1 | 窗口框架：顶栏 46 / 导航 56 / 侧栏 240 / 检查器 280 / 底栏 190 / 状态栏 26 / 面包屑 34 |
| P4.2 | 顶栏：品牌标、项目胶囊、搜索触发器、运行/停止、主题菜单、设置、Comfy 状态 |
| P4.3 | 导航栏 10 项 + 2.5px accent 选中条 |
| P4.4 | 侧栏：4 种树（小说/资产/分镜出图出片/通用） + 折叠把手（20×48） |
| P4.5 | 检查器：3 段可折叠（属性/预览/关联） |
| P4.6 | 底栏：4 标签 + 日志自动滚动 + 产物/报告行 |
| P4.7 | 状态栏：6 项可点，各开对应 Drawer |
| P4.8 | 面包屑 + 右侧快捷键提示串 |
| P4.9 | 命令面板：3 组、模糊过滤、空组丢弃、循环选区、↑↓/Enter/Esc |
| P4.10 | 浮层：Toast / Drawer 4 变体 / 确认弹窗 / 通用 Modal / 首启向导 / 样式编辑器 |
| P4.11 | 项目中心页（不进外壳，全屏） |

**快捷键**：`Ctrl+B` 侧栏、`Ctrl+J` 底栏、`Ctrl+I` 检查器、`Ctrl+K` 命令面板、
`Ctrl+Enter` 运行。⚠️ `Ctrl+N` 在 webui 里没实现 → **在 ImGui 侧接上**
（新建项目是真实功能，不该是 toast）。

**验收**：空工作区下把外壳整套走一遍，与 `webui` 外壳逐项对齐；
所有面板开合状态能存能读。

---

## P5 六个工作区

> **顺序按风险从低到高**，不是按重要性。每个做完单独截图验收。

### P5.1 总控 `Overview`（pipeline）
运行状态卡 + `StageFlow`(T1–T17) + 5 个 KPI + `.grid-3-1`（甘特 + 账本 / 停止条件 + 产物 + 运行信息）。
**先做这页**：纯数据展示，不碰图像与画布，风险最低，且能验证数据通路
（`pipeline::{Runner, StageMachine, Budget, StopPolicy, Ledger}`）。

### P5.2 小说 `Novel`
最大的一页：8 个模式标签 + 8 套视图 + 右侧 280px 属性栏。
依赖 `novel::*` 全部（`NovelDirector`/`NovelRunLoop`/`NovelGraph`/`NovelChecks`…）。
重点：`.draft` 正文行高 1.9 + 首行缩进 2em + 粗体染 accent + 闪烁光标；
`.chap-summary` 3px accent 左边框；评审页 8 维量表条。

### P5.3 资产 `Assets`
**第一处真正用上 `src/gpu` 的页面** —— 派生链缩略图、一致性对比、参考图、
时间线 pin 的 52×36 缩略图，全部走 `gpu::TextureCache`。
⚠️ 解码异步：首帧可能没纹理，需显式等就绪信号再取证（`visualsReady` 的 ImGui 版）。

### P5.4 分镜 `Storyboard`
头 + V1–V8 状态卡 + `.shots-wrap`（镜头详情 / 连续性）+ 故事板时间线（128px 卡 + 拖拽排序）
+ 编辑镜头弹窗。

### P5.5 出图 `ImageFlow`（含 `FlowCanvas` ⚠️）
满幅画布 + 点阵背景 + 浮动工具条 + 348px 浮动面板（4 标签）+ 左下画布工具。
`FlowCanvas` 在 P3.5 先做，这里是集成。
⚠️ **QML 缩放陷阱**（本机已踩过）：按左上角推的 cover 缩放，
内容内层**必须 `transformOrigin: TopLeft` 等价** —— 缩放绕中心会让整幅画左上平移
`w/2*(k-1)` / `h/2*(k-1)`，只画出一角。ImGui 里对应的是
`AddImage` 的 `uv`/`src` 计算，别用中心锚。

### P5.6 出片 `VideoFlow`
复用 `FlowCanvas` + 浮动面板（首尾帧链/视频任务/成片）+ 底部胶片条（6 × 118px）。

**验收**：6 页 × 5 主题 = 30 组截图，对照 `webui` 同页面逐项比对
（方法见 `design-spec.md` §9）。功能侧：每个页面至少走通一条真实数据路径
（读项目 → 显示 → 一次交互 → 状态回写）。

---

## P6 验收取证

`src/ui/verify/**` 现有 47 个文件，**抓图全靠 `QWidget::grab` / `QQuickWidget::grabToImage`**，
在 ImGui 下全部失效。重建：

| 现有 | ImGui 版替代 |
|---|---|
| `ReviewProbe::GrabWidgetImage` | 从 D3D11 后备缓冲拷像素 → PNG |
| `Pump()` | 帧循环推进 N 帧 + `DrainUiQueue()` |
| manifest 行 `"<name> <bytes> saved|FAILED"` | **保留格式**（脚本依赖） |
| `SHINE_P0x_REVIEW` / `SHINE_QML_REVIEW` / `SHINE_GALLERY_SHOTS` / `SHINE_THEME_TOUR` | **保留全部环境变量名**（`scripts/*.ps1` 依赖） |
| `StartupChecks` / `AcceptanceChecks` 分派 | 保留，返回 `optional<int>` |

⚠️ **取证纪律**（本机反复踩过，写下来当判据）：
- 多图取证跑完**先把所有图的 md5 排一遍**。任意两张逐字节相同 = 有一张没拍到它承诺的状态。
- 等待结果要写进 manifest（`scan converged|TIMEOUT`），别丢返回值。
- 每张图的前置动作显式写全（切视图 + 选实体），别依赖"默认状态恰好是我要的"。
- **像素差不是内容指标**：跨进程 ~2000px 噪声，跨构建 ~40000px。
  小于它不必当回事，**大于它也不能**直接判成回归。

新增 `SHINE_IMGUI_REVIEW`：一次跑完 6 工作区 × 5 主题，作为总回归入口。

---

## P7 收尾

1. 删 `src/ui/kit/`（20 `.cpp` + 头）、`src/ui/qml/`（52 个 `.qml`）、
   `src/ui/pages/**` 里的 Widgets 类、`src/ui/layout/`
2. `shine_kit` / `shine_qml` 两个 CMake 目标与 `SHINE_UI_QT` 开关一起删
3. `find_package(Qt6 ...)` 与 `CMAKE_AUTOMOC/AUTORCC` 全部移除
4. `tools/check-layers.ps1` 定型为「`src/` 零 Qt 头 + 零硬编码颜色」
5. `scripts/package-qt.ps1` → `package-imgui.ps1`：
   - 删 `windeployqt` 段与 Qt 运行库拷贝
   - 修掉那个**根本不存在的 `-DSHINE_QT_UI=ON`**（`package-qt.ps1:18`）
   - 保留 MSYS2 MinGW64 工具链段
6. 更新 `AGENTS.md` 的分层规则（Qt 相关条款作废）
7. 实测并记录产物体积 → 填进 `PROGRESS.md`

**收尾判据**：
```
cmake -S . -B build-imgui   # 不装 Qt 也能配过
check-layers.ps1            # PASS
build-imgui/ShineTVStudio.exe  # 6 工作区全部可用
```

---

## 风险登记

| # | 风险 | 影响 | 对策 | 阶段 |
|---|---|---|---|---|
| R1 | 中文字体 / TTC / 图集爆显存 | 满屏豆腐块，不报错 | P2.5 硬判据：无豆腐块 + <64MB | P2 |
| R2 | 门禁自拦（禁 imgui） | 自己拦自己 | P0.3 先改门禁 | P0 |
| R3 | `FlowCanvas` 节点图 | 最大单控件，最易拖成重写 | P3.5 提前做 + 单独验收 | P3 |
| R4 | 毛玻璃无等价 | 顶栏/浮层面板观感差 | 已知降级，用 `--glass` 实色；**不追求像素一致** | 全程 |
| R5 | box-shadow 无等价 | 浮层"浮不起来" | 上边框高光 + scrim 描边近似 | 全程 |
| R6 | `color-mix` 无合成 | 淡底淡边全错 | 预混 12 组派生色写进主题表 | P2.3 |
| R7 | 取证代码全废 | 无法验收 | P6 重建，保留环境变量名与 manifest 格式 | P6 |
| R8 | 纹理异步未就绪 | 截到空图 | 显式等就绪信号 + 写进 manifest | P5.3 |
| R9 | 图片 cover 缩放锚点 | 只画出一角 | 按左上角推 uv/src，不用中心锚 | P5.5 |
| R10 | 392MB 依赖 Qt 隐式初始化顺序 | 启动崩 | 照 `AppEntry.cpp` 顺序平移 | P1.4 |

---

## 明确**不做**的事

- ❌ 不实现 webui 的 `.doctabs`（死 CSS，无 JSX 引用）
- ❌ 不把 `Ctrl+N` / 样式编辑器"另存"做成 toast 桩 —— 那是 webui 的未完成项
- ❌ 不照抄 `Shell.jsx:100` 侧栏开关的反向 active 逻辑
- ❌ 不照抄 `Gallery.jsx:140` 未定义 `flow` 图标的兜底
- ❌ 不按 `src/ui/qml/Storyboard.qml` / `ImageFlow.qml` 实现分镜与出图
  （这两份 QML 无生产代码加载，按 Widgets 行为）
- ❌ 不追求像素级 1:1（离屏渲染跨进程有噪声，见 `README.md` §7）
- ❌ 不引入第二套线程池 / HTTP 客户端 / JSON 约定（沿用 `shine::async`、`net::HttpClient`、`util::Reflect`）
