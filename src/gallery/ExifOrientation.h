#pragma once
// shine::gallery::ExifOrientation —— EXIF Orientation + 相机型号（G-S13）
//
// 只读 JPEG APP1(EXIF) 与 PNG eXIf；**不引通用 EXIF 库**。
// 无 EXIF → Normal / 空相机名。
#include "gallery/Image.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace shine::gallery {

// EXIF Orientation（1–8）；0/非法 → Normal
enum class Orientation : std::uint8_t {
    Normal = 1,
    FlipH = 2,
    Rotate180 = 3,
    FlipV = 4,
    Transpose = 5,   // 镜像 + 旋转 270° CW（按显示正向）
    Rotate90 = 6,    // 顺时针 90°
    Transverse = 7,
    Rotate270 = 8,   // 顺时针 270°
};

[[nodiscard]] const char* OrientationLabel(Orientation o) noexcept;
[[nodiscard]] bool SwapsAxes(Orientation o) noexcept; // 90/270 及对应镜像：宽高对调

struct ExifMeta {
    Orientation orientation = Orientation::Normal;
    std::string camera; // "Make Model"，无则空
};

// 读文件 EXIF（JPEG APP1 / PNG eXIf）；失败/无 → Normal + 空 camera
[[nodiscard]] ExifMeta ReadExif(const std::filesystem::path& path);

// 原地把 RGBA8 按 orientation 转成正向显示；Normal 为 no-op
void ApplyOrientation(Image& img, Orientation o);

} // namespace shine::gallery
