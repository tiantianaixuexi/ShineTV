#pragma once
// shine::gallery::decoders::PngDecoder —— libpng 的唯一入口（G-S2 S2）
//
// **libpng 符号只出现在 PngDecoder.cpp**：本头文件不 include 任何 libpng 头，
// 因此 `ImageLoader` / `MetaProbe` / UI 都不可能泄漏 libpng 类型。
#include "gallery/decoders/IImageDecoder.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>

namespace shine::gallery::decoders {

class PngDecoder final : public IImageDecoder {
public:
    [[nodiscard]] bool can_decode(std::span<const std::byte> header) const noexcept override;
    [[nodiscard]] std::expected<Image, DecodeError> decode(const std::filesystem::path& path) override;

    // 内存解码：媒体预览（P4 README/`src/media/ImageFetch.cpp`）先把 `/view` 字节拉下来再解，
    // 不允许再写第二份 libpng 包装 —— 共用这里。
    [[nodiscard]] static std::expected<Image, DecodeError> DecodeMemory(std::span<const std::byte> bytes);
};

} // namespace shine::gallery::decoders
