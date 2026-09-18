#pragma once
// shine::gallery::decoders::WebpDecoder —— libwebp 唯一入口（G-S12b）
//
// **WebP* 符号只允许出现在 WebpDecoder.cpp**。RGBA8 直出。
#include "gallery/decoders/IImageDecoder.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>

namespace shine::gallery::decoders {

class WebpDecoder final : public IImageDecoder {
public:
    [[nodiscard]] bool can_decode(std::span<const std::byte> header) const noexcept override;
    [[nodiscard]] std::expected<Image, DecodeError> decode(const std::filesystem::path& path) override;
};

} // namespace shine::gallery::decoders
