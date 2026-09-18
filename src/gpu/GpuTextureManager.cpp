#include "gpu/GpuTextureManager.h"

#include "core/Log.h"
#include "gpu/GpuDevice.h"

#include <utility>

namespace shine::gpu {
namespace {

// 单张纹理上限 1 GiB（防止误传尺寸把显存打爆）
constexpr std::size_t kMaxTextureBytes = 1ull << 30;

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

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = rgba8.data();
    init.SysMemPitch = w * 4u;

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = Device()->CreateTexture2D(&desc, &init, &texture);
    if (FAILED(hr) || texture == nullptr) {
        log::Warn("纹理创建失败 {}x{}（hr=0x{:08X}）", w, h, static_cast<unsigned>(hr));
        return std::unexpected(hr == E_OUTOFMEMORY ? GpuError::OutOfMemory : GpuError::DeviceLost);
    }
    ID3D11ShaderResourceView* view = nullptr;
    hr = Device()->CreateShaderResourceView(texture, nullptr, &view);
    if (FAILED(hr) || view == nullptr) {
        texture->Release();
        log::Warn("SRV 创建失败（hr=0x{:08X}）", static_cast<unsigned>(hr));
        return std::unexpected(GpuError::DeviceLost);
    }

    auto owned = std::make_unique<GpuTexture>(texture, view, w, h);
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
