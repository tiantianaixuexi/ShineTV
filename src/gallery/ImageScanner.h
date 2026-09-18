#pragma once
// shine::gallery —— 目录扫描（G-S4）
//
// 职责：把「一个目录」变成「一串 ImageInfo」。只读**文件头**（`MetaProbe`），
// **绝不解码像素**（扫描阶段不产生 `Image`，`Image::AllocateCallCount()` 必须保持不变）。
//
// 线程模型（遵守 `Doc/RULES-AI.md` 的异步规范）：
//   * `ScanAsync()` 在 **UI 线程**调用，内部 `async::RunOnWorker` 扫描 + 探针；
//   * 结果经 `async::PostToUi` 回到 **UI 线程**后才交给回调 —— 回调里才能改 UI/模型。
//   * 需要先 `async::Init()`（`app::Init()` 已做）。
#include "gallery/GalleryTypes.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace shine::gallery {

struct ScanOptions {
    std::filesystem::path root;
    bool recursive = true;
    std::vector<std::string> extensions;   // 空 = `DefaultImageExtensions()`；"png" / ".png" / "*.PNG" 都认
    std::size_t maxItems = 50000;          // **结果条数**上限（探针失败的不占额度）；超过则截断 + `Warn`（Plan/任务/G-参考.md §7）
    SourceKind source = SourceKind::Local; // 打进每个 `ImageInfo::source`
};

struct ScanResult {
    std::vector<ImageInfo> items;
    std::size_t skipped = 0;    // 探针失败（文件损坏 / 头不认识 / 读不了）
    std::size_t filesSeen = 0;  // 扩展名匹配的文件数（日志里的 "scanned N files"）
    double seconds = 0.0;
    std::string error;     // 空 = 成功
    bool truncated = false; // 命中 `maxItems` 上限
    bool cancelled = false; // 被更新的请求取代（此时结果会被 `ScanAsync` 丢弃）
    SourceKind source = SourceKind::Local;

    [[nodiscard]] bool ok() const noexcept { return error.empty() && !cancelled; }
};

// 固定签名同 `Plan/任务/G-图片库.md`「新增接口」。
// **连续多次调用只有最后一次的回调会被执行**（内部递增 `requestSeq` 校验），
// 因此"重复点打开文件夹"不会让旧结果覆盖新结果；旧任务发现被取代会提前退出。
void ScanAsync(ScanOptions opt, std::move_only_function<void(ScanResult)> onDone);

// G-S4 阶段只有 PNG（`{"png"}`）；S12 起逐个加入 jpeg / webp / avif
[[nodiscard]] std::vector<std::string> DefaultImageExtensions();

// 侧栏/状态栏显示用（"本地文件夹" / "Comfy 输出" / "Comfy 输入"）
[[nodiscard]] std::string SourceLabel(SourceKind k);

// —— Comfy 来源可用性（G-S4 S4）——
// ComfyUI 的安装目录**无法**从 `comfyBaseUrl` 推断（服务可能跑在另一台机器上），
// 因此 output/input 目录只能来自设置；为空即"该来源不可用"（侧栏置灰 + 给提示）。
[[nodiscard]] std::filesystem::path SourceRoot(SourceKind k);
[[nodiscard]] bool IsSourceAvailable(SourceKind k);
// 可用时返回空串；否则返回可直接显示的中文提示
[[nodiscard]] std::string SourceUnavailableHint(SourceKind k);
// 按当前设置拼一次扫描请求（`Gallery::RequestScan` 用）
[[nodiscard]] ScanOptions MakeScanOptions(SourceKind k);

} // namespace shine::gallery
