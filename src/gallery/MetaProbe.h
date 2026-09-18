#pragma once
// shine::gallery —— 文件头探针（G-S1 S3）
//
// `imageinfo` 的适配层：**imageinfo 头文件只允许出现在 MetaProbe.cpp 里**，
// 业务层只看到 `ImageInfo`（换探针实现时不用动扫描器/网格/UI）。
#include "gallery/GalleryTypes.h"

#include <cstdint>
#include <string>

namespace shine::gallery {

// 只读文件头：拿到格式 / MIME / 宽高；**不解码像素**（不触发 Image::Allocate）
// 文件不存在或无法识别 → 返回 width/height = 0 的 ImageInfo（`valid()` 为 false）
[[nodiscard]] ImageInfo ProbeFile(const std::filesystem::path& path, ImageId id);

// 供日志/诊断：格式是否被探针认识
[[nodiscard]] bool IsProbeSupportedFormat(std::string_view format) noexcept;

} // namespace shine::gallery
