# G — 图片库（并行线）

> **施工图**（每个 S 的做法与判据）。勾选与状态在 `../PROGRESS.md`；实测证据（含每个 S 的 ✅ 记录）在 `../证据.md`。

> 一次只做一个 S：做完 configure → build → 运行，逐条对验收，再回 `../PROGRESS.md` 勾选。


---

### 大类总纲（原文来自 `Plan/GALLERY.md`，只去掉了已过期的步骤索引表与旧路径引用）


> 图片库（并行线 G）：小类 G-S0…G-S14 的施工图**就在本文件下半部分**；参考材料（接口汇总 / 默认参数 / 日志 / 降级 / 边界 / 最终验收清单）在同目录 `G-参考.md`，进度在 `../PROGRESS.md`。
> 取代 `cpp26_imgui_image_gallery_plan_v2.md`；改写原则：**只写当前真正要做的步骤，不写"以后再评估"**——每个 S 都有改动文件、判据与"本步不做"。
> 工具链与规范遵循 `../Doc/AGENTS.md`：GCC 16.1.0（MinGW64）、C++26、mimalloc、spdlog+fmt、stdexec、yyjson、libhv、中文 UI 字体。
> **怎么用**：在本文件里挑一个小类 → 挑一个 S（**只做一个**）→ 做完 configure/build/运行并逐条对验收 → 回 `../PROGRESS.md` 勾选 + 在 `../证据.md` 写证据。

### 0. 一句话目标

在 ShineTV Studio 现有六区里新增一个**图库**：扫描本地文件夹与 ComfyUI `output`/`input` 目录，在 worker 线程异步解码出缩略图、上传为 DX11 纹理，铺成虚拟化缩略图网格；双击缩略图弹出独立查看器可缩放平移；选中的图片可直接「设为工作流输入」上传给 ComfyUI 供节点图 `@image` 引用。

**终点（本文写完即达成，不再延伸）**：打开一个图库来源 → 缩略图墙流畅滚动（1–2 万张规模）→ 双击放大查看细节 → 右键复制路径 / 在资源管理器显示 / 删除到回收站 → 选中图上传为工作流输入。

---

### 1. 挂载位置（六区对应关系）

| 区 | 现状（已核实） | 本次改动 |
|----|----------------|----------|
| ① 顶栏菜单 | `App.cpp::DrawMenuBar()`：文件 / Comfy / 视图 / 主题 / 帮助（`App.cpp:595-667`） | 「文件」加「打开图片文件夹... Ctrl+O」「图库设置...」；「视图」**只加「查看器」**（`MenuItem("查看器", nullptr, &g_showViewer)`）——「图库」会由 `kActivities` 遍历（`App.cpp:625-630`）自动出现，手工再加会重复 |
| ② 活动栏 | `SideView` 枚举 + `kActivities[]`（资 / 节 / 流 / C） | 新增 `SideView::Gallery` + 活动项 `{"图", "图库"}` |
| ③ 侧栏 | `DrawSideBar()` switch 分发 4 个面板 | 新增分支 `DrawGallerySidePanel()`：来源切换、路径、缩略图尺寸档、缓存占用 |
| ④ 中央 | `DrawDockedPanels()` 里 `ImGui::Begin("图")` 承载 VNS 节点图 | 新增 `ImGui::Begin("图库")`，与「图」共用同一 Dock 节点、tab 切换 |
| ⑤ 右栏 | `ImGui::Begin("属性")` / `ImGui::Begin("预览")` | 「属性」加一段图片元数据；「预览」显示当前选中图的适应窗口预览 |
| ⑥ 底栏 | `ImGui::Begin("队列")` → 队列 / 日志 / 输出 | 不改（图库统计走状态栏与侧栏，不新增 tab） |
| ⑦ 状态栏 | `DrawStatusBar()` | 追加「图片 12000 · 已加载 860 · 缓存 128MB」。**前缀必须用「图片」**：现有 `Text("图 %zu/%zu", graph::NodeCount(), ...)`（`App.cpp:698`）的「图」指节点图 |
| 独立窗口 | `ImGui::Begin("关于")`、`ImGui::Begin("样式编辑器")` 等浮窗 | 新增 `ImGui::Begin("查看器")` 全屏浮窗（非 Dock）；需要新增可见性开关 `bool g_showViewer`，参照 `g_showAbout` / `g_showStyleEditor`（`App.cpp:33-35`）；查看器**不加** `DockBuilderDockWindow` |

