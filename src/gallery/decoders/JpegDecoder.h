#pragma once
// shine::gallery::decoders::JpegDecoder —— libjpeg-turbo 唯一入口（G-S12a）
//
// **jpeg_* 符号只允许出现在 JpegDecoder.cpp**。覆盖 Gray / RGB / CMYK。
#include "gallery/decoders/IImageDecoder.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>

namespace shine::gallery::decoders {

class JpegDecoder final : public IImageDecoder {
public:
    [[nodiscard]] bool can_decode(std::span<const std::byte> header) const noexcept override;
    [[nodiscard]] std::expected<Image, DecodeError> decode(const std::filesystem::path& path) override;
};

} // namespace shine::gallery::decoders
