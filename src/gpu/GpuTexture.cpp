#include "gpu/GpuTexture.h"

#include "gpu/GpuDevice.h"

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

GpuTexture::GpuTexture(unsigned int texture, std::uint32_t w, std::uint32_t h) noexcept
    : texture_(texture), width_(w), height_(h) {}

GpuTexture::~GpuTexture() {
    if (texture_ != 0) {
        // GL 纹理名不是指针，不能靠置 0 让别人释放 —— 必须显式删。
        if (const GlApi& gl = Gl(); gl.complete()) {
            gl.deleteTextures(1, &texture_);
        }
        texture_ = 0;
    }
}

GpuTexture::GpuTexture(GpuTexture&& other) noexcept
    : texture_(std::exchange(other.texture_, 0u)),
      width_(std::exchange(other.width_, 0)),
      height_(std::exchange(other.height_, 0)) {}

GpuTexture& GpuTexture::operator=(GpuTexture&& other) noexcept {
    if (this != &other) {
        if (texture_ != 0) {
            if (const GlApi& gl = Gl(); gl.complete()) {
                gl.deleteTextures(1, &texture_);
            }
        }
        texture_ = std::exchange(other.texture_, 0u);
        width_ = std::exchange(other.width_, 0);
        height_ = std::exchange(other.height_, 0);
    }
    return *this;
}

} // namespace shine::gpu