默认停靠：`src/app/DockLayout.cpp::BuildDefaultLayout()` 追加

```cpp
ImGui::DockBuilderDockWindow("图库", center);
```

---

### 2. 明确不做（永久剔除，不进入任何步骤）

| 项 | 原因 |
|----|------|
| SQLite / 任何索引数据库 | 内存态 `GalleryModel` + 磁盘缩略图缓存已足够；1–2 万张规模不需要索引（用户明确砍掉） |
| RAW（CR2/NEF/ARW…）/ OpenImageIO | 超出"看图库"需求 |
| libvips / 任何 `IImageBackend` 抽象 | 当前只有解码 + 缩略图缩放，抽象层没有第二个实现者 |
| Texture Atlas / GPU Upload 独立管线 | 缩略图最大 512px、单纹理上传 <1ms，不存在该瓶颈 |
| 10 万级虚拟化压测 / Profiling 框架 | 目标规模 1–2 万张；卡顿用现有日志耗时定位即可 |
| 自建线程池 | 必须复用 `shine::async::RunOnWorker / PostToUi / DrainUiQueue` |
| 新建独立工程 / 自带 third_party 目录 | 本功能是 ShineTV 的一个模块，编进同一个 `ShineTVStudio` 可执行文件 |
| lodepng / stb_image | 已定 libpng；stb 会破坏"每格式一个 Decoder"的隔离 |
| 图片编码（保存 / 转换 / 批量重命名） | 只读图库 |

---

### 3. 依赖与技术栈

### 3.1 复用现有（不新增）

| 用途 | 现有设施 | 位置 |
|------|----------|------|
| 线程池 + UI 邮箱 | `async::RunOnWorker / PostToUi / DrainUiQueue`（stdexec `static_thread_pool`，**当前 4 线程**） | `src/core/Async.h` / `Async.cpp` |
| 日志 | `log::Info/Warn/Error`（spdlog + fmt）+ UI 日志环形缓冲 | `src/core/Log.h` |
| 设置 | `AppSettings` + `settings.json`（yyjson，`%APPDATA%\ShineTVStudio\settings.json`） | `src/core/Settings.h/.cpp` |
| HTTP 上传 | `comfy::HttpUploadImage(url, fileName, bytes, fields, timeoutSec)`（multipart，worker 线程调用） | `src/comfy/ComfyHttp.h` |
| 主题 token | `theme::Current()`：`accent / accentAlt / panelBg / childBg / border / textDim / danger / success` | `src/theme/Theme.h` |
| 字体 | `LoadUIFonts(18.f)` 微软雅黑，中文文案直接可用 | `src/app/Fonts.cpp` |
| 内存 | mimalloc（`mi_process_init()` 已在 `main.cpp` 最先调用） | `third/mimalloc` |
| GPU | DX11（`d3d11/dxgi/d3dcompiler` 已链接） | `src/main.cpp` |

### 3.2 新增第三方库（源码编入，禁止引预编译 dll/lib）

| 库 | 用途 | 来源/许可 | 接入方式 | 库 API 只允许出现在 |
|----|------|-----------|----------|----------------------|
| zlib 1.3.1 | libpng 的依赖 | `third/zlib`（**已在仓库但未接入编译**） | `add_subdirectory(${THIRD}/zlib EXCLUDE_FROM_ALL)`，链 `zlibstatic`；需 `set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)` | 不直接调用 |
| libpng 1.6.x | PNG 解码 | 新增 `third/libpng`，libpng-2.0 许可 | 15 个 `.c` 编成 `shine_png` 静态库，`pnglibconf.h` 用 `scripts/pnglibconf.h.prebuilt` 复制（MinGW 不跑 configure 脚本） | `src/gallery/decoders/PngDecoder.cpp` |
| imageinfo | 文件头探针（格式/宽高/MIME） | 新增 `third/imageinfo/imageinfo.hpp`（`xiaozhuai/imageinfo`，MIT，头文件式） | 只加 include 目录，无需编译 | `src/gallery/MetaProbe.cpp` |
| libjpeg-turbo | JPEG 解码（S12a） | 新增 `third/libjpeg-turbo` | CMake 源码编入；确认 NASM 可用或关闭 SIMD 汇编路径 | `src/gallery/decoders/JpegDecoder.cpp` |
| libwebp | WebP 解码（S12b） | 新增 `third/libwebp` | 全部 `WEBP_BUILD_*` 关掉，只编 decode 静态库 | `src/gallery/decoders/WebpDecoder.cpp` |
| libavif + dav1d | AVIF 解码（S12c） | 新增 `third/libavif`、`third/dav1d` | 最重的一步，独立成小步，失败可单独搁置 | `src/gallery/decoders/AvifDecoder.cpp` |

