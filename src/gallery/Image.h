#pragma once
// shine::gallery::Image —— RGBA8 CPU 图对象（G-S1 S2）
//
// - 像素由 **mimalloc** 分配（本项目全局用 mimalloc，别用 new/vector 混着来）；
// - **可移动、不可拷贝**（大图拷贝代价高，且所有权明确才能放心跨线程搬）；
// - `AllocateCallCount()` 是给验收用的打点：扫描阶段应保持 0（证明"零像素分配"）。
#include <cstddef>
#include <cstdint>

namespace shine::gallery {

struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t stride = 0; // = width * 4
    std::byte* data = nullptr;
    std::size_t bytes = 0; // = stride * height

    Image() = default;
    ~Image();
    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    // 失败（尺寸为 0 / 过大 / 分配失败）返回未 valid 的对象
    [[nodiscard]] static Image Allocate(std::uint32_t w, std::uint32_t h);
    [[nodiscard]] bool valid() const noexcept { return data != nullptr && width > 0 && height > 0; }

    void Reset() noexcept;

    // 验收打点：`Allocate()` 被调用过多少次（扫描/探针阶段必须为 0）
    [[nodiscard]] static std::uint64_t AllocateCallCount() noexcept;
    static void ResetAllocateCallCount() noexcept; // 仅测试用
};

} // namespace shine::gallery
