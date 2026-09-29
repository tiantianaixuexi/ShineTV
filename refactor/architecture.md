---
id: refactor.architecture
kind: architecture
status: current
source_of_truth:
  - CMakeLists.txt
  - src/gpu/
  - src/ui/app/AppEntry.cpp
  - tools/check-layers.ps1
last_verified: 2026-09-30
---

# 目标架构：Qt → ImGui

---

## 1. 现状分层

```
shine_core  (STATIC, 零 Qt)
  core util net db llm comfy gpu media flow visual novel paint mcp project pipeline
        ▲
        │  纯 C++ 业务层 + D3D11 纹理管线 —— 本次重构不动一行
        │
   ┌────┴────────────┬──────────────────┐
   │                 │                  │
shine_kit        shine_qml         ShineTVStudio (exe)
(Qt6::Widgets)   (QuickWidgets)     main + shell + 6 工作区 + 取证
20 .cpp          2 .cpp + 52 .qml  143 文件
```

`shine_core` 不含任何 Qt 头，`tools/check-layers.ps1` 当前 PASS。
**这个边界已经是干净的，重构只需要把上面两层换掉。**

---

## 2. 关键发现：`src/gpu` 本来就是给 ImGui 写的

`src/gpu/GpuDevice.h`：

```cpp
void AttachDevice(ID3D11Device* device, ID3D11DeviceContext* context);
[[nodiscard]] ID3D11Device* Device();
[[nodiscard]] ID3D11DeviceContext* Context();
```

- 全局持有 DX11 设备与上下文（`GpuDevice.cpp:8-9`）
- `GpuTextureManager` 负责 `D3D11_TEXTURE2D` + SRV 创建与上传（`GpuTextureManager.cpp:36-56`）
- `src/media` 已经全面依赖它：`Viewer.cpp`、`MediaLibrary.cpp`、`VideoThumb.cpp` 都走
  `gpu::Textures().Upload(...)` + `gpu::TextureCache().Insert(...)`

**全仓没有任何一处调用 `AttachDevice()`。** 它在等一个宿主。

初始导入 `aa61046` 的 `CMakeLists.txt` 里，ImGui 后端正是
`imgui_impl_win32.cpp` + `imgui_impl_dx11.cpp`，链接 `d3d11 dxgi d3dcompiler`——
与 `src/gpu` 是同一套 DX11。**两边本来就是配套的。**

---

## 3. 目标分层

```
shine_core  (STATIC, 零 Qt 零 ImGui)          ← 完全不动
        ▲
   ┌────┴─────────────────────┐
   │  shine_imgui (STATIC)    │
   │  third/imgui (核心+win32+dx11)
   │  ui/imgui/theme     主题表 / ImGuiStyle 生成 / 派生色
   │  ui/imgui/font      字体图集 / CJK 字形范围
   │  ui/imgui/kit       20 个设计稿组件
   │  ui/imgui/host      Win32 窗口 + D3D11 设备 + 帧循环
   └────┬─────────────────────┘
        │
  ShineTVStudio (exe)
  main + shell + 6 工作区 + 取证
```

对应关系：

| 现状 | 目标 | 说明 |
|---|---|---|
| `shine_kit` (Qt Widgets) | `shine_imgui/kit` | 20 个设计稿组件，**按 `UI.jsx` 重写，不按 `kit/controls` 逐个翻译** |
| `shine_qml` (QuickWidgets) | `ui/imgui/pages` | 4 个大 QML 页面用 ImGui 重写 |
| `src/ui/kit/theme/Token.h` | `ui/imgui/theme/Tokens.h` | **纯 std、零 Qt，可近乎原样搬运** |
| `src/ui/kit/theme/Themes/*.json` | 原地复用 | 5 个 JSON 格式不变 |
| `src/ui/kit/canvas/FlowCanvas` | `ui/imgui/kit/FlowCanvas` | 节点图，自绘 `ImDrawList` |
| `src/ui/verify/**` (47 文件) | `src/ui/imgui/verify/**` | 抓图从 `QWidget::grab` 换成 D3D11 离屏拷贝 |
| `src/ui/app/AppEntry.cpp` | `src/ui/imgui/host/Host.cpp` | `QApplication::exec()` → Win32 消息循环 |

---

## 4. `Token.h` 几乎可以原样搬

`src/ui/kit/theme/Token.h` 只 include `<array> <cstddef> <cstdint> <string_view>` —— **零 Qt**。
里面有：

- `ColorToken`（31 项 `uint32_t` RRGGBBAA）
- `radius::{kXs 4, kSm 6, kMd 10, kLg 14, kXl 18, kPill 999}`
- `space::kSteps[9] = {0,2,4,8,12,16,24,32,48}` + `kXs 6 / kXl 20 / kXxl 40`
- `font::kSizes[6] = {12,13,14,16,20,28}`、`kBase 13`、`kRegular 400`、`kSemibold 600`
- `shadow::{kSm, kMd, kLg}`、`border::{kNormal 1, kFocus 2}`
- `motion::kDurFastMs 120 / kDurBaseMs 200 / kDurSlowMs 320` + 三条 `Ease`

**⚠️ 搬运时的坑**：`kColorTokenNames` 的顺序 == `ColorToken` 字段顺序 == QSS `%N` 顺序，
中途插入会让整张模板错位（`Token.h:66-68`）。ImGui 不需要 `%N` 位置替换，但
**`ApplyTheme()` 的映射必须一次性写全 31 项**，不能靠循环凑。

`Token.h` 里写死的 `kFamilyQss` 是 CSS 字体族串，ImGui 侧用不到；但
`Theme.cpp:57-60` 的"水墨是唯一换字体的主题"这条规则要保留。

