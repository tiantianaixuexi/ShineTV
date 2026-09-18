# 新会话交接说明

> 用途：新会话**从这里开始**，不用重新交代背景。规则见 `../Doc/AGENTS.md`，进度见 `PROGRESS.md`，
> 经验/踩坑见 `坑与手法.md`。最后更新：2026-09-16 晚。

## 0. 开场白（直接抄）

**小说 Agent 线（当前）**：
```
读 Plan/HANDOFF-novel-agent.md，再读 docs/compose/plans/novel-agent/00-总纲.md、RULES-HOOK.md、PROGRESS.md，
从 PROGRESS 里未完成的最小一项开始写码（建议 P1.1+P1.3 或 P2.B0/B1）。一次一个 ID，build 通过再勾选。
```

**推荐（最短）**：
```
先读 Plan/PLAN.md、Plan/HANDOFF.md 和 Plan/PROGRESS.md，然后继续 G-S5（挂进六区）。
```

**只想接着干活**：
```
继续 G-S5（施工图 Plan/任务/G-图片库.md，进度 Plan/PROGRESS.md）
```

**想先确认现状**：
```
读 Plan/PLAN.md 和 Plan/PROGRESS.md，用几句话总结当前进度与下一步，然后等我指示。
```

## 1. 项目与工具链

- 项目：`ShineTV Studio`（C++26 / ImGui Docking / VisualNodeSystem / DX11），工程根 `E:\c++\ShineTV`。
- 编译器：**GCC 16.1.0 MinGW64**（`C:\msys64\mingw64\bin`），C++26 + 静态反射（`-freflection`）。
- 第三方：`third/`（mimalloc / spdlog+fmt / stdexec / libhv / yyjson / zlib / libpng 1.6.44 / imageinfo / VisualNodeSystem）。
- 构建与运行：

```powershell
cmake -S . -B build -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe
cmake --build build -j 8
.\build\ShineTVStudio.exe
```

- 设置：`%APPDATA%\ShineTVStudio\settings.json`；节点图：`%APPDATA%\ShineTVStudio\graph.json`。

## 2. 现状一览（截至 2026-09-16）

| 线 | 内容 | 状态 |
|----|------|------|
| **P3** | 节点与图（WS 错误层 → 节点定义 → 动态注册 → 编译器 → 提交 → 导入导出） | ✅ **55/55**（含真机出图） |
| **P4** | 媒体与纹理（GPU 纹理 / `/view` / 历史缓存 / 预览面板 / 输出列表 / 生成中预览） | ✅ **27/27**（含真机） |
| **G** | 图片库（**并行线**） | 🟡 **S0–S4 全过（29/79）**；**下一步 G-S5** |
| P5 / P6 / P7 / P8 | 视频分镜 / 画布 inpaint / MCP / 收尾 | ⬜ 未开始 |
| 归档 | P0 / P1 / P2 / P2.9 / **R 线**（`App.cpp` 拆分 + 主题配色） | ✅ 见 `归档-已完成.md` |

**下一步：G-S5（挂进六区）** —— `GalleryModel` + `Gallery.h/.cpp` 模块入口 + `ui/GalleryView`（先列表）
+ 侧栏 `DrawGallerySidePanel()`（来源切换/置灰，直接用 G-S4 的 `IsSourceAvailable`/`SourceUnavailableHint`）
+ `App.cpp` 挂载（含 `gallery::Tick()` 与 Shutdown 顺序）+ `DockLayout` + `Ctrl+O`。之后 **G-S6 = 第一次能用（缩略图墙）**。

## 3. 已经建好的基础设施（直接用，别重复造）

