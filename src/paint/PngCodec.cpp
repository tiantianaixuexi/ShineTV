#include "paint/PngCodec.h"

#include "core/Log.h"

#include <png.h>

#include <csetjmp>
#include <cstring>
#include <string>
#include <vector>

namespace shine::paint {
namespace {

void ErrorFn(png_structp png, png_const_charp msg) {
    auto* err = static_cast<std::string*>(png_get_error_ptr(png));
    if (err != nullptr && err->empty()) {
        *err = (msg != nullptr) ? msg : "PNG 失败";
    }
    longjmp(png_jmpbuf(png), 1);
}
void WarnFn(png_structp, png_const_charp) {}

struct VecWriter {
    std::vector<std::byte>* out = nullptr;
};
void WriteFn(png_structp png, png_bytep data, png_size_t len) {
    auto* w = static_cast<VecWriter*>(png_get_io_ptr(png));
    if (w == nullptr || w->out == nullptr) {
        return;
    }
    const std::byte* p = reinterpret_cast<const std::byte*>(data);
    w->out->insert(w->out->end(), p, p + len);
}
void FlushFn(png_structp) {}

struct MemReader {
    const std::byte* data = nullptr;
    std::size_t size = 0;
    std::size_t offset = 0;
};
void ReadFn(png_structp png, png_bytep out, png_size_t count) {
    auto* r = static_cast<MemReader*>(png_get_io_ptr(png));
    if (r == nullptr || r->offset + count > r->size) {
        png_error(png, "PNG 数据不足");
        return;
    }
    std::memcpy(out, r->data + r->offset, count);
    r->offset += count;
}

} // namespace

const char* PngCodecErrorText(PngCodecError e) noexcept {
    switch (e) {
    case PngCodecError::InvalidArgs: return "参数非法";
    case PngCodecError::EncodeFailed: return "PNG 编码失败";
    case PngCodecError::DecodeFailed: return "PNG 解码失败";
    case PngCodecError::OutOfMemory: return "内存不足";
    }
    return "PNG 操作失败";
}

std::expected<std::vector<std::byte>, PngCodecError> EncodePng(std::uint32_t width, std::uint32_t height,
                                                               std::span<const std::byte> rgba8) {
    if (width == 0 || height == 0 || rgba8.size() < static_cast<std::size_t>(width) * height * 4u) {
        return std::unexpected(PngCodecError::InvalidArgs);
    }
    std::vector<std::byte> out;
    std::string error;
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, &error, ErrorFn, WarnFn);
    if (png == nullptr) {
        return std::unexpected(PngCodecError::OutOfMemory);
    }
    png_infop info = png_create_info_struct(png);
    if (info == nullptr) {
        png_destroy_write_struct(&png, nullptr);
        return std::unexpected(PngCodecError::OutOfMemory);
    }
    VecWriter writer{&out};
    if (setjmp(png_jmpbuf(png)) == 0) {
        png_set_write_fn(png, &writer, WriteFn, FlushFn);
        png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                     PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
        png_write_info(png, info);
        std::vector<png_bytep> rows(height);
        for (std::uint32_t y = 0; y < height; ++y) {
            rows[y] = reinterpret_cast<png_bytep>(const_cast<std::byte*>(rgba8.data()) +
                                                  static_cast<std::size_t>(y) * width * 4u);
        }
        png_write_image(png, rows.data());
        png_write_end(png, nullptr);
        png_destroy_write_struct(&png, &info);
        return out;
    }
    png_destroy_write_struct(&png, &info);
    return std::unexpected(PngCodecError::EncodeFailed);
}

