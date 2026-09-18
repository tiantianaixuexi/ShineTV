#include "gallery/Image.h"

#include "core/Log.h"

#include <mimalloc.h>

#include <atomic>
#include <utility>

namespace shine::gallery {
namespace {

std::atomic<std::uint64_t> g_allocateCalls{0};
constexpr std::uint64_t kMaxPixels = 1ull << 26; // 64 MPix 上限（防呆，避免误传尺寸打爆内存）

} // namespace

Image::~Image() { Reset(); }

Image::Image(Image&& other) noexcept
    : width(std::exchange(other.width, 0)), height(std::exchange(other.height, 0)),
      stride(std::exchange(other.stride, 0)), data(std::exchange(other.data, nullptr)),
      bytes(std::exchange(other.bytes, 0)) {}

Image& Image::operator=(Image&& other) noexcept {
    if (this != &other) {
        Reset();
        width = std::exchange(other.width, 0);
        height = std::exchange(other.height, 0);
        stride = std::exchange(other.stride, 0);
        data = std::exchange(other.data, nullptr);
        bytes = std::exchange(other.bytes, 0);
    }
    return *this;
}

void Image::Reset() noexcept {
    if (data != nullptr) {
        mi_free(data);
        data = nullptr;
    }
    width = 0;
    height = 0;
    stride = 0;
    bytes = 0;
}

Image Image::Allocate(std::uint32_t w, std::uint32_t h) {
    g_allocateCalls.fetch_add(1, std::memory_order_relaxed);
    Image image;
    if (w == 0 || h == 0) {
        return image;
    }
    const std::uint64_t pixels = static_cast<std::uint64_t>(w) * h;
    if (pixels > kMaxPixels) {
        log::Warn("Image::Allocate 拒绝过大尺寸 {}x{}", w, h);
        return image;
    }
    const std::size_t bytes = static_cast<std::size_t>(pixels) * 4u;
    auto* buffer = static_cast<std::byte*>(mi_malloc(bytes));
    if (buffer == nullptr) {
        log::Warn("Image::Allocate 分配失败 {}x{}（{} 字节）", w, h, bytes);
        return image;
    }
    image.width = w;
    image.height = h;
    image.stride = static_cast<std::size_t>(w) * 4u;
    image.data = buffer;
    image.bytes = bytes;
    return image;
}

std::uint64_t Image::AllocateCallCount() noexcept { return g_allocateCalls.load(std::memory_order_relaxed); }

void Image::ResetAllocateCallCount() noexcept { g_allocateCalls.store(0, std::memory_order_relaxed); }

} // namespace shine::gallery
