#pragma once
// shine::gpu::GpuTexture —— DX11 Texture2D + SRV 封装（P4.1 S2）
//
// **仅 UI 线程**创建/释放（DX11 的 immediate context 不是线程安全的）。
// 纹理生命周期归 `GpuTextureManager` 独占：外部只拿 `GpuTextureHandle`（uint64 句柄），
// 避免裸 SRV 传出模块边界。
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#include <d3d11.h>
#include <imgui.h>

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
    GpuTexture(ID3D11Texture2D* texture, ID3D11ShaderResourceView* view, std::uint32_t w, std::uint32_t h) noexcept;
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
    // ImGui 1.93：ImTextureID = ImU64 → 直接放进 SRV 指针值
    [[nodiscard]] ImTextureID imgui_id() const noexcept {
        return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(view_));
    }
    [[nodiscard]] bool valid() const noexcept { return view_ != nullptr; }

private:
    ID3D11Texture2D* texture_ = nullptr;
    ID3D11ShaderResourceView* view_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

} // namespace shine::gpu
