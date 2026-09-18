#include "gpu/GpuTexture.h"

#include <utility>

namespace shine::gpu {

const char* GpuErrorText(GpuError e) noexcept {
    switch (e) {
    case GpuError::NotReady: return "GPU 设备未就绪";
    case GpuError::DeviceLost: return "设备丢失";
    case GpuError::OutOfMemory: return "显存分配失败";
    case GpuError::InvalidSize: return "尺寸非法（0 或过大）";
    }
    return "未知 GPU 错误";
}

GpuTexture::GpuTexture(ID3D11Texture2D* texture, ID3D11ShaderResourceView* view, std::uint32_t w,
                       std::uint32_t h) noexcept
    : texture_(texture), view_(view), width_(w), height_(h) {}

GpuTexture::~GpuTexture() {
    if (view_ != nullptr) {
        view_->Release();
        view_ = nullptr;
    }
    if (texture_ != nullptr) {
        texture_->Release();
        texture_ = nullptr;
    }
}

GpuTexture::GpuTexture(GpuTexture&& other) noexcept
    : texture_(std::exchange(other.texture_, nullptr)), view_(std::exchange(other.view_, nullptr)),
      width_(std::exchange(other.width_, 0)), height_(std::exchange(other.height_, 0)) {}

GpuTexture& GpuTexture::operator=(GpuTexture&& other) noexcept {
    if (this != &other) {
        if (view_ != nullptr) {
            view_->Release();
        }
        if (texture_ != nullptr) {
            texture_->Release();
        }
        texture_ = std::exchange(other.texture_, nullptr);
        view_ = std::exchange(other.view_, nullptr);
        width_ = std::exchange(other.width_, 0);
        height_ = std::exchange(other.height_, 0);
    }
    return *this;
}

} // namespace shine::gpu
