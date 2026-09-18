#include "gallery/ExifOrientation.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace shine::gallery {
namespace {

[[nodiscard]] std::uint16_t ReadBe16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}
[[nodiscard]] std::uint32_t ReadBe32(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}
[[nodiscard]] std::uint16_t ReadLe16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
[[nodiscard]] std::uint32_t ReadLe32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

struct TiffCursor {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    bool little = false;

    [[nodiscard]] std::uint16_t U16(std::size_t off) const noexcept {
        if (off + 2 > size) {
            return 0;
        }
        return little ? ReadLe16(data + off) : ReadBe16(data + off);
    }
    [[nodiscard]] std::uint32_t U32(std::size_t off) const noexcept {
        if (off + 4 > size) {
            return 0;
        }
        return little ? ReadLe32(data + off) : ReadBe32(data + off);
    }
};

// 解析 TIFF IFD0：orientation 0x0112，Make 0x010F，Model 0x0110
void ParseTiff(std::span<const std::uint8_t> tiff, ExifMeta& out) {
    if (tiff.size() < 8) {
        return;
    }
    TiffCursor c;
    c.data = tiff.data();
    c.size = tiff.size();
    const std::uint8_t* p = tiff.data();
    if (p[0] == 'I' && p[1] == 'I') {
        c.little = true;
    } else if (p[0] == 'M' && p[1] == 'M') {
        c.little = false;
    } else {
        return;
    }
    const std::uint16_t magic = c.U16(2);
    if (magic != 42) {
        return;
    }
    const std::uint32_t ifd0 = c.U32(4);
    if (ifd0 + 2 > tiff.size()) {
        return;
    }
    const std::uint16_t count = c.U16(ifd0);
    std::string make;
    std::string model;
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::size_t e = ifd0 + 2 + static_cast<std::size_t>(i) * 12;
        if (e + 12 > tiff.size()) {
            break;
        }
        const std::uint16_t tag = c.U16(e);
        const std::uint16_t type = c.U16(e + 2);
        const std::uint32_t num = c.U32(e + 4);
        if (tag == 0x0112 && type == 3 && num >= 1) {
            const std::uint16_t v = c.U16(e + 8);
            if (v >= 1 && v <= 8) {
                out.orientation = static_cast<Orientation>(v);
            }
        } else if ((tag == 0x010F || tag == 0x0110) && type == 2 && num >= 1) {
            std::size_t strOff = 0;
            if (num <= 4) {
                strOff = e + 8;
            } else {
                strOff = c.U32(e + 8);
            }
            if (strOff < tiff.size()) {
                const std::size_t maxLen = std::min<std::size_t>(num, tiff.size() - strOff);
                std::string s(reinterpret_cast<const char*>(tiff.data() + strOff), maxLen);
                const auto z = s.find('\0');
                if (z != std::string::npos) {
                    s.resize(z);
                }
                if (tag == 0x010F) {
                    make = std::move(s);
                } else {
                    model = std::move(s);
                }
            }
        }
    }
    if (!make.empty() || !model.empty()) {
        out.camera = make;
        if (!model.empty()) {
            if (!out.camera.empty()) {
                out.camera += " ";
            }
            out.camera += model;
        }
    }
}

[[nodiscard]] ExifMeta ParseJpegExif(std::span<const std::uint8_t> bytes) {
    ExifMeta meta;
    if (bytes.size() < 4 || bytes[0] != 0xFF || bytes[1] != 0xD8) {
        return meta;
    }
    std::size_t i = 2;
    while (i + 4 <= bytes.size()) {
        if (bytes[i] != 0xFF) {
            ++i;
            continue;
        }
        const std::uint8_t marker = bytes[i + 1];
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            i += 2;
            continue;
        }
        if (marker == 0xDA || marker == 0xD9) {
            break; // SOS / EOI：EXIF 必在之前
        }
        if (i + 4 > bytes.size()) {
            break;
        }
        const std::uint16_t segLen = static_cast<std::uint16_t>((bytes[i + 2] << 8) | bytes[i + 3]);
        if (segLen < 2) {
            break;
        }
        const std::size_t segData = i + 4;
        const std::size_t segEnd = i + 2 + segLen;
        if (marker == 0xE1 && segData + 6 <= bytes.size() && std::memcmp(bytes.data() + segData, "Exif\0\0", 6) == 0) {
            ParseTiff(bytes.subspan(segData + 6, segEnd > segData + 6 ? segEnd - (segData + 6) : 0), meta);
            break;
        }
        i = segEnd;
    }
    return meta;
}

[[nodiscard]] ExifMeta ParsePngExif(std::span<const std::uint8_t> bytes) {
    ExifMeta meta;
    static constexpr std::uint8_t kSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (bytes.size() < 8 || std::memcmp(bytes.data(), kSig, 8) != 0) {
        return meta;
    }
    std::size_t i = 8;
    while (i + 8 <= bytes.size()) {
        const std::uint32_t len = ReadBe32(bytes.data() + i);
        const char type[5] = {static_cast<char>(bytes[i + 4]), static_cast<char>(bytes[i + 5]),
                              static_cast<char>(bytes[i + 6]), static_cast<char>(bytes[i + 7]), 0};
        const std::size_t dataOff = i + 8;
        if (dataOff + len > bytes.size()) {
            break;
        }
        if (std::strcmp(type, "eXIf") == 0) {
            ParseTiff(bytes.subspan(dataOff, len), meta);
            break;
        }
        if (std::strcmp(type, "IDAT") == 0 || std::strcmp(type, "IEND") == 0) {
            break; // eXIf 应在 IDAT 前
        }
        i = dataOff + len + 4; // +CRC
    }
    return meta;
}

