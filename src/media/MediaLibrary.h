#pragma once
// shine::media::MediaLibrary —— ComfyUI 历史产物清单 + 本地缓存 + 纹理获取（P4.2 / P4.3 / P4.5）
//
// 异步规范（`MEMORY.md`「异步任务规范」）：下载/解码/读盘一律在 worker；UI 线程只做
// 状态与纹理（`gpu::Textures().Upload`）。WS 回调（二进制预览帧 / prompt 事件）在 **WS 线程**，
// 本类里只做节流 + 投递，不直接改 UI 状态。
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "comfy/ComfyTypes.h"
#include "gallery/Image.h"
#include "gpu/GpuTexture.h"
#include "media/ImageFetch.h"

namespace shine::media {

struct MediaItem {
    std::uint64_t key = 0; // fileName+subfolder+type 哈希，兼作纹理缓存 key
    std::string fileName;
    std::string subfolder;
    std::string type;
    std::string kind; // image / video / audio（复用 comfy::ClassifyMediaKind）
    std::string promptId;
    std::int64_t createdAtMillis = 0;
    std::filesystem::path localPath; // 空 = 未缓存
    int width = 0;                    // 视频分辨率（P5.6：Shell 属性填；0 = 未知）
    int height = 0;
};

// Idle/…：常规取图状态；`Unavailable` = **本来就没有可显示的缩略图**（P5.6：视频未缓存，
// 或系统不给首帧）→ UI 显示 `—` 而不是"失败"，两者语义不同。
enum class TextureState { Idle, Loading, Ready, Failed, Unavailable };

class MediaLibrary {
public:
    static MediaLibrary& Instance();

    void Init(); // 建缓存目录 + 订阅 WS 预览帧 / prompt 事件
    void Shutdown();

    void Refresh(int maxItems = 100); // FetchHistory → 合并去重（时间倒序）

    // P5.6 S3：刷新历史，并把"最近输出"设为当前选中（生成完成后自动高亮刚出的产物）。
    // `preferFileName` 非空时优先选中同名条目（`VideoTaskRunner` 落盘的文件名），找不到才退回最新一条。
    void RefreshAndSelectLatest(std::string_view preferFileName = {});
    [[nodiscard]] const std::vector<MediaItem>& Items() const noexcept { return items_; }
    [[nodiscard]] const MediaItem* Latest() const noexcept;
    [[nodiscard]] const MediaItem* Selected() const noexcept;
    void Select(std::uint64_t key) noexcept { selectedKey_ = key; }
    [[nodiscard]] std::uint64_t SelectedKey() const noexcept { return selectedKey_; }

    // worker 下载 → 写盘 → PostToUi 回调；已缓存则直接命中（日志 cache hit）
    void EnsureLocal(const MediaItem& item, std::function<void(bool, std::filesystem::path)> onDone);
    [[nodiscard]] std::filesystem::path CacheDir() const;

    // 纹理（UI 线程）：命中即返回；未命中触发异步拉取+解码并返回空句柄（稍后命中）
    [[nodiscard]] gpu::GpuTextureHandle TextureFor(const MediaItem& item);
    [[nodiscard]] TextureState StateOf(std::uint64_t key) const;
    [[nodiscard]] std::string ErrorOf(std::uint64_t key) const;

    // —— P4.5：生成中预览 ——
    [[nodiscard]] gpu::GpuTextureHandle PreviewTexture() const noexcept { return previewTexture_; }
    [[nodiscard]] bool PreviewActive() const noexcept { return previewActive_; }
    [[nodiscard]] bool PreviewFromWs() const noexcept { return previewFromWs_; }
    [[nodiscard]] std::int64_t LastPreviewMs() const noexcept { return lastPreviewUiMs_; }
    void ClearPreview();

    [[nodiscard]] std::size_t CachedBytes() const;
    void ClearCache();
    void MarkUncached(std::uint64_t key); // 用户删掉本地缓存后调用（条目仍保留、标为“未缓存”）

private:
    MediaLibrary() = default;

    void OnBinaryFrame(const comfy::BinaryFrame& frame);        // **WS 线程**
    void OnPromptEventForPreview(const comfy::PromptEvent& ev); // **WS 线程**
    void ApplyFetched(FetchedImage&& img, bool asPreview);      // **UI 线程**
    void UploadPreview(gallery::Image&& image, bool fromWs);    // **UI 线程**
    void MergeHistory(const class comfy::HistoryResult& result);
    [[nodiscard]] bool StartFetch(const MediaItem& item, std::uint64_t key, bool asPreview);

    // P5.6 S2：视频条目走"Windows Shell 首帧缩略图"（没有本地文件就拿不到，见 `media/VideoThumb.h`）
    [[nodiscard]] gpu::GpuTextureHandle TextureForVideo(const MediaItem& item); // **UI 线程**
    void ApplyVideoThumb(std::uint64_t key, struct VideoThumbResult result);    // **UI 线程**

    std::vector<MediaItem> items_;
    std::unordered_map<std::uint64_t, TextureState> states_;
    std::unordered_map<std::uint64_t, std::string> errors_;
    // 视频首帧：key → 已经试过的**本地路径**（同一路径只试一次，避免每帧重发 worker 任务）
    std::unordered_map<std::uint64_t, std::filesystem::path> videoThumbTried_;
    std::uint64_t selectedKey_ = 0;
    bool selectLatestWhenMerged_ = false; // P5.6 S3：本次历史合并完要自动选中最近输出
    std::string pendingSelectName_;       // 优先按文件名匹配（生成器刚落盘的文件名）
    gpu::GpuTextureHandle previewTexture_;
    bool previewActive_ = false;
    bool previewFromWs_ = false;
    std::int64_t lastPreviewUiMs_ = 0;
    std::atomic<std::int64_t> lastWsPreviewMs_{0};  // WS 线程：节流 ≤ 2 次/秒
    std::int64_t lastFallbackMs_ = 0;               // UI 线程：兜底拉取节流
    bool inited_ = false;
};

} // namespace shine::media