std::expected<std::vector<std::byte>, PngCodecError> DecodePng(std::span<const std::byte> pngBytes,
                                                               std::uint32_t& outW, std::uint32_t& outH) {
    outW = outH = 0;
    if (pngBytes.size() < 8) {
        return std::unexpected(PngCodecError::DecodeFailed);
    }
    std::string error;
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, &error, ErrorFn, WarnFn);
    if (png == nullptr) {
        return std::unexpected(PngCodecError::OutOfMemory);
    }
    png_infop info = png_create_info_struct(png);
    if (info == nullptr) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        return std::unexpected(PngCodecError::OutOfMemory);
    }
    std::vector<std::byte> rgba;
    MemReader reader{pngBytes.data(), pngBytes.size(), 0};
    if (setjmp(png_jmpbuf(png)) == 0) {
        png_set_read_fn(png, &reader, ReadFn);
        png_read_info(png, info);
        const png_uint_32 w = png_get_image_width(png, info);
        const png_uint_32 h = png_get_image_height(png, info);
        const int color = png_get_color_type(png, info);
        const int depth = png_get_bit_depth(png, info);
        if (depth == 16) {
            png_set_strip_16(png);
        }
        if (color == PNG_COLOR_TYPE_PALETTE) {
            png_set_palette_to_rgb(png);
        }
        if (color == PNG_COLOR_TYPE_GRAY && depth < 8) {
            png_set_expand_gray_1_2_4_to_8(png);
        }
        if (png_get_valid(png, info, PNG_INFO_tRNS)) {
            png_set_tRNS_to_alpha(png);
        }
        if (color == PNG_COLOR_TYPE_RGB || color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_PALETTE) {
            png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
        }
        if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA) {
            png_set_gray_to_rgb(png);
        }
        png_read_update_info(png, info);
        rgba.resize(static_cast<std::size_t>(w) * h * 4u);
        std::vector<png_bytep> rows(h);
        for (png_uint_32 y = 0; y < h; ++y) {
            rows[y] = reinterpret_cast<png_bytep>(rgba.data() + static_cast<std::size_t>(y) * w * 4u);
        }
        png_read_image(png, rows.data());
        png_destroy_read_struct(&png, &info, nullptr);
        outW = w;
        outH = h;
        return rgba;
    }
    png_destroy_read_struct(&png, &info, nullptr);
    return std::unexpected(PngCodecError::DecodeFailed);
}

std::expected<std::vector<std::byte>, PngCodecError> EncodeMaskPng(std::uint32_t width, std::uint32_t height,
                                                                   std::span<const std::uint8_t> mask) {
    if (width == 0 || height == 0 || mask.size() < static_cast<std::size_t>(width) * height) {
        return std::unexpected(PngCodecError::InvalidArgs);
    }
    std::vector<std::byte> rgba(static_cast<std::size_t>(width) * height * 4u);
    for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i) {
        const std::uint8_t v = mask[i] >= 128 ? 255 : 0;
        rgba[i * 4 + 0] = static_cast<std::byte>(v);
        rgba[i * 4 + 1] = static_cast<std::byte>(v);
        rgba[i * 4 + 2] = static_cast<std::byte>(v);
        rgba[i * 4 + 3] = static_cast<std::byte>(255);
    }
    return EncodePng(width, height, rgba);
}

int RunPngCodecSelfCheck() {
    int fail = 0;
    const auto expect = [&](bool ok, const char* name) {
        if (ok) {
            log::Info("pngcodec PASS {}", name);
        } else {
            ++fail;
            log::Error("pngcodec FAIL {}", name);
        }
    };
    constexpr std::uint32_t W = 64, H = 64;
    std::vector<std::byte> src(static_cast<std::size_t>(W) * H * 4u);
    for (std::size_t i = 0; i < static_cast<std::size_t>(W) * H; ++i) {
        src[i * 4 + 0] = static_cast<std::byte>(i % 256);
        src[i * 4 + 1] = static_cast<std::byte>((i * 3) % 256);
        src[i * 4 + 2] = static_cast<std::byte>(128);
        src[i * 4 + 3] = static_cast<std::byte>(255);
    }
    auto png = EncodePng(W, H, src);
    expect(png.has_value() && png->size() > 32, "EncodePng 64x64");
    if (png) {
        std::uint32_t w = 0, h = 0;
        auto back = DecodePng(*png, w, h);
        expect(back.has_value() && w == W && h == H, "DecodePng size");
        expect(back && *back == src, "pixel roundtrip");
    }
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(W) * H, 0);
    mask[10] = 255;
    auto mp = EncodeMaskPng(W, H, mask);
    expect(mp.has_value(), "EncodeMaskPng");
    log::Info("PNGCODEC_SELF_CHECK {}", fail == 0 ? "PASS" : "FAIL");
    return fail;
}

} // namespace shine::paint
