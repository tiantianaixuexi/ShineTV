#pragma once
// P05-S8 媒体引用链：同一图片被 visual_assets / visual_artifacts / generated_images /
// ReferenceLibrary 哪些实体、资产或分镜使用。
#include "db/sqlite/SqliteDb.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace shine::visual {

enum class ReferenceUsageKind { Asset, Shot, ReferenceBinding };

struct ReferenceUsage {
    ReferenceUsageKind kind = ReferenceUsageKind::Asset;
    std::int64_t id = 0;
    std::int64_t entity_id = 0;
    std::int64_t asset_id = 0;
    std::int64_t shot_id = 0;
    std::string label;
};

[[nodiscard]] std::vector<ReferenceUsage>
FindReferenceUsages(db::sqlite::Database& db, const std::filesystem::path& project_root,
                    const std::filesystem::path& image_path);

} // namespace shine::visual
