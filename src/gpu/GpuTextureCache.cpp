#include "gpu/GpuTextureCache.h"

#include "core/Log.h"
#include "gpu/GpuTextureManager.h"

namespace shine::gpu {

GpuTextureCache& GpuTextureCache::Instance() {
    static GpuTextureCache cache;
    return cache;
}

GpuTextureCache& TextureCache() { return GpuTextureCache::Instance(); }

void GpuTextureCache::SetBudget(std::size_t bytes) noexcept {
    budget_ = bytes;
    EvictIfNeeded();
}

GpuTextureHandle GpuTextureCache::Find(std::uint64_t key) noexcept {
    ++lookups_;
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        return {};
    }
    ++hits_;
    it->second.tick = ++tick_; // 命中刷新（LRU）
    return it->second.handle;
}

void GpuTextureCache::Insert(std::uint64_t key, GpuTextureHandle handle) noexcept {
    auto* texture = Textures().Get(handle);
    if (texture == nullptr) {
        return;
    }
    if (const auto it = entries_.find(key); it != entries_.end()) {
        if (it->second.handle == handle) {
            it->second.tick = ++tick_;
            return;
        }
        bytes_ -= it->second.bytes;
        Textures().Release(it->second.handle);
        entries_.erase(it);
    }
    entries_.emplace(key, Entry{handle, texture->bytes(), ++tick_});
    bytes_ += texture->bytes();
    EvictIfNeeded();
}

void GpuTextureCache::Erase(std::uint64_t key) noexcept {
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        return;
    }
    bytes_ -= it->second.bytes;
    Textures().Release(it->second.handle);
    entries_.erase(it);
}

void GpuTextureCache::EvictIfNeeded() noexcept {
    while (bytes_ > budget_ && !entries_.empty()) {
        auto victim = entries_.begin();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.tick < victim->second.tick) {
                victim = it;
            }
        }
        bytes_ -= victim->second.bytes;
        Textures().Release(victim->second.handle);
        entries_.erase(victim);
    }
}

void GpuTextureCache::Clear() noexcept {
    for (const auto& [key, entry] : entries_) {
        (void)key;
        Textures().Release(entry.handle);
    }
    entries_.clear();
    bytes_ = 0;
}

void GpuTextureCache::OnDeviceLost() noexcept {
    const std::size_t dropped = entries_.size();
    entries_.clear(); // 纹理已随设备失效：条目全清，调用方按需重新上传
    bytes_ = 0;
    log::Warn("纹理缓存随设备丢失清空（{} 项）", dropped);
}

double GpuTextureCache::HitRate() const noexcept {
    return lookups_ == 0 ? 0.0 : static_cast<double>(hits_) / static_cast<double>(lookups_);
}

void GpuTextureCache::ResetStats() noexcept {
    hits_ = 0;
    lookups_ = 0;
}

} // namespace shine::gpu
