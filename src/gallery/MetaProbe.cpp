#include "gallery/MetaProbe.h"
#include "util/File.h"

#include "core/Log.h"
#include "util/Strings.h"

#include <imageinfo.hpp> // ✅ 只在这一个文件里 include（G-S1 S3 / `Plan/任务/G-图片库.md` §3.2）

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace shine::gallery {
namespace {

constexpr std::string_view kSupported[] = {"png", "jpg", "jpeg", "webp", "avif", "bmp", "gif", "tga", "heic"};

// imageinfo 自带的 `FilePathReader(path.string())` 会**按 ANSI 代码页**打开窄字符串，
// 中文（非 ASCII）路径必失败 —— 目录名带中文时整个图库扫不出任何图。
// 这里改用 `std::ifstream(path)` 打开（MinGW 下 `filesystem::path` → `_wfopen`，宽字符安全），
// 再把读接口交给 imageinfo，行为与 `FilePathReader` 完全一致。
[[nodiscard]] imageinfo::ImageInfo ProbeByPath(const std::filesystem::path& path) {
    std::ifstream in = util::OpenInput(path);
    if (!in) {
        return imageinfo::ImageInfo(imageinfo::kUnrecognizedFormat);
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        return imageinfo::ImageInfo(imageinfo::kUnrecognizedFormat);
    }
    in.seekg(0, std::ios::beg);
    imageinfo::ReadFunc readFunc = [&in](void* buf, off_t offset, size_t length) {
        in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        in.read(static_cast<char*>(buf), static_cast<std::streamsize>(length));
    };
    imageinfo::ReadInterface reader(readFunc, static_cast<std::size_t>(size));
    return imageinfo::parse(reader);
}

} // namespace

bool IsProbeSupportedFormat(std::string_view format) noexcept {
    return std::ranges::find(kSupported, format) != std::end(kSupported);
}

ImageInfo ProbeFile(const std::filesystem::path& path, ImageId id) {
    ImageInfo info;
    info.id = id;
    info.path = path;

    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (!ec) {
        info.fileSize = size;
    }
    const auto modified = std::filesystem::last_write_time(path, ec);
    if (!ec) {
        info.modified = modified;
    }

    // imageinfo：只读文件头（内部按需 read 若干字节，不做像素解码）
    const imageinfo::ImageInfo probed = ProbeByPath(path);
    if (!probed.ok()) {
        return info; // 不认识的格式 / 打不开：width/height 保持 0
    }
    info.width = static_cast<std::uint32_t>(std::max<std::int64_t>(0, probed.size().width));
    info.height = static_cast<std::uint32_t>(std::max<std::int64_t>(0, probed.size().height));
    const char* fullExt = probed.full_ext();
    info.format = util::ToLower(fullExt != nullptr ? std::string_view{fullExt} : std::string_view{});
    const char* mime = probed.mimetype();
    info.mime = (mime != nullptr) ? mime : "";
    return info;
}

} // namespace shine::gallery
