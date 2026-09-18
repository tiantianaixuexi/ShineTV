#pragma once
// shine::paint::PngCodec —— RGBA8 ↔ PNG（P6.3 S1）
// libpng 符号只允许出现在本 .cpp 与 gallery/decoders/PngDecoder.cpp
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace shine::paint {

enum class PngCodecError { InvalidArgs, EncodeFailed, DecodeFailed, OutOfMemory };

[[nodiscard]] const char* PngCodecErrorText(PngCodecError e) noexcept;

// RGBA8 内存 → PNG 字节
[[nodiscard]] std::expected<std::vector<std::byte>, PngCodecError>
EncodePng(std::uint32_t width, std::uint32_t height, std::span<const std::byte> rgba8);

// PNG 字节 → RGBA8（outW/outH 回填）
[[nodiscard]] std::expected<std::vector<std::byte>, PngCodecError>
DecodePng(std::span<const std::byte> png, std::uint32_t& outW, std::uint32_t& outH);

// 单通道遮罩 0/255 → 灰度 PNG（R=G=B=mask, A=255），供 LoadImageMask 使用
[[nodiscard]] std::expected<std::vector<std::byte>, PngCodecError>
EncodeMaskPng(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> mask);

// 可选：写盘（UTF-8 路径由调用方用 util::File）
[[nodiscard]] int RunPngCodecSelfCheck();

} // namespace shine::paint
