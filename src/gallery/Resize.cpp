#include "gallery/Resize.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace shine::gallery {
namespace {

// f≥2 时对源图做 f×f box 均值；不足整格的边缘单独平均
[[nodiscard]] Image BoxIntegerFactor(const Image& src, std::uint32_t factor) {
    const std::uint32_t dstW = std::max(1u, src.width / factor);
    const std::uint32_t dstH = std::max(1u, src.height / factor);
    Image out = Image::Allocate(dstW, dstH);
    if (!out.valid()) {
        return {};
    }
    const auto* s = reinterpret_cast<const std::uint8_t*>(src.data);
    auto* d = reinterpret_cast<std::uint8_t*>(out.data);
    for (std::uint32_t y = 0; y < dstH; ++y) {
        const std::uint32_t y0 = y * factor;
        const std::uint32_t y1 = std::min(src.height, y0 + factor);
        for (std::uint32_t x = 0; x < dstW; ++x) {
            const std::uint32_t x0 = x * factor;
            const std::uint32_t x1 = std::min(src.width, x0 + factor);
            std::uint32_t sum[4] = {0, 0, 0, 0};
            std::uint32_t n = 0;
            for (std::uint32_t yy = y0; yy < y1; ++yy) {
                const std::uint8_t* row = s + static_cast<std::size_t>(yy) * src.stride +
                                          static_cast<std::size_t>(x0) * 4u;
                for (std::uint32_t xx = x0; xx < x1; ++xx) {
                    sum[0] += row[0];
                    sum[1] += row[1];
                    sum[2] += row[2];
                    sum[3] += row[3];
                    row += 4;
                    ++n;
                }
            }
            std::uint8_t* p = d + static_cast<std::size_t>(y) * out.stride + static_cast<std::size_t>(x) * 4u;
            if (n == 0) {
                p[0] = p[1] = p[2] = 0;
                p[3] = 255;
                continue;
            }
            p[0] = static_cast<std::uint8_t>(sum[0] / n);
            p[1] = static_cast<std::uint8_t>(sum[1] / n);
            p[2] = static_cast<std::uint8_t>(sum[2] / n);
            p[3] = static_cast<std::uint8_t>(sum[3] / n);
        }
    }
    return out;
}

// 通用 box：把 src 精确缩到 dstW×dstH（允许非整数比例）
[[nodiscard]] Image BoxToExact(const Image& src, std::uint32_t dstW, std::uint32_t dstH) {
    Image out = Image::Allocate(dstW, dstH);
    if (!out.valid()) {
        return {};
    }
    const auto* s = reinterpret_cast<const std::uint8_t*>(src.data);
    auto* d = reinterpret_cast<std::uint8_t*>(out.data);
    const double sx = static_cast<double>(src.width) / static_cast<double>(dstW);
    const double sy = static_cast<double>(src.height) / static_cast<double>(dstH);
    for (std::uint32_t y = 0; y < dstH; ++y) {
        const double fy0 = y * sy;
        const double fy1 = (y + 1) * sy;
        const std::uint32_t y0 = static_cast<std::uint32_t>(fy0);
        const std::uint32_t y1 = std::min(src.height, static_cast<std::uint32_t>(std::ceil(fy1)));
        for (std::uint32_t x = 0; x < dstW; ++x) {
            const double fx0 = x * sx;
            const double fx1 = (x + 1) * sx;
            const std::uint32_t x0 = static_cast<std::uint32_t>(fx0);
            const std::uint32_t x1 = std::min(src.width, static_cast<std::uint32_t>(std::ceil(fx1)));
            std::uint32_t sum[4] = {0, 0, 0, 0};
            std::uint32_t n = 0;
            for (std::uint32_t yy = y0; yy < y1; ++yy) {
                const std::uint8_t* row = s + static_cast<std::size_t>(yy) * src.stride +
                                          static_cast<std::size_t>(x0) * 4u;
                for (std::uint32_t xx = x0; xx < x1; ++xx) {
                    sum[0] += row[0];
                    sum[1] += row[1];
                    sum[2] += row[2];
                    sum[3] += row[3];
                    row += 4;
                    ++n;
                }
            }
            std::uint8_t* p = d + static_cast<std::size_t>(y) * out.stride + static_cast<std::size_t>(x) * 4u;
            if (n == 0) {
                p[0] = p[1] = p[2] = 0;
                p[3] = 255;
                continue;
            }
            p[0] = static_cast<std::uint8_t>(sum[0] / n);
            p[1] = static_cast<std::uint8_t>(sum[1] / n);
            p[2] = static_cast<std::uint8_t>(sum[2] / n);
            p[3] = static_cast<std::uint8_t>(sum[3] / n);
        }
    }
    return out;
}

// 双线性（RGBA8，忽略跨像素的 alpha 预乘 —— 缩略图场景足够）
[[nodiscard]] Image BilinearToExact(const Image& src, std::uint32_t dstW, std::uint32_t dstH) {
    if (src.width == dstW && src.height == dstH) {
        Image out = Image::Allocate(dstW, dstH);
        if (out.valid()) {
            std::memcpy(out.data, src.data, src.bytes);
        }
        return out;
    }
    Image out = Image::Allocate(dstW, dstH);
    if (!out.valid()) {
        return {};
    }
    const auto* s = reinterpret_cast<const std::uint8_t*>(src.data);
    auto* d = reinterpret_cast<std::uint8_t*>(out.data);
    const double scaleX = static_cast<double>(src.width) / static_cast<double>(dstW);
    const double scaleY = static_cast<double>(src.height) / static_cast<double>(dstH);
    for (std::uint32_t y = 0; y < dstH; ++y) {
        const double fy = (y + 0.5) * scaleY - 0.5;
        const std::int32_t y0 = static_cast<std::int32_t>(std::floor(fy));
        const double ty = fy - y0;
        const std::uint32_t ya = static_cast<std::uint32_t>(std::clamp<std::int32_t>(y0, 0, static_cast<std::int32_t>(src.height) - 1));
        const std::uint32_t yb = static_cast<std::uint32_t>(std::clamp<std::int32_t>(y0 + 1, 0, static_cast<std::int32_t>(src.height) - 1));
        for (std::uint32_t x = 0; x < dstW; ++x) {
            const double fx = (x + 0.5) * scaleX - 0.5;
            const std::int32_t x0 = static_cast<std::int32_t>(std::floor(fx));
            const double tx = fx - x0;
            const std::uint32_t xa =
                static_cast<std::uint32_t>(std::clamp<std::int32_t>(x0, 0, static_cast<std::int32_t>(src.width) - 1));
            const std::uint32_t xb = static_cast<std::uint32_t>(
                std::clamp<std::int32_t>(x0 + 1, 0, static_cast<std::int32_t>(src.width) - 1));
            const std::uint8_t* p00 = s + static_cast<std::size_t>(ya) * src.stride + static_cast<std::size_t>(xa) * 4u;
            const std::uint8_t* p10 = s + static_cast<std::size_t>(ya) * src.stride + static_cast<std::size_t>(xb) * 4u;
            const std::uint8_t* p01 = s + static_cast<std::size_t>(yb) * src.stride + static_cast<std::size_t>(xa) * 4u;
            const std::uint8_t* p11 = s + static_cast<std::size_t>(yb) * src.stride + static_cast<std::size_t>(xb) * 4u;
            std::uint8_t* p = d + static_cast<std::size_t>(y) * out.stride + static_cast<std::size_t>(x) * 4u;
            for (int c = 0; c < 4; ++c) {
                const double top = p00[c] * (1.0 - tx) + p10[c] * tx;
                const double bot = p01[c] * (1.0 - tx) + p11[c] * tx;
                const double v = top * (1.0 - ty) + bot * ty;
                p[c] = static_cast<std::uint8_t>(std::clamp(v + 0.5, 0.0, 255.0));
            }
        }
    }
    return out;
}

[[nodiscard]] Image CopyImage(const Image& src) {
    Image out = Image::Allocate(src.width, src.height);
    if (out.valid()) {
        std::memcpy(out.data, src.data, src.bytes);
    }
    return out;
}

[[nodiscard]] std::uint32_t AtLeast1(double v) {
    const long r = std::lround(v);
    return static_cast<std::uint32_t>(r < 1 ? 1 : r);
}

void TargetSize(std::uint32_t srcW, std::uint32_t srcH, std::uint32_t maxSide, std::uint32_t& dstW,
                std::uint32_t& dstH) {
    const std::uint32_t srcMax = std::max(srcW, srcH);
    if (srcMax <= maxSide) {
        dstW = srcW;
        dstH = srcH;
        return;
    }
    const double scale = static_cast<double>(maxSide) / static_cast<double>(srcMax);
    dstW = AtLeast1(static_cast<double>(srcW) * scale);
    dstH = AtLeast1(static_cast<double>(srcH) * scale);
    if (std::max(dstW, dstH) > maxSide) {
        if (dstW >= dstH) {
            dstW = maxSide;
            dstH = AtLeast1(static_cast<double>(srcH) * (static_cast<double>(maxSide) / srcW));
        } else {
            dstH = maxSide;
            dstW = AtLeast1(static_cast<double>(srcW) * (static_cast<double>(maxSide) / srcH));
        }
    }
}

} // namespace

Image ResizeBox(const Image& src, std::uint32_t maxSide) {
    if (!src.valid() || maxSide == 0) {
        return {};
    }
    std::uint32_t dstW = 0;
    std::uint32_t dstH = 0;
    TargetSize(src.width, src.height, maxSide, dstW, dstH);
    if (dstW == src.width && dstH == src.height) {
        return CopyImage(src);
    }

    const std::uint32_t srcMax = std::max(src.width, src.height);
    const std::uint32_t factor = srcMax / maxSide; // 整数部分
    if (factor >= 2) {
        Image box = BoxIntegerFactor(src, factor);
        if (!box.valid()) {
            return {};
        }
        if (box.width == dstW && box.height == dstH) {
            return box;
        }
        // 双线性收尾到精确目标尺寸
        return BilinearToExact(box, dstW, dstH);
    }
    // 非整数比例：先 box 到目标，再对奇数边缘用双线性平滑一次（dst 与 box 相同时直接返回）
    Image box = BoxToExact(src, dstW, dstH);
    return box;
}

} // namespace shine::gallery
