#include "gallery/decoders/WebpDecoder.h"
#include "util/Encoding.h"
#include "util/File.h"

#include "core/Log.h"

#include <webp/decode.h> // ✅ libwebp 只在本 .cpp

#include <cstdint>
#include <cstring>

namespace shine::gallery::decoders {

bool WebpDecoder::can_decode(std::span<const std::byte> header) const noexcept {
    // RIFF....WEBP
    if (header.size() < 12) {
        return false;
    }
    const auto* p = reinterpret_cast<const unsigned char*>(header.data());
    return std::memcmp(p, "RIFF", 4) == 0 && std::memcmp(p + 8, "WEBP", 4) == 0;
}

std::expected<Image, DecodeError> WebpDecoder::decode(const std::filesystem::path& path) {
    const auto bytes = util::ReadFileBytes(path);
    if (!bytes) {
        return std::unexpected(DecodeError::NotFound);
    }
    if (bytes->size() < 16) {
        return std::unexpected(DecodeError::Corrupt);
    }

    int w = 0;
    int h = 0;
    if (!WebPGetInfo(reinterpret_cast<const uint8_t*>(bytes->data()), bytes->size(), &w, &h) || w <= 0 || h <= 0) {
        return std::unexpected(DecodeError::Corrupt);
    }

    Image img = Image::Allocate(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
    if (!img.valid()) {
        return std::unexpected(DecodeError::OutOfMemory);
    }

    uint8_t* decoded =
        WebPDecodeRGBAInto(reinterpret_cast<const uint8_t*>(bytes->data()), bytes->size(),
                           reinterpret_cast<uint8_t*>(img.data), img.bytes, static_cast<int>(img.stride));
    if (decoded == nullptr) {
        log::Warn("WebP 解码失败：{}", util::PathToUtf8(path));
        return std::unexpected(DecodeError::DecodeFailed);
    }
    return img;
}

} // namespace shine::gallery::decoders
