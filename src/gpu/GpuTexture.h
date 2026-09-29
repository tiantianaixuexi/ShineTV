#pragma once
// shine::gpu::GpuTexture —— OpenGL 2D 纹理封装
//
// **仅 UI 线程**创建/销毁（glGenTextures/glDeleteTextures 要当前 GL 上下文）。
// 生命周期周期由 `GpuTextureManager` 独占：外部只拿 `GpuTextureHandle`（uint64 句柄），
// 避免把 GL 纹理名传出模块边界。
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace shine::gpu {

struct GpuTextureHandle {
    std::uint64_t id = 0;
    [[nodiscard]] explicit operator bool() const noexcept { return id != 0; }
    [[nodiscard]] bool operator==(const GpuTextureHandle& o) const noexcept { return id == o.id; }
};

enum class GpuError { NotReady, DeviceLost, OutOfMemory, InvalidSize };

[[nodiscard]] const char* GpuErrorText(GpuError e) noexcept;

class GpuTexture {
public:
    GpuTexture() = default;
    GpuTexture(unsigned int texture, std::uint32_t w, std::uint32_t h) noexcept;
    ~GpuTexture();
    GpuTexture(const GpuTexture&) = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;
    GpuTexture(GpuTexture&& other) noexcept;
    GpuTexture& operator=(GpuTexture&& other) noexcept;

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] std::size_t bytes() const noexcept {
        return static_cast<std::size_t>(width_) * height_ * 4u; // RGBA8
    }
    [[nodiscard]] bool valid() const noexcept { return texture_ != 0; }
    // GL 纹理名，给渲染后端绑到采样单元上（ImGui 侧包成 ImTextureRef）。
    [[nodiscard]] unsigned int texture() const noexcept { return texture_; }

private:
    unsigned int texture_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

} // namespace shine::gpu
