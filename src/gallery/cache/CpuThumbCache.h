#pragma once
// shine::gallery::CpuThumbCache —— CPU 缩略图 LRU（G-S8 S1）
//
// 只在 **UI 线程**读写。存的是已缩放的 `Image`（RGBA8，mimalloc）。
// key = ImageId + targetSize（换尺寸档后旧条目自然 miss，由 LRU 淘汰）。
#include "gallery/GalleryTypes.h"
#include "gallery/Image.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace shine::gallery {

class CpuThumbCache {
public:
    static CpuThumbCache& Instance() noexcept;

    void SetBudget(std::size_t bytes) noexcept;
    [[nodiscard]] std::size_t Budget() const noexcept { return budget_; }

    // 命中刷新 LRU；尺寸档不符视为未命中。返回的指针在下次 Insert/Clear/Evict 前有效。
    [[nodiscard]] const Image* Find(ImageId id, std::uint32_t targetSize) noexcept;
    void Insert(ImageId id, std::uint32_t targetSize, Image image);
    void Clear() noexcept;

    [[nodiscard]] std::size_t Bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::size_t Count() const noexcept { return entries_.size(); }
    [[nodiscard]] double HitRate() const noexcept;
    void ResetStats() noexcept;

private:
    CpuThumbCache() = default;
    void EvictIfNeeded() noexcept;

    struct Entry {
        Image image;
        std::uint32_t targetSize = 0;
        std::uint64_t tick = 0;
    };

    std::unordered_map<ImageId, Entry> entries_;
    std::size_t budget_ = 256ull << 20; // 默认 256 MB（Settings::galleryCpuBudgetMB）
    std::size_t bytes_ = 0;
    std::uint64_t tick_ = 0;
    std::uint64_t hits_ = 0;
    std::uint64_t lookups_ = 0;
};

} // namespace shine::gallery