### 3.3 缩略图缩放

**自研**（`src/gallery/Resize.cpp`）：整数因子 box 均值降采样 + 双线性收尾，保持宽高比，不引入第三方缩放库。

---

### 4. 三段总览

| 段 | 步骤 | 交付的可见结果 |
|----|------|----------------|
| **前置** | S0 | 依赖编译通过、DX11 设备注入成功、图库设置项落盘 |
| **前段（端到端打通）** | S1 → S6 | 打开一个文件夹 → 看到缩略图墙 → 点击选中 → 双击看大图（**第一次可用**） |
| **中段（好用与流畅）** | S7 → S11 | 异步加载不卡 UI、缓存控内存显存、虚拟化 2 万张流畅、独立查看器、右键动作与 @image 上传 |
| **后段（最终完善）** | S12 → S14 | JPEG/WebP/AVIF 可看、手机竖拍方向正确、元数据面板、磁盘缓存秒开、搜索排序 |

**每个 S 自带四段式要点**：目标 / 改动文件 / 新增接口 / 验收标准 + 本步不做（**就地写在本文件，不再单独维护 `说明/` 文档**）。
**每步收尾动作固定**：

```powershell
cmake -S . -B build -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe
cmake --build build -j
./build/ShineTVStudio.exe
```

发现问题先修，不进入下一步。

---



---

## G-S0 — 前置接入（依赖 + CMake + 设备注入 + 设置项）  ✅ 9/9


- **S1 zlib 接进 CMake** — 加 `set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)` + `add_subdirectory(${THIRD}/zlib EXCLUDE_FROM_ALL)`（1.3.1 自带 CMake，**不改 third/zlib 里的文件**）。判据：`cmake` 配置通过，生成 `zlibstatic` 目标。
- **S2 vendor libpng** — `third/libpng/` 放入源码，并从 `scripts/pnglibconf.h.prebuilt` 复制出 `pnglibconf.h`（**必须做这一步**，否则编不过）。判据：目录内 `png.c`…`pngwutil.c` 齐全。
- **S3 vendor imageinfo** — `third/imageinfo/imageinfo.hpp`（单头文件，`xiaozhuai/imageinfo`）。判据：文件存在且可 `#include`。
- **S4 CMake：libpng 静态库 + 链接** — 新增 `shine_png STATIC`（15 个 `.c`）→ `target_include_directories(... ${THIRD}/libpng ${THIRD}/zlib)` → `target_link_libraries(shine_png PUBLIC zlibstatic)` → `target_compile_definitions(shine_png PRIVATE PNG_ARM_NEON_OPT=0 PNG_INTEL_SSE_OPT=0)`；给 `ShineTVStudio` 加 `${THIRD}/imageinfo` include 并 `PRIVATE shine_png`。判据：`cmake --build build -j` 全绿。
- **S5 `src/gpu/GpuDevice.h/.cpp`（共享层）** — `AttachDevice(device, context)`（仅 UI 线程）/ `Device()` / `Context()` / `Ready()`。**P4.1 也要用同一份**，谁先做谁建，另一个只接入。
- **S6 设备注入** — `src/main.cpp`：在 `ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);` **之后**、`shine::app::Init();` **之前**插入 `shine::gpu::AttachDevice(g_pd3dDevice, g_pd3dDeviceContext);`，并 `log::Info("gpu device attached")`（**必须是 fmt 模板形式，不能只传字符串变量**）。
- **S7 Settings 字段** — `AppSettings` 追加 `gallerySource` / `galleryLocalDir` / `comfyOutputDir` / `comfyInputDir` / `galleryThumbSize` / `galleryGpuBudgetMB` / `galleryCpuBudgetMB` / `viewerFitOnOpen`；**两处**：`LoadSettings()` 在 `Settings.cpp:61` 后追加读取、`SaveSettings()` 在 `Settings.cpp:73` 后追加写入（字符串 `yyjson_mut_obj_add_strcpy` / `yyjson_obj_get`+`yyjson_get_str`，整数 `yyjson_mut_obj_add_int`+`yyjson_get_int`）。
- **S8 设置窗口「图库」段** — `App.cpp::DrawSettingsWindow()`（`App.cpp:787-848`）在「主题」「ComfyUI」两段之后新增：本地目录 / ComfyUI output 目录 / input 目录 / 缩略图尺寸档 / CPU·GPU 预算；编辑缓冲照 `g_editComfyUrl` 范式（`App.cpp:794-797`）。
- **S9 验收自测（3 条）** — 配置+构建全绿；日志有 `gpu device attached`；启动→退出→再启动后 `settings.json` 出现新字段且值保留。结果贴 `../PROGRESS.md`。**本步不写任何图片逻辑。**

