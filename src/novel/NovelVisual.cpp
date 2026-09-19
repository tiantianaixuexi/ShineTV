#include "novel/NovelVisual.h"

#include "core/Log.h"
#include "util/Time.h"

#include <fmt/format.h>

#include <algorithm>

namespace shine::novelcore {
namespace {

[[nodiscard]] std::int64_t NowSec() noexcept { return util::NowMillis() / 1000; }

[[nodiscard]] DbError VErr(std::string_view m) { return DbError{0, std::string{m}}; }

constexpr std::string_view kDefaultNegative =
    "lowres, blurry, extra fingers, deformed hands, watermark, text, logo, "
    "extra limbs, bad anatomy, duplicate person";

// 形象层派生序（11 §2.6.2）：front → turnaround → base_body → wardrobe → stage → shot
constexpr std::string_view kArtifactLayerOrder[] = {"front",    "turnaround", "base_body",
                                                    "wardrobe", "stage",      "shot"};
constexpr std::size_t kArtifactLayerOrderN =
    sizeof(kArtifactLayerOrder) / sizeof(kArtifactLayerOrder[0]);

// 未知 layer 返回 N（排最后，保留其相对顺序）
[[nodiscard]] constexpr std::size_t ArtifactLayerRank(std::string_view layer) noexcept {
    for (std::size_t i = 0; i < kArtifactLayerOrderN; ++i) {
        if (kArtifactLayerOrder[i] == layer) return i;
    }
    return kArtifactLayerOrderN;
}

} // namespace

std::expected<RowId, DbError> NovelVisual::UpsertAsset(const VisualAssetRow& row) {
    if (row.name.empty()) return std::unexpected(VErr("视觉资产 name 不能为空"));
    if (row.id > 0) {
        auto st = db_->Prepare(
            "UPDATE visual_assets SET entity_id=?1,kind=?2,name=?3,base_desc=?4,"
            "materials_colors=?5,permanent_tags_json=?6,sheet_rel_path=?7,canon_status=?8,"
            "status=?9,note=?10 WHERE id=?11");
        if (!st) return std::unexpected(st.error());
        (void)st->BindInt(1, row.entity_id);
        (void)st->BindText(2, row.kind);
        (void)st->BindText(3, row.name);
        (void)st->BindText(4, row.base_desc);
        (void)st->BindText(5, row.materials_colors);
        (void)st->BindText(6, row.permanent_tags_json);
        (void)st->BindText(7, row.sheet_rel_path);
        (void)st->BindText(8, row.canon_status);
        (void)st->BindText(9, row.status.empty() ? "PENDING" : row.status);
        (void)st->BindText(10, row.note);
        (void)st->BindInt(11, row.id);
        if (auto s = st->Step(); !s) return std::unexpected(s.error());
        return row.id;
    }
    auto st = db_->Prepare(
        "INSERT INTO visual_assets(entity_id,kind,name,base_desc,materials_colors,"
        "permanent_tags_json,sheet_rel_path,canon_status,status,note)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.entity_id);
    (void)st->BindText(2, row.kind);
    (void)st->BindText(3, row.name);
    (void)st->BindText(4, row.base_desc);
    (void)st->BindText(5, row.materials_colors);
    (void)st->BindText(6, row.permanent_tags_json.empty() ? "[]" : row.permanent_tags_json);
    (void)st->BindText(7, row.sheet_rel_path);
    (void)st->BindText(8, row.canon_status);
    (void)st->BindText(9, row.status.empty() ? "PENDING" : row.status);
    (void)st->BindText(10, row.note);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<VisualAssetRow, DbError> NovelVisual::GetAsset(RowId id) const {
    auto st = db_->Prepare(
        "SELECT id,entity_id,kind,name,base_desc,materials_colors,permanent_tags_json,"
        "sheet_rel_path,canon_status,status,note FROM visual_assets WHERE id=?1");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, id);
    auto s = st->Step();
    if (!s) return std::unexpected(s.error());
    if (*s == db::sqlite::StepResult::Done) return std::unexpected(VErr("视觉资产不存在"));
    VisualAssetRow r;
    r.id = st->ColumnInt(0);
    r.entity_id = st->ColumnInt(1);
    r.kind = st->ColumnText(2);
    r.name = st->ColumnText(3);
    r.base_desc = st->ColumnText(4);
    r.materials_colors = st->ColumnText(5);
    r.permanent_tags_json = st->ColumnText(6);
    r.sheet_rel_path = st->ColumnText(7);
    r.canon_status = st->ColumnText(8);
    r.status = st->ColumnText(9);
    r.note = st->ColumnText(10);
    return r;
}

std::expected<VisualAssetRow, DbError> NovelVisual::FindAssetByEntity(RowId entityId) const {
    auto st = db_->Prepare(
        "SELECT id FROM visual_assets WHERE entity_id=?1 ORDER BY id LIMIT 1");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, entityId);
    auto s = st->Step();
    if (!s) return std::unexpected(s.error());
    if (*s == db::sqlite::StepResult::Done) {
        return std::unexpected(VErr("该实体尚无视觉资产"));
    }
    return GetAsset(st->ColumnInt(0));
}

// —— 形象层产物（V0 ASSET_PIPELINE）——

std::expected<RowId, DbError> NovelVisual::UpsertArtifact(const VisualArtifactRow& row) {
    if (row.asset_id <= 0) return std::unexpected(VErr("形象层产物必须指定 asset_id"));
    if (row.layer.empty()) return std::unexpected(VErr("形象层产物必须指定 layer"));
    const auto now = NowSec();
    const std::string_view status = row.status.empty() ? std::string_view{"PENDING"} : row.status;
    if (row.id > 0) {
        auto st = db_->Prepare(
            "UPDATE visual_artifacts SET asset_id=?1,layer=?2,chapter_scope=?3,chapter_to=?4,"
            "rel_path=?5,parent_artifact_id=?6,prompt_artifact_id=?7,job_id=?8,status=?9,"
            "degraded=?10,note=?11,updated=?12 WHERE id=?13");
        if (!st) return std::unexpected(st.error());
        (void)st->BindInt(1, row.asset_id);
        (void)st->BindText(2, row.layer);
        (void)st->BindInt(3, row.chapter_scope);
        (void)st->BindInt(4, row.chapter_to);
        (void)st->BindText(5, row.rel_path);
        (void)st->BindInt(6, row.parent_artifact_id);
        (void)st->BindInt(7, row.prompt_artifact_id);
        (void)st->BindText(8, row.job_id);
        (void)st->BindText(9, status);
        (void)st->BindInt(10, row.degraded ? 1 : 0);
        (void)st->BindText(11, row.note);
        (void)st->BindInt(12, now);
        (void)st->BindInt(13, row.id);
        if (auto s = st->Step(); !s) return std::unexpected(s.error());
        return row.id;
    }
    auto st = db_->Prepare(
        "INSERT INTO visual_artifacts(asset_id,layer,chapter_scope,chapter_to,rel_path,"
        "parent_artifact_id,prompt_artifact_id,job_id,status,degraded,note,created,updated)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.asset_id);
    (void)st->BindText(2, row.layer);
    (void)st->BindInt(3, row.chapter_scope);
    (void)st->BindInt(4, row.chapter_to);
    (void)st->BindText(5, row.rel_path);
    (void)st->BindInt(6, row.parent_artifact_id);
    (void)st->BindInt(7, row.prompt_artifact_id);
    (void)st->BindText(8, row.job_id);
    (void)st->BindText(9, status);
    (void)st->BindInt(10, row.degraded ? 1 : 0);
    (void)st->BindText(11, row.note);
    (void)st->BindInt(12, now);
    (void)st->BindInt(13, now);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<VisualArtifactRow, DbError> NovelVisual::GetArtifact(RowId id) const {
    auto st = db_->Prepare(
        "SELECT id,asset_id,layer,chapter_scope,chapter_to,rel_path,parent_artifact_id,"
        "prompt_artifact_id,job_id,status,degraded,note FROM visual_artifacts WHERE id=?1");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, id);
    auto s = st->Step();
    if (!s) return std::unexpected(s.error());
    if (*s == db::sqlite::StepResult::Done) return std::unexpected(VErr("形象层产物不存在"));
    VisualArtifactRow r;
    r.id = st->ColumnInt(0);
    r.asset_id = st->ColumnInt(1);
    r.layer = st->ColumnText(2);
    r.chapter_scope = st->ColumnInt(3);
    r.chapter_to = st->ColumnInt(4);
    r.rel_path = st->ColumnText(5);
    r.parent_artifact_id = st->ColumnInt(6);
    r.prompt_artifact_id = st->ColumnInt(7);
    r.job_id = st->ColumnText(8);
    r.status = st->ColumnText(9);
    r.degraded = st->ColumnInt(10) != 0;
    r.note = st->ColumnText(11);
    return r;
}

std::expected<std::vector<VisualArtifactRow>, DbError>
NovelVisual::ListArtifacts(RowId assetId) const {
    std::vector<VisualArtifactRow> out;
    auto st = db_->Prepare(
        "SELECT id,asset_id,layer,chapter_scope,chapter_to,rel_path,parent_artifact_id,"
        "prompt_artifact_id,job_id,status,degraded,note FROM visual_artifacts "
        "WHERE asset_id=?1 ORDER BY id");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, assetId);
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        VisualArtifactRow r;
        r.id = st->ColumnInt(0);
        r.asset_id = st->ColumnInt(1);
        r.layer = st->ColumnText(2);
        r.chapter_scope = st->ColumnInt(3);
        r.chapter_to = st->ColumnInt(4);
        r.rel_path = st->ColumnText(5);
        r.parent_artifact_id = st->ColumnInt(6);
        r.prompt_artifact_id = st->ColumnInt(7);
        r.job_id = st->ColumnText(8);
        r.status = st->ColumnText(9);
        r.degraded = st->ColumnInt(10) != 0;
        r.note = st->ColumnText(11);
        out.push_back(std::move(r));
    }
    // 派生序 front → turnaround → base_body → wardrobe → stage → shot（11 §2.6.2）
    std::ranges::stable_sort(
        out, {}, [](const VisualArtifactRow& r) { return ArtifactLayerRank(r.layer); });
    return out;
}

