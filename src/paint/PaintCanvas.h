#pragma once
// shine::paint::PaintCanvas —— inpaint 画布 CPU 模型（P6.1）
//
// - 底图 RGBA8 + 初始快照 + 二值遮罩（仅 0/255）；
// - 像素 mimalloc 分配；**只在 UI 线程**读写（与绘制同线程）；
// - 每次修改 `Revision()` 递增，UI 据此决定是否重传 GPU 纹理。
#include "paint/PaintTypes.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace shine::paint {

class PaintCanvas {
public:
    PaintCanvas() = default;
    ~PaintCanvas();
    PaintCanvas(const PaintCanvas&) = delete;
    PaintCanvas& operator=(const PaintCanvas&) = delete;
    PaintCanvas(PaintCanvas&&) noexcept;
    PaintCanvas& operator=(PaintCanvas&&) noexcept;

    // 分配底图并用 defaultColor 填充，同时保存初始快照、清空遮罩。失败返回 false。
    bool Resize(std::uint32_t width, std::uint32_t height, Rgba8 defaultColor = {.r = 32, .g = 32, .b = 32, .a = 255});
    // 从外部 RGBA8 缓冲载入为底图（并复制为初始快照）
    bool LoadFromRgba(std::uint32_t width, std::uint32_t height, std::span<const std::byte> rgba8);

    [[nodiscard]] std::uint32_t Width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t Height() const noexcept { return height_; }
    [[nodiscard]] bool valid() const noexcept { return base_ != nullptr && width_ > 0 && height_ > 0; }
    [[nodiscard]] std::uint64_t Revision() const noexcept { return revision_; }

    [[nodiscard]] std::span<const std::byte> BasePixels() const noexcept;
    [[nodiscard]] std::span<const std::byte> InitialPixels() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> MaskPixels() const noexcept;

    // tool 决定含义：PaintBase=用 color 涂满底图；EraseBase=恢复初始；
    // PaintMask=遮罩全 255；EraseMask=遮罩全 0。
    void FillAll(Tool tool, Rgba8 color = {});
    void ClearMask();      // 遮罩全 0
    void ResetToInitial(); // 底图恢复快照 + 清空遮罩

    void BeginStroke() noexcept;
    void EndStroke() noexcept;
    [[nodiscard]] bool StrokeActive() const noexcept { return strokeActive_; }

    // UV ∈ [0,1]；radiusUv 相对 max(w,h)。线段光栅化 + 圆头；from≈to 画一个点。
    // tool=EraseBase 时从初始快照回写像素。
    void PaintStroke(Vec2 fromUv, Vec2 toUv, float radiusUv, Rgba8 color, Tool tool);

    // P6.1 S5：离线自测，返回 0=PASS
    [[nodiscard]] static int RunSelfCheck();

private:
    void BumpRevision() noexcept { ++revision_; }
    void StampDisk(float cx, float cy, float radiusPx, Rgba8 color, Tool tool);

    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::size_t stride_ = 0; // width * 4
    std::byte* base_ = nullptr;
    std::byte* initial_ = nullptr;
    std::uint8_t* mask_ = nullptr;
    std::uint64_t revision_ = 0;
    bool strokeActive_ = false;
};

} // namespace shine::paint