---

## G-S1 — Image / ImageInfo / MetaProbe  ✅ 5/5


- **S1 `GalleryTypes.h`** — `ImageId = uint64_t` / `SourceKind{Local,ComfyOutput,ComfyInput}` / `LoadState{NotLoaded,Queued,Loading,Ready,Failed}` / `ImageInfo{id,path,width,height,fileSize,modified,format,mime}`。
- **S2 `Image.h/.cpp`** — RGBA8 CPU 图对象：`data` 由 **mimalloc** 分配、**可移动不可拷贝**、`Allocate(w,h)`、`valid()`、析构释放。判据：移动后源对象 `data == nullptr`。
- **S3 `MetaProbe.h/.cpp`** — `imageinfo` 适配层：`ProbeFile(path, id) → ImageInfo`（format 小写、宽高、mime）；**业务层只见 `ImageInfo`**，`imageinfo` 头只在这个 `.cpp` 里 include。
- **S4 CMake 登记 + 隔离检查** — `CMakeLists.txt` 追加 `Image.cpp` / `MetaProbe.cpp`；跑 `grep imageinfo src/` 应**只命中 `MetaProbe.cpp`**。
- **S5 验收自测（3 条）** — ① 探针一张 PNG 日志输出 `probe ok: a.png 1920x1080 png in 0.3ms`；② 1 万张探针总耗时 < 3s 且 `Image::Allocate` **未被调用**（打点验证零像素分配）；③ `grep imageinfo` 命中数 = 1。结果贴 `../PROGRESS.md`。

---

## G-S2 — IImageDecoder / ImageLoader / PngDecoder  ✅ 5/5


- **S1 `decoders/IImageDecoder.h`** — `DecodeError{NotFound,Corrupt,Unsupported,OutOfMemory,DecodeFailed}`；`can_decode(std::span<const std::byte> header)`（看魔数）；`decode(path) → std::expected<Image, DecodeError>`（**不用 `bool` + 出参**；`DecodeError` 里不再有 `Ok`）。
- **S2 `decoders/PngDecoder`** — libpng 用法全部封在这里：`png_create_read_struct` + `png_create_info_struct` + `setjmp(png_jmpbuf(...))` 错误跳转（失败一律转 `DecodeError`，**绝不允许 `abort`**）；统一 RGBA8 六条 transform（`png_set_palette_to_rgb` / `expand_gray_1_2_4_to_8` / `tRNS_to_alpha` / `strip_16` / `gray_to_rgb` / `add_alpha(0xFF)`）；逐行读到 `Image::Allocate(w,h)`，**不在 libpng 内部再整块复制**。
- **S3 `ImageLoader`** — `Register(std::unique_ptr<IImageDecoder>)` / `Load(path)`（按 header 魔数选解码器）/ `Supports(path)`；注册式，UI 与图库不认识任何图片库。
- **S4 CMake + 符号隔离** — 登记 `PngDecoder.cpp` / `ImageLoader.cpp`；`grep -i "png_" src/` 应**只命中 `PngDecoder.cpp`**（`ImageLoader` / `MetaProbe` / UI 里不得有 libpng 符号）。
- **S5 验收自测（2 条）** — ① 8bit RGB / RGBA / Gray / Palette / 16bit 各解一张，输出都是 `w*h*4` 且 Alpha 正确；② 截断文件 → `DecodeError::Corrupt`、不存在文件 → `NotFound`，进程不崩，调用处写 `if (!r) switch (r.error())`。结果贴 `../PROGRESS.md`。

