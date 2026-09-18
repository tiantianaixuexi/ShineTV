# G（图片库）参考材料 — 接口汇总 / 默认参数 / 日志 / 降级 / 边界

> 2026-09-17：`Plan/说明/` 目录已删，本文件移到施工图同目录（`Plan/任务/G-参考.md`），正文未改；
> **它仍是 G 线的验收依据**（§11 是最终验收清单，§7/§8 是默认参数与日志格式）。
> 施工图在 `../任务/G-图片库.md`；勾选在 `../PROGRESS.md`。

> 本文件是图片库（并行线 G）的**参考材料**，不是任务清单：任务在 `G/` 下（G-S0 … G-S14），总纲在 `../`Plan/任务/G-图片库.md`。
> 章节号沿用拆分前 `Plan/任务/G-图片库.md` 的编号（§6–§14），便于旧引用对照。

---

## 6. 关键接口汇总（冻结部分）

以下接口一旦在对应步骤落地，后续步骤只允许**新增**，不允许改动签名（改了说明架构判断失误）：

```cpp
// 解码层（失败一律 std::expected，不再用 bool + 出参；枚举里没有 Ok）
enum class DecodeError { NotFound, Corrupt, Unsupported, OutOfMemory, DecodeFailed };
class IImageDecoder { /* can_decode(span) noexcept / decode(path) -> expected<Image, DecodeError> */ };
class ImageLoader    { /* Register / Load(path) -> expected<Image, DecodeError> / Supports(path) const */ };

// 探针层
[[nodiscard]] ImageInfo ProbeFile(const std::filesystem::path& path, ImageId id);

// GPU 层（共享层，src/gpu/）
namespace shine::gpu { void AttachDevice(ID3D11Device*, ID3D11DeviceContext*); [[nodiscard]] bool Ready() noexcept; }
class GpuTextureManager { /* Upload(w, h, span<const byte>) -> expected<Handle, GpuError> / Get / Release / ReleaseAll / UsedBytes / OnDeviceLost */ };

// 缩略图调度
class ThumbnailService { /* Request / Demote / DrainCompleted / State / Texture / Retry */ };

// 模块入口（App.cpp 只依赖它）
namespace shine::gallery {
bool Init(); void Shutdown(); void Tick();
void RequestScan(SourceKind kind);
}
```

---

## 7. 默认参数与可靠性

| 参数 | 默认值 | 位置 |
|------|--------|------|
| worker 并发上限 | 4（等于 `Async.cpp` 线程池大小） | `ThumbnailService` |
| 待处理队列上限 | 4096（超出丢最低优先级并告警） | `ThumbnailService` |
| 缩略图尺寸档 | 128 / 256 / 512，默认 256 | `AppSettings::galleryThumbSize` |
| CPU 缩略图缓存预算 | 256 MB | `AppSettings::galleryCpuBudgetMB` |
| GPU 纹理缓存预算 | 512 MB | `AppSettings::galleryGpuBudgetMB` |
| 扫描条数上限 | 50000（超出截断 + WARN） | `ScanOptions::maxItems` |
| 预取行数 | 上下各 1 行 | `GalleryLayout` |
| 缩放范围 | 0.1x – 16x | `GalleryViewer` |
| 失败重试 | 1 次 | `ThumbnailService` |
| 磁盘缓存版本号 | `kThumbAlgorithmVersion = 1` | `DiskThumbCache` |

**设备丢失（`DXGI_ERROR_DEVICE_REMOVED`）**：`GpuTextureManager` 检测到后调用 `GpuTextureCache::OnDeviceLost()` 清空句柄，CPU 缓存与磁盘缓存保留；下一帧按可见项重新 `Upload` 重建纹理。

**降级显示**：文件被占用/损坏 → 该格显示灰色占位 + 红色 `!` 角标，可点击重试；探针失败的文件不进列表（计入 `ScanResult::skipped`）。

---

## 8. 日志规范

- 统一 `shine::log`（`log::Info/Warn/Error`），禁止 `printf` / `std::cout` 做正式日志。
- 必须打点：扫描汇总（文件数/成功/失败/耗时）、单张缩略图（尺寸/耗时拆分）、上传结果、缓存命中率（每 300 帧一次）、设备丢失。
- 失败一律 `Warn` 并带路径与 `DecodeError` 名，便于从底栏「日志」直接定位。

示例：

```text
[info ] scanned 10000 files, probe ok 9998, failed 2, 821 ms (root=D:/pics)
[info ] thumb a.png 1920x1080 -> 256 in 43ms (decode 38 / resize 5)
[warn ] decode failed: broken.png err=Corrupt
[info ] cache hit 93.4% cpu=118MB gpu=304MB
```

---

## 9. 风险与降级

| 风险 | 触发点 | 处置 |
|------|--------|------|
| libpng 在 MinGW 下缺 `pnglibconf.h` | S0 | 直接复制 `scripts/pnglibconf.h.prebuilt`；不跑 configure 脚本 |
| libjpeg-turbo 需要 NASM | S12a | 关闭 SIMD 汇编路径（纯 C 编过优先），性能损失可接受 |
| libavif / dav1d 在 MinGW 汇编路径复杂 | S12c | 该小步单独搁置并记录原因；`AvifDecoder` 不注册，其余功能不受影响 |
| 大图解码占用内存峰值高 | S7 | 4K 图解码约 33MB RGBA8，并发 4 → 峰值约 130MB；`ResizeBox` 后立即释放原图（`Image` 移动语义） |
| EXIF 方向解析出错 | S13 | 解析失败一律按 `Normal` 处理，绝不阻塞显示 |
| 缩略图与查看器方向不一致 | S13 | 方向统一在 `ImageLoader` 出口应用，下游一律拿已纠正的 `Image` |
| 陈旧 `build/imgui.ini` 导致停靠观感异常 | S5 起 | 删除 `build/imgui.ini` 或执行「视图 → 重置布局」；这是环境残留，不是代码问题 |

---

## 10. AI 执行规则

1. **一次只做一个步骤**：完成 S0 → 编译 → 运行 → 逐条对验收 → 确认后再进 S1。禁止一次性生成多步代码。
2. **每步必须可编译可运行**：`cmake configure` → `cmake --build` → 启动 exe → 走该步验收。发现问题先修，不进入下一步。
3. **禁止跨模块泄漏依赖**：`libpng/jpeg/webp/avif/imageinfo` 的符号只允许出现在各自的 decoder 或 `MetaProbe.cpp`；`Gallery/UI/Cache/GPU` 里出现第三方图片库符号即为违规（用 `grep` 自检）。
4. **禁止重复造基础设施**：线程池用 `shine::async`，日志用 `shine::log`，JSON 用 `yyjson`，设置用 `AppSettings`；禁止新起线程/新写日志宏。
5. **新增 `.cpp` 必须加进 `CMakeLists.txt` 的 `add_executable` 列表**（本项目源文件逐个列出，不会自动搜集）。
6. **UI 线程纪律**：绘制阶段禁止任何磁盘 IO、解码、纹理创建以外的阻塞操作；解码/扫描/写缓存只能在 worker。
7. **不过度设计**：不实现 §2 里任何一项；不为"以后可能的多格式"提前写抽象基类——新增格式时只加一个 Decoder 实现 + 一次 `Register`。
8. **中文 UI**：所有文案中文，沿用 `theme::Current()` 的 token 取色，不硬编码颜色；不使用 ImGui 默认拉丁字体渲染中文。
9. **语言特性（强制）**：本项目是 C++26，新代码一律用 `std::string_view` / `std::span` / `std::expected` / `std::optional` / `std::ranges` / `[[nodiscard]]` / `concepts` / 结构化绑定；
   禁止 `const std::string&`（只读参数）、`const void* + size`、`bool + out 参数`、`atoi/sprintf`、**`std::format`**（统一用 `fmt`）。
   完整对照表见 `Doc/AGENTS.md`「语言特性」与 `Doc/RULES-LANG.md` §13；历史代码**触碰即升级**，不做跨模块大重构。

---

## 11. 最终验收清单（S14 完成后逐条打勾）

```text
[ ] 活动栏出现「图」，可切换侧栏与中央「图库」面板
[ ] 三种来源：本地文件夹 / ComfyUI output / ComfyUI input（未配置时置灰并提示）
[ ] Ctrl+O 打开文件夹，扫描期间 UI 不卡；1 万张扫描 < 3s（探针阶段）
[ ] 缩略图墙：比例正确、透明正确、悬停高亮、选中描边、加载占位、失败角标可重试
[ ] 2 万张规模滚动流畅，只处理可见 + 预取项
[ ] CPU 缓存 ≤ 256MB、GPU 缓存 ≤ 512MB（可设置），来回滚动命中率 > 90%
[ ] 磁盘缓存二次启动命中率 > 95%，删除缓存目录可重建
[ ] 双击缩略图 → 独立查看器：滚轮锚定缩放 0.1x–16x、拖拽平移、适应窗口 / 1:1、方向键切换、Esc 关闭
[ ] 右键：复制路径 / 复制文件名 / 资源管理器显示 / 删除到回收站（有确认）
[ ] 「设为工作流输入」上传成功并记录 LastUploadedName，供 P3 @image 引用
[ ] 拖拽缩略图产生 SHINE_IMAGE_PATH payload
[ ] PNG / JPEG / WebP（+ AVIF 若编译通过）都能出缩略图
[ ] EXIF orientation=6 竖拍图显示方向正确，缩略图与查看器一致
[ ] 搜索（文件名）、排序（名称/时间/大小/格式）、扩展名过滤可用
[ ] 退出程序无崩溃、无 D3D11 Live Object 报告
[ ] src/ 中第三方图片库符号只出现在各自 decoder / MetaProbe.cpp
```

---

## 12. 最终模块边界

```text
main.cpp ──AttachDevice──► gpu::GpuDevice（src/gpu/，共享层）
   │
app::App.cpp（六区）
   ├── 活动栏 / 侧栏 / 菜单  ──► gallery::Init / Tick / RequestScan
   ├── 「图库」面板 ──► ui::GalleryView ──► GalleryModel（内存列表/选择/排序/过滤）
   │                        └──► GalleryLayout（列数 / 可见区间 / 命中测试）
   ├── 「查看器」浮窗 ──► ui::GalleryViewer
   └── 「属性」面板 ──► ImageInfo + 元数据

ui::GalleryView
   └── ThumbnailService ──┬── ImageScanner ──► MetaProbe ──► third/imageinfo
                          ├── CpuThumbCache（LRU 256MB）
                          ├── DiskThumbCache（S14）
                          ├── GpuTextureCache（LRU 512MB，src/gpu/ 共享）──► GpuTextureManager ──► DX11
                          ├── async::RunOnWorker / PostToUi
                          └── ImageLoader ──► IImageDecoder
                                                 ├── PngDecoder  ──► libpng + zlib
                                                 ├── JpegDecoder ──► libjpeg-turbo
                                                 ├── WebpDecoder ──► libwebp
                                                 └── AvifDecoder ──► libavif + dav1d
```

依赖方向单向：`UI → Gallery/Model/Layout → ThumbnailService → (Scanner/Probe、Loader/Decoder、Cache) → GPU`。

---

## 13. 目录改动清单

```text
e:\c++\ShineTV\
├── `Plan/任务/G-图片库.md`                                   [新增] 本文
├── CMakeLists.txt                               [改动] third/zlib 接入、shine_png 静态库、gallery 全部 .cpp、include 目录
├── third/
│   ├── zlib/                                    [改动] 仅 CMake 接入（文件已在仓库）
│   ├── libpng/                                  [新增] + pnglibconf.h（来自 prebuilt）
│   ├── imageinfo/imageinfo.hpp                  [新增] 头文件式
│   ├── libjpeg-turbo/ libwebp/ libavif/ dav1d/  [新增] S12 逐个接入
└── src/
    ├── main.cpp                                 [改动] AttachDevice 注入
    ├── app/
    │   ├── App.cpp                              [改动] 活动项 / 侧栏 / 图库面板 / 菜单 / Ctrl+O / Init·Shutdown / 状态栏
    │   └── DockLayout.cpp                       [改动] DockBuilderDockWindow("图库", center)
    ├── core/
    │   └── Settings.h / Settings.cpp            [改动] 图库字段与读写
    ├── gpu/                                     # 共享纹理层（图库 + P4 媒体 + 画布共用）
    │   ├── GpuDevice.h / .cpp                   [新增] AttachDevice / Device / Context / Ready（S0）
    │   ├── GpuTexture.h / .cpp                  [新增] DX11 Texture2D + SRV → ImTextureID（S3）
    │   ├── GpuTextureManager.h / .cpp           [新增] Upload / Get / Release / ReleaseAll / OnDeviceLost（S3）
    │   └── GpuTextureCache.h / .cpp             [新增] 通用 LRU，key = uint64_t（S8）
    └── gallery/
        ├── Gallery.h / Gallery.cpp              [新增] 模块入口 Init/Shutdown/Tick/RequestScan
        ├── GalleryTypes.h                       [新增] ImageId / ImageInfo / SourceKind / LoadState
        ├── Image.h / Image.cpp                  [新增] RGBA8 CPU 图（mimalloc）
        ├── MetaProbe.h / MetaProbe.cpp          [新增] imageinfo 适配
        ├── ImageLoader.h / ImageLoader.cpp      [新增] 解码器注册表
        ├── ExifOrientation.h / .cpp             [新增] S13
        ├── Resize.h / Resize.cpp                [新增] box + 双线性
        ├── ImageScanner.h / ImageScanner.cpp    [新增] 本地 + Comfy 目录扫描
        ├── ThumbnailService.h / .cpp            [新增] 请求调度 + 缓存编排
        ├── FileActions.h / .cpp                 [新增] S11 文件操作
        ├── UploadToComfy.h / .cpp               [新增] S11 工作流输入
        ├── GalleryModel.h / GalleryModel.cpp    [新增] 列表/选择/排序/过滤
        ├── GalleryLayout.h / GalleryLayout.cpp  [新增] 网格几何 + 可见区间
        ├── decoders/
        │   ├── IImageDecoder.h                  [新增]
        │   ├── PngDecoder.h / .cpp              [新增] libpng
        │   ├── JpegDecoder.h / .cpp             [新增] S12a
        │   ├── WebpDecoder.h / .cpp             [新增] S12b
        │   └── AvifDecoder.h / .cpp             [新增] S12c
        ├── cache/
        │   ├── CpuThumbCache.h / .cpp           [新增] S8
        │   └── DiskThumbCache.h / .cpp          [新增] S14
        └── ui/
            ├── GalleryView.h / GalleryView.cpp      [新增] 来源栏 + 网格 + 状态条 + 右键 + 拖拽
            ├── GalleryViewer.h / GalleryViewer.cpp  [新增] 独立查看器
            └── FolderPicker.h / FolderPicker.cpp    [新增] IFileOpenDialog
```

---

## 14. 与 P3 NodeKit 的衔接

- S11 产出两样东西：`gallery::LastUploadedName()`（ComfyUI 侧文件名）与拖拽 payload `"SHINE_IMAGE_PATH"`（本地 UTF-8 路径）。
- P3 做 `object_info → NodeTypeDef` 时，`LoadImage` 类节点的图片下拉项直接列 `gallery::GalleryModel` 的名字；`@image` 引用优先用 `LastUploadedName()`。
- 本计划**不修改** `src/comfy/` 与 `src/graph/` 的任何文件；衔接全部通过上述两个只读接口完成。
