#pragma once
// P8 视觉体系：资产/阶段/场景/镜头/分层 Prompt/分镜
// 原则：文本 Canon → 视觉 Canon → 剧情状态 → 视觉状态 → Prompt；生成默认 PROPOSED
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "db/sqlite/SqliteDb.h"
#include "novel/NovelDb.h"
#include "novel/NovelTypes.h"

namespace shine::novelcore {

struct VisualAssetRow {
    RowId id = 0;
    RowId entity_id = 0;
    std::string kind = "character"; // character|clothing|item|location|prop
    std::string name;
    std::string base_desc;
    std::string materials_colors;
    std::string permanent_tags_json = "[]";
    std::string sheet_rel_path;
    std::string canon_status = "DRAFT";
    std::string note;
};

struct VisualStateRow {
    RowId id = 0;
    RowId asset_id = 0;
    std::string stage_key;
    std::string stage_label;
    int ord = 0;
    RowId from_chapter = 0;
    RowId to_chapter = 0; // 0 = 至今
    std::string appearance;
    std::string materials_colors;
    RowId clothing_asset_id = 0;
    std::string item_asset_ids_json = "[]";
    std::string effects;
    std::string environment_hint;
    std::string canon_status = "DRAFT";
    std::string note;
};

struct ShotRow {
    RowId id = 0;
    RowId scene_id = 0;
    int ord = 0;
    std::string duration_note;
    RowId camera_id = 0;
    std::string character_ids_json = "[]";
    std::string action;
    std::string expression;
    std::string prop_ids_json = "[]";
    RowId lighting_id = 0;
    RowId composition_id = 0;
    std::string dialogue;
    std::string narration;
    std::string sfx;
    std::string mood;
    std::string prompt_text;
    std::string negative_text;
    std::string reference_json = "{}";
    std::string canon_status = "PROPOSED";
};

struct AssemblePromptInput {
    RowId chapter_id = 0;
    RowId scene_id = 0;
    std::optional<RowId> character_id;
    std::optional<RowId> asset_id;
    std::optional<RowId> shot_id;
    // 可选指定镜头/构图/光（0 = 用 shot 或默认）
    RowId camera_id = 0;
    RowId composition_id = 0;
    RowId lighting_id = 0;
};

struct AssemblePromptOutput {
    std::string final_prompt;
    std::string negative_prompt;
    std::vector<RowId> used_layer_ids;
    RowId resolved_state_id = 0;
    RowId resolved_asset_id = 0;
};

class NovelVisual {
public:
    explicit NovelVisual(db::sqlite::Database& db) noexcept : db_(&db) {}

    // —— 资产 ——
    [[nodiscard]] std::expected<RowId, DbError> UpsertAsset(const VisualAssetRow& row);
    [[nodiscard]] std::expected<VisualAssetRow, DbError> GetAsset(RowId id) const;
    // 按 entity 找主视觉资产（第一个）
    [[nodiscard]] std::expected<VisualAssetRow, DbError> FindAssetByEntity(RowId entityId) const;

    // —— 阶段 ——
    [[nodiscard]] std::expected<RowId, DbError> UpsertState(const VisualStateRow& row);
    // 按剧情章解析当前阶段：from_ch≤N 且 (to_ch=0 或 to_ch≥N)，取 from_ch 最大者
    [[nodiscard]] std::expected<VisualStateRow, DbError>
    ResolveVisualState(RowId assetId, RowId chapterId) const;

    // —— 镜头/光/构图 ——
    [[nodiscard]] std::expected<RowId, DbError> UpsertCamera(std::string_view name,
                                                             std::string_view text);
    [[nodiscard]] std::expected<RowId, DbError> UpsertLighting(std::string_view name,
                                                               std::string_view text);
    [[nodiscard]] std::expected<RowId, DbError> UpsertComposition(std::string_view name,
                                                                  std::string_view text);
    [[nodiscard]] std::expected<RowId, DbError> UpsertStyle(std::string_view name,
                                                            std::string_view text);

    // —— 分镜 ——
    [[nodiscard]] std::expected<RowId, DbError> UpsertShot(const ShotRow& row);
    [[nodiscard]] std::expected<ShotRow, DbError> GetShot(RowId id) const;

    // —— 分层 Prompt ——
    // layer: base/stage/scene/action/camera/composition/lighting/style/quality/negative
    [[nodiscard]] std::expected<RowId, DbError> SetLayer(std::string_view ownerKind, RowId ownerId,
                                                         std::string_view layer,
                                                         std::string_view text);
    // 九层组装
    [[nodiscard]] std::expected<AssemblePromptOutput, DbError>
    Assemble(const AssemblePromptInput& in) const;

    // 文本侧一致性检查（发色/人数/未登场等，返回 issue 文案列表）
    [[nodiscard]] std::expected<std::vector<std::string>, DbError>
    CheckConsistency(const AssemblePromptInput& in, std::string_view prompt) const;

    // 视觉 canon
    [[nodiscard]] std::expected<void, DbError> SetVisualCanon(std::string_view targetKind,
                                                              RowId targetId,
                                                              std::string_view status);

    [[nodiscard]] static bool RunSelfCheck();

private:
    db::sqlite::Database* db_;

    [[nodiscard]] std::string QueryLayer(std::string_view ownerKind, RowId ownerId,
                                         std::string_view layer, RowId* outId) const;
    [[nodiscard]] std::string QueryDefText(std::string_view table, RowId id) const;
};

} // namespace shine::novelcore
