#pragma once
// shine::gpu::GpuTextureCache —— 通用 GPU 纹理 LRU（P4.1 S4）
//
// **图库（G 线）、媒体预览、画布共用这一份**：key 为 `uint64_t`（图库传 ImageId，
// 媒体传自己的哈希，画布传 revision），value 是 `GpuTextureHandle`。
// 只做"查找/记账/淘汰"，不做上传 —— 上传由调用方在 UI 线程走 `gpu::Textures().Upload()`。
#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "gpu/GpuTexture.h"

namespace shine::gpu {

class GpuTextureCache {
public:
    static GpuTextureCache& Instance();

    void SetBudget(std::size_t bytes) noexcept;
    [[nodiscard]] std::size_t Budget() const noexcept { return budget_; }

    [[nodiscard]] GpuTextureHandle Find(std::uint64_t key) noexcept; // 命中刷新 LRU；未命中返回空句柄
    void Insert(std::uint64_t key, GpuTextureHandle handle) noexcept; // 同 key 会替换并释放旧纹理
    void Erase(std::uint64_t key) noexcept; // 删除单键并释放纹理（图库 Reset 只清自己的 key）
    void EvictIfNeeded() noexcept;
    void Clear() noexcept;
    void OnDeviceLost() noexcept; // 纹理已失效 → 条目全清（调用方按需重新上传）
    void ResetStats() noexcept;

    [[nodiscard]] std::size_t Bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::size_t Count() const noexcept { return entries_.size(); }
    [[nodiscard]] double HitRate() const noexcept;

private:
    GpuTextureCache() = default;

    struct Entry {
        GpuTextureHandle handle;
        std::size_t bytes = 0;
        std::uint64_t tick = 0;
    };
    std::unordered_map<std::uint64_t, Entry> entries_;
    std::size_t budget_ = 512ull << 20; // 默认 512 MB（G 线默认；设置窗口可改）
    std::size_t bytes_ = 0;
    std::uint64_t tick_ = 0;
    std::uint64_t hits_ = 0;
    std::uint64_t lookups_ = 0;
};

[[nodiscard]] GpuTextureCache& TextureCache();

} // namespace shine::gpu
