#pragma once
// shine::gallery —— 图片库公共类型（G-S1 S1）
//
// 这一层**不认识任何图片库**（libpng / imageinfo 都不许出现在这里）：
// 上层（扫描器 / 网格 / 查看器 / UI）只使用 `ImageInfo` 与 `Image`。
#include <cstdint>
#include <filesystem>
#include <string>

namespace shine::gallery {

using ImageId = std::uint64_t;

enum class SourceKind { Local, ComfyOutput, ComfyInput };

enum class LoadState { NotLoaded, Queued, Loading, Ready, Failed };

// 扫描阶段（**零像素解码**）只填这些：格式与宽高来自文件头探针
struct ImageInfo {
    ImageId id = 0;
    std::filesystem::path path;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t fileSize = 0;
    std::filesystem::file_time_type modified{};
    std::string format; // "png" / "jpeg" / "webp" / "avif"…（小写）
    std::string mime;   // "image/png" …
    SourceKind source = SourceKind::Local;

    [[nodiscard]] bool valid() const noexcept { return width > 0 && height > 0 && !format.empty(); }
};

} // namespace shine::gallery
