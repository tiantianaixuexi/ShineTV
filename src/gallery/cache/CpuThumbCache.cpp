#include "gallery/cache/CpuThumbCache.h"

#include "core/Log.h"

#include <algorithm>
#include <utility>

namespace shine::gallery {

CpuThumbCache& CpuThumbCache::Instance() noexcept {
    static CpuThumbCache cache;
    return cache;
}

void CpuThumbCache::SetBudget(std::size_t bytes) noexcept {
    budget_ = bytes;
    EvictIfNeeded();
}

const Image* CpuThumbCache::Find(ImageId id, std::uint32_t targetSize) noexcept {
    ++lookups_;
    const auto it = entries_.find(id);
    if (it == entries_.end() || it->second.targetSize != targetSize || !it->second.image.valid()) {
        return nullptr;
    }
    ++hits_;
    it->second.tick = ++tick_;
    return &it->second.image;
}

void CpuThumbCache::Insert(ImageId id, std::uint32_t targetSize, Image image) {
    if (id == 0 || targetSize == 0 || !image.valid()) {
        return;
    }
    const std::size_t add = image.bytes;
    if (const auto it = entries_.find(id); it != entries_.end()) {
        bytes_ -= it->second.image.bytes;
        entries_.erase(it);
    }
    Entry entry;
    entry.targetSize = targetSize;
    entry.tick = ++tick_;
    entry.image = std::move(image);
    entries_.emplace(id, std::move(entry));
    bytes_ += add;
    EvictIfNeeded();
}

void CpuThumbCache::Clear() noexcept {
    entries_.clear();
    bytes_ = 0;
}

double CpuThumbCache::HitRate() const noexcept {
    return lookups_ == 0 ? 0.0 : static_cast<double>(hits_) / static_cast<double>(lookups_);
}

void CpuThumbCache::ResetStats() noexcept {
    hits_ = 0;
    lookups_ = 0;
}

void CpuThumbCache::EvictIfNeeded() noexcept {
    while (bytes_ > budget_ && !entries_.empty()) {
        auto victim = entries_.begin();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.tick < victim->second.tick) {
                victim = it;
            }
        }
        bytes_ -= victim->second.image.bytes;
        entries_.erase(victim); // Image 析构释放 mimalloc
    }
}

} // namespace shine::gallery
