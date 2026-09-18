#pragma once
// shine::gallery::ImageLoader —— 注册式解码器分发（G-S2 S3）
//
// UI / 图库其它模块**不认识任何图片库**：只 `Load(path)` 拿 `Image`。
// 解码器按注册顺序用**文件头魔数**匹配（不靠扩展名）。
#include "gallery/Image.h"
#include "gallery/decoders/IImageDecoder.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace shine::gallery {

class ImageLoader {
public:
    static ImageLoader& Instance();

    void Register(std::unique_ptr<IImageDecoder> decoder);
    [[nodiscard]] std::expected<Image, DecodeError> Load(const std::filesystem::path& path);
    [[nodiscard]] bool Supports(const std::filesystem::path& path) const;
    [[nodiscard]] std::size_t DecoderCount() const noexcept { return decoders_.size(); }

private:
    ImageLoader() = default;
    [[nodiscard]] static std::vector<std::byte> ReadHeader(const std::filesystem::path& path, std::size_t maxBytes);
    std::vector<std::unique_ptr<IImageDecoder>> decoders_;
};

// 启动时调用一次（幂等）：注册内置解码器（当前只有 PNG）
void RegisterBuiltinDecoders();

} // namespace shine::gallery
