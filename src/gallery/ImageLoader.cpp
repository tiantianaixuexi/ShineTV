#include "gallery/ImageLoader.h"
#include "util/Encoding.h"
#include "util/File.h"

#include "core/Log.h"
#include "gallery/ExifOrientation.h"
#include "gallery/decoders/JpegDecoder.h"
#include "gallery/decoders/PngDecoder.h"
#include "gallery/decoders/WebpDecoder.h"

#include <fstream>
#include <utility>

namespace shine::gallery {

ImageLoader& ImageLoader::Instance() {
    static ImageLoader loader;
    return loader;
}

void ImageLoader::Register(std::unique_ptr<IImageDecoder> decoder) {
    if (decoder != nullptr) {
        decoders_.push_back(std::move(decoder));
    }
}

std::vector<std::byte> ImageLoader::ReadHeader(const std::filesystem::path& path, std::size_t maxBytes) {
    std::ifstream file = util::OpenInput(path);
    if (!file) {
        return {};
    }
    std::vector<std::byte> header(maxBytes);
    file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(maxBytes));
    header.resize(static_cast<std::size_t>(file.gcount()));
    return header;
}

std::expected<Image, DecodeError> ImageLoader::Load(const std::filesystem::path& path) {
    const std::vector<std::byte> header = ReadHeader(path, 32);
    if (header.empty()) {
        return std::unexpected(DecodeError::NotFound);
    }
    for (const auto& decoder : decoders_) {
        if (decoder->can_decode(std::span<const std::byte>(header.data(), header.size()))) {
            auto decoded = decoder->decode(path);
            if (decoded && decoded->valid()) {
                // G-S13：解码后立刻按 EXIF 摆正（必须在缩略图 ResizeBox 之前）
                const ExifMeta exif = ReadExif(path);
                if (exif.orientation != Orientation::Normal) {
                    const auto w0 = decoded->width;
                    const auto h0 = decoded->height;
                    ApplyOrientation(*decoded, exif.orientation);
                    log::Info("EXIF 方向已校正：{} → {}（{}x{} → {}x{}）", util::FileNameToUtf8(path),
                              OrientationLabel(exif.orientation), w0, h0, decoded->width, decoded->height);
                }
            }
            return decoded;
        }
    }
    return std::unexpected(DecodeError::Unsupported);
}

bool ImageLoader::Supports(const std::filesystem::path& path) const {
    const std::vector<std::byte> header = ReadHeader(path, 32);
    if (header.empty()) {
        return false;
    }
    for (const auto& decoder : decoders_) {
        if (decoder->can_decode(std::span<const std::byte>(header.data(), header.size()))) {
            return true;
        }
    }
    return false;
}

void RegisterBuiltinDecoders() {
    auto& loader = ImageLoader::Instance();
    if (loader.DecoderCount() == 0) {
        loader.Register(std::make_unique<decoders::PngDecoder>());
        loader.Register(std::make_unique<decoders::JpegDecoder>());
        loader.Register(std::make_unique<decoders::WebpDecoder>());
        log::Info("图库解码器已注册：{} 个（png / jpeg / webp）", loader.DecoderCount());
    }
}

} // namespace shine::gallery