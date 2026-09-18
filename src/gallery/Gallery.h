#pragma once
// shine::gallery —— 图片库模块入口（G-S5 S2）
//
// **`App.cpp` 只依赖本头的业务 API**；视图在 `src/app/gallery/`。
//
// 线程模型（照 `Doc/RULES-AI.md` 的异步规范）：
//   * 本头的所有函数都**只在 UI 线程**调用；
//   * 扫描在 worker（`ImageScanner::ScanAsync`）→ 结果经 `async::PostToUi` 回 UI 线程才写模型；
//   * `Shutdown()` 必须早于 `async::Shutdown()`（见 `App::Shutdown` 的调用顺序）。
#include "gallery/GalleryModel.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace shine::gallery {

// 图库运行期状态（UI 线程独占；侧栏 / 状态栏显示用）
struct GalleryState {
    SourceKind source = SourceKind::Local;
    std::filesystem::path root;
    bool scanning = false;
    bool truncated = false;   // 命中扫描条数上限（50000）
    std::size_t skipped = 0;  // 探针失败的文件数
    std::size_t filesSeen = 0;
    double seconds = 0.0;
    std::string message; // 中文提示（成功 / 失败都写这里）
    std::string error;   // 非空 = 上次扫描失败
};

[[nodiscard]] GalleryState& State() noexcept;
[[nodiscard]] GalleryModel& Model() noexcept;

// 启动：读设置里的上次来源与目录；目录可用就**自动扫一次**（不用用户先点一下）。
void Init();
// 退出：状态收尾（不落盘、不 join 线程）。**必须早于 `async::Shutdown()`**。
void Shutdown();
// 每帧（UI 线程；`App::DrawFrame` 里 `async::DrainUiQueue()` 之后调用）。
// G-S5 只做簿记；G-S7 起在这里驱动缩略图流水线的 `DrainCompleted()`。
void Tick();

// 请求扫描某个来源（UI 线程）。来源不可用 → 不发起扫描，写 `State().message` 并返回 false。
bool RequestScan(SourceKind kind);
// 用「当前来源」重扫（「刷新」按钮 / Ctrl+O 之后用）
bool RescanCurrent();

// 「打开图片文件夹…」编排在 `app/gallery/FolderPicker.h`（UI 层弹框后调 `RequestScan`）。

// 条目表被替换/清空时递增：视图层用它丢弃过期缩略图缓存（G-S6）。
[[nodiscard]] std::uint64_t ItemsGeneration() noexcept;

// G-S6 查看器（第一版只做适应窗口；完整交互在 G-S10）
void OpenViewer(ImageId id);
void CloseViewer() noexcept;
[[nodiscard]] bool ViewerOpen() noexcept;
[[nodiscard]] ImageId ViewerImageId() noexcept;

// G-S11：设为工作流输入（上传 ComfyUI /upload/image）
// 异步：worker 读文件 + HttpUploadImage；结果经 PostToUi 更新 LastUploadedName
void UploadToComfyInput(const std::filesystem::path& path);
[[nodiscard]] std::string LastUploadedName() noexcept; // ComfyUI 返回的文件名（@image 用）
[[nodiscard]] std::string LastUploadError() noexcept;
[[nodiscard]] bool UploadInFlight() noexcept;

// 拖到节点图的最近路径（G-S11 S4 反馈；P3 再真正消费）
[[nodiscard]] std::string LastGraphDropPath() noexcept;
void SetLastGraphDropPath(std::string path);

// 视图层上报「已就绪缩略图」计数（状态栏「已加载」）
void SetThumbReadyCount(std::size_t n) noexcept;

// —— 统计（状态栏 / 侧栏读；全部只在 UI 线程）——
[[nodiscard]] std::size_t ItemCount() noexcept;                // 条目总数
[[nodiscard]] std::size_t LoadedCount() noexcept;              // 已就绪缩略图数（G-S6 起由视图上报）
[[nodiscard]] std::size_t CacheBytes() noexcept;               // 纹理缓存占用字节（GPU；CPU 缓存在 G-S8 加入）
[[nodiscard]] std::filesystem::path CurrentRoot() noexcept;    // 当前来源目录

} // namespace shine::gallery
