#include "gpu/GpuTextureManager.h"

#include "core/Log.h"
#include "gpu/GpuDevice.h"

#include <utility>

namespace shine::gpu {
namespace {

// 单张纹理上限 1 GiB（防止误传尺寸把显存打爆）
constexpr std::size_t kMaxTextureBytes = 1ull << 30;

// GL 1.1 常量，理由同 GpuDevice.cpp：不能拉 <GL/gl.h>。
constexpr unsigned int GL_TEXTURE_2D = 0x0DE1;
constexpr unsigned int GL_TEXTURE_WRAP_S = 0x2802;
constexpr unsigned int GL_TEXTURE_WRAP_T = 0x2803;
constexpr unsigned int GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr unsigned int GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr unsigned int GL_RGBA = 0x1908;
constexpr unsigned int GL_UNSIGNED_BYTE = 0x1401;
constexpr unsigned int GL_CLAMP_TO_EDGE = 0x812F;
constexpr int GL_LINEAR = 0x2601;
constexpr unsigned int GL_UNPACK_ALIGNMENT = 0x0CF5;

} // namespace

GpuTextureManager& GpuTextureManager::Instance() {
    static GpuTextureManager manager;
    return manager;
}

GpuTextureManager& Textures() { return GpuTextureManager::Instance(); }

std::expected<GpuTextureHandle, GpuError> GpuTextureManager::Upload(std::uint32_t w, std::uint32_t h,
                                                                   std::span<const std::byte> rgba8) noexcept {
    if (!Ready()) {
        return std::unexpected(GpuError::NotReady);
    }
    if (w == 0 || h == 0) {
        return std::unexpected(GpuError::InvalidSize);
    }
    const std::size_t need = static_cast<std::size_t>(w) * h * 4u;
    if (need > kMaxTextureBytes || rgba8.size() < need) {
        return std::unexpected(GpuError::InvalidSize);
    }

    const GlApi& gl = Gl();

    // 行距不是 4 的倍数时 GL 会按 UNPACK_ALIGNMENT 补齐，读出来整行错位。
    // 缩略图解码出来的宽度不保证是 4 的倍数，这里显式按 1 字节对齐。
    gl.pixelStorei(GL_UNPACK_ALIGNMENT, 1);

    unsigned int name = 0;
    gl.genTextures(1, &name);
    if (name == 0) {
        log::Warn("glGenTextures 失败 {}x{}（纹理名 0）", w, h);
        return std::unexpected(GpuError::OutOfMemory);
    }
    gl.bindTexture(GL_TEXTURE_2D, name);
    gl.texImage2D(GL_TEXTURE_2D, 0, static_cast<int>(GL_RGBA), static_cast<int>(w), static_cast<int>(h), 0,
                  GL_RGBA, GL_UNSIGNED_BYTE, rgba8.data());
    // 缩略图尺寸不一，采样必须钳边 + 线性，否则相邻缩略图会互相渗色。
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.bindTexture(GL_TEXTURE_2D, 0);

    auto owned = std::make_unique<GpuTexture>(name, w, h);
    const std::uint64_t id = nextId_++;
    usedBytes_ += owned->bytes();
    textures_.emplace(id, std::move(owned));
    return GpuTextureHandle{id};
}

GpuTexture* GpuTextureManager::Get(GpuTextureHandle handle) noexcept {
    const auto it = textures_.find(handle.id);
    return it == textures_.end() ? nullptr : it->second.get();
}

void GpuTextureManager::Release(GpuTextureHandle handle) noexcept {
    const auto it = textures_.find(handle.id);
    if (it == textures_.end()) {
        return;
    }
    usedBytes_ -= it->second->bytes();
    textures_.erase(it);
}

void GpuTextureManager::ReleaseAll() noexcept {
    textures_.clear();
    usedBytes_ = 0;
}

std::size_t GpuTextureManager::UsedBytes() const noexcept { return usedBytes_; }

std::size_t GpuTextureManager::TextureCount() const noexcept { return textures_.size(); }

void GpuTextureManager::OnDeviceLost() noexcept {
    ReleaseAll();
    log::Warn("GPU 设备丢失：已释放全部纹理，后续按需重建");
}

} // namespace shine::gpu
