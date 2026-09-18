#include "gallery/decoders/JpegDecoder.h"
#include "util/Encoding.h"
#include "util/File.h"

#include "core/Log.h"

#include <jpeglib.h> // ✅ libjpeg 只在本 .cpp

#include <csetjmp>
#include <cstdint>
#include <cstring>
#include <vector>

namespace shine::gallery::decoders {
namespace {

struct JpegErrorMgr {
    jpeg_error_mgr pub;
    jmp_buf jump;
    char message[JMSG_LENGTH_MAX]{};
};

void JpegErrorExit(j_common_ptr cinfo) {
    auto* err = reinterpret_cast<JpegErrorMgr*>(cinfo->err);
    (*cinfo->err->format_message)(cinfo, err->message);
    longjmp(err->jump, 1);
}

constexpr std::uint32_t kMaxSide = 32768;

} // namespace

bool JpegDecoder::can_decode(std::span<const std::byte> header) const noexcept {
    // JPEG SOI：FF D8 FF
    return header.size() >= 3 && header[0] == std::byte{0xFF} && header[1] == std::byte{0xD8} &&
           header[2] == std::byte{0xFF};
}

std::expected<Image, DecodeError> JpegDecoder::decode(const std::filesystem::path& path) {
    const auto bytes = util::ReadFileBytes(path);
    if (!bytes) {
        return std::unexpected(DecodeError::NotFound);
    }
    if (bytes->size() < 4) {
        return std::unexpected(DecodeError::Corrupt);
    }

    jpeg_decompress_struct cinfo{};
    JpegErrorMgr jerr{};
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = JpegErrorExit;

    if (setjmp(jerr.jump) != 0) {
        jpeg_destroy_decompress(&cinfo);
        log::Warn("JPEG 解码失败：{} —— {}", util::PathToUtf8(path), jerr.message);
        return std::unexpected(DecodeError::Corrupt);
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, reinterpret_cast<const unsigned char*>(bytes->data()),
                 static_cast<unsigned long>(bytes->size()));
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return std::unexpected(DecodeError::Corrupt);
    }

    // 输出统一 RGBA8：Gray / RGB / CMYK 都要覆盖
    switch (cinfo.jpeg_color_space) {
    case JCS_GRAYSCALE:
        cinfo.out_color_space = JCS_GRAYSCALE;
        break;
    case JCS_CMYK:
    case JCS_YCCK:
        cinfo.out_color_space = JCS_CMYK;
        break;
    default:
        cinfo.out_color_space = JCS_RGB;
        break;
    }

    jpeg_start_decompress(&cinfo);
    const std::uint32_t w = static_cast<std::uint32_t>(cinfo.output_width);
    const std::uint32_t h = static_cast<std::uint32_t>(cinfo.output_height);
    const std::uint32_t comps = static_cast<std::uint32_t>(cinfo.output_components);
    if (w == 0 || h == 0 || w > kMaxSide || h > kMaxSide) {
        jpeg_destroy_decompress(&cinfo);
        return std::unexpected(DecodeError::DecodeFailed);
    }

    Image img = Image::Allocate(w, h);
    if (!img.valid()) {
        jpeg_destroy_decompress(&cinfo);
        return std::unexpected(DecodeError::OutOfMemory);
    }

    std::vector<std::uint8_t> row(static_cast<std::size_t>(w) * comps);
    const bool cmyk = cinfo.out_color_space == JCS_CMYK;
    const bool gray = cinfo.out_color_space == JCS_GRAYSCALE;
    for (std::uint32_t y = 0; y < h && cinfo.output_scanline < cinfo.output_height; ++y) {
        JSAMPROW rows[1] = {row.data()};
        jpeg_read_scanlines(&cinfo, rows, 1);
        auto* out = reinterpret_cast<std::uint8_t*>(img.data) + static_cast<std::size_t>(y) * img.stride;
        for (std::uint32_t x = 0; x < w; ++x) {
            std::uint8_t r = 0, g = 0, b = 0;
            if (gray) {
                r = g = b = row[x];
            } else if (cmyk) {
                // Adobe CMYK：通常为 inverted（255=无墨）
                const std::uint8_t c = row[x * 4 + 0];
                const std::uint8_t m = row[x * 4 + 1];
                const std::uint8_t yv = row[x * 4 + 2];
                const std::uint8_t k = row[x * 4 + 3];
                // 经验式：与 libjpeg 文档一致（非 Adobe 时用 255-k 乘）
                r = static_cast<std::uint8_t>((c * k) / 255);
                g = static_cast<std::uint8_t>((m * k) / 255);
                b = static_cast<std::uint8_t>((yv * k) / 255);
            } else {
                r = row[x * comps + 0];
                g = comps > 1 ? row[x * comps + 1] : r;
                b = comps > 2 ? row[x * comps + 2] : r;
            }
            out[x * 4 + 0] = r;
            out[x * 4 + 1] = g;
            out[x * 4 + 2] = b;
            out[x * 4 + 3] = 255;
        }
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return img;
}

} // namespace shine::gallery::decoders
