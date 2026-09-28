#include "visual/ReferenceChain.h"

#include "util/Encoding.h"
#include "util/Strings.h"
#include "visual/ReferenceLibrary.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace shine::visual {
namespace {

struct AssetInfo {
    std::int64_t id = 0;
    std::int64_t entity_id = 0;
    std::string name;
    std::string entity_name;
};

[[nodiscard]] std::filesystem::path AbsolutePath(const std::filesystem::path& root,
                                                  const std::filesystem::path& path) {
    const std::filesystem::path normalized_input =
        path.is_absolute() ? path : root / util::PathFromUtf8(util::PathToUtf8(path));
    return normalized_input.lexically_normal();
}

[[nodiscard]] std::string PathKey(const std::filesystem::path& path) {
    return util::ToLower(util::PathToUtf8(path.lexically_normal()));
}


[[nodiscard]] std::string AssetLabel(const AssetInfo& info) {
    return "资产 #" + std::to_string(info.id) + " · " + info.name +
           (info.entity_name.empty() ? std::string{}
                                     : " · 实体 " + info.entity_name);
}
void AddUsage(std::vector<ReferenceUsage>& out, std::unordered_set<std::string>& seen,
              ReferenceUsage usage) {
    std::string key = std::to_string(std::to_underlying(usage.kind)) + ":" +
                      std::to_string(usage.asset_id) + ":" + std::to_string(usage.shot_id) + ":" +
                      std::to_string(usage.entity_id);
    if (seen.insert(key).second) {
        out.push_back(std::move(usage));
    }
}

} // namespace

std::vector<ReferenceUsage>
FindReferenceUsages(db::sqlite::Database& db, const std::filesystem::path& project_root,
                    const std::filesystem::path& image_path) {
    std::vector<ReferenceUsage> out;
    std::unordered_set<std::string> seen;
    const std::filesystem::path absolute = AbsolutePath(project_root, image_path);
    const std::string key = PathKey(absolute);

    std::unordered_map<std::int64_t, AssetInfo> assets;
    if (auto statement = db.Prepare(
            "SELECT va.id,va.entity_id,va.name,va.sheet_rel_path,COALESCE(e.name,'') "
            "FROM visual_assets va LEFT JOIN entities e ON e.id=va.entity_id ORDER BY va.id")) {
        while (auto step = statement->Step()) {
            if (*step != db::sqlite::StepResult::Row) {
                break;
            }
            AssetInfo info;
            info.id = statement->ColumnInt(0);
            info.entity_id = statement->ColumnInt(1);
            info.name = statement->ColumnText(2);
            const std::string sheet = statement->ColumnText(3);
            info.entity_name = statement->ColumnText(4);
            assets.emplace(info.id, info);
            if (!sheet.empty() &&
                PathKey(AbsolutePath(project_root, util::PathFromUtf8(sheet))) == key) {
                AddUsage(out, seen,
                         {.kind = ReferenceUsageKind::Asset,
                          .id = info.id,
                          .entity_id = info.entity_id,
                          .asset_id = info.id,
                          .label = AssetLabel(info)});
            }
        }
    }
    if (auto statement = db.Prepare(
            "SELECT id,asset_id,rel_path FROM visual_artifacts ORDER BY asset_id,id")) {
        while (auto step = statement->Step()) {
            if (*step != db::sqlite::StepResult::Row) {
                break;
            }
            const std::int64_t artifact_id = statement->ColumnInt(0);
            const std::int64_t asset_id = statement->ColumnInt(1);
            const std::string rel = statement->ColumnText(2);
            if (rel.empty() || PathKey(AbsolutePath(project_root, util::PathFromUtf8(rel))) != key) {
                continue;
            }
            const auto found = assets.find(asset_id);
            if (found == assets.end()) {
                continue;
            }
            const AssetInfo& info = found->second;
            AddUsage(out, seen,
                     {.kind = ReferenceUsageKind::Asset,
                      .id = info.id,
                      .entity_id = info.entity_id,
                      .asset_id = info.id,
                      .label = AssetLabel(info) + " · artifact #" +
                                   std::to_string(artifact_id)});
        }
    }

    if (auto statement = db.Prepare(
            "SELECT id,source_kind,source_id,rel_path FROM generated_images "
            "WHERE rel_path<>'' ORDER BY id")) {
        while (auto step = statement->Step()) {
            if (*step != db::sqlite::StepResult::Row) {
                break;
            }
            const std::int64_t image_id = statement->ColumnInt(0);
            const std::string source_kind = statement->ColumnText(1);
            const std::int64_t source_id = statement->ColumnInt(2);
            const std::string rel = statement->ColumnText(3);
            if (PathKey(AbsolutePath(project_root, util::PathFromUtf8(rel))) != key) {
                continue;
            }
            if (source_kind == "asset") {
                const auto found = assets.find(source_id);
                if (found != assets.end()) {
                    AddUsage(out, seen,
                             {.kind = ReferenceUsageKind::Asset,
                              .id = found->second.id,
                              .entity_id = found->second.entity_id,
                              .asset_id = found->second.id,
                              .label = AssetLabel(found->second) + " · image #" +
                                           std::to_string(image_id)});
                }
            } else if (source_kind == "shot" && source_id > 0) {
                std::string label = "分镜 #" + std::to_string(source_id);
                if (auto shot = db.Prepare(
                        "SELECT s.ord,sc.ord,c.ord FROM shots s "
                        "JOIN scenes sc ON sc.id=s.scene_id "
                        "JOIN chapters c ON c.id=sc.chapter_id WHERE s.id=?1")) {
                    (void)shot->BindInt(1, source_id);
                    if (auto row = shot->Step();
                        row && *row == db::sqlite::StepResult::Row) {
                        label = "第 " + std::to_string(shot->ColumnInt(2)) + " 章 / 场 " +
                                std::to_string(shot->ColumnInt(1)) + " / 镜 " +
                                std::to_string(shot->ColumnInt(0));
                    }
                }
                AddUsage(out, seen,
                         {.kind = ReferenceUsageKind::Shot,
                          .id = source_id,
                          .shot_id = source_id,
                          .label = std::move(label)});
            }
        }
    }

    ReferenceLibrary references(project_root);
    if (references.Load()) {
        for (const ReferenceImage& image : references.Images()) {
            bool matches = PathKey(references.Resolve(image.rel_path)) == key;
            if (!matches && !image.source_path.empty()) {
                matches = PathKey(AbsolutePath(
                                   project_root, util::PathFromUtf8(image.source_path))) == key;
            }
            if (!matches) {
                continue;
            }
            AddUsage(out, seen,
                     {.kind = ReferenceUsageKind::ReferenceBinding,
                      .id = image.entity_id,
                      .entity_id = image.entity_id,
                      .asset_id = image.asset_id,
                      .label = "项目参考图 · " + image.original_name +
                                   (image.entity_id > 0
                                        ? " · 实体 #" + std::to_string(image.entity_id)
                                        : std::string{" · 未绑定"})});
        }
    }

    std::ranges::sort(out, {}, [](const ReferenceUsage& usage) {
        return static_cast<int>(usage.kind);
    });
    return out;
}

} // namespace shine::visual