---

## G-S3 — GpuTexture / GpuTextureManager  ✅ 5/5

>
> ⚠️ **与 P4.1 的重叠（已裁决，2026-09-16）**：`src/gpu/GpuTexture*` / `GpuTextureManager` **已由主线 P4.1 先落地**
> （`PLAN.md` §3「重叠点」表：共享 `src/gpu/` 归主线先建）。本步**不重做** S1/S2，只做 **S3 用法对齐 + S4/S5 验收**，
> 产物即 `src/gpu/` 那两份文件。


- **S1 `GpuTexture.h/.cpp`** — DX11 `Texture2D` + SRV 封装；`width/height/bytes()`；`imgui_id()` 返回 SRV 指针转 `ImTextureID`；创建用 `D3D11_USAGE_DEFAULT` + `DXGI_FORMAT_R8G8B8A8_UNORM` + `CreateShaderResourceView`，尺寸用 `D3D11_SUBRESOURCE_DATA` 一次填入。
- **S2 `GpuTextureManager`** — `Upload(w,h,std::span<const std::byte>) → std::expected<GpuTextureHandle, GpuError>`（**仅 UI 线程**）/ `Get` / `Release` / `ReleaseAll` / `UsedBytes` / `OnDeviceLost`（本步先留空实现，S8 生效）；纹理生命周期由它独占，**禁止把裸 SRV 传出模块边界**。
- **S3 ImGui 用法对齐** — 本仓库 ImGui 1.93.0 WIP（`IMGUI_VERSION_NUM 19297`）：`ImTextureID = ImU64`，绘制写 `ImGui::Image(ImTextureRef(tex.imgui_id()), size)`。
- **S4 临时验收代码** — 主循环里临时画一行 4 张测试纹理（**S6 移到网格后删掉**）。
- **S5 验收自测（3 条）** — 4 张纹理能画出；创建/释放 500 次后 `UsedBytes()==0` 且退出无 D3D11 Live Object 报错；256 尺寸时 `bytes()==256*256*4`。结果贴 `../PROGRESS.md`。

---

## G-S4 — ImageScanner（本地 + ComfyUI 目录）  ✅ 5/5


- **S1 `ImageScanner.h/.cpp`** — `ScanOptions{root,recursive,extensions,maxItems=50000}` / `ScanResult{items,skipped,seconds,error}` / `DefaultImageExtensions()`（**S4 阶段仅 `{"png"}`**）/ `SourceLabel(kind)`。
- **S2 `ScanAsync`（worker + PostToUi）** — 固定签名 `void ScanAsync(ScanOptions, std::function<void(ScanResult)>)`：内部 `RunOnWorker` 扫描 + `MetaProbe` 探针，完成后 `PostToUi` 回调；**UI 线程收到后才替换模型**；用递增 `requestSeq` 校验回调，重复点「打开文件夹」能取消/覆盖上一次。
- **S3 `ui/FolderPicker`** — `PickFolder(outUtf8Path)` / `PickImageFolderForGallery(outUtf8Path)`（带"上次目录"记忆）；用 `IFileOpenDialog`，**首次调用 `CoInitializeEx`**；UI 线程调用。
- **S4 Comfy 来源可用性** — ComfyUI 安装目录**无法**从 BaseURL 推断：`comfyOutputDir` / `comfyInputDir` 为空时，「Comfy 输出 / 输入」两个来源在侧栏**置灰**并提示"请在设置中填写 ComfyUI output 目录"。
- **S5 CMake + 验收（3 条）** — ① 1 万张目录扫描期间拖窗口/滚日志不卡，日志有 `scanned 10000 files, probe ok 9998, failed 2, 821 ms`；② 扫描 1 万张内存增量 < 50MB；③ 填了 output 目录后能在侧栏切到该来源。结果贴 `../PROGRESS.md`。