void SwapPixels(Image& img, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) {
    auto* a = reinterpret_cast<std::uint8_t*>(img.data) + static_cast<std::size_t>(y0) * img.stride + x0 * 4;
    auto* b = reinterpret_cast<std::uint8_t*>(img.data) + static_cast<std::size_t>(y1) * img.stride + x1 * 4;
    std::uint8_t t[4];
    std::memcpy(t, a, 4);
    std::memcpy(a, b, 4);
    std::memcpy(b, t, 4);
}

} // namespace

const char* OrientationLabel(Orientation o) noexcept {
    switch (o) {
    case Orientation::Normal: return "1 正常";
    case Orientation::FlipH: return "2 水平镜像";
    case Orientation::Rotate180: return "3 旋转180°";
    case Orientation::FlipV: return "4 垂直镜像";
    case Orientation::Transpose: return "5 转置";
    case Orientation::Rotate90: return "6 顺时针90°";
    case Orientation::Transverse: return "7 反转置";
    case Orientation::Rotate270: return "8 逆时针90°";
    }
    return "1 正常";
}

bool SwapsAxes(Orientation o) noexcept {
    return o == Orientation::Transpose || o == Orientation::Rotate90 || o == Orientation::Transverse ||
           o == Orientation::Rotate270;
}

ExifMeta ReadExif(const std::filesystem::path& path) {
    const auto bytes = util::ReadFileBytes(path);
    if (!bytes || bytes->size() < 8) {
        return {};
    }
    const auto span = std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(bytes->data()),
                                                    bytes->size()};
    if (span[0] == 0xFF && span[1] == 0xD8) {
        return ParseJpegExif(span);
    }
    if (span[0] == 0x89 && span[1] == 'P') {
        return ParsePngExif(span);
    }
    return {};
}

void ApplyOrientation(Image& img, Orientation o) {
    if (o == Orientation::Normal || !img.valid()) {
        return;
    }
    const std::uint32_t w = img.width;
    const std::uint32_t h = img.height;

    auto flipH = [&]() {
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w / 2; ++x) {
                SwapPixels(img, x, y, w - 1 - x, y);
            }
        }
    };
    auto flipV = [&]() {
        for (std::uint32_t y = 0; y < h / 2; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                SwapPixels(img, x, y, x, h - 1 - y);
            }
        }
    };
    auto rotate180 = [&]() {
        for (std::uint32_t y = 0; y < h / 2; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                SwapPixels(img, x, y, w - 1 - x, h - 1 - y);
            }
        }
        if ((h & 1u) != 0) {
            const std::uint32_t y = h / 2;
            for (std::uint32_t x = 0; x < w / 2; ++x) {
                SwapPixels(img, x, y, w - 1 - x, y);
            }
        }
    };
    // 顺时针 90°：新图 H×W
    auto rotate90 = [&]() {
        Image out = Image::Allocate(h, w);
        if (!out.valid()) {
            return;
        }
        const auto* src = reinterpret_cast<const std::uint8_t*>(img.data);
        auto* dst = reinterpret_cast<std::uint8_t*>(out.data);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                // (x,y) → (h-1-y, x)
                const std::uint32_t dx = h - 1 - y;
                const std::uint32_t dy = x;
                std::memcpy(dst + static_cast<std::size_t>(dy) * out.stride + dx * 4,
                            src + static_cast<std::size_t>(y) * img.stride + x * 4, 4);
            }
        }
        img = std::move(out);
    };
    auto rotate270 = [&]() {
        Image out = Image::Allocate(h, w);
        if (!out.valid()) {
            return;
        }
        const auto* src = reinterpret_cast<const std::uint8_t*>(img.data);
        auto* dst = reinterpret_cast<std::uint8_t*>(out.data);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                // (x,y) → (y, w-1-x)
                const std::uint32_t dx = y;
                const std::uint32_t dy = w - 1 - x;
                std::memcpy(dst + static_cast<std::size_t>(dy) * out.stride + dx * 4,
                            src + static_cast<std::size_t>(y) * img.stride + x * 4, 4);
            }
        }
        img = std::move(out);
    };

    switch (o) {
    case Orientation::FlipH:
        flipH();
        break;
    case Orientation::Rotate180:
        rotate180();
        break;
    case Orientation::FlipV:
        flipV();
        break;
    case Orientation::Transpose:
        // 水平镜像 + 顺时针 90°
        flipH();
        rotate90();
        break;
    case Orientation::Rotate90:
        rotate90();
        break;
    case Orientation::Transverse:
        flipH();
        rotate270();
        break;
    case Orientation::Rotate270:
        rotate270();
        break;
    case Orientation::Normal:
    default:
        break;
    }
}

} // namespace shine::gallery
