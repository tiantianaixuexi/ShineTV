#pragma once
// shine::gallery::IImageDecoder —— 解码器接口（G-S2 S1）
//
// 失败用 `std::expected` 表达（`Doc/RULES-LANG.md` §13），所以**没有** `DecodeError::Ok`。
// 具体图片库（libpng / libjpeg-turbo / libwebp / libavif）的符号只允许出现在各自的
// `decoders/XxxDecoder.cpp` 里；UI 与图库其它模块只认这个接口。
#include "gallery/Image.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>

namespace shine::gallery {

enum class DecodeError { NotFound, Corrupt, Unsupported, OutOfMemory, DecodeFailed };

[[nodiscard]] const char* DecodeErrorText(DecodeError error) noexcept; // 中文，可直接显示

class IImageDecoder {
public:
    virtual ~IImageDecoder() = default;

    // 看文件头魔数（不做 IO）
    [[nodiscard]] virtual bool can_decode(std::span<const std::byte> header) const noexcept = 0;
    // 解码为 RGBA8；失败返回 expected 的错误
    [[nodiscard]] virtual std::expected<Image, DecodeError> decode(const std::filesystem::path& path) = 0;
};

} // namespace shine::gallery