---

## G-S5 — 挂进六区  ✅ 6/6


- **S1 `GalleryModel`** — `SetItems` / `Items()` / `Find(id)` / 选中 API（`SelectOnly` / `ToggleSelect` / `SelectRange` / `ClearSelection` / `IsSelected` / `Selection()`）/ `SortBy`（S5 可先留空实现）/ `SetFilter`（留空，S14 实装）。
- **S2 `Gallery.h/.cpp`（模块入口）** — `Init` / `Shutdown` / `Tick` / `RequestScan(kind)` / `CurrentRoot` / `ItemCount` / `LoadedCount` / `CacheBytes`；**`App.cpp` 只依赖这一个头**。
- **S3 `ui/GalleryView`（先出列表）** — 列表显示文件名 + 尺寸 + 格式；侧栏面板 `DrawGallerySidePanel()`：来源切换、路径、缩略图尺寸档、缓存占用。
- **S4 App.cpp 挂载（逐处核对待办）** — `SideView` 加 `Gallery`；`kActivities[]` 加 `{SideView::Gallery,"图","图库"}`；`DrawSideBar()` 加 case；`DrawDockedPanels()` 加 `ImGui::Begin("图库")`；`DrawMenuBar()`「文件」加「打开图片文件夹... Ctrl+O」、「视图」**只加「查看器」**；`DrawFrame()` 在 `async::DrainUiQueue();`（`App.cpp:878`）之后加 **`gallery::Tick();`**（漏掉永远不显示图）+ `Ctrl+O`；`Init()` 在 `graph::Init();` 后加 `gallery::Init();`；`Shutdown()` 加 `gallery::Shutdown();` **且必须排在 `SaveSettings()` 之后、`async::Shutdown()` / `log::Shutdown()` 之前**；新增 `bool g_showViewer`；`DrawStatusBar()` 加图库统计（前缀用「图片」）。
- **S5 DockLayout** — `DockBuilderDockWindow("图库", center);`（与「图」同 Dock 节点、tab 切换）。
- **S6 CMake + 验收（4 条）** — ① 活动栏出现「图」，点它左栏标题变「图库」、中央出现「图库」tab；② `Ctrl+O` / 菜单弹系统目录框，选完列出文件；③ 「视图 → 重置布局」后「图库」仍在中央 Dock；④ 位置不对时先删**陈旧 `build/imgui.ini`** 或执行重置布局（`dock::RequestRebuild()`，`App.cpp:632-635`）。结果贴 `../PROGRESS.md`。

---

## G-S6 — Resize + 最简网格 = **第一次可用**  ✅ 5/5


- **S1 `Resize.h/.cpp`** — `ResizeBox(const Image& src, uint32_t maxSide) → Image`：保持宽高比、box 均值缩略 + 双线性收尾。
- **S2 `GalleryLayout.h/.cpp`** — `GridMetrics` / `VisibleRange`；`ComputeGrid(availW, availH, thumbSize, zoom)` / `ComputeVisible(...)`（本步可先只算全部）/ `HitTest(...)`。
- **S3 `GalleryView` 列表 → 网格** — 缩略图（本步**允许同步加载、允许卡顿**）、宽高比正确、透明通道用灰格背景可见；悬停高亮、单击选中描边、底部状态条「共 N 张 · 已加载 M · 耗时 X ms」。
- **S4 `ui/GalleryViewer` 第一版** — 只做"适应窗口"显示；双击缩略图弹出并显示该图。
- **S5 验收（第一次可用，5 条全打勾）** — ① 打开 PNG 文件夹出网格；② 悬停/选中/状态条正确；③ 双击弹查看器；④ 缩窗拉宽列数自适应；⑤ 退出无崩溃、无 D3D11 Live Object 报告。**这一步过了才算"图库能用"**；结果贴 `../PROGRESS.md`。

---

## G-S7 — 异步缩略图管线  ✅ 5/5