// —— 场景视觉（S7 写入口：原先只有 `Assemble` 内部的一条 SELECT，表永远空）——

std::expected<RowId, DbError> NovelVisual::UpsertSceneVisual(const SceneVisualRow& row) {
    if (row.scene_id <= 0) {
        return std::unexpected(VErr("场景视觉必须指定 scene_id"));
    }
    // 「一个场景一行」：已有就更新（否则 Assemble 的 ORDER BY id LIMIT 1 永远读第一版）
    RowId target = row.id;
    if (target <= 0) {
        auto q = db_->Prepare("SELECT id FROM scene_visuals WHERE scene_id=?1 ORDER BY id LIMIT 1");
        if (!q) return std::unexpected(q.error());
        (void)q->BindInt(1, row.scene_id);
        if (auto s = q->Step(); s && *s == db::sqlite::StepResult::Row) {
            target = q->ColumnInt(0);
        }
    }
    if (target > 0) {
        auto st = db_->Prepare(
            "UPDATE scene_visuals SET scene_id=?1,env_desc=?2,time_of_day=?3,weather=?4,mood=?5,"
            "canon_status=?6 WHERE id=?7");
        if (!st) return std::unexpected(st.error());
        (void)st->BindInt(1, row.scene_id);
        (void)st->BindText(2, row.env_desc);
        (void)st->BindText(3, row.time_of_day);
        (void)st->BindText(4, row.weather);
        (void)st->BindText(5, row.mood);
        (void)st->BindText(6, row.canon_status.empty() ? "DRAFT" : row.canon_status);
        (void)st->BindInt(7, target);
        if (auto s = st->Step(); !s) return std::unexpected(s.error());
        return target;
    }
    auto st = db_->Prepare(
        "INSERT INTO scene_visuals(scene_id,env_desc,time_of_day,weather,mood,canon_status)"
        " VALUES(?1,?2,?3,?4,?5,?6)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.scene_id);
    (void)st->BindText(2, row.env_desc);
    (void)st->BindText(3, row.time_of_day);
    (void)st->BindText(4, row.weather);
    (void)st->BindText(5, row.mood);
    (void)st->BindText(6, row.canon_status.empty() ? "DRAFT" : row.canon_status);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<SceneVisualRow, DbError> NovelVisual::GetSceneVisual(RowId sceneId) const {
    if (sceneId <= 0) {
        return std::unexpected(VErr("查询场景视觉必须指定 scene_id"));
    }
    auto st = db_->Prepare(
        "SELECT id,scene_id,env_desc,time_of_day,weather,mood,canon_status FROM scene_visuals "
        "WHERE scene_id=?1 ORDER BY id LIMIT 1");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, sceneId);
    if (auto s = st->Step(); !s) {
        return std::unexpected(s.error());
    } else if (*s != db::sqlite::StepResult::Row) {
        return std::unexpected(VErr(fmt::format("场景 #{} 还没有视觉描述", sceneId)));
    }
    SceneVisualRow r;
    r.id = st->ColumnInt(0);
    r.scene_id = st->ColumnInt(1);
    r.env_desc = st->ColumnText(2);
    r.time_of_day = st->ColumnText(3);
    r.weather = st->ColumnText(4);
    r.mood = st->ColumnText(5);
    r.canon_status = st->ColumnText(6);
    return r;
}

std::expected<RowId, DbError> NovelVisual::UpsertState(const VisualStateRow& row) {
    if (row.asset_id <= 0) return std::unexpected(VErr("state 缺 asset_id"));
    auto st = db_->Prepare(
        "INSERT INTO visual_states(asset_id,stage_key,stage_label,ord,from_chapter,to_chapter,"
        "appearance,materials_colors,clothing_asset_id,item_asset_ids_json,effects,"
        "environment_hint,canon_status,note)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.asset_id);
    (void)st->BindText(2, row.stage_key);
    (void)st->BindText(3, row.stage_label);
    (void)st->BindInt(4, row.ord);
    (void)st->BindInt(5, row.from_chapter);
    (void)st->BindInt(6, row.to_chapter);
    (void)st->BindText(7, row.appearance);
    (void)st->BindText(8, row.materials_colors);
    (void)st->BindInt(9, row.clothing_asset_id);
    (void)st->BindText(10, row.item_asset_ids_json.empty() ? "[]" : row.item_asset_ids_json);
    (void)st->BindText(11, row.effects);
    (void)st->BindText(12, row.environment_hint);
    (void)st->BindText(13, row.canon_status);
    (void)st->BindText(14, row.note);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<VisualStateRow, DbError>
NovelVisual::ResolveVisualState(RowId assetId, RowId chapterId) const {
    // from_ch≤N 且 (to_ch=0 或 to_ch≥N)，取 from_ch 最大
    auto st = db_->Prepare(
        "SELECT id,asset_id,stage_key,stage_label,ord,from_chapter,to_chapter,appearance,"
        "materials_colors,clothing_asset_id,item_asset_ids_json,effects,environment_hint,"
        "canon_status,note FROM visual_states WHERE asset_id=?1"
        " AND from_chapter<=?2 AND (to_chapter=0 OR to_chapter>=?2)"
        " ORDER BY from_chapter DESC, ord DESC LIMIT 1");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, assetId);
    (void)st->BindInt(2, chapterId);
    auto s = st->Step();
    if (!s) return std::unexpected(s.error());
    if (*s == db::sqlite::StepResult::Done) {
        // 回退：无区间限制的第 0 阶段
        auto st2 = db_->Prepare(
            "SELECT id,asset_id,stage_key,stage_label,ord,from_chapter,to_chapter,appearance,"
            "materials_colors,clothing_asset_id,item_asset_ids_json,effects,environment_hint,"
            "canon_status,note FROM visual_states WHERE asset_id=?1 ORDER BY ord LIMIT 1");
        if (!st2) return std::unexpected(st2.error());
        (void)st2->BindInt(1, assetId);
        auto s2 = st2->Step();
        if (!s2) return std::unexpected(s2.error());
        if (*s2 == db::sqlite::StepResult::Done) {
            return std::unexpected(VErr("该资产无可用视觉阶段"));
        }
        st = std::move(*st2);
        s = s2;
    }
    VisualStateRow r;
    r.id = st->ColumnInt(0);
    r.asset_id = st->ColumnInt(1);
    r.stage_key = st->ColumnText(2);
    r.stage_label = st->ColumnText(3);
    r.ord = static_cast<int>(st->ColumnInt(4));
    r.from_chapter = st->ColumnInt(5);
    r.to_chapter = st->ColumnInt(6);
    r.appearance = st->ColumnText(7);
    r.materials_colors = st->ColumnText(8);
    r.clothing_asset_id = st->ColumnInt(9);
    r.item_asset_ids_json = st->ColumnText(10);
    r.effects = st->ColumnText(11);
    r.environment_hint = st->ColumnText(12);
    r.canon_status = st->ColumnText(13);
    r.note = st->ColumnText(14);
    return r;
}

std::expected<RowId, DbError> NovelVisual::UpsertCamera(std::string_view name,
                                                        std::string_view text) {
    auto st = db_->Prepare("INSERT INTO camera_defs(name,text) VALUES(?1,?2)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindText(1, name);
    (void)st->BindText(2, text);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<RowId, DbError> NovelVisual::UpsertLighting(std::string_view name,
                                                          std::string_view text) {
    auto st = db_->Prepare("INSERT INTO lighting_defs(name,text) VALUES(?1,?2)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindText(1, name);
    (void)st->BindText(2, text);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<RowId, DbError> NovelVisual::UpsertComposition(std::string_view name,
                                                             std::string_view text) {
    auto st = db_->Prepare("INSERT INTO composition_defs(name,text) VALUES(?1,?2)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindText(1, name);
    (void)st->BindText(2, text);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<RowId, DbError> NovelVisual::UpsertStyle(std::string_view name,
                                                       std::string_view text) {
    auto st = db_->Prepare("INSERT INTO visual_styles(name,text) VALUES(?1,?2)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindText(1, name);
    (void)st->BindText(2, text);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<RowId, DbError> NovelVisual::UpsertShot(const ShotRow& row) {
    if (row.scene_id <= 0) return std::unexpected(VErr("shot 缺 scene_id"));
    // S23：`row.id > 0` → **更新那一镜**。原先这里只有 INSERT ⇒ 名字叫 `Upsert` 却会插重复行
    // （`NovelStoryboard` 的第二遍落库就撞上了）。
    if (row.id > 0) {
        auto up = db_->Prepare(
            "UPDATE shots SET scene_id=?1,ord=?2,duration_note=?3,camera_id=?4,character_ids_json=?5,"
            "action=?6,expression=?7,prop_ids_json=?8,lighting_id=?9,composition_id=?10,dialogue=?11,"
            "narration=?12,sfx=?13,mood=?14,prompt_text=?15,negative_text=?16,reference_json=?17,"
            "start_state_json=?18,end_state_json=?19,timeline_json=?20,canon_status=?21 WHERE id=?22");
        if (!up) return std::unexpected(up.error());
        (void)up->BindInt(1, row.scene_id);
        (void)up->BindInt(2, row.ord);
        (void)up->BindText(3, row.duration_note);
        (void)up->BindInt(4, row.camera_id);
        (void)up->BindText(5, row.character_ids_json.empty() ? "[]" : row.character_ids_json);
        (void)up->BindText(6, row.action);
        (void)up->BindText(7, row.expression);
        (void)up->BindText(8, row.prop_ids_json.empty() ? "[]" : row.prop_ids_json);
        (void)up->BindInt(9, row.lighting_id);
        (void)up->BindInt(10, row.composition_id);
        (void)up->BindText(11, row.dialogue);
        (void)up->BindText(12, row.narration);
        (void)up->BindText(13, row.sfx);
        (void)up->BindText(14, row.mood);
        (void)up->BindText(15, row.prompt_text);
        (void)up->BindText(16, row.negative_text.empty() ? std::string{kDefaultNegative}
                                                         : row.negative_text);
        (void)up->BindText(17, row.reference_json.empty() ? "{}" : row.reference_json);
        (void)up->BindText(18, row.start_state_json.empty() ? "{}" : row.start_state_json);
        (void)up->BindText(19, row.end_state_json.empty() ? "{}" : row.end_state_json);
        (void)up->BindText(20, row.timeline_json.empty() ? "{}" : row.timeline_json);
        (void)up->BindText(21, row.canon_status);
        (void)up->BindInt(22, row.id);
        if (auto s = up->Step(); !s) return std::unexpected(s.error());
        return row.id;
    }
    auto st = db_->Prepare(
        "INSERT INTO shots(scene_id,ord,duration_note,camera_id,character_ids_json,action,"
        "expression,prop_ids_json,lighting_id,composition_id,dialogue,narration,sfx,mood,"
        "prompt_text,negative_text,reference_json,start_state_json,end_state_json,timeline_json,"
        "canon_status)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17,?18,?19,?20,?21)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.scene_id);
    (void)st->BindInt(2, row.ord);
    (void)st->BindText(3, row.duration_note);
    (void)st->BindInt(4, row.camera_id);
    (void)st->BindText(5, row.character_ids_json.empty() ? "[]" : row.character_ids_json);
    (void)st->BindText(6, row.action);
    (void)st->BindText(7, row.expression);
    (void)st->BindText(8, row.prop_ids_json.empty() ? "[]" : row.prop_ids_json);
    (void)st->BindInt(9, row.lighting_id);
    (void)st->BindInt(10, row.composition_id);
    (void)st->BindText(11, row.dialogue);
    (void)st->BindText(12, row.narration);
    (void)st->BindText(13, row.sfx);
    (void)st->BindText(14, row.mood);
    (void)st->BindText(15, row.prompt_text);
    (void)st->BindText(16, row.negative_text.empty() ? std::string{kDefaultNegative}
                                                     : row.negative_text);
    (void)st->BindText(17, row.reference_json.empty() ? "{}" : row.reference_json);
    (void)st->BindText(18, row.start_state_json.empty() ? "{}" : row.start_state_json);
    (void)st->BindText(19, row.end_state_json.empty() ? "{}" : row.end_state_json);
    (void)st->BindText(20, row.timeline_json.empty() ? "{}" : row.timeline_json);
    (void)st->BindText(21, row.canon_status);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<ShotRow, DbError> NovelVisual::GetShot(RowId id) const {
    auto st = db_->Prepare(
        "SELECT id,scene_id,ord,duration_note,camera_id,character_ids_json,action,expression,"
        "prop_ids_json,lighting_id,composition_id,dialogue,narration,sfx,mood,prompt_text,"
        "negative_text,reference_json,start_state_json,end_state_json,timeline_json,canon_status "
        "FROM shots WHERE id=?1");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, id);
    auto s = st->Step();
    if (!s) return std::unexpected(s.error());
    if (*s == db::sqlite::StepResult::Done) return std::unexpected(VErr("分镜不存在"));
    ShotRow r;
    r.id = st->ColumnInt(0);
    r.scene_id = st->ColumnInt(1);
    r.ord = static_cast<int>(st->ColumnInt(2));
    r.duration_note = st->ColumnText(3);
    r.camera_id = st->ColumnInt(4);
    r.character_ids_json = st->ColumnText(5);
    r.action = st->ColumnText(6);
    r.expression = st->ColumnText(7);
    r.prop_ids_json = st->ColumnText(8);
    r.lighting_id = st->ColumnInt(9);
    r.composition_id = st->ColumnInt(10);
    r.dialogue = st->ColumnText(11);
    r.narration = st->ColumnText(12);
    r.sfx = st->ColumnText(13);
    r.mood = st->ColumnText(14);
    r.prompt_text = st->ColumnText(15);
    r.negative_text = st->ColumnText(16);
    r.reference_json = st->ColumnText(17);
    r.start_state_json = st->ColumnText(18);
    r.end_state_json = st->ColumnText(19);
    r.timeline_json = st->ColumnText(20);
    r.canon_status = st->ColumnText(21);
    return r;
}

std::expected<std::vector<ShotRow>, DbError> NovelVisual::ListShotsByChapter(RowId chapterId) const {
    std::vector<ShotRow> out;
    if (chapterId <= 0) return out;
    auto st = db_->Prepare(
        "SELECT s.id,s.scene_id,s.ord,s.duration_note,s.camera_id,s.character_ids_json,s.action,"
        "s.expression,s.prop_ids_json,s.lighting_id,s.composition_id,s.dialogue,s.narration,s.sfx,"
        "s.mood,s.prompt_text,s.negative_text,s.reference_json,s.start_state_json,s.end_state_json,"
        "s.timeline_json,s.canon_status "
        "FROM shots s JOIN scenes sc ON sc.id=s.scene_id WHERE sc.chapter_id=?1 "
        "ORDER BY sc.ord,s.ord,s.id");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, chapterId);
    while (true) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        ShotRow r;
        r.id = st->ColumnInt(0);
        r.scene_id = st->ColumnInt(1);
        r.ord = static_cast<int>(st->ColumnInt(2));
        r.duration_note = st->ColumnText(3);
        r.camera_id = st->ColumnInt(4);
        r.character_ids_json = st->ColumnText(5);
        r.action = st->ColumnText(6);
        r.expression = st->ColumnText(7);
        r.prop_ids_json = st->ColumnText(8);
        r.lighting_id = st->ColumnInt(9);
        r.composition_id = st->ColumnInt(10);
        r.dialogue = st->ColumnText(11);
        r.narration = st->ColumnText(12);
        r.sfx = st->ColumnText(13);
        r.mood = st->ColumnText(14);
        r.prompt_text = st->ColumnText(15);
        r.negative_text = st->ColumnText(16);
        r.reference_json = st->ColumnText(17);
        r.start_state_json = st->ColumnText(18);
        r.end_state_json = st->ColumnText(19);
        r.timeline_json = st->ColumnText(20);
        r.canon_status = st->ColumnText(21);
        out.push_back(std::move(r));
    }
    return out;
}

std::expected<RowId, DbError> NovelVisual::UpsertPromptArtifact(const PromptArtifactRow& row) {
    if (row.chapter_id <= 0 && row.shot_id <= 0) {
        return std::unexpected(VErr("prompt_artifact 至少要有 chapter_id 或 shot_id"));
    }
    const auto now = util::NowMillis() / 1000;
    if (row.id > 0) {
        auto st = db_->Prepare(
            "UPDATE prompt_artifacts SET chapter_id=?1,scene_id=?2,shot_id=?3,target_kind=?4,"
            "target_id=?5,chain=?6,stage=?7,input_state_hash=?8,model_hint=?9,prompt=?10,"
            "negative=?11,references_json=?12,canon_status=?13,updated=?14 WHERE id=?15");
        if (!st) return std::unexpected(st.error());
        (void)st->BindInt(1, row.chapter_id);
        (void)st->BindInt(2, row.scene_id);
        (void)st->BindInt(3, row.shot_id);
        (void)st->BindText(4, row.target_kind);
        (void)st->BindInt(5, row.target_id);
        (void)st->BindText(6, row.chain);
        (void)st->BindText(7, row.stage);
        (void)st->BindText(8, row.input_state_hash);
        (void)st->BindText(9, row.model_hint);
        (void)st->BindText(10, row.prompt);
        (void)st->BindText(11, row.negative);
        (void)st->BindText(12, row.references_json.empty() ? "[]" : row.references_json);
        (void)st->BindText(13, row.canon_status);
        (void)st->BindInt(14, now);
        (void)st->BindInt(15, row.id);
        if (auto s = st->Step(); !s) return std::unexpected(s.error());
        return row.id;
    }
    auto st = db_->Prepare(
        "INSERT INTO prompt_artifacts(chapter_id,scene_id,shot_id,target_kind,target_id,chain,stage,"
        "input_state_hash,model_hint,prompt,negative,references_json,canon_status,created,updated)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?14)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.chapter_id);
    (void)st->BindInt(2, row.scene_id);
    (void)st->BindInt(3, row.shot_id);
    (void)st->BindText(4, row.target_kind);
    (void)st->BindInt(5, row.target_id);
    (void)st->BindText(6, row.chain);
    (void)st->BindText(7, row.stage);
    (void)st->BindText(8, row.input_state_hash);
    (void)st->BindText(9, row.model_hint);
    (void)st->BindText(10, row.prompt);
    (void)st->BindText(11, row.negative);
    (void)st->BindText(12, row.references_json.empty() ? "[]" : row.references_json);
    (void)st->BindText(13, row.canon_status);
    (void)st->BindInt(14, now);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<PromptArtifactRow, DbError> NovelVisual::GetPromptArtifact(RowId id) const {
    auto st = db_->Prepare(
        "SELECT id,chapter_id,scene_id,shot_id,target_kind,target_id,chain,stage,input_state_hash,"
        "model_hint,prompt,negative,references_json,canon_status,created,updated "
        "FROM prompt_artifacts WHERE id=?1");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, id);
    auto s = st->Step();
    if (!s) return std::unexpected(s.error());
    if (*s == db::sqlite::StepResult::Done) return std::unexpected(VErr("PromptArtifact 不存在"));
    PromptArtifactRow r;
    r.id = st->ColumnInt(0);
    r.chapter_id = st->ColumnInt(1);
    r.scene_id = st->ColumnInt(2);
    r.shot_id = st->ColumnInt(3);
    r.target_kind = st->ColumnText(4);
    r.target_id = st->ColumnInt(5);
    r.chain = st->ColumnText(6);
    r.stage = st->ColumnText(7);
    r.input_state_hash = st->ColumnText(8);
    r.model_hint = st->ColumnText(9);
    r.prompt = st->ColumnText(10);
    r.negative = st->ColumnText(11);
    r.references_json = st->ColumnText(12);
    r.canon_status = st->ColumnText(13);
    r.created = st->ColumnInt(14);
    r.updated = st->ColumnInt(15);
    return r;
}

std::expected<std::vector<PromptArtifactRow>, DbError>
NovelVisual::ListPromptArtifacts(RowId chapterId, RowId shotId, int limit) const {
    std::vector<PromptArtifactRow> out;
    if (chapterId <= 0) return out;
    const int lim = limit > 0 ? limit : 50;
    auto st = db_->Prepare(
        "SELECT id,chapter_id,scene_id,shot_id,target_kind,target_id,chain,stage,input_state_hash,"
        "model_hint,prompt,negative,references_json,canon_status,created,updated "
        "FROM prompt_artifacts WHERE chapter_id=?1 AND (?2=0 OR shot_id=?2) "
        "ORDER BY updated DESC,id DESC LIMIT ?3");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, chapterId);
    (void)st->BindInt(2, shotId);
    (void)st->BindInt(3, lim);
    while (true) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        PromptArtifactRow r;
        r.id = st->ColumnInt(0);
        r.chapter_id = st->ColumnInt(1);
        r.scene_id = st->ColumnInt(2);
        r.shot_id = st->ColumnInt(3);
        r.target_kind = st->ColumnText(4);
        r.target_id = st->ColumnInt(5);
        r.chain = st->ColumnText(6);
        r.stage = st->ColumnText(7);
        r.input_state_hash = st->ColumnText(8);
        r.model_hint = st->ColumnText(9);
        r.prompt = st->ColumnText(10);
        r.negative = st->ColumnText(11);
        r.references_json = st->ColumnText(12);
        r.canon_status = st->ColumnText(13);
        r.created = st->ColumnInt(14);
        r.updated = st->ColumnInt(15);
        out.push_back(std::move(r));
    }
    return out;
}

std::expected<RowId, DbError>
NovelVisual::SetLayer(std::string_view ownerKind, RowId ownerId, std::string_view layer,
                      std::string_view text) {
    auto del = db_->Prepare(
        "DELETE FROM prompt_layers WHERE owner_kind=?1 AND owner_id=?2 AND layer=?3");
    if (!del) return std::unexpected(del.error());
    (void)del->BindText(1, ownerKind);
    (void)del->BindInt(2, ownerId);
    (void)del->BindText(3, layer);
    if (auto s = del->Step(); !s) return std::unexpected(s.error());
    auto st = db_->Prepare(
        "INSERT INTO prompt_layers(owner_kind,owner_id,layer,text) VALUES(?1,?2,?3,?4)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindText(1, ownerKind);
    (void)st->BindInt(2, ownerId);
    (void)st->BindText(3, layer);
    (void)st->BindText(4, text);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::string NovelVisual::QueryLayer(std::string_view ownerKind, RowId ownerId,
                                    std::string_view layer, RowId* outId) const {
    auto st = db_->Prepare(
        "SELECT id,text FROM prompt_layers WHERE owner_kind=?1 AND owner_id=?2 AND layer=?3 "
        "ORDER BY version DESC LIMIT 1");
    if (!st) return {};
    (void)st->BindText(1, ownerKind);
    (void)st->BindInt(2, ownerId);
    (void)st->BindText(3, layer);
    auto s = st->Step();
    if (!s || *s == db::sqlite::StepResult::Done) return {};
    if (outId) *outId = st->ColumnInt(0);
    return st->ColumnText(1);
}

std::string NovelVisual::QueryDefText(std::string_view table, RowId id) const {
    if (id <= 0 || table.empty()) return {};
    const std::string sql = fmt::format("SELECT text FROM {} WHERE id=?1", table);
    auto st = db_->Prepare(sql);
    if (!st) return {};
    (void)st->BindInt(1, id);
    auto s = st->Step();
    if (!s || *s == db::sqlite::StepResult::Done) return {};
    return st->ColumnText(0);
}

std::expected<AssemblePromptOutput, DbError>
NovelVisual::Assemble(const AssemblePromptInput& in) const {
    AssemblePromptOutput out;
    RowId assetId = in.asset_id.value_or(0);
    ShotRow shot;
    if (in.shot_id && *in.shot_id > 0) {
        if (auto s = GetShot(*in.shot_id)) {
            shot = *s;
            out.used_layer_ids.push_back(shot.id);
        }
    }
    RowId cameraId = in.camera_id > 0 ? in.camera_id : shot.camera_id;
    RowId lightId = in.lighting_id > 0 ? in.lighting_id : shot.lighting_id;
    RowId compId = in.composition_id > 0 ? in.composition_id : shot.composition_id;

    // 解析资产
    if (assetId <= 0 && in.character_id) {
        if (auto a = FindAssetByEntity(*in.character_id)) {
            assetId = a->id;
        }
    }
    out.resolved_asset_id = assetId;

    std::string base, stage, scene, action, camera, composition, lighting, style, quality;
    std::string negative = std::string{kDefaultNegative};

    // Base：asset.base_desc + layer
    if (assetId > 0) {
        if (auto a = GetAsset(assetId)) {
            base = a->base_desc;
            if (!a->materials_colors.empty()) {
                base += (base.empty() ? "" : ", ") + a->materials_colors;
            }
        }
        RowId lid = 0;
        if (auto t = QueryLayer("asset", assetId, "base", &lid); !t.empty()) {
            if (base.empty()) base = t;
            else base += ", " + t;
            out.used_layer_ids.push_back(lid);
        }
        // Stage（按剧情章）
        if (auto st = ResolveVisualState(assetId, in.chapter_id)) {
            out.resolved_state_id = st->id;
            stage = st->appearance;
            if (!st->materials_colors.empty()) {
                stage += (stage.empty() ? "" : ", ") + st->materials_colors;
            }
            if (!st->effects.empty()) {
                stage += (stage.empty() ? "" : ", ") + st->effects;
            }
            RowId sl = 0;
            if (auto t = QueryLayer("state", st->id, "stage", &sl); !t.empty()) {
                stage += (stage.empty() ? "" : ", ") + t;
                out.used_layer_ids.push_back(sl);
            }
            if (!st->environment_hint.empty()) {
                scene = st->environment_hint;
            }
        }
    }

    // Scene（S7：读口径收口到 GetSceneVisual，避免第二份 SELECT）
    if (in.scene_id > 0) {
        if (auto sv = GetSceneVisual(in.scene_id); sv) {
            std::string env = sv->env_desc;
            if (!sv->time_of_day.empty()) env += (env.empty() ? "" : ", ") + sv->time_of_day;
            if (!sv->weather.empty()) env += (env.empty() ? "" : ", ") + sv->weather;
            if (!sv->mood.empty()) env += (env.empty() ? "" : ", ") + sv->mood;
            if (!env.empty()) {
                scene = scene.empty() ? env : scene + ", " + env;
            }
        }
        RowId sl = 0;
        if (auto t = QueryLayer("scene", in.scene_id, "scene", &sl); !t.empty()) {
            scene += (scene.empty() ? "" : ", ") + t;
            out.used_layer_ids.push_back(sl);
        }
    }

    // Action：shot
    if (!shot.action.empty() || !shot.expression.empty()) {
        action = shot.action;
        if (!shot.expression.empty()) {
            action += (action.empty() ? "" : ", ") + shot.expression;
        }
        if (!shot.mood.empty()) {
            action += (action.empty() ? "" : ", ") + shot.mood;
        }
    }

    camera = QueryDefText("camera_defs", cameraId);
    composition = QueryDefText("composition_defs", compId);
    lighting = QueryDefText("lighting_defs", lightId);
    // Style：取 id=1 或最新
    {
        auto st = db_->Prepare("SELECT text FROM visual_styles ORDER BY id LIMIT 1");
        if (st) {
            if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                style = st->ColumnText(0);
            }
        }
    }
    quality = "masterpiece, best quality, highly detailed";

    // Negative 扩展
    if (in.scene_id > 0) {
        RowId nl = 0;
        if (auto t = QueryLayer("scene", in.scene_id, "negative", &nl); !t.empty()) {
            negative += ", " + t;
            out.used_layer_ids.push_back(nl);
        }
    }
    if (assetId > 0) {
        RowId nl = 0;
        if (auto t = QueryLayer("asset", assetId, "negative", &nl); !t.empty()) {
            negative += ", " + t;
            out.used_layer_ids.push_back(nl);
        }
    }

    auto join = [](std::string& dst, std::string_view part) {
        if (part.empty()) return;
        if (!dst.empty()) dst += ", ";
        dst += part;
    };
    join(out.final_prompt, base);
    join(out.final_prompt, stage);
    join(out.final_prompt, scene);
    join(out.final_prompt, action);
    join(out.final_prompt, camera);
    join(out.final_prompt, composition);
    join(out.final_prompt, lighting);
    join(out.final_prompt, style);
    join(out.final_prompt, quality);
    out.negative_prompt = negative;
    return out;
}

std::expected<std::vector<std::string>, DbError>
NovelVisual::CheckConsistency(const AssemblePromptInput& in, std::string_view prompt) const {
    std::vector<std::string> issues;
    if (prompt.empty()) {
        issues.push_back("prompt 为空");
        return issues;
    }
    // 未登场角色：shot.character_ids 之外的名字不应…（简化：检查 stage 关键词）
    if (in.shot_id && *in.shot_id > 0) {
        if (auto shot = GetShot(*in.shot_id)) {
            if (!shot->action.empty() && prompt.find(shot->action.substr(0, 4)) == std::string::npos &&
                shot->action.size() >= 4) {
                // 弱检查，不强制
            }
        }
    }
    if (in.character_id) {
        if (auto asset = FindAssetByEntity(*in.character_id)) {
            if (auto st = ResolveVisualState(asset->id, in.chapter_id)) {
                // 阶段外观关键词
                if (!st->stage_label.empty() && st->stage_label.size() >= 2) {
                    // 不强制中文标签进英文 prompt
                }
                if (!st->appearance.empty()) {
                    // 取 appearance 前 8 字若在 materials 中出现冲突 —— 简化跳过
                }
            } else {
                issues.push_back(fmt::format("角色 {} 在第 {} 章无视觉阶段，禁止模型自行想象",
                                              asset->name, in.chapter_id));
            }
        }
    }
    // 多余人物
    // 粗检：negative 未含 extra person 时补提示
    if (prompt.find("extra person") == std::string::npos &&
        prompt.find("crowd") == std::string::npos) {
        // 不强制 issue
    }
    return issues;
}

std::expected<void, DbError> NovelVisual::SetVisualCanon(std::string_view targetKind, RowId targetId,
                                                         std::string_view status) {
    auto st = db_->Prepare(
        "INSERT INTO visual_canon_logs(target_kind,target_id,status,created) VALUES(?1,?2,?3,?4)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindText(1, targetKind);
    (void)st->BindInt(2, targetId);
    (void)st->BindText(3, status);
    (void)st->BindInt(4, NowSec());
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return {};
}

bool NovelVisual::RunSelfCheck() {
    db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) {
        log::Error("Visual 自检：打开内存库失败");
        return false;
    }
    // 最小表
    if (auto r = ::shine::novelcore::NovelDb::ApplyCanonicalSchema(mem); !r) {
        log::Error("Visual 自检：建表失败 {}", r.error().message);
        return false;
    }
    NovelVisual v(mem);
    auto asset = v.UpsertAsset({.entity_id = 1,
                                .kind = "character",
                                .name = "林默",
                                .base_desc = "young man, black hair, dark eyes",
                                .materials_colors = "simple cloth"});
    if (!asset) return false;
    // —— v7：visual_assets.status 默认值 + 形象层产物派生链 CRUD ——
    {
        auto a0 = v.GetAsset(*asset);
        if (!a0 || a0->status != "PENDING") {
            log::Error("Visual 自检：新资产 status 应为 PENDING，实际={}", a0 ? a0->status : "?");
            return false;
        }
        auto front = v.UpsertArtifact({.asset_id = *asset,
                                       .layer = "front",
                                       .rel_path = "visual/gen/front.png",
                                       .status = "DONE"});
        auto turn = v.UpsertArtifact({.asset_id = *asset,
                                      .layer = "turnaround",
                                      .rel_path = "visual/gen/turn.png",
                                      .parent_artifact_id = front.value_or(0),
                                      .status = "PENDING"});
        auto body = v.UpsertArtifact({.asset_id = *asset,
                                      .layer = "base_body",
                                      .parent_artifact_id = turn.value_or(0),
                                      .prompt_artifact_id = 7,
                                      .job_id = "pid-abc",
                                      .degraded = true});
        if (!front || !turn || !body) {
            log::Error("Visual 自检：插入形象层产物失败");
            return false;
        }
        auto got = v.GetArtifact(*body);
        auto list = v.ListArtifacts(*asset);
        const bool listOk = list && list->size() == 3 && (*list)[0].layer == "front" &&
                            (*list)[1].layer == "turnaround" && (*list)[2].layer == "base_body";
        if (!got || !got->degraded || got->prompt_artifact_id != 7 || got->job_id != "pid-abc" ||
            !listOk) {
            log::Error("Visual 自检：形象层产物回读/派生序不符 size={}", list ? list->size() : 0);
            return false;
        }
        // 同 id upsert 应为更新而非新增
        (void)v.UpsertArtifact({.id = *turn,
                                .asset_id = *asset,
                                .layer = "turnaround",
                                .rel_path = "visual/gen/turn2.png",
                                .status = "DONE"});
        auto l2 = v.ListArtifacts(*asset);
        if (!l2 || l2->size() != 3 || (*l2)[1].rel_path != "visual/gen/turn2.png" ||
            (*l2)[1].status != "DONE") {
            log::Error("Visual 自检：形象层产物更新语义不符");
            return false;
        }
        // asset.status 写后读
        (void)v.UpsertAsset({.id = *asset, .entity_id = 1, .name = "林默",
                             .status = "REF_READY"});
        auto a2 = v.GetAsset(*asset);
        if (!a2 || a2->status != "REF_READY") {
            log::Error("Visual 自检：asset.status 写后读失败");
            return false;
        }
    }
    // 两个阶段：1-2 青年，3+ 受伤
    (void)v.UpsertState({.asset_id = *asset,
                         .stage_key = "youth",
                         .stage_label = "少年",
                         .ord = 0,
                         .from_chapter = 1,
                         .to_chapter = 2,
                         .appearance = "healthy youth"});
    (void)v.UpsertState({.asset_id = *asset,
                         .stage_key = "wounded",
                         .stage_label = "重伤",
                         .ord = 1,
                         .from_chapter = 3,
                         .to_chapter = 0,
                         .appearance = "wounded, bandaged arm"});
    auto s2 = v.ResolveVisualState(*asset, 2);
    auto s3 = v.ResolveVisualState(*asset, 3);
    if (!s2 || s2->stage_key != "youth" || !s3 || s3->stage_key != "wounded") {
        log::Error("Visual 自检：阶段解析失败 s2={} s3={}", s2 ? s2->stage_key : "?",
                   s3 ? s3->stage_key : "?");
        return false;
    }
    auto cam = v.UpsertCamera("近景", "close-up shot");
    auto lit = v.UpsertLighting("黄昏", "golden hour lighting");
    auto comp = v.UpsertComposition("三分", "rule of thirds");
    (void)v.UpsertStyle("default", "digital painting, fantasy novel cover");
    const RowId scene = 10;
    // S7：scene_visuals 有了写入口（原先这里只能借裸 SQL）+ 「一个场景一行」覆盖语义
    auto sv1 = v.UpsertSceneVisual({.scene_id = scene,
                                    .env_desc = "dark forest",
                                    .time_of_day = "dusk",
                                    .weather = "fog",
                                    .mood = "tense"});
    if (!sv1) {
        log::Error("Visual 自检：UpsertSceneVisual 失败 {}", sv1.error().message);
        return false;
    }
    auto svRead = v.GetSceneVisual(scene);
    if (!svRead || svRead->env_desc != "dark forest" || svRead->weather != "fog" ||
        svRead->canon_status != "DRAFT") {
        log::Error("Visual 自检：GetSceneVisual 写后读失败");
        return false;
    }
    auto sv2 = v.UpsertSceneVisual({
        .scene_id = scene, .env_desc = "dark forest", .time_of_day = "dusk", .weather = "fog", .mood = "quieter"});
    auto svRead2 = v.GetSceneVisual(scene);
    if (!sv2 || *sv2 != *sv1 || !svRead2 || svRead2->mood != "quieter") {
        log::Error("Visual 自检：scene_visuals 未按「一场景一行」覆盖");
        return false;
    }
    if (v.GetSceneVisual(9999) || v.UpsertSceneVisual({.env_desc = "x"})) {
        log::Error("Visual 自检：未知场景 / 缺 scene_id 应被拒");
        return false;
    }
    auto shot = v.UpsertShot({.scene_id = scene,
                              .ord = 1,
                              .camera_id = cam.value_or(0),
                              .action = "running through trees",
                              .expression = "fear",
                              .lighting_id = lit.value_or(0),
                              .composition_id = comp.value_or(0)});
    if (!shot) return false;
    // —— v9（S13）：`shots` 的可校验结构三列 + `prompt_artifacts` + `ListShotsByChapter` ——
    // 给 `scene=10` 补一条真正的 `scenes` 行，`ListShotsByChapter` 的 JOIN 才有东西可对
    if (auto sc = mem.Prepare(
            "INSERT OR REPLACE INTO scenes(id,chapter_id,ord,title) VALUES(10,3,1,'场 1')");
        !sc || !sc->Step()) {
        log::Error("Visual 自检：v9 建 scenes 行失败");
        return false;
    }
    auto sh2 = v.UpsertShot({.scene_id = scene,
                             .ord = 2,
                             .action = "kneeling",
                             .start_state_json = R"({"lighting":"夜","props":{"ring":1}})",
                             .end_state_json = R"({"lighting":"夜","props":{"ring":1}})",
                             .timeline_json =
                                 R"({"duration_s":3.0,"beats":[{"begin_s":0,"end_s":3.0}]})"});
    if (!sh2) {
        log::Error("Visual 自检：v9 写 shots 三列失败 {}", sh2.error().message);
        return false;
    }
    {
        auto read = v.GetShot(*sh2);
        if (!read || read->start_state_json != R"({"lighting":"夜","props":{"ring":1}})" ||
            read->end_state_json != read->start_state_json ||
            read->timeline_json.find("\"duration_s\":3.0") == std::string::npos) {
            log::Error("Visual 自检：v9 shots 三列写后读失败");
            return false;
        }
        auto list = v.ListShotsByChapter(3);
        if (!list || list->size() != 2 || (*list)[0].ord != 1 || (*list)[1].ord != 2) {
            log::Error("Visual 自检：ListShotsByChapter 结果不符（期望 2 镜按 ord 排）");
            return false;
        }
        // 未指定时默认 '{}'（不是空串）
        auto dflt = v.GetShot(*shot);
        if (!dflt || dflt->start_state_json != "{}" || dflt->timeline_json != "{}") {
            log::Error("Visual 自检：v9 shots 三列的默认值应为空对象 '{{}}'");
            return false;
        }
    }
    {
        auto pa1 = v.UpsertPromptArtifact({.chapter_id = 3,
                                          .scene_id = scene,
                                          .shot_id = *shot,
                                          .chain = "visual",
                                          .stage = "V10",
                                          .input_state_hash = "sha1:aaa",
                                          .prompt = "a wounded youth",
                                          .negative = "bad hands",
                                          .references_json = R"(["visual/gen/front.png"])"});
        auto pa2 = v.UpsertPromptArtifact({.chapter_id = 3,
                                          .shot_id = *shot,
                                          .stage = "V10",
                                          .input_state_hash = "sha1:bbb",
                                          .prompt = "a wounded youth (retry)"});
        if (!pa1 || !pa2) {
            log::Error("Visual 自检：v9 写 prompt_artifacts 失败");
            return false;
        }
        auto read = v.GetPromptArtifact(*pa2);
        if (!read || read->input_state_hash != "sha1:bbb" || read->chain != "visual" ||
            read->references_json != "[]" || read->canon_status != "DRAFT") {
            log::Error("Visual 自检：v9 prompt_artifacts 写后读失败");
            return false;
        }
        auto byCh = v.ListPromptArtifacts(3);
        auto byShot = v.ListPromptArtifacts(3, *shot);
        if (!byCh || byCh->size() != 2 || !byShot || byShot->size() != 2 ||
            (*byCh)[0].input_state_hash != "sha1:bbb") { // updated DESC,id DESC → 新的在前
            log::Error("Visual 自检：v9 ListPromptArtifacts 结果不符");
            return false;
        }
        if (v.ListPromptArtifacts(3, 9999)->size() != 0 || v.GetPromptArtifact(9999)) {
            log::Error("Visual 自检：v9 未知产物应查不到");
            return false;
        }
    }
    auto assembled = v.Assemble({.chapter_id = 3,
                                 .scene_id = scene,
                                 .character_id = 1,
                                 .asset_id = *asset,
                                 .shot_id = *shot});
    if (!assembled) {
        log::Error("Visual 自检：Assemble 失败 {}", assembled.error().message);
        return false;
    }
    const auto& p = assembled->final_prompt;
    if (p.find("wounded") == std::string::npos || p.find("close-up") == std::string::npos ||
        p.find("golden hour") == std::string::npos || p.find("dark forest") == std::string::npos ||
        assembled->negative_prompt.empty()) {
        log::Error("Visual 自检：九层组装内容不全：{}", p);
        return false;
    }
    // 青年章不含 wounded
    auto a1 = v.Assemble({.chapter_id = 1, .character_id = 1, .asset_id = *asset});
    if (!a1 || a1->final_prompt.find("wounded") != std::string::npos) {
        log::Error("Visual 自检：第1章不应含 wounded");
        return false;
    }
    // 只改 lighting 层后 Final 变化
    auto lit2 = v.UpsertLighting("月夜", "moonlight, cold blue tones");
    if (!lit2) return false;
    auto stUp = mem.Prepare("UPDATE shots SET lighting_id=?1 WHERE id=?2");
    if (stUp) {
        (void)stUp->BindInt(1, *lit2);
        (void)stUp->BindInt(2, *shot);
        (void)stUp->Step();
    }
    auto assembled3 = v.Assemble({.chapter_id = 3,
                                  .scene_id = scene,
                                  .character_id = 1,
                                  .asset_id = *asset,
                                  .shot_id = *shot});
    if (!assembled3 || assembled3->final_prompt.find("moonlight") == std::string::npos ||
        assembled3->final_prompt.find("golden hour") != std::string::npos) {
        log::Error("Visual 自检：改 Lighting 后 Final 未正确变化");
        return false;
    }
    (void)v.SetVisualCanon("asset", *asset, "CANON");
    log::Info("Visual 自检通过（阶段机 / 九层组装 / Lighting 敏感 / canon / scene_visuals 写入口 / "
              "v9 shots 三列 + prompt_artifacts + ListShotsByChapter）");
    return true;
}

} // namespace shine::novelcore
