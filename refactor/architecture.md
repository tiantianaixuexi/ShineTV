---
id: refactor.architecture
kind: architecture
status: current
source_of_truth:
  - CMakeLists.txt
  - src/ui/imgui/host/AppEntry.cpp
  - src/ui/imgui/host/Host.cpp
  - src/gpu/GpuDevice.h
  - tools/check-layers.ps1
last_verified: 2026-09-30
---

> **本页回答**：业务层 + ImGui 界面层怎么分、线程契约、纹理与图片路径、门禁规则。

# 目标架构：业务层 + ImGui 界面层

> 本文描述**现状**。历史（Qt 前端、`docs/` 旧文档树、迁移期双前端共存）只在 git 里。

---

## 1. 分层

```
shine_core  (STATIC)  零 Qt、零 ImGui       ← 本次移植一行不改
  core util net db llm comfy gpu media flow visual novel paint mcp project pipeline
        ▲
        │  gpu::AttachDevice(GlApi) —— 唯一注入点（见 §4）
        │
shine_imgui (STATIC)  src/ui/imgui/**（GLOB_RECURSE，排除 app/）
  theme/    31 色 token · 派生色 · ImGuiStyle 生成 · 持久化
  kit/      15 族 Widget_* + Draw / Fonts / Icon / Anim / Scroll / Overlays / Views
  pages/    外壳 + 6 工作区 + 项目中心 + 组件画廊
  host/     Win32 窗口 + WGL/GL3 + 帧循环 + DPI + 取证注入
  verify/   1:1 回归取证（抓图 / 悬停探针 / 浮层点击 / 快捷键 / 判定）
        │
ShineTVStudio (exe)   src/ui/imgui/app/main.cpp —— 只做 CLI 分派
```

CMake 目标（`CMakeLists.txt`）：

| 目标 | 内容 |
|---|---|
| `shine_core` | 业务层（`amalgamation` 里编入 yyjson） |
| `shine_imgui` | `src/ui/imgui/**` 全部 `.cpp`（除 `app/`） |
| `ShineTVStudio` | 唯一 exe，源码只有 `app/main.cpp` |
| 第三方 | `shine_imgui_third`(imgui，含 `imgui_demo`) · **`shine_imanim`**(只取 `im_anim.cpp`) · `shine_vns`(VisualNodeSystem) · `shine_png` · `shine_jpeg` · `shine_webp` · `shine_hiredis` · `shine_sqlite` · `hv_static`(libhv) · `zlibstatic` · `mimalloc` |

**渲染后端是 OpenGL 3.3 core（WGL）**，不是 D3D11 —— 选它的三个理由写在 `host/Host.h:7-11`：
ImGui 的 gl3 后端自带加载器与着色器编译；抓图直接 `glReadPixels(GL_RGBA)`，
没有交换链、没有 staging 纹理、也没有「后备缓冲是 BGRA 还是 RGBA」的歧义。

---

## 2. 启动顺序与线程契约

`src/ui/imgui/host/AppEntry.cpp` 的 `RunApp()`，顺序**不能动**：

```
ConfigureAppDataSandbox()        ← 必须在读任何路径之前（它改 %APPDATA%）
  → shine::log::Init()           ← 任何 shine::log:: 调用之前（默认 logger 无 sink）
  → CLI 分派：--mcp-stdio → mcp::RunStdioServerMain()
              --novel-*  → novel::RunNovelCli()
  → Host::Initialize()           ← Win32 窗口 + 像素格式 + GL 上下文 + ImGui 上下文
  → LoadStartupTheme()           ← 读主题 JSON → ApplyCurrentTheme() → 再谈持久化
  → async::Init() / gallery::Init()
  → if (SHINE_IMGUI_REVIEW) RunReview() → _Exit(pass ? 0 : 1)
  → host.RunLoop([](dt){ shell.DrawFrame(dt); })
  → gallery::Shutdown() / async::Shutdown() / log::Shutdown()
  → std::_Exit(0)                ← 故意不 join worker
```

**要点**：

