#pragma once
// shine::gallery::DiskThumbCache —— 磁盘缩略图缓存（G-S14 S1）
//
// 目录：`%APPDATA%\ShineTVStudio\cache\thumbs\{128,256,512}\`
// 文件：`<fnv1a64>.thumb` = 8 字节头 (w,h little-endian) + RGBA8
// key 输入：规范化路径 | 文件大小 | 修改时间 | 尺寸档 | kThumbAlgorithmVersion(=1)
// 调用：worker 线程读写；UI 不直接碰盘。
#include "gallery/Image.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace shine::gallery::disk_cache {

inline constexpr std::uint32_t kThumbAlgorithmVersion = 1;

// targetSide → 存档档位（128/256/512）
[[nodiscard]] std::uint32_t SizeBucket(std::uint32_t targetSide) noexcept;

[[nodiscard]] std::filesystem::path RootDir();
[[nodiscard]] std::filesystem::path DirForBucket(std::uint32_t bucket);

// 命中返回 RGBA8；未命中/损坏/版本不符 → nullopt
[[nodiscard]] std::optional<Image> Load(const std::filesystem::path& file, std::uint32_t targetSide);

// 写盘；失败只返回 false（调用方告警，不影响显示）
[[nodiscard]] bool Store(const std::filesystem::path& file, std::uint32_t targetSide, const Image& img);

// 观测
[[nodiscard]] std::size_t Hits() noexcept;
[[nodiscard]] std::size_t Lookups() noexcept;
[[nodiscard]] double HitRate() noexcept;
void ResetStats() noexcept;
[[nodiscard]] std::string Summary();

} // namespace shine::gallery::disk_cache
