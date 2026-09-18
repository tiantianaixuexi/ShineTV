#include "gallery/decoders/PngDecoder.h"
#include "util/File.h"

#include "core/Log.h"

#include <png.h> // ✅ libpng 只在这个 .cpp 里出现

#include <algorithm>
#include <csetjmp>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace shine::gallery {

const char* DecodeErrorText(DecodeError error) noexcept {
    switch (error) {
    case DecodeError::NotFound: return "文件不存在或无法读取";
    case DecodeError::Corrupt: return "文件损坏或数据被截断";
    case DecodeError::Unsupported: return "不支持的图片格式";
    case DecodeError::OutOfMemory: return "内存不足（图片过大？）";
    case DecodeError::DecodeFailed: return "解码失败";
    }
    return "解码失败";
}

namespace decoders {
namespace {

constexpr std::uint32_t kMaxSide = 32768; // 单边上限（防呆，避免误传巨图）

struct MemReader {
    const std::byte* data = nullptr;
    std::size_t size = 0;
    std::size_t offset = 0;
};

void ReadFn(png_structp png, png_bytep out, png_size_t count) {
    auto* reader = static_cast<MemReader*>(png_get_io_ptr(png));
    if (reader == nullptr || reader->data == nullptr || reader->offset + count > reader->size) {
        png_error(png, "PNG 数据不足");
        return;
    }
    std::memcpy(out, reader->data + reader->offset, count);
    reader->offset += count;
}

void ErrorFn(png_structp png, png_const_charp message) {
    auto* error = static_cast<std::string*>(png_get_error_ptr(png));
    if (error != nullptr && error->empty()) {
        *error = (message != nullptr) ? message : "PNG 解码失败";
    }
    longjmp(png_jmpbuf(png), 1); // libpng 的错误跳转：一律转成返回值，绝不让它 abort
}

void WarnFn(png_structp, png_const_charp) {}

} // namespace

std::expected<Image, DecodeError> PngDecoder::DecodeMemory(std::span<const std::byte> bytes) {
    if (bytes.size() < 8) {
        return std::unexpected(DecodeError::Corrupt);
    }
    std::string error;
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, &error, ErrorFn, WarnFn);
    if (png == nullptr) {
        return std::unexpected(DecodeError::OutOfMemory);
    }
    png_infop info = png_create_info_struct(png);
    if (info == nullptr) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        return std::unexpected(DecodeError::OutOfMemory);
    }

    std::expected<Image, DecodeError> result = std::unexpected(DecodeError::DecodeFailed);
    if (setjmp(png_jmpbuf(png)) == 0) {
        MemReader reader{bytes.data(), bytes.size(), 0};
        png_set_read_fn(png, &reader, ReadFn);
        png_read_info(png, info);

        const png_uint_32 width = png_get_image_width(png, info);
        const png_uint_32 height = png_get_image_height(png, info);
        const png_byte colorType = png_get_color_type(png, info);
        const png_byte bitDepth = png_get_bit_depth(png, info);
        if (width == 0 || height == 0 || width > kMaxSide || height > kMaxSide) {
            png_error(png, "PNG 尺寸非法");
        } else {
            // 六条统一 transform：一律产出 RGBA8
            if (bitDepth == 16) {
                png_set_strip_16(png);
            }
            if (colorType == PNG_COLOR_TYPE_PALETTE) {
                png_set_palette_to_rgb(png);
            }
            if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) {
                png_set_expand_gray_1_2_4_to_8(png);
            }
            if (png_get_valid(png, info, PNG_INFO_tRNS) != 0) {
                png_set_tRNS_to_alpha(png);
            }
            if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
                png_set_gray_to_rgb(png);
            }
            if ((colorType & PNG_COLOR_MASK_ALPHA) == 0) {
                png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
            }
            png_read_update_info(png, info);

            Image image = Image::Allocate(width, height);
            if (!image.valid()) {
                result = std::unexpected(DecodeError::OutOfMemory);
            } else {
                // 逐行直接读进 Image 的缓冲（不在 libpng 内部再整块复制）
                std::vector<png_bytep> rows(height);
                for (png_uint_32 y = 0; y < height; ++y) {
                    rows[y] = reinterpret_cast<png_bytep>(image.data + static_cast<std::size_t>(y) * image.stride);
                }
                png_read_image(png, rows.data());
                png_read_end(png, nullptr);
                result = std::move(image);
            }
        }
    } else {
        result = std::unexpected(DecodeError::Corrupt);
    }

    png_destroy_read_struct(&png, &info, nullptr);
    return result;
}

bool PngDecoder::can_decode(std::span<const std::byte> header) const noexcept {
    static constexpr unsigned char kMagic[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (header.size() < 8) {
        return false;
    }
    for (std::size_t i = 0; i < 8; ++i) {
        if (static_cast<unsigned char>(header[i]) != kMagic[i]) {
            return false;
        }
    }
    return true;
}

std::expected<Image, DecodeError> PngDecoder::decode(const std::filesystem::path& path) {
    std::ifstream file = util::OpenInput(path);
    if (!file) {
        return std::unexpected(DecodeError::NotFound);
    }
    const std::string bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (bytes.empty()) {
        return std::unexpected(DecodeError::Corrupt);
    }
    return DecodeMemory(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
}

} // namespace decoders
} // namespace shine::gallery