- **S1 `ThumbnailService` 骨架** — `Init/Shutdown`；UI 线程接口 `Request(id,targetSize,priority)`（重复请求自动去重）/ `Demote(id)`（离开可见区降优先级，**不是取消**）/ `DrainCompleted()` / `State(id)` / `Texture(id)`（未就绪返回空句柄）/ `Retry(id)` / `InFlightAndQueued()`。
- **S2 worker 流水线** — 每张图：`RunOnWorker` → `ImageLoader::Load` → `ResizeBox` → `PostToUi(CPU 缩略图)` → UI 线程 `GpuTextureManager::Upload`；`Gallery.cpp::Tick()` 里驱动 `DrainCompleted()`。
- **S3 并发与队列上限** — 并发默认 **4**（与 `src/core/Async.cpp` 的 `static_thread_pool(4)` 一致，**不改 Async.cpp**）；队列上限 4096，超出丢弃最低优先级并告警。
- **S4 失败处理与日志** — 失败重试 1 次，仍失败 → `LoadState::Failed` + 格子右下角画 `!` 角标（`theme::Current().danger`），点击该格重试；成功打 `loaded <name> 1920x1080 -> 256 in 43ms (decode 38 / resize 5)`，失败打 `log::Warn` 带路径与 `DecodeError`。
- **S5 GalleryView 接线 + 验收（3 条）** — 按 `LoadState` 画占位/失败角标；① 大目录只请求可见+预取带、无整窗白块；② 在飞/队列受控、无 OOM；③ 损坏 PNG 显示角标、可点击重试、有 WARN。结果贴 `../PROGRESS.md`。

---

## G-S8 — CPU 缩略图 LRU + GPU 纹理 LRU  ✅ 5/5

>
> ⚠️ **与 P4.1 的重叠（已裁决，2026-09-16）**：S2 的 `src/gpu/GpuTextureCache`（通用 LRU，key=`uint64_t`）
> **已由主线 P4.1 S4 先落地**，本步**不重做**，只做 S3 两级缓存接入与 S4/S5 验收；S1（`cache/CpuThumbCache`）是图片库自己的，照做。


- **S1 `cache/CpuThumbCache`** — `SetBudget` / `Find(id,targetSize)` / `Insert` / `Clear` / `Bytes` / `HitRate`；LRU 淘汰。
- **S2 `src/gpu/GpuTextureCache`（共享 LRU）** — P4.1 已有；本步补 `Erase` / `ResetStats`；图库 key = `ImageId|(1ull<<63)`。
- **S3 ThumbnailService 两级缓存接入** — GPU → CPU → worker；**Evict 只能在 `Gallery::Tick`（绘制之前）**。
- **S4 设备丢失 + 可观测** — `OnDeviceLost`（CPU 保留）；侧栏命中率 + 重放/模拟丢失按钮。
- **S5 验收（3 条）** — ① 重放第二轮命中率 > 90%；② 占用不超预算；③ 设备丢失后 CPU 重建。结果贴 `../PROGRESS.md`。

---

## G-S9 — 虚拟化网格  ✅ 5/5


- **S1 `GalleryLayout` 可见区间** — `ComputeVisible(..., prefetchRows, ScrollDir)` 实装：可见行 + 上下各 1 行；向下/向上再多预取 1 行。
- **S2 `GalleryView` 只请求可见项** — 只对请求带填 cell 并 `Request`；ThumbGrid 绘制本就只画可见行。
- **S3 `Ctrl+滚轮` 切尺寸档** — 128/256/512 + 连续缩放；换档不 Clear CPU 缓存，按 `targetSize` 重请求可见带。
- **S4 优先级接线** — 可见 High / 预取 Normal；带外 `Demote`（不取消）。
- **S5 验收（3 条）** — ① 2500 张首屏 `requestBand [0..28)`、worker 28 ≪ 2500；② UI 不因总数全量解码；③ 尺寸档布局正确。结果贴 `../PROGRESS.md`。

---

## G-S10 — 独立查看器（缩放 / 平移 / 切换）  ✅ 5/5


