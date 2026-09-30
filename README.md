# ShineTV Studio

小说 → 分镜 → 出图 → 出片的 AI 影视化流水线工作台。

界面是**双端架构**：`webui/`（React + CSS）是视觉设计稿，**只做效果、不管功能**；
`src/ui/imgui/`（C++ / Dear ImGui）是它的 **1:1 复刻**，也是真正跑起来的界面。

---

## 快速开始

```powershell
Build.bat                 # Debug   -> build\debug\ShineTVStudio.exe
Build.bat --release       # Release -> build\release\ShineTVStudio.exe
.\build\release\ShineTVStudio.exe
```

工具链固定走 MSYS2 MinGW64（默认 `C:\msys64\mingw64`，用环境变量 `SHINE_MINGW` 覆盖）。
`Build.bat` 会先检查编译器存在，找不到就退出——避免 CMake 自己挑一个「看起来能编过」的编译器。

---

## 架构

```
shine_core (STATIC)          业务层：零 Qt、零 ImGui
  core util net db llm comfy gpu media flow visual novel paint mcp project pipeline
        ▲  gpu::AttachDevice(GlApi) —— 纹理上传的注入点，宿主启动时接上
        │
shine_imgui (STATIC)         界面层：src/ui/imgui/**
  theme/    5 套主题 · 31 色 token · ImGuiStyle 生成 · 派生色 · 持久化
  kit/      组件库：15 族 Widget_* + Draw / Fonts / Icon / Anim / Scroll / Overlays / Views
  pages/    应用外壳 + 6 个工作区 + 项目中心 + 组件画廊
  host/     Win32 窗口 + WGL/OpenGL 3.3 + 帧循环 + DPI + 取证注入
  verify/   1:1 回归取证（抓图 / 悬停探针 / 浮层点击 / 快捷键 / 判定）
        │
ShineTVStudio (exe)          src/ui/imgui/app/main.cpp —— CLI 分派
```

- 渲染后端 **OpenGL 3.3 core (WGL)**（`imgui_impl_win32` + `imgui_impl_opengl3`）。
  抓图直接 `glReadPixels`，没有交换链与 staging 纹理那一层。
- 动画走 **ImAnim**（`third/ImAnim`，只编引擎那一个 `.cpp`）。
- 组件画廊页与 `imgui_demo` 是组件的回归对照基准。

### UI 分层（改代码前先认这一层）

```
pages/          页面骨架与业务接线（外壳、6 工作区、项目中心、画廊）
   ↓ 只调用
kit/            组件库（按钮/输入/卡/表/树/浮层/图标/动画/滚动/自绘原语）
   ↓ 只取色
theme/          主题表（31 token）→ ImGuiStyle / 派生色
   ↓ 注入
host/           Win32 + GL3 + 帧循环 + 取证注入点
```

**颜色只能从 `theme/` 来**；调用点写 `ImVec4(...)` / `IM_COL32(...)` 字面量会被门禁拦下。

---

## 目录

| 路径 | 内容 |
|---|---|
| `src/ui/imgui/` | 界面层（`theme` / `kit` / `pages` / `host` / `verify` / `app`） |
| `src/` 其余 | 业务层 `shine_core`（零 Qt、零 ImGui） |
| `third/` | 第三方库（imgui、**ImAnim**、VisualNodeSystem、libhv、sqlite、yyjson、spdlog、glaze、mimalloc …） |
| `tools/` | 门禁与静默失效扫描器 |
| `scripts/` | 构建/运行/取证辅助脚本 |
| `refactor/` | **界面移植文档**（入口见下） |
| `webui/` | React + CSS 设计稿；**不在版本控制里**（本地才有） |
| `runtime/` | 运行时数据（**不入库**） |
| `build/` | 构建输出（**不入库**） |

---

## 常用命令

```powershell
# 门禁
powershell -File tools\check-layers.ps1      # 分层越界
powershell -File tools\check-colors.ps1      # 硬编码颜色
powershell -File tools\check-theme.ps1       # 主题表完整性

# 静默失效扫描（编译不报错、运行也不崩，只是画不出来/点不动）
powershell -File tools\find-rect-wh-misuse.ps1
powershell -File tools\find-draw-arg-order.ps1
powershell -File tools\find-silent-truncation.ps1

# 自动化 1:1 回归
$env:SHINE_IMGUI_REVIEW = "$PWD\build\shots"
.\build\release\ShineTVStudio.exe
```

`SHINE_IMGUI_REVIEW` 跑完直接退出；`shots-manifest.txt` 结尾的 `overall=PASS|FAIL`
与进程退出码一致（PASS↔0 / FAIL↔1）。

---

## 文档

| 文档 | 内容 |
|---|---|
| [`AGENTS.md`](AGENTS.md) | 协作规则、构建、门禁、地雷 —— **每次开工先读** |
| [`refactor/README.md`](refactor/README.md) | 现状、分层、**文件地图（要改什么 → 改哪个文件）** |
| [`refactor/design-spec.md`](refactor/design-spec.md) | webui 1:1 视觉规范：几何/字号、31×5 色表、图标、外壳尺寸、组件、逐页布局、验收方法 |
| [`refactor/architecture.md`](refactor/architecture.md) | 分层、线程契约、纹理路径、门禁规则 |
| [`refactor/PROGRESS.md`](refactor/PROGRESS.md) | 已验证的事实、已修缺陷、**未修偏差清单**、跨轮硬事实 |
