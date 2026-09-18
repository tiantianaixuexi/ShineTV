#pragma once
// shine::gallery::ThumbnailService —— 异步缩略图管线 + 两级 LRU（G-S7 / G-S8）
//
// 线程模型（只在 UI 线程调本头函数）：
//   worker：`ImageLoader::Load` + `ResizeBox` → `PostToUi`
//   UI：`DrainCompleted` 上传 GPU，并写入 CPU/GPU 两级缓存
//
// 缓存命中顺序：GPU `gpu::TextureCache` → CPU `CpuThumbCache` → worker 解码。
// GPU key = ImageId | (1ull<<63)（与媒体预览 key 隔离）。
// 淘汰：`EvictCaches()` 必须在 **本帧绘制之前** 调（`Gallery::Tick`）。
#include "gallery/GalleryTypes.h"
#include "gallery/decoders/IImageDecoder.h"
#include "gpu/GpuTexture.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace shine::gallery {

enum class ThumbPriority : int {
    Low = 0,
    Normal = 1,
    High = 2,
};

class ThumbnailService {
public:
    static ThumbnailService& Instance() noexcept;

    void Init();
    void Shutdown();

    void Request(ImageId id, std::filesystem::path path, std::uint32_t targetSize,
                 ThumbPriority priority = ThumbPriority::Normal);
    void Demote(ImageId id);
    void DrainCompleted();
    void Retry(ImageId id);
    void Reset(); // 条目换代/换尺寸档

    // G-S8：本帧绘制前淘汰 GPU LRU（CPU 在 Insert 时自淘汰）
    void EvictCaches();
    // 模拟设备丢失：释放 GPU 纹理 + 清 GPU 缓存；CPU 缩略图保留，下次 Request 重传
    void OnDeviceLost();
    // 缓存重放：清掉图库 GPU 条目后重走 Request，统计 CPU 命中 vs 回源解码
    [[nodiscard]] std::size_t ReplayFromCache();

    [[nodiscard]] LoadState State(ImageId id) const;
    [[nodiscard]] gpu::GpuTextureHandle Texture(ImageId id) const;
    [[nodiscard]] std::size_t InFlightAndQueued() const;
    [[nodiscard]] std::size_t ReadyCount() const;
    [[nodiscard]] std::size_t FailedCount() const;

    [[nodiscard]] std::size_t CpuCacheBytes() const;
    [[nodiscard]] std::size_t GpuCacheBytes() const;
    [[nodiscard]] double CpuHitRate() const;
    [[nodiscard]] double GpuHitRate() const;
    [[nodiscard]] double DiskHitRate() const; // G-S14
    [[nodiscard]] std::string CacheSummary() const;

    // G-S9：派工统计（验收「首次只为可见行发请求」）
    [[nodiscard]] std::size_t WorkerStarts() const;
    [[nodiscard]] std::size_t RequestCalls() const;
    void ResetRequestStats() noexcept;

private:
    ThumbnailService() = default;
    void Pump();
    void EnqueueWithCap(ImageId id, int priority);
    bool TryServeFromCache(ImageId id, std::uint32_t targetSize);
    void ApplyBudgetsFromSettings();

    struct Impl;
    [[nodiscard]] static Impl& Get() noexcept;
};

[[nodiscard]] ThumbnailService& Thumbs() noexcept;

} // namespace shine::gallery