- **S1 打开/关闭接口** — `viewer::Open/Close`；`OpenViewer` 转发；浮窗「查看器」+ `showViewer`。
- **S2 缩放与平移** — 滚轮光标锚点缩放 0.1x–16x；拖拽平移；`ClampPan` 软限制。
- **S3 键盘与浮层** — `F` 适应 / `1` 1:1 / `←→` 切换（跳过失败）/ `Esc` 关闭；文件名·尺寸·缩放%·i/N。
- **S4 原图加载** — worker 解码原图；未就绪缩略图占位；`loadGen` 丢弃旧请求；GPU 缓存上限 4。
- **S5 验收（3 条）** — ① 打开与浮层；② 连切 12 张 discarded=1、缓存不涨；③ OVERALL PASS。结果贴 `../PROGRESS.md`。

---

## G-S11 — 交互动作（右键菜单 / @image / 拖拽）  ✅ 5/5


- **S1 `FileActions.h/.cpp`** — 复制路径/文件名、资源管理器定位、回收站删除（IFileOperation / SHFileOperation）。
- **S2 右键菜单与多选** — 查看器 / 设为工作流输入 / 复制 / 资源管理器 / 删除（二次确认）；Ctrl/Shift 多选。
- **S3 设为工作流输入** — `UploadToComfyInput` + `LastUploadedName`；真机 `type=input&overwrite=true` PASS。
- **S4 拖拽** — 网格 `SHINE_IMAGE_PATH` → 节点图接收（日志 + 反馈）。
- **S5 验收** — 复制/定位/上传/回收站/拖拽记账；真机 OVERALL PASS。结果贴 `../PROGRESS.md`。

---

## G-S12 — 多格式（拆成三个小步，逐个落地）  ✅ S1/S2/S4 · S3 搁置


- **S1 JpegDecoder（S12a）** — ✅ `third/libjpeg-turbo` 源码编入（`shine_jpeg`+12/16bit）；Gray/RGB/CMYK；扩展名 jpg/jpeg。
- **S2 WebpDecoder（S12b）** — ✅ `third/libwebp` decode 静态库；`WebPDecodeRGBAInto`；扩展名 webp。
- **S3 AvifDecoder（S12c）** — ⛔ **搁置**：未 vendor libavif+dav1d（MinGW 汇编风险）；`ImageLoader` 不注册，avif 返回 Unsupported。
- **S4 隔离验证 + 验收** — ✅ 符号仅在各自 Decoder.cpp；JPEG/WebP 网格出图；`解码器已注册：3 个（png / jpeg / webp）`。

---

## G-S13 — EXIF 方向 + 元数据面板  ✅ 5/5


- **S1 `ExifOrientation.h/.cpp`** — ✅ Orientation 1–8 / ReadExif / ApplyOrientation / Label / SwapsAxes。
- **S2 读取来源** — ✅ JPEG APP1(EXIF) + PNG eXIf；无第三方库；无 EXIF → Normal。
- **S3 接入 `ImageLoader`** — ✅ 解码后、**缩放前** ApplyOrientation。
- **S4 属性面板** — ✅ 「图片信息」：路径/格式/原始尺寸/显示尺寸/大小/时间/方向/相机。
- **S5 验收（3 条）** — ✅ orient6 正向（80×40→40×80）；PNG 不受影响；面板字段一致。结果贴 `../PROGRESS.md`。

---

## G-S14 — 磁盘缩略图缓存 + 搜索排序 + 最终验收  ✅ 5/5（图库线完成）


- **S1 `cache/DiskThumbCache`** — ✅ thumbs/{128,256,512}；hash.thumb = 头+RGBA8；key 含路径/大小/mtime/档位/版本。
- **S2 ThumbnailService 磁盘层** — ✅ GPU→CPU→Disk→解码；worker 读写；日志 disk-hit。
- **S3 排序过滤** — ✅ GalleryModel::View + SortBy + SetFilter。
- **S4 顶部控件** — ✅ 搜索 / 排序 / 升序 / 扩展名多选。
- **S5 验收** — ✅ 二次启动 disk-hit；删缓存重建；搜索 bulk=400；§11 清单覆盖。结果贴 `../PROGRESS.md`。

---

## 附录：图库线状态（2026-09-18）

**G-S5–S14 ✅ 完成**；G-S12 S3 AVIF **搁置**（未 vendor libavif+dav1d）。主线见 `PLAN.md`。

---