| 设施 | 位置 | 说明 |
|------|------|------|
| 线程池 + UI 信箱 | `src/core/Async.h` | `RunOnWorker/PostToUi/DrainUiQueue`，参数是 `std::move_only_function<void()>` |
| CPU 图对象 | `src/gallery/Image.h` | RGBA8、mimalloc、可移动不可拷贝、`AllocateCallCount()` 打点 |
| 文件头探针 | `src/gallery/MetaProbe.h` | `ProbeFile(path,id)` → `ImageInfo`，零像素解码（1 万张 ≈ 450ms）；中文路径可用 |
| 解码器 | `src/gallery/decoders/*` | `IImageDecoder` + `PngDecoder`（libpng 唯一入口，含 `DecodeMemory`）+ `ImageLoader` 注册式 |
| 目录扫描 | `src/gallery/ImageScanner.h/.cpp` | `ScanAsync(ScanOptions, cb)`（worker 扫描 + `PostToUi` 回投，`requestSeq` 自动丢弃旧请求）；`SourceRoot/IsSourceAvailable/SourceUnavailableHint/MakeScanOptions` |
| 目录选择 | `src/gallery/ui/FolderPicker.h/.cpp` | `PickFolder(...)`（**返回空串 = 取消**）、`PickImageFolderForGallery()`（带上次目录记忆） |
| GPU 纹理 | `src/gpu/GpuTextureManager.h` | `Upload → expected<Handle,GpuError>`、`Get/Release/ReleaseAll/UsedBytes/OnDeviceLost`，仅 UI 线程 |
| UI 状态 | `src/app/UiState.h` | `State()`（可见性开关 / 侧栏 / 编辑缓冲）+ `Activities()` |
| 窗口声明 | `src/app/AppInternal.h` | 18 个窗口入口声明；`AppIncludes.h` 是过渡聚合 include |
| UI 组件 | `src/app/ui/Widgets.h` | `PanelHeader/SectionText/KvRow/EmptyState/Badge/PathPickerRow/ThumbTile` |
| 主题可配置 | `src/theme/ThemeTokens.h` | 25 token 表 + `Overrides/SetOverride/ReapplyTheme/ExportOverrides/ApplyOverrides`；存盘 `Settings::themeCustomColors` |
| 编码与路径 | `src/util/Encoding.h` | `Utf16ToUtf8 / Utf8ToUtf16 / AcpToUtf8 / WinErrorMessage / **PathFromUtf8 / PathToUtf8 / FileNameToUtf8**`（**文本与路径的唯一入口**） |
| 文件读写 | `src/util/File.h` | `OpenInput / OpenOutput / ReadFileBytes / WriteFileBytes`（**一律传 `path`**，中文路径安全） |
| 截图验收 | `scripts/capture_window.ps1` + `scripts/crop_zoom.ps1` | AI 自己截图看界面（用法与坑见 `坑与手法.md` §3） |

## 4. 本机环境事实（别再问路径）

- **ComfyUI**：`F:\AI\ComfyUI-aki-v3`（秋叶整合包，核心 **v0.36.0**，依赖已修好）。
  无头启动：
  `& "F:\AI\ComfyUI-aki-v3\python\python.exe" main.py --port 8188 --disable-auto-launch`
  （CWD `F:\AI\ComfyUI-aki-v3\ComfyUI`；**必须带 `-RedirectStandardOutput/-RedirectStandardError`**）→ 约 20s 就绪。
  `/object_info` 实测 = **2173 类 / 14037 输入 / 全部 V1 形状**。**用完 `Stop-Process` 关掉**。
- **本机没有 SD/SDXL 出图模型**（只有 `ltx-2-19b-dev-fp8` + H3 VAE/LoRA + `gemma_3_12B` CLIP）
  → 真机"出图"验收用不依赖模型的节点（`EmptyImage → SaveImage` 已验证可用）。
- 界面字体微软雅黑；`theme::Presets()` 有 5 个预设。

## 5. 遗留待办

见 `坑与手法.md` §7（TEMP-G3 待删 / `AppIncludes.h` 收窄故意不做 / `SHINE_EXIT_AFTER_SEC` 未生效）。
编码与路径问题已全部收口到 `src/util/Encoding.h` + `src/util/File.h`（见 `坑与手法.md` §4）。
视觉回归：G-S5 落地后做一次（AI 现在可自己截图，见 §3）。

## 6. 文档索引

- 总纲与文档地图：`Plan/PLAN.md`
- 进度勾选：`Plan/PROGRESS.md` ｜ 实测证据：`Plan/证据.md`
- 施工图：`Plan/任务/<大类>.md`（G 的参考材料：`Plan/任务/G-参考.md`）
- 坑与手法：`Plan/坑与手法.md` ｜ 归档：`Plan/归档-已完成.md`
- 规则：`Doc/AGENTS.md`（总入口）、`Doc/RULES-AI.md`、`Doc/RULES-LANG.md`、`Doc/RULES-COMFY.md`、`Doc/STYLE-UI.md`、`Doc/BASELINE.md`、`Doc/BUILD.md`