- **故意 `std::_Exit`、故意不 join worker**：HTTP connect 或静态析构可能让快速关窗永久卡住。
- `DisplaysFramebufferScale` 固定 `(1,1)`：design-spec 的几何表全部是「100% 缩放下的 px」，
  1:1 验收就是拿 1600×960 的抓图去比 webui 100% 的截图（`Host.cpp:174-179`）。
  面板宽（240/280/348/390）按 `Host::DisplayScaledFactor()` 换算，不写死像素。
- 纹理上传是**异步**的（`src/media` 在 worker 里解码后回填）⇒ 首帧可能还没有纹理，
  取证前必须显式等就绪，不能「泵一帧就抓」。

---

## 3. 门禁规则（`tools/check-layers.ps1`）

| 规则 | 内容 |
|---|---|
| 1 | `src/` 全树不得出现 Qt 头 |
| 2 | 页面层不得直连业务层底层设施（要走 `kit/` 与 `pages/PageCommon.h` 的通道） |
| 3 | 调用点禁止硬编码颜色字面量 —— 颜色只从 `theme/` 来 |

`check-colors.ps1` 只认字面量（`IM_COL32` / `ImVec4(数字…)` / `"#rrggbb"` / `0xAARRGGBB`），
不拦从 token 来的值。**已知盲区**：抓不到 `ImU32(0x…)` 形态（实例见 `进度.md`）。

---

## 4. 纹理与图片路径

```
磁盘 PNG/JPEG/WebP
  → src/media 解码（已有 Png/Jpeg/Webp 解码器，跑在 worker 线程）
  → gpu::Textures().Upload(w, h, bytes)      ← 经 gpu::GlApi 的函数指针建 GL 纹理
  → gpu::TextureCache().Insert(key, handle)  ← 按 MediaItem::key 缓存 + 预算淘汰
  → gpu::Textures().Get(handle) → GL 纹理 id
  → ImGui::Image((ImTextureID)(intptr_t)id, size)
```

- 注入点在 `host/Host.cpp:206-215`：GL 上下文就绪后填 `gpu::GlApi`（六个函数指针）并
  `AttachDevice`。缺函数会**明确报日志**「图库/媒体预览会不出图」，不会静默。
- 退出前必须 `gpu::DetachDevice()`（`Host.cpp:453-454`）—— 它先把纹理 `glDeleteTextures`
  放掉，**必须在 GL 上下文销毁之前**，否则是悬空 GL 对象。
- ⚠️ **界面侧尚未消费纹理**：`src/ui/imgui/**` 里目前**没有任何 `ImGui::Image` 调用**。
  业务侧整条链是通的（`src/media/Viewer.cpp`、`ThumbnailService.cpp`、`MediaLibrary.cpp`
  都在上传），缺的是 UI 那一端。
  这**不影响 webui 的视觉 1:1** —— 设计稿里的图全是程序化占位画
  （`UI.jsx` 的 `Art` / `ArtInk`，`kit/Views.cpp` 已复刻），设计稿本身没有真图。
  要显示真图时，动作是：加一个「纹理 → `ImTextureID`」的薄封装放进 `kit/`，
  再接到资产页/查看器上；`ImageId → handle` 的查询口已经有
  （`media::ThumbnailService::Texture(id)`、`media::MediaLibrary::TextureFor(item)`）。

---

## 5. 线程与取证的契约

- UI 线程只做绘制；解码/上传/HTTP 全在 worker。
- 每帧先泵 UI 队列（原来的 15ms 定时器改成每帧，延迟只更好）。
- 取证的鼠标/按键注入**必须早于所有后端 `NewFrame`**
  （`Host::SetFrameMouseOverride` / `SetFrameKeyOverride` / `SetFrameMouseButtonOverride`）：
  在帧回调里写 `io.MousePos` 活不过 `NewFrame`，win32 后端在窗口有焦点时一定会塞真实光标，
  症状是「能不能复现取决于取证窗口当时有没有焦点」。
- 同一帧的像素指纹靠 `ReviewPixel.cpp`（`FNV-1a64` + `kSettleFrames=3` 稳定帧）。
