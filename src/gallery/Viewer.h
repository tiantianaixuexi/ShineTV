#pragma once
// shine::gallery::viewer —— 独立查看器状态与异步原图（G-S10）
//
// 仅 UI 线程调用。原图解码在 worker，结果 `PostToUi`；快速切换用 generation 丢弃旧请求。
// 绘制与输入在 `app/gallery/GalleryView.cpp`；本模块只做状态 / 导航 / 缓存。
#include "gallery/GalleryTypes.h"
#include "gpu/GpuTexture.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace shine::gallery::viewer {

struct State {
    ImageId id = 0;
    bool open = false;
    bool loading = false;
    float zoom = 1.f; // 相对「适应窗口」的倍率；1:1 时 = 1/fitScale
    float panX = 0.f; // 相对适应窗口居中位置的像素偏移
    float panY = 0.f;
    std::size_t index = 0; // 在 Model 中的下标
    std::size_t total = 0;
    std::uint32_t imgW = 0;
    std::uint32_t imgH = 0;
    std::string name; // 文件名 UTF-8
    std::uint64_t loadGen = 0;
};

void Open(ImageId id);
void Close() noexcept;
[[nodiscard]] bool IsOpen() noexcept;
[[nodiscard]] const State& Get() noexcept;

// 每帧：丢弃过期异步结果 + 保持 total/index 与模型同步（Tick 里也可调）
void Tick();

// 导航：dir=+1 下一张 / -1 上一张，**跳过 Failed**；返回是否切换成功
[[nodiscard]] bool Step(int dir);

void Fit() noexcept;         // zoom=1, pan=0
void ActualPixels(float fitScale) noexcept; // 1:1：zoom = 1/fitScale（fitScale>0）
void OnWheel(float wheelSteps, float mouseInContentX, float mouseInContentY, float contentW, float contentH,
             float fitScale) noexcept;
void OnDrag(float dx, float dy) noexcept;
void ClampPan(float contentW, float contentH, float drawW, float drawH) noexcept;

// 原图纹理（未就绪返回空句柄）；缩略图占位走 ThumbnailService
[[nodiscard]] gpu::GpuTextureHandle FullTexture(ImageId id) noexcept;
// 统计：当前查看器持有的原图纹理数（验收：连切不应无界上涨）
[[nodiscard]] std::size_t FullCacheCount() noexcept;
[[nodiscard]] std::size_t FullLoads() noexcept;
[[nodiscard]] std::size_t DiscardedLoads() noexcept;

} // namespace shine::gallery::viewer
