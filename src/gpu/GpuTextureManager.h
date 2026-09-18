#pragma once
// shine::gpu::GpuTextureManager —— 纹理生命周期归口（P4.1 S3）
//
// **仅 UI 线程调用**（DX11 immediate context 非线程安全）：worker 只负责产出 RGBA8 字节，
// 上传一律 `PostToUi` 后在本类完成 —— 见 `MEMORY.md` 的「异步任务规范」。
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <unordered_map>

#include "gpu/GpuTexture.h"

namespace shine::gpu {

class GpuTextureManager {
public:
    static GpuTextureManager& Instance();

    // RGBA8 用 span 传；失败用 expected 表达（Doc/RULES-LANG.md §13.3）
    [[nodiscard]] std::expected<GpuTextureHandle, GpuError> Upload(std::uint32_t w, std::uint32_t h,
                                                                   std::span<const std::byte> rgba8) noexcept;

    [[nodiscard]] GpuTexture* Get(GpuTextureHandle handle) noexcept;
    void Release(GpuTextureHandle handle) noexcept;
    void ReleaseAll() noexcept;
    [[nodiscard]] std::size_t UsedBytes() const noexcept;
    [[nodiscard]] std::size_t TextureCount() const noexcept;
    void OnDeviceLost() noexcept; // 句柄全部作废（调用方需重新上传）

private:
    GpuTextureManager() = default;
    std::unordered_map<std::uint64_t, std::unique_ptr<GpuTexture>> textures_;
    std::uint64_t nextId_ = 1;
    std::size_t usedBytes_ = 0;
};

[[nodiscard]] GpuTextureManager& Textures();

} // namespace shine::gpu