---

## 5. 生命周期与线程契约（从 `AppEntry.cpp:98-152` 平移）

现状顺序：

```
ConfigureAppDataSandbox()          ← 必须在读任何路径之前
  → CLI 分派：--mcp-stdio / --novel-*
  → ConfigureSceneGraphBackend()   ← Qt 专属，删掉
  → QApplication
  → RunStartupChecks()             → 返回 optional<int> 就接管进程
  → async::Init() / gallery::Init()
  → 15ms QTimer 泵：DrainUiQueue() + gallery::Tick()
  → MainWindow
  → acceptance::RegisterChecks()   → 返回 optional<int> 就接管进程
  → app.exec()
  → gallery::Shutdown() / async::Shutdown() / log::Shutdown()
  → std::_Exit(result)             ← 故意不 join worker
```

目标顺序（Win32）：

```
ConfigureAppDataSandbox()
  → CLI 分派（不变：--mcp-stdio → RunStdioServerMain；--novel-* → RunNovelCli）
  → 创建 Win32 窗口 + D3D11 设备/交换链/深度缓冲
  → gpu::AttachDevice(device, context)        ← 补上这一行
  → ImGui 初始化 + 字体图集
  → RunStartupChecks()                        → 可选<int> 接管
  → async::Init() / gallery::Init()
  → 主循环：
        PeekMessage / Translate / Dispatch
        if (PeekMessage(WM_QUIT)) break
        DrainUiQueue()      ← 原 15ms 定时器，改成每帧
        gallery::Tick()
        ImGui::NewFrame() → 绘制 → Render → Present
  → ImGui 销毁 / 释放 D3D11
  → gallery::Shutdown() / async::Shutdown() / log::Shutdown()
  → std::_Exit(result)
```

**要点**：
- `DrainUiQueue()` 从 15ms 定时器改成**每帧**调用 → 延迟从最坏 15ms 降到一帧，**只更好，不会更差**。
- 仍然**不 join worker**，仍然 `std::_Exit`（`AppEntry.cpp:148-151` 的理由：HTTP connect 或
  静态析构可能让快速关窗永久卡住）。
- `src/media` 的纹理上传是异步的（`Viewer.cpp:102-109` 的 worker 回填）→ 首帧可能还没有纹理，
  取证前必须显式等就绪信号（Qt 版是靠 `visualsReady` 轮询，ImGui 版需要另设一个）。

---

## 6. 纹理与图片路径

```
磁盘 PNG/JPEG/WebP
  → src/media 解码（已有 PngDecoder/JpegDecoder/WebpDecoder，在 worker）
  → shine_png / shine_jpeg（第三方）
  → gpu::Textures().Upload(w, h, bytes)     ← 已有，D3D11_TEXTURE2D + SRV
  → gpu::TextureCache().Insert(key, tex)     ← 已有，按 MediaItem::key
  → gpu::Textures().Get(handle) → ID3D11ShaderResourceView*
  → ImGui::Image(reinterpret_cast<ImTextureID>(srv))
```

**这是整条链里最省事的一段**：`shine_core` 侧一行不改，ImGui 侧只差最后一步
`ImGui::Image()`。D3D11 后端的 `ImTextureID` 就是 SRV 指针，直接传。

需要注意的：
- `ID3D11ShaderResourceView*` 是 `ComPtr` 托管的，ImGui 只是借用，**不要在 UI 侧释放**。
- 缩略图网格要虚拟化（Qt 版 `ThumbGrid` 支持 2 万张基准）→ ImGui 用 `ImGuiListClipper`。
- 图片必须走 worker 解码，UI 线程不做 IO。

---

## 7. 分层门禁改造

`tools/check-layers.ps1` 改三条规则：

| 规则 | 现状 | 目标 |
|---|---|---|
| 1 | `shine_core` 不得含 Qt 头 | **全 `src/` 不得含 Qt 头** |
| 2 | 禁止 `imgui\|ImVec\|ImDraw\|…` | **删除**（ImGui 成了正主） |
| 3 | 禁止 `setStyleSheet`/`QColor` 硬编码色 | 改为**禁止硬编码颜色字面量**（`0x…` / `#…`），颜色只从主题表来 |

第 3 条要小心：ImGui 侧会用大量 `ImVec4(...)` 构造颜色。做法是让所有颜色都从
`theme::Token` 取，脚本只拦**字面量**（`ImVec4(0.1f, 0.9f, …)` 里的裸小数、
`0x35D0B4`、`ImColor(0x…)`），不拦从 token 来的。

---

## 8. 构建与打包

```cmake
option(SHINE_UI_IMGUI "ImGui 前端（重构后默认）" ON)
option(SHINE_UI_QT    "Qt 前端（重构期间共存）"  OFF)
```

**迁移期两个前端共存**，靠这个开关切换。好处：
- 任何阶段都能回退到 Qt 版对照，1:1 比对随时可做。
- `webui` 对照截图可以在同一台机器上抓。

`scripts/package-qt.ps1` 要改名 `package-imgui.ps1`：
- ⚠️ 它现在传 `-DSHINE_QT_UI=ON`，**而 `CMakeLists.txt` 里根本没有这个 option**——
  CMake 会当未识别的缓存变量忽略掉，只发一条警告。重构时顺手修掉。
- 现在的 `windeployqt` + Qt 运行库拷贝整段删除（ImGui 静态链接，不需要）。
- 保留 MSYS2 MinGW64 工具链那段（`C:/msys64/mingw64`），**ImGui + MinGW 没问题**。

预计产物：**392 MB → 十几 MB 量级**（RelWithDebInfo 带调试信息也会小两个数量级）。
这一项等 P7 实测填进 `PROGRESS.md`。
