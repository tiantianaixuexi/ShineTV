#include "novel/NovelGraph.h"

#include "core/Log.h"
#include "util/Reflect.h" // 快照载荷（EntitySnapshot）走反射序列化
#include "util/Time.h"

#include <fmt/format.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <unordered_set>

namespace shine::novelcore {
namespace {

[[nodiscard]] std::int64_t NowSec() noexcept { return util::NowMillis() / 1000; }

[[nodiscard]] DbError Err(std::string_view msg) { return DbError{0, std::string{msg}}; }

// 自检用最小 schema（列名与 v3 对齐）
constexpr std::string_view kMinSchema = R"SQL(
CREATE TABLE IF NOT EXISTS entities(id INTEGER PRIMARY KEY AUTOINCREMENT,kind TEXT NOT NULL,name TEXT NOT NULL,summary TEXT,status TEXT,meta_json TEXT,created_chapter INTEGER,updated INTEGER);
CREATE TABLE IF NOT EXISTS relations(id INTEGER PRIMARY KEY AUTOINCREMENT,from_id INTEGER,to_id INTEGER,rel_type TEXT,strength INTEGER,from_chapter INTEGER,to_chapter INTEGER,reason TEXT,status TEXT);
CREATE TABLE IF NOT EXISTS entity_personas(entity_id INTEGER PRIMARY KEY,age TEXT,appearance TEXT,personality TEXT,background TEXT,"values" TEXT,desire TEXT,goal TEXT,fear TEXT,weakness TEXT,strength TEXT,ability_note TEXT,knowledge_note TEXT,memory_note TEXT);
CREATE TABLE IF NOT EXISTS character_status(id INTEGER PRIMARY KEY AUTOINCREMENT,entity_id INTEGER,chapter_id INTEGER,location_id INTEGER,body_state TEXT,mind_state TEXT,emotion_json TEXT,goal TEXT,relation_note TEXT,resource_note TEXT,secret_note TEXT,updated INTEGER);
CREATE TABLE IF NOT EXISTS chapters(id INTEGER PRIMARY KEY AUTOINCREMENT,volume_id INTEGER,ord INTEGER,title TEXT,status TEXT,summary TEXT,body TEXT,pov_entity_id INTEGER,words INTEGER,updated INTEGER);
CREATE TABLE IF NOT EXISTS volumes(id INTEGER PRIMARY KEY AUTOINCREMENT,title TEXT,ord INTEGER,summary TEXT);
CREATE TABLE IF NOT EXISTS scenes(id INTEGER PRIMARY KEY AUTOINCREMENT,chapter_id INTEGER,ord INTEGER,title TEXT,location_id INTEGER,time_label TEXT,pov_entity_id INTEGER,conflict_id INTEGER,goal TEXT,action TEXT,conflict TEXT,result TEXT,emotion TEXT,info_reveal TEXT,hook TEXT,body TEXT);
CREATE TABLE IF NOT EXISTS event_details(entity_id INTEGER PRIMARY KEY,time_label TEXT,location_id INTEGER,cause_note TEXT,result_note TEXT);
CREATE TABLE IF NOT EXISTS causal_links(id INTEGER PRIMARY KEY AUTOINCREMENT,cause_event_id INTEGER,effect_event_id INTEGER,link_type TEXT,note TEXT,ord INTEGER);
CREATE TABLE IF NOT EXISTS foreshadowings(id INTEGER PRIMARY KEY AUTOINCREMENT,title TEXT,content TEXT,status TEXT,setup_ch INTEGER,payoff_ch INTEGER,importance INTEGER,truth TEXT,entity_ids_json TEXT);
CREATE TABLE IF NOT EXISTS secrets(id INTEGER PRIMARY KEY AUTOINCREMENT,content TEXT,truth TEXT,reveal_ch INTEGER,reveal_condition TEXT,entity_id INTEGER,scope TEXT);
CREATE TABLE IF NOT EXISTS secret_knowledge(id INTEGER PRIMARY KEY AUTOINCREMENT,secret_id INTEGER,entity_id INTEGER,knows INTEGER,chapter_known INTEGER);
CREATE TABLE IF NOT EXISTS entity_ownerships(id INTEGER PRIMARY KEY AUTOINCREMENT,owner_id INTEGER,item_id INTEGER,from_chapter INTEGER,to_chapter INTEGER,how TEXT,note TEXT);
CREATE TABLE IF NOT EXISTS character_knowledge(id INTEGER PRIMARY KEY AUTOINCREMENT,entity_id INTEGER,fact_kind TEXT,fact_id INTEGER,fact_text TEXT,knows INTEGER,chapter_known INTEGER);
CREATE TABLE IF NOT EXISTS event_participants(id INTEGER PRIMARY KEY AUTOINCREMENT,event_id INTEGER,entity_id INTEGER,role TEXT);
CREATE TABLE IF NOT EXISTS scene_cast(id INTEGER PRIMARY KEY AUTOINCREMENT,scene_id INTEGER,entity_id INTEGER,role TEXT);
CREATE TABLE IF NOT EXISTS scene_foreshadows(id INTEGER PRIMARY KEY AUTOINCREMENT,scene_id INTEGER,foreshadowing_id INTEGER,action TEXT);
CREATE TABLE IF NOT EXISTS plots(id INTEGER PRIMARY KEY AUTOINCREMENT,kind TEXT,title TEXT,status TEXT,intro_ch INTEGER,target_ch INTEGER,note TEXT);
CREATE TABLE IF NOT EXISTS plot_beats(id INTEGER PRIMARY KEY AUTOINCREMENT,plot_id INTEGER,chapter_id INTEGER,ord INTEGER,beat_type TEXT,title TEXT,summary TEXT,cast_json TEXT);
CREATE TABLE IF NOT EXISTS mysteries(id INTEGER PRIMARY KEY AUTOINCREMENT,entity_id INTEGER,question TEXT,answer TEXT,status TEXT,ask_ch INTEGER,answer_ch INTEGER,importance INTEGER);
CREATE TABLE IF NOT EXISTS mystery_beats(id INTEGER PRIMARY KEY AUTOINCREMENT,mystery_id INTEGER,beat_type TEXT,chapter_id INTEGER,content TEXT,target_entity_id INTEGER,ord INTEGER);
CREATE TABLE IF NOT EXISTS character_arcs(id INTEGER PRIMARY KEY AUTOINCREMENT,entity_id INTEGER NOT NULL,ord INTEGER NOT NULL DEFAULT 0,stage TEXT NOT NULL DEFAULT '',trigger_event_id INTEGER NOT NULL DEFAULT 0,note TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS dialogue_styles(entity_id INTEGER PRIMARY KEY,sentence_len TEXT NOT NULL DEFAULT '',vocabulary TEXT NOT NULL DEFAULT '',catchphrase TEXT NOT NULL DEFAULT '',taboo_words TEXT NOT NULL DEFAULT '',habit TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS world_meta(key TEXT PRIMARY KEY,value TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS themes(id INTEGER PRIMARY KEY AUTOINCREMENT,title TEXT NOT NULL,statement TEXT NOT NULL DEFAULT '',linked_plot_id INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS entity_versions(id INTEGER PRIMARY KEY AUTOINCREMENT,entity_id INTEGER NOT NULL,ver INTEGER NOT NULL DEFAULT 1,snapshot_json TEXT NOT NULL DEFAULT '{}',note TEXT NOT NULL DEFAULT '',created INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS location_distances(id INTEGER PRIMARY KEY AUTOINCREMENT,from_id INTEGER NOT NULL,to_id INTEGER NOT NULL,distance_km REAL NOT NULL DEFAULT 0,travel_note TEXT NOT NULL DEFAULT '',days_estimate REAL NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS writing_style(id INTEGER PRIMARY KEY CHECK(id=1),pov_mode TEXT NOT NULL DEFAULT 'third_limited',sentence_len TEXT NOT NULL DEFAULT 'medium',density TEXT NOT NULL DEFAULT '',dialogue_ratio REAL NOT NULL DEFAULT 0.3,action_ratio REAL NOT NULL DEFAULT 0.3,thought_ratio REAL NOT NULL DEFAULT 0.2,env_ratio REAL NOT NULL DEFAULT 0.2,humor INTEGER NOT NULL DEFAULT 0,serious INTEGER NOT NULL DEFAULT 50,pacing TEXT NOT NULL DEFAULT '',note TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS author_rules(id INTEGER PRIMARY KEY AUTOINCREMENT,rule TEXT NOT NULL,severity TEXT NOT NULL DEFAULT 'warn',note TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS audit_logs(id INTEGER PRIMARY KEY AUTOINCREMENT,actor TEXT,action TEXT,target_kind TEXT,target_id INTEGER,detail TEXT,created INTEGER);
CREATE TABLE IF NOT EXISTS canon_logs(id INTEGER PRIMARY KEY AUTOINCREMENT,target_kind TEXT,target_id INTEGER,status TEXT,note TEXT,created INTEGER);
)SQL";

} // namespace

EntityRow NovelGraph::ReadEntity(db::sqlite::Statement& st) const {
    EntityRow e;
    e.id = st.ColumnInt(0);
    e.kind = st.ColumnText(1);
    e.name = st.ColumnText(2);
    e.summary = st.ColumnText(3);
    e.status = st.ColumnText(4);
    e.meta_json = st.ColumnText(5);
    e.created_chapter = st.ColumnInt(6);
    e.updated = st.ColumnInt(7);
    return e;
}

std::expected<RowId, DbError> NovelGraph::UpsertEntity(const EntityRow& row) {
    if (row.kind.empty() || row.name.empty()) {
        return std::unexpected(Err("实体 kind/name 不能为空"));
    }
    const auto ts = NowSec();
    if (row.id > 0) {
        auto st = db_->Prepare(
            "UPDATE entities SET kind=?1,name=?2,summary=?3,status=?4,meta_json=?5,"
            "created_chapter=?6,updated=?7 WHERE id=?8");
        if (!st) return std::unexpected(st.error());
        (void)st->BindText(1, row.kind);
        (void)st->BindText(2, row.name);
        (void)st->BindText(3, row.summary);
        (void)st->BindText(4, row.status);
        (void)st->BindText(5, row.meta_json.empty() ? "{}" : row.meta_json);
        (void)st->BindInt(6, row.created_chapter);
        (void)st->BindInt(7, ts);
        (void)st->BindInt(8, row.id);
        if (auto s = st->Step(); !s) {
            return std::unexpected(s.error());
        }
        (void)LogAudit("agent", "upsert_entity", row.kind, row.id, row.name);
        return row.id;
    }
    auto st = db_->Prepare(
        "INSERT INTO entities(kind,name,summary,status,meta_json,created_chapter,updated)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindText(1, row.kind);
    (void)st->BindText(2, row.name);
    (void)st->BindText(3, row.summary);
    (void)st->BindText(4, row.status.empty() ? "active" : row.status);
    (void)st->BindText(5, row.meta_json.empty() ? "{}" : row.meta_json);
    (void)st->BindInt(6, row.created_chapter);
    (void)st->BindInt(7, ts);
    if (auto s = st->Step(); !s) {
        return std::unexpected(s.error());
    }
    const auto id = db_->LastInsertRowId();
    (void)LogAudit("agent", "insert_entity", row.kind, id, row.name);
    return id;
}

std::expected<EntityRow, DbError> NovelGraph::GetEntity(RowId id) const {
    auto st = db_->Prepare(
        "SELECT id,kind,name,summary,status,meta_json,created_chapter,updated FROM entities WHERE id=?1");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, id);
    auto s = st->Step();
    if (!s) return std::unexpected(s.error());
    if (*s == db::sqlite::StepResult::Done) {
        return std::unexpected(Err(fmt::format("实体不存在: {}", id)));
    }
    return ReadEntity(*st);
}

std::expected<std::vector<EntityRow>, DbError>
NovelGraph::ListEntities(std::string_view kind, std::string_view nameFilter, int limit) const {
    std::string sql =
        "SELECT id,kind,name,summary,status,meta_json,created_chapter,updated FROM entities WHERE 1=1";
    if (!kind.empty()) sql += " AND kind=?1";
    if (!nameFilter.empty()) sql += " AND name LIKE ?2";
    sql += " ORDER BY id LIMIT " + std::to_string(limit > 0 ? limit : 200);

    auto st = db_->Prepare(sql);
    if (!st) return std::unexpected(st.error());
    int bind = 1;
    if (!kind.empty()) {
        (void)st->BindText(bind++, kind);
    }
    if (!nameFilter.empty()) {
        (void)st->BindText(bind++, "%" + std::string{nameFilter} + "%");
    }
    std::vector<EntityRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        out.push_back(ReadEntity(*st));
    }
    return out;
}

std::expected<RowId, DbError> NovelGraph::UpsertRelation(const RelationRow& row) {
    if (row.from_id <= 0 || row.to_id <= 0 || row.rel_type.empty()) {
        return std::unexpected(Err("关系 from/to/type 必填"));
    }
    if (row.id > 0) {
        auto st = db_->Prepare(
            "UPDATE relations SET from_id=?1,to_id=?2,rel_type=?3,strength=?4,"
            "from_chapter=?5,to_chapter=?6,reason=?7,status=?8 WHERE id=?9");
        if (!st) return std::unexpected(st.error());
        (void)st->BindInt(1, row.from_id);
        (void)st->BindInt(2, row.to_id);
        (void)st->BindText(3, row.rel_type);
        (void)st->BindInt(4, row.strength);
        (void)st->BindInt(5, row.from_chapter);
        (void)st->BindInt(6, row.to_chapter);
        (void)st->BindText(7, row.reason);
        (void)st->BindText(8, row.status);
        (void)st->BindInt(9, row.id);
        if (auto s = st->Step(); !s) return std::unexpected(s.error());
        return row.id;
    }
    auto st = db_->Prepare(
        "INSERT INTO relations(from_id,to_id,rel_type,strength,from_chapter,to_chapter,reason,status)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.from_id);
    (void)st->BindInt(2, row.to_id);
    (void)st->BindText(3, row.rel_type);
    (void)st->BindInt(4, row.strength);
    (void)st->BindInt(5, row.from_chapter);
    (void)st->BindInt(6, row.to_chapter);
    (void)st->BindText(7, row.reason);
    (void)st->BindText(8, row.status.empty() ? "active" : row.status);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<std::vector<RelationRow>, DbError>
NovelGraph::GetRelations(RowId entityId, bool includeIncoming) const {
    std::string sql =
        "SELECT id,from_id,to_id,rel_type,strength,from_chapter,to_chapter,reason,status"
        " FROM relations WHERE from_id=?1";
    if (includeIncoming) sql += " OR to_id=?1";
    auto st = db_->Prepare(sql);
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, entityId);
    std::vector<RelationRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        RelationRow r;
        r.id = st->ColumnInt(0);
        r.from_id = st->ColumnInt(1);
        r.to_id = st->ColumnInt(2);
        r.rel_type = st->ColumnText(3);
        r.strength = static_cast<int>(st->ColumnInt(4));
        r.from_chapter = st->ColumnInt(5);
        r.to_chapter = st->ColumnInt(6);
        r.reason = st->ColumnText(7);
        r.status = st->ColumnText(8);
        out.push_back(std::move(r));
    }
    return out;
}

std::expected<void, DbError> NovelGraph::UpsertPersona(const PersonaRow& row) {
    if (row.entity_id <= 0) return std::unexpected(Err("persona 缺 entity_id"));
    // ⚠️ `values` 是 SQLite 关键字 → 列名**必须加双引号**，否则 prepare 阶段就报
    // `near "values": syntax error`（2026-09-19 由 S7 的快照自检撞出来：原先人设写不进也读不出）。
    // 这里用 `INSERT OR REPLACE`（`entity_id` 是主键 → 一人一行覆盖），避免在 `excluded."values"`
    // 这种位置上再踩一次关键字。
    auto st = db_->Prepare(
        "INSERT OR REPLACE INTO entity_personas(entity_id,age,appearance,personality,background,"
        "\"values\",desire,goal,fear,weakness,strength,ability_note,knowledge_note,memory_note)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.entity_id);
    (void)st->BindText(2, row.age);
    (void)st->BindText(3, row.appearance);
    (void)st->BindText(4, row.personality);
    (void)st->BindText(5, row.background);
    (void)st->BindText(6, row.values);
    (void)st->BindText(7, row.desire);
    (void)st->BindText(8, row.goal);
    (void)st->BindText(9, row.fear);
    (void)st->BindText(10, row.weakness);
    (void)st->BindText(11, row.strength);
    (void)st->BindText(12, row.ability_note);
    (void)st->BindText(13, row.knowledge_note);
    (void)st->BindText(14, row.memory_note);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return {};
}

std::expected<PersonaRow, DbError> NovelGraph::GetPersona(RowId entityId) const {
    auto st = db_->Prepare(
        "SELECT entity_id,age,appearance,personality,background,\"values\",desire,goal,fear,"
        "weakness,strength,ability_note,knowledge_note,memory_note"
        " FROM entity_personas WHERE entity_id=?1");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, entityId);
    auto s = st->Step();
    if (!s) return std::unexpected(s.error());
    if (*s == db::sqlite::StepResult::Done) {
        return PersonaRow{.entity_id = entityId};
    }
    PersonaRow p;
    p.entity_id = st->ColumnInt(0);
    p.age = st->ColumnText(1);
    p.appearance = st->ColumnText(2);
    p.personality = st->ColumnText(3);
    p.background = st->ColumnText(4);
    p.values = st->ColumnText(5);
    p.desire = st->ColumnText(6);
    p.goal = st->ColumnText(7);
    p.fear = st->ColumnText(8);
    p.weakness = st->ColumnText(9);
    p.strength = st->ColumnText(10);
    p.ability_note = st->ColumnText(11);
    p.knowledge_note = st->ColumnText(12);
    p.memory_note = st->ColumnText(13);
    return p;
}

std::expected<RowId, DbError> NovelGraph::UpsertCharacterStatus(const CharacterStatusRow& row) {
    if (row.entity_id <= 0) return std::unexpected(Err("status 缺 entity_id"));
    const auto ts = row.updated > 0 ? row.updated : NowSec();
    auto st = db_->Prepare(
        "INSERT INTO character_status(entity_id,chapter_id,location_id,body_state,mind_state,"
        "emotion_json,goal,relation_note,resource_note,secret_note,updated)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.entity_id);
    (void)st->BindInt(2, row.chapter_id);
    (void)st->BindInt(3, row.location_id);
    (void)st->BindText(4, row.body_state);
    (void)st->BindText(5, row.mind_state);
    (void)st->BindText(6, row.emotion_json.empty() ? "{}" : row.emotion_json);
    (void)st->BindText(7, row.goal);
    (void)st->BindText(8, row.relation_note);
    (void)st->BindText(9, row.resource_note);
    (void)st->BindText(10, row.secret_note);
    (void)st->BindInt(11, ts);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<CharacterStatusRow, DbError>
NovelGraph::GetLatestCharacterStatus(RowId entityId, RowId chapterId) const {
    std::string sql =
        "SELECT id,entity_id,chapter_id,location_id,body_state,mind_state,emotion_json,goal,"
        "relation_note,resource_note,secret_note,updated FROM character_status WHERE entity_id=?1";
    if (chapterId > 0) sql += " AND chapter_id<=?2";
    sql += " ORDER BY chapter_id DESC, id DESC LIMIT 1";
    auto st = db_->Prepare(sql);
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, entityId);
    if (chapterId > 0) (void)st->BindInt(2, chapterId);
    auto s = st->Step();
    if (!s) return std::unexpected(s.error());
    if (*s == db::sqlite::StepResult::Done) {
        return CharacterStatusRow{.entity_id = entityId};
    }
    CharacterStatusRow r;
    r.id = st->ColumnInt(0);
    r.entity_id = st->ColumnInt(1);
    r.chapter_id = st->ColumnInt(2);
    r.location_id = st->ColumnInt(3);
    r.body_state = st->ColumnText(4);
    r.mind_state = st->ColumnText(5);
    r.emotion_json = st->ColumnText(6);
    r.goal = st->ColumnText(7);
    r.relation_note = st->ColumnText(8);
    r.resource_note = st->ColumnText(9);
    r.secret_note = st->ColumnText(10);
    r.updated = st->ColumnInt(11);
    return r;
}

std::expected<RowId, DbError> NovelGraph::UpsertVolume(const VolumeRow& row) {
    if (row.id > 0) {
        auto st = db_->Prepare("UPDATE volumes SET title=?1,ord=?2,summary=?3 WHERE id=?4");
        if (!st) return std::unexpected(st.error());
        (void)st->BindText(1, row.title);
        (void)st->BindInt(2, row.ord);
        (void)st->BindText(3, row.summary);
        (void)st->BindInt(4, row.id);
        if (auto s = st->Step(); !s) return std::unexpected(s.error());
        return row.id;
    }
    auto st = db_->Prepare("INSERT INTO volumes(title,ord,summary) VALUES(?1,?2,?3)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindText(1, row.title);
    (void)st->BindInt(2, row.ord);
    (void)st->BindText(3, row.summary);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<RowId, DbError> NovelGraph::UpsertChapter(const ChapterRow& row) {
    const auto ts = NowSec();
    if (row.id > 0) {
        auto st = db_->Prepare(
            "UPDATE chapters SET volume_id=?1,ord=?2,title=?3,status=?4,summary=?5,body=?6,"
            "pov_entity_id=?7,words=?8,updated=?9 WHERE id=?10");
        if (!st) return std::unexpected(st.error());
        (void)st->BindInt(1, row.volume_id);
        (void)st->BindInt(2, row.ord);
        (void)st->BindText(3, row.title);
        (void)st->BindText(4, row.status);
        (void)st->BindText(5, row.summary);
        (void)st->BindText(6, row.body);
        (void)st->BindInt(7, row.pov_entity_id);
        (void)st->BindInt(8, row.words);
        (void)st->BindInt(9, ts);
        (void)st->BindInt(10, row.id);
        if (auto s = st->Step(); !s) return std::unexpected(s.error());
        return row.id;
    }
    auto st = db_->Prepare(
        "INSERT INTO chapters(volume_id,ord,title,status,summary,body,pov_entity_id,words,updated)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.volume_id);
    (void)st->BindInt(2, row.ord);
    (void)st->BindText(3, row.title);
    (void)st->BindText(4, row.status.empty() ? "draft" : row.status);
    (void)st->BindText(5, row.summary);
    (void)st->BindText(6, row.body);
    (void)st->BindInt(7, row.pov_entity_id);
    (void)st->BindInt(8, row.words);
    (void)st->BindInt(9, ts);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<ChapterRow, DbError> NovelGraph::GetChapter(RowId id) const {
    auto st = db_->Prepare(
        "SELECT id,volume_id,ord,title,status,summary,body,pov_entity_id,words,updated"
        " FROM chapters WHERE id=?1");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, id);
    auto s = st->Step();
    if (!s) return std::unexpected(s.error());
    if (*s == db::sqlite::StepResult::Done) {
        return std::unexpected(Err(fmt::format("章节不存在: {}", id)));
    }
    ChapterRow c;
    c.id = st->ColumnInt(0);
    c.volume_id = st->ColumnInt(1);
    c.ord = static_cast<int>(st->ColumnInt(2));
    c.title = st->ColumnText(3);
    c.status = st->ColumnText(4);
    c.summary = st->ColumnText(5);
    c.body = st->ColumnText(6);
    c.pov_entity_id = st->ColumnInt(7);
    c.words = static_cast<int>(st->ColumnInt(8));
    c.updated = st->ColumnInt(9);
    return c;
}

std::expected<std::vector<ChapterRow>, DbError> NovelGraph::ListChapters(int limit) const {
    auto st = db_->Prepare(
        "SELECT id,volume_id,ord,title,status,summary,body,pov_entity_id,words,updated"
        " FROM chapters ORDER BY ord,id LIMIT ?1");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, limit > 0 ? limit : 50);
    std::vector<ChapterRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        ChapterRow c;
        c.id = st->ColumnInt(0);
        c.volume_id = st->ColumnInt(1);
        c.ord = static_cast<int>(st->ColumnInt(2));
        c.title = st->ColumnText(3);
        c.status = st->ColumnText(4);
        c.summary = st->ColumnText(5);
        c.body = st->ColumnText(6);
        c.pov_entity_id = st->ColumnInt(7);
        c.words = static_cast<int>(st->ColumnInt(8));
        c.updated = st->ColumnInt(9);
        out.push_back(std::move(c));
    }
    return out;
}

std::expected<RowId, DbError> NovelGraph::UpsertScene(const SceneRow& row) {
    if (row.chapter_id <= 0) return std::unexpected(Err("scene 缺 chapter_id"));
    auto st = db_->Prepare(
        "INSERT INTO scenes(chapter_id,ord,title,location_id,time_label,pov_entity_id,conflict_id,"
        "goal,action,conflict,result,emotion,info_reveal,hook,body)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.chapter_id);
    (void)st->BindInt(2, row.ord);
    (void)st->BindText(3, row.title);
    (void)st->BindInt(4, row.location_id);
    (void)st->BindText(5, row.time_label);
    (void)st->BindInt(6, row.pov_entity_id);
    (void)st->BindInt(7, row.conflict_id);
    (void)st->BindText(8, row.goal);
    (void)st->BindText(9, row.action);
    (void)st->BindText(10, row.conflict);
    (void)st->BindText(11, row.result);
    (void)st->BindText(12, row.emotion);
    (void)st->BindText(13, row.info_reveal);
    (void)st->BindText(14, row.hook);
    (void)st->BindText(15, row.body);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<std::vector<SceneRow>, DbError> NovelGraph::ListScenes(RowId chapterId) const {
    auto st = db_->Prepare(
        "SELECT id,chapter_id,ord,title,location_id,time_label,pov_entity_id,conflict_id,"
        "goal,action,conflict,result,emotion,info_reveal,hook,body"
        " FROM scenes WHERE chapter_id=?1 ORDER BY ord,id");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, chapterId);
    std::vector<SceneRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        SceneRow sc;
        sc.id = st->ColumnInt(0);
        sc.chapter_id = st->ColumnInt(1);
        sc.ord = static_cast<int>(st->ColumnInt(2));
        sc.title = st->ColumnText(3);
        sc.location_id = st->ColumnInt(4);
        sc.time_label = st->ColumnText(5);
        sc.pov_entity_id = st->ColumnInt(6);
        sc.conflict_id = st->ColumnInt(7);
        sc.goal = st->ColumnText(8);
        sc.action = st->ColumnText(9);
        sc.conflict = st->ColumnText(10);
        sc.result = st->ColumnText(11);
        sc.emotion = st->ColumnText(12);
        sc.info_reveal = st->ColumnText(13);
        sc.hook = st->ColumnText(14);
        sc.body = st->ColumnText(15);
        out.push_back(std::move(sc));
    }
    return out;
}

std::expected<RowId, DbError> NovelGraph::UpsertEvent(const EntityRow& entity,
                                                      const EventDetailRow& detail) {
    EntityRow e = entity;
    e.kind = kind::event;
    auto id = UpsertEntity(e);
    if (!id) return id;
    auto st = db_->Prepare(
        "INSERT INTO event_details(entity_id,time_label,location_id,cause_note,result_note)"
        " VALUES(?1,?2,?3,?4,?5)"
        " ON CONFLICT(entity_id) DO UPDATE SET time_label=excluded.time_label,"
        "location_id=excluded.location_id,cause_note=excluded.cause_note,"
        "result_note=excluded.result_note");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, *id);
    (void)st->BindText(2, detail.time_label);
    (void)st->BindInt(3, detail.location_id);
    (void)st->BindText(4, detail.cause_note);
    (void)st->BindText(5, detail.result_note);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return id;
}

std::expected<RowId, DbError> NovelGraph::UpsertCausalLink(const CausalLinkRow& row) {
    if (row.cause_event_id <= 0 || row.effect_event_id <= 0) {
        return std::unexpected(Err("因果边缺 cause/effect"));
    }
    if (row.cause_event_id == row.effect_event_id) {
        return std::unexpected(Err("因果边禁止自环"));
    }
    auto st = db_->Prepare(
        "INSERT INTO causal_links(cause_event_id,effect_event_id,link_type,note,ord)"
        " VALUES(?1,?2,?3,?4,?5)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.cause_event_id);
    (void)st->BindInt(2, row.effect_event_id);
    (void)st->BindText(3, row.link_type.empty() ? "causes" : row.link_type);
    (void)st->BindText(4, row.note);
    (void)st->BindInt(5, row.ord);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<std::vector<CausalLinkRow>, DbError>
NovelGraph::GetEventChain(RowId eventId, int depth) const {
    if (eventId <= 0 || depth <= 0) {
        return std::vector<CausalLinkRow>{};
    }
    std::vector<CausalLinkRow> edges;
    std::unordered_set<RowId> seenNodes{eventId};
    std::queue<std::pair<RowId, int>> q;
    q.emplace(eventId, 0);
    while (!q.empty()) {
        auto [node, d] = q.front();
        q.pop();
        if (d >= depth) continue;
        auto st = db_->Prepare(
            "SELECT id,cause_event_id,effect_event_id,link_type,note,ord FROM causal_links"
            " WHERE cause_event_id=?1 OR effect_event_id=?1");
        if (!st) return std::unexpected(st.error());
        (void)st->BindInt(1, node);
        for (;;) {
            auto s = st->Step();
            if (!s) return std::unexpected(s.error());
            if (*s == db::sqlite::StepResult::Done) break;
            CausalLinkRow e;
            e.id = st->ColumnInt(0);
            e.cause_event_id = st->ColumnInt(1);
            e.effect_event_id = st->ColumnInt(2);
            e.link_type = st->ColumnText(3);
            e.note = st->ColumnText(4);
            e.ord = static_cast<int>(st->ColumnInt(5));
            edges.push_back(e);
            const RowId other = (e.cause_event_id == node) ? e.effect_event_id : e.cause_event_id;
            if (seenNodes.insert(other).second) {
                q.emplace(other, d + 1);
            }
        }
    }
    return edges;
}

std::expected<RowId, DbError> NovelGraph::UpsertForeshadow(const ForeshadowRow& row) {
    if (row.id > 0) {
        auto st = db_->Prepare(
            "UPDATE foreshadowings SET title=?1,content=?2,status=?3,setup_ch=?4,payoff_ch=?5,"
            "importance=?6,truth=?7,entity_ids_json=?8 WHERE id=?9");
        if (!st) return std::unexpected(st.error());
        (void)st->BindText(1, row.title);
        (void)st->BindText(2, row.content);
        (void)st->BindText(3, row.status);
        (void)st->BindInt(4, row.setup_ch);
        (void)st->BindInt(5, row.payoff_ch);
        (void)st->BindInt(6, row.importance);
        (void)st->BindText(7, row.truth);
        (void)st->BindText(8, row.entity_ids_json);
        (void)st->BindInt(9, row.id);
        if (auto s = st->Step(); !s) return std::unexpected(s.error());
        return row.id;
    }
    auto st = db_->Prepare(
        "INSERT INTO foreshadowings(title,content,status,setup_ch,payoff_ch,importance,truth,"
        "entity_ids_json) VALUES(?1,?2,?3,?4,?5,?6,?7,?8)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindText(1, row.title);
    (void)st->BindText(2, row.content);
    (void)st->BindText(3, row.status.empty() ? "PLANNED" : row.status);
    (void)st->BindInt(4, row.setup_ch);
    (void)st->BindInt(5, row.payoff_ch);
    (void)st->BindInt(6, row.importance);
    (void)st->BindText(7, row.truth);
    (void)st->BindText(8, row.entity_ids_json.empty() ? "[]" : row.entity_ids_json);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<std::vector<ForeshadowRow>, DbError> NovelGraph::ListOpenForeshadows() const {
    auto st = db_->Prepare(
        "SELECT id,title,content,status,setup_ch,payoff_ch,importance,truth,entity_ids_json"
        " FROM foreshadowings WHERE status IN ('PLANNED','PLANTED','DEVELOPING')"
        " ORDER BY importance DESC, id");
    if (!st) return std::unexpected(st.error());
    std::vector<ForeshadowRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        ForeshadowRow f;
        f.id = st->ColumnInt(0);
        f.title = st->ColumnText(1);
        f.content = st->ColumnText(2);
        f.status = st->ColumnText(3);
        f.setup_ch = st->ColumnInt(4);
        f.payoff_ch = st->ColumnInt(5);
        f.importance = static_cast<int>(st->ColumnInt(6));
        f.truth = st->ColumnText(7);
        f.entity_ids_json = st->ColumnText(8);
        out.push_back(std::move(f));
    }
    return out;
}

std::expected<RowId, DbError> NovelGraph::UpsertSecret(const SecretRow& row) {
    auto st = db_->Prepare(
        "INSERT INTO secrets(content,truth,reveal_ch,reveal_condition,entity_id,scope)"
        " VALUES(?1,?2,?3,?4,?5,?6)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindText(1, row.content);
    (void)st->BindText(2, row.truth);
    (void)st->BindInt(3, row.reveal_ch);
    (void)st->BindText(4, row.reveal_condition);
    (void)st->BindInt(5, row.entity_id);
    (void)st->BindText(6, row.scope.empty() ? "character" : row.scope);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<void, DbError> NovelGraph::SetSecretKnowledge(RowId secretId, RowId entityId,
                                                            int knows, RowId chapterKnown) {
    auto del = db_->Prepare("DELETE FROM secret_knowledge WHERE secret_id=?1 AND entity_id=?2");
    if (!del) return std::unexpected(del.error());
    (void)del->BindInt(1, secretId);
    (void)del->BindInt(2, entityId);
    if (auto s = del->Step(); !s) return std::unexpected(s.error());

    auto ins = db_->Prepare(
        "INSERT INTO secret_knowledge(secret_id,entity_id,knows,chapter_known) VALUES(?1,?2,?3,?4)");
    if (!ins) return std::unexpected(ins.error());
    (void)ins->BindInt(1, secretId);
    (void)ins->BindInt(2, entityId);
    (void)ins->BindInt(3, knows);
    (void)ins->BindInt(4, chapterKnown);
    if (auto s = ins->Step(); !s) return std::unexpected(s.error());
    return {};
}

std::expected<std::vector<SecretRow>, DbError>
NovelGraph::GetSecretsFor(RowId entityId, RowId chapterId) const {
    // 该角色知道的、且已到揭示章或未强制 reveal 的秘密
    std::string sql =
        "SELECT s.id,s.content,s.truth,s.reveal_ch,s.reveal_condition,s.entity_id,s.scope"
        " FROM secrets s JOIN secret_knowledge k ON k.secret_id=s.id"
        " WHERE k.entity_id=?1 AND k.knows=1";
    if (chapterId > 0) {
        sql += " AND (k.chapter_known=0 OR k.chapter_known<=?2)";
    }
    auto st = db_->Prepare(sql);
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, entityId);
    if (chapterId > 0) (void)st->BindInt(2, chapterId);
    std::vector<SecretRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        SecretRow r;
        r.id = st->ColumnInt(0);
        r.content = st->ColumnText(1);
        r.truth = st->ColumnText(2);
        r.reveal_ch = st->ColumnInt(3);
        r.reveal_condition = st->ColumnText(4);
        r.entity_id = st->ColumnInt(5);
        r.scope = st->ColumnText(6);
        out.push_back(std::move(r));
    }
    return out;
}

std::expected<RowId, DbError> NovelGraph::UpsertOwnership(const OwnershipRow& row) {
    auto st = db_->Prepare(
        "INSERT INTO entity_ownerships(owner_id,item_id,from_chapter,to_chapter,how,note)"
        " VALUES(?1,?2,?3,?4,?5,?6)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.owner_id);
    (void)st->BindInt(2, row.item_id);
    (void)st->BindInt(3, row.from_chapter);
    (void)st->BindInt(4, row.to_chapter);
    (void)st->BindText(5, row.how);
    (void)st->BindText(6, row.note);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<std::vector<OwnershipRow>, DbError> NovelGraph::GetOwnerships(RowId ownerId) const {
    auto st = db_->Prepare(
        "SELECT id,owner_id,item_id,from_chapter,to_chapter,how,note FROM entity_ownerships"
        " WHERE owner_id=?1 ORDER BY from_chapter DESC, id DESC");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, ownerId);
    std::vector<OwnershipRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        OwnershipRow o;
        o.id = st->ColumnInt(0);
        o.owner_id = st->ColumnInt(1);
        o.item_id = st->ColumnInt(2);
        o.from_chapter = st->ColumnInt(3);
        o.to_chapter = st->ColumnInt(4);
        o.how = st->ColumnText(5);
        o.note = st->ColumnText(6);
        out.push_back(std::move(o));
    }
    return out;
}

std::expected<CharacterSlice, DbError>
NovelGraph::GetCharacterSlice(RowId entityId, RowId chapterId) const {
    CharacterSlice slice;
    if (auto e = GetEntity(entityId)) {
        slice.entity = *e;
    } else {
        return std::unexpected(e.error());
    }
    if (auto p = GetPersona(entityId)) {
        slice.persona = *p;
    }
    if (auto s = GetLatestCharacterStatus(entityId, chapterId)) {
        slice.status = *s;
    }
    if (auto r = GetRelations(entityId, true)) {
        slice.relations = *r;
    }
    // knowledge
    {
        auto st = db_->Prepare(
            "SELECT id,entity_id,fact_kind,fact_id,fact_text,knows,chapter_known"
            " FROM character_knowledge WHERE entity_id=?1");
        if (st) {
            (void)st->BindInt(1, entityId);
            for (;;) {
                auto s = st->Step();
                if (!s || *s == db::sqlite::StepResult::Done) break;
                CharacterKnowledgeRow k;
                k.id = st->ColumnInt(0);
                k.entity_id = st->ColumnInt(1);
                k.fact_kind = st->ColumnText(2);
                k.fact_id = st->ColumnInt(3);
                k.fact_text = st->ColumnText(4);
                k.knows = static_cast<int>(st->ColumnInt(5));
                k.chapter_known = st->ColumnInt(6);
                slice.knowledge.push_back(std::move(k));
            }
        }
    }
    if (auto f = ListOpenForeshadows()) {
        slice.openForeshadows = *f;
    }
    return slice;
}

std::expected<WorldSlice, DbError> NovelGraph::GetWorldSlice() const {
    WorldSlice w;
    if (auto r = ListEntities(kind::world_rule, {}, 50)) w.rules = *r;
    else return std::unexpected(r.error());
    if (auto r = ListEntities(kind::location, {}, 80)) w.locations = *r;
    if (auto r = ListEntities(kind::faction, {}, 50)) w.factions = *r;
    if (auto r = ListEntities(kind::power_system, {}, 20)) w.powerSystems = *r;
    return w;
}

std::expected<void, DbError> NovelGraph::LogAudit(std::string_view actor, std::string_view action,
                                                  std::string_view targetKind, RowId targetId,
                                                  std::string_view detail) {
    auto st = db_->Prepare(
        "INSERT INTO audit_logs(actor,action,target_kind,target_id,detail,created)"
        " VALUES(?1,?2,?3,?4,?5,?6)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindText(1, actor);
    (void)st->BindText(2, action);
    (void)st->BindText(3, targetKind);
    (void)st->BindInt(4, targetId);
    (void)st->BindText(5, detail);
    (void)st->BindInt(6, NowSec());
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return {};
}

std::expected<void, DbError> NovelGraph::SetCanon(std::string_view targetKind, RowId targetId,
                                                  std::string_view status, std::string_view note) {
    auto st = db_->Prepare(
        "INSERT INTO canon_logs(target_kind,target_id,status,note,created) VALUES(?1,?2,?3,?4,?5)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindText(1, targetKind);
    (void)st->BindInt(2, targetId);
    (void)st->BindText(3, status);
    (void)st->BindText(4, note);
    (void)st->BindInt(5, NowSec());
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return LogAudit("user", "promote_canon", targetKind, targetId, status);
}

// —— P0 八表（S2）：原先只有表、没有 API，是闭环的阻断点 ——

// 知情（character_knowledge）
std::expected<RowId, DbError> NovelGraph::UpsertKnowledge(const CharacterKnowledgeRow& row) {
    if (row.entity_id <= 0) return std::unexpected(Err("知情记录必须指定 entity_id"));
    if (row.id > 0) {
        auto st = db_->Prepare(
            "UPDATE character_knowledge SET entity_id=?1,fact_kind=?2,fact_id=?3,fact_text=?4,"
            "knows=?5,chapter_known=?6 WHERE id=?7");
        if (!st) return std::unexpected(st.error());
        (void)st->BindInt(1, row.entity_id);
        (void)st->BindText(2, row.fact_kind);
        (void)st->BindInt(3, row.fact_id);
        (void)st->BindText(4, row.fact_text);
        (void)st->BindInt(5, row.knows);
        (void)st->BindInt(6, row.chapter_known);
        (void)st->BindInt(7, row.id);
        if (auto s = st->Step(); !s) return std::unexpected(s.error());
        return row.id;
    }
    auto st = db_->Prepare(
        "INSERT INTO character_knowledge(entity_id,fact_kind,fact_id,fact_text,knows,chapter_known)"
        " VALUES(?1,?2,?3,?4,?5,?6)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.entity_id);
    (void)st->BindText(2, row.fact_kind);
    (void)st->BindInt(3, row.fact_id);
    (void)st->BindText(4, row.fact_text);
    (void)st->BindInt(5, row.knows);
    (void)st->BindInt(6, row.chapter_known);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<std::vector<CharacterKnowledgeRow>, DbError>
NovelGraph::ListKnowledge(RowId entityId, RowId chapterId) const {
    std::string sql =
        "SELECT id,entity_id,fact_kind,fact_id,fact_text,knows,chapter_known"
        " FROM character_knowledge WHERE entity_id=?1";
    if (chapterId > 0) sql += " AND (chapter_known=0 OR chapter_known<=?2)";
    sql += " ORDER BY chapter_known, id";
    auto st = db_->Prepare(sql);
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, entityId);
    if (chapterId > 0) (void)st->BindInt(2, chapterId);
    std::vector<CharacterKnowledgeRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        CharacterKnowledgeRow r;
        r.id = st->ColumnInt(0);
        r.entity_id = st->ColumnInt(1);
        r.fact_kind = st->ColumnText(2);
        r.fact_id = st->ColumnInt(3);
        r.fact_text = st->ColumnText(4);
        r.knows = static_cast<int>(st->ColumnInt(5));
        r.chapter_known = st->ColumnInt(6);
        out.push_back(std::move(r));
    }
    return out;
}

std::expected<bool, DbError> NovelGraph::CharacterKnows(RowId entityId, std::string_view factKind,
                                                        RowId factId, RowId chapterId) const {
    // 01 §2.3.2：knows=1 且 (chapter_known=0 或 chapter_known<=N)
    std::string sql =
        "SELECT COUNT(*) FROM character_knowledge"
        " WHERE entity_id=?1 AND fact_kind=?2 AND fact_id=?3 AND knows=1";
    if (chapterId > 0) sql += " AND (chapter_known=0 OR chapter_known<=?4)";
    auto st = db_->Prepare(sql);
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, entityId);
    (void)st->BindText(2, factKind);
    (void)st->BindInt(3, factId);
    if (chapterId > 0) (void)st->BindInt(4, chapterId);
    auto s = st->Step();
    if (!s) return std::unexpected(s.error());
    if (*s != db::sqlite::StepResult::Row) return false;
    return st->ColumnInt(0) > 0;
}

// 事件参与（event_participants）
std::expected<RowId, DbError> NovelGraph::UpsertEventParticipant(const EventParticipantRow& row) {
    if (row.event_id <= 0 || row.entity_id <= 0) {
        return std::unexpected(Err("事件参与必须指定 event_id 与 entity_id"));
    }
    if (row.id > 0) {
        auto st = db_->Prepare(
            "UPDATE event_participants SET event_id=?1,entity_id=?2,role=?3 WHERE id=?4");
        if (!st) return std::unexpected(st.error());
        (void)st->BindInt(1, row.event_id);
        (void)st->BindInt(2, row.entity_id);
        (void)st->BindText(3, row.role);
        (void)st->BindInt(4, row.id);
        if (auto s = st->Step(); !s) return std::unexpected(s.error());
        return row.id;
    }
    auto st =
        db_->Prepare("INSERT INTO event_participants(event_id,entity_id,role) VALUES(?1,?2,?3)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.event_id);
    (void)st->BindInt(2, row.entity_id);
    (void)st->BindText(3, row.role);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<std::vector<EventParticipantRow>, DbError>
NovelGraph::ListEventParticipants(RowId eventId) const {
    auto st = db_->Prepare(
        "SELECT id,event_id,entity_id,role FROM event_participants WHERE event_id=?1 ORDER BY id");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, eventId);
    std::vector<EventParticipantRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        EventParticipantRow r;
        r.id = st->ColumnInt(0);
        r.event_id = st->ColumnInt(1);
        r.entity_id = st->ColumnInt(2);
        r.role = st->ColumnText(3);
        out.push_back(std::move(r));
    }
    return out;
}

std::expected<std::vector<EventParticipantRow>, DbError>
NovelGraph::ListEntityParticipations(RowId entityId, std::string_view role, int limit) const {
    std::string sql =
        "SELECT id,event_id,entity_id,role FROM event_participants WHERE entity_id=?1";
    if (!role.empty()) sql += " AND role=?2";
    sql += " ORDER BY event_id, id LIMIT " + std::to_string(limit > 0 ? limit : 200);
    auto st = db_->Prepare(sql);
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, entityId);
    if (!role.empty()) (void)st->BindText(2, role);
    std::vector<EventParticipantRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        EventParticipantRow r;
        r.id = st->ColumnInt(0);
        r.event_id = st->ColumnInt(1);
        r.entity_id = st->ColumnInt(2);
        r.role = st->ColumnText(3);
        out.push_back(std::move(r));
    }
    return out;
}

// 场次在场 / 场次伏笔
std::expected<RowId, DbError> NovelGraph::UpsertSceneCast(const SceneCastRow& row) {
    if (row.scene_id <= 0 || row.entity_id <= 0) {
        return std::unexpected(Err("场次在场必须指定 scene_id 与 entity_id"));
    }
    if (row.id > 0) {
        auto st = db_->Prepare("UPDATE scene_cast SET scene_id=?1,entity_id=?2,role=?3 WHERE id=?4");
        if (!st) return std::unexpected(st.error());
        (void)st->BindInt(1, row.scene_id);
        (void)st->BindInt(2, row.entity_id);
        (void)st->BindText(3, row.role);
        (void)st->BindInt(4, row.id);
        if (auto s = st->Step(); !s) return std::unexpected(s.error());
        return row.id;
    }
    auto st = db_->Prepare("INSERT INTO scene_cast(scene_id,entity_id,role) VALUES(?1,?2,?3)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.scene_id);
    (void)st->BindInt(2, row.entity_id);
    (void)st->BindText(3, row.role);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<std::vector<SceneCastRow>, DbError> NovelGraph::ListSceneCast(RowId sceneId) const {
    auto st = db_->Prepare(
        "SELECT id,scene_id,entity_id,role FROM scene_cast WHERE scene_id=?1 ORDER BY id");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, sceneId);
    std::vector<SceneCastRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        SceneCastRow r;
        r.id = st->ColumnInt(0);
        r.scene_id = st->ColumnInt(1);
        r.entity_id = st->ColumnInt(2);
        r.role = st->ColumnText(3);
        out.push_back(std::move(r));
    }
    return out;
}

std::expected<RowId, DbError> NovelGraph::UpsertSceneForeshadow(const SceneForeshadowRow& row) {
    if (row.scene_id <= 0 || row.foreshadowing_id <= 0) {
        return std::unexpected(Err("场次伏笔必须指定 scene_id 与 foreshadowing_id"));
    }
    if (row.id > 0) {
        auto st = db_->Prepare(
            "UPDATE scene_foreshadows SET scene_id=?1,foreshadowing_id=?2,action=?3 WHERE id=?4");
        if (!st) return std::unexpected(st.error());
        (void)st->BindInt(1, row.scene_id);
        (void)st->BindInt(2, row.foreshadowing_id);
        (void)st->BindText(3, row.action);
        (void)st->BindInt(4, row.id);
        if (auto s = st->Step(); !s) return std::unexpected(s.error());
        return row.id;
    }
    auto st = db_->Prepare(
        "INSERT INTO scene_foreshadows(scene_id,foreshadowing_id,action) VALUES(?1,?2,?3)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.scene_id);
    (void)st->BindInt(2, row.foreshadowing_id);
    (void)st->BindText(3, row.action);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<std::vector<SceneForeshadowRow>, DbError>
NovelGraph::ListSceneForeshadows(RowId sceneId) const {
    auto st = db_->Prepare(
        "SELECT id,scene_id,foreshadowing_id,action FROM scene_foreshadows"
        " WHERE scene_id=?1 ORDER BY id");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, sceneId);
    std::vector<SceneForeshadowRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        SceneForeshadowRow r;
        r.id = st->ColumnInt(0);
        r.scene_id = st->ColumnInt(1);
        r.foreshadowing_id = st->ColumnInt(2);
        r.action = st->ColumnText(3);
        out.push_back(std::move(r));
    }
    return out;
}

// 剧情线（plots / plot_beats）
std::expected<RowId, DbError> NovelGraph::UpsertPlot(const PlotRow& row) {
    if (row.title.empty()) return std::unexpected(Err("剧情线 title 不能为空"));
    const std::string_view kind = row.kind.empty() ? std::string_view{"main"} : row.kind;
    const std::string_view status = row.status.empty() ? std::string_view{"active"} : row.status;
    if (row.id > 0) {
        auto st = db_->Prepare(
            "UPDATE plots SET kind=?1,title=?2,status=?3,intro_ch=?4,target_ch=?5,note=?6"
            " WHERE id=?7");
        if (!st) return std::unexpected(st.error());
        (void)st->BindText(1, kind);
        (void)st->BindText(2, row.title);
        (void)st->BindText(3, status);
        (void)st->BindInt(4, row.intro_ch);
        (void)st->BindInt(5, row.target_ch);
        (void)st->BindText(6, row.note);
        (void)st->BindInt(7, row.id);
        if (auto s = st->Step(); !s) return std::unexpected(s.error());
        return row.id;
    }
    auto st = db_->Prepare(
        "INSERT INTO plots(kind,title,status,intro_ch,target_ch,note) VALUES(?1,?2,?3,?4,?5,?6)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindText(1, kind);
    (void)st->BindText(2, row.title);
    (void)st->BindText(3, status);
    (void)st->BindInt(4, row.intro_ch);
    (void)st->BindInt(5, row.target_ch);
    (void)st->BindText(6, row.note);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<PlotRow, DbError> NovelGraph::GetPlot(RowId id) const {
    auto st = db_->Prepare(
        "SELECT id,kind,title,status,intro_ch,target_ch,note FROM plots WHERE id=?1");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, id);
    auto s = st->Step();
    if (!s) return std::unexpected(s.error());
    if (*s == db::sqlite::StepResult::Done) return std::unexpected(Err("剧情线不存在"));
    PlotRow r;
    r.id = st->ColumnInt(0);
    r.kind = st->ColumnText(1);
    r.title = st->ColumnText(2);
    r.status = st->ColumnText(3);
    r.intro_ch = st->ColumnInt(4);
    r.target_ch = st->ColumnInt(5);
    r.note = st->ColumnText(6);
    return r;
}

std::expected<std::vector<PlotRow>, DbError> NovelGraph::ListPlots(std::string_view kind,
                                                                   int limit) const {
    std::string sql = "SELECT id,kind,title,status,intro_ch,target_ch,note FROM plots";
    if (!kind.empty()) sql += " WHERE kind=?1";
    sql += " ORDER BY intro_ch, id LIMIT " + std::to_string(limit > 0 ? limit : 100);
    auto st = db_->Prepare(sql);
    if (!st) return std::unexpected(st.error());
    if (!kind.empty()) (void)st->BindText(1, kind);
    std::vector<PlotRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        PlotRow r;
        r.id = st->ColumnInt(0);
        r.kind = st->ColumnText(1);
        r.title = st->ColumnText(2);
        r.status = st->ColumnText(3);
        r.intro_ch = st->ColumnInt(4);
        r.target_ch = st->ColumnInt(5);
        r.note = st->ColumnText(6);
        out.push_back(std::move(r));
    }
    return out;
}

std::expected<RowId, DbError> NovelGraph::UpsertPlotBeat(const PlotBeatRow& row) {
    if (row.plot_id <= 0) return std::unexpected(Err("剧情节拍必须指定 plot_id"));
    const std::string_view type =
        row.beat_type.empty() ? std::string_view{"setup"} : row.beat_type;
    const std::string_view cast = row.cast_json.empty() ? std::string_view{"[]"} : row.cast_json;
    if (row.id > 0) {
        auto st = db_->Prepare(
            "UPDATE plot_beats SET plot_id=?1,chapter_id=?2,ord=?3,beat_type=?4,title=?5,"
            "summary=?6,cast_json=?7 WHERE id=?8");
        if (!st) return std::unexpected(st.error());
        (void)st->BindInt(1, row.plot_id);
        (void)st->BindInt(2, row.chapter_id);
        (void)st->BindInt(3, row.ord);
        (void)st->BindText(4, type);
        (void)st->BindText(5, row.title);
        (void)st->BindText(6, row.summary);
        (void)st->BindText(7, cast);
        (void)st->BindInt(8, row.id);
        if (auto s = st->Step(); !s) return std::unexpected(s.error());
        return row.id;
    }
    auto st = db_->Prepare(
        "INSERT INTO plot_beats(plot_id,chapter_id,ord,beat_type,title,summary,cast_json)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.plot_id);
    (void)st->BindInt(2, row.chapter_id);
    (void)st->BindInt(3, row.ord);
    (void)st->BindText(4, type);
    (void)st->BindText(5, row.title);
    (void)st->BindText(6, row.summary);
    (void)st->BindText(7, cast);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<std::vector<PlotBeatRow>, DbError> NovelGraph::ListPlotBeats(RowId plotId) const {
    auto st = db_->Prepare(
        "SELECT id,plot_id,chapter_id,ord,beat_type,title,summary,cast_json FROM plot_beats"
        " WHERE plot_id=?1 ORDER BY ord, id");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, plotId);
    std::vector<PlotBeatRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        PlotBeatRow r;
        r.id = st->ColumnInt(0);
        r.plot_id = st->ColumnInt(1);
        r.chapter_id = st->ColumnInt(2);
        r.ord = static_cast<int>(st->ColumnInt(3));
        r.beat_type = st->ColumnText(4);
        r.title = st->ColumnText(5);
        r.summary = st->ColumnText(6);
        r.cast_json = st->ColumnText(7);
        out.push_back(std::move(r));
    }
    return out;
}

// 谜团（mysteries / mystery_beats）
std::expected<RowId, DbError> NovelGraph::UpsertMystery(const MysteryRow& row) {
    if (row.question.empty()) return std::unexpected(Err("谜团 question 不能为空"));
    const std::string_view status = row.status.empty() ? std::string_view{"open"} : row.status;
    if (row.id > 0) {
        auto st = db_->Prepare(
            "UPDATE mysteries SET entity_id=?1,question=?2,answer=?3,status=?4,ask_ch=?5,"
            "answer_ch=?6,importance=?7 WHERE id=?8");
        if (!st) return std::unexpected(st.error());
        (void)st->BindInt(1, row.entity_id);
        (void)st->BindText(2, row.question);
        (void)st->BindText(3, row.answer);
        (void)st->BindText(4, status);
        (void)st->BindInt(5, row.ask_ch);
        (void)st->BindInt(6, row.answer_ch);
        (void)st->BindInt(7, row.importance);
        (void)st->BindInt(8, row.id);
        if (auto s = st->Step(); !s) return std::unexpected(s.error());
        return row.id;
    }
    auto st = db_->Prepare(
        "INSERT INTO mysteries(entity_id,question,answer,status,ask_ch,answer_ch,importance)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.entity_id);
    (void)st->BindText(2, row.question);
    (void)st->BindText(3, row.answer);
    (void)st->BindText(4, status);
    (void)st->BindInt(5, row.ask_ch);
    (void)st->BindInt(6, row.answer_ch);
    (void)st->BindInt(7, row.importance);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<MysteryRow, DbError> NovelGraph::GetMystery(RowId id) const {
    auto st = db_->Prepare(
        "SELECT id,entity_id,question,answer,status,ask_ch,answer_ch,importance"
        " FROM mysteries WHERE id=?1");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, id);
    auto s = st->Step();
    if (!s) return std::unexpected(s.error());
    if (*s == db::sqlite::StepResult::Done) return std::unexpected(Err("谜团不存在"));
    MysteryRow r;
    r.id = st->ColumnInt(0);
    r.entity_id = st->ColumnInt(1);
    r.question = st->ColumnText(2);
    r.answer = st->ColumnText(3);
    r.status = st->ColumnText(4);
    r.ask_ch = st->ColumnInt(5);
    r.answer_ch = st->ColumnInt(6);
    r.importance = static_cast<int>(st->ColumnInt(7));
    return r;
}

std::expected<std::vector<MysteryRow>, DbError> NovelGraph::ListOpenMysteries() const {
    // 「未解」= open | hinted —— 与 ListOpenForeshadows 一起构成 00 §2.5 的「开放线索」
    auto st = db_->Prepare(
        "SELECT id,entity_id,question,answer,status,ask_ch,answer_ch,importance FROM mysteries"
        " WHERE status IN ('open','hinted') ORDER BY importance DESC, id");
    if (!st) return std::unexpected(st.error());
    std::vector<MysteryRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        MysteryRow r;
        r.id = st->ColumnInt(0);
        r.entity_id = st->ColumnInt(1);
        r.question = st->ColumnText(2);
        r.answer = st->ColumnText(3);
        r.status = st->ColumnText(4);
        r.ask_ch = st->ColumnInt(5);
        r.answer_ch = st->ColumnInt(6);
        r.importance = static_cast<int>(st->ColumnInt(7));
        out.push_back(std::move(r));
    }
    return out;
}

std::expected<std::vector<MysteryRow>, DbError> NovelGraph::ListMysteries(RowId entityId,
                                                                          int limit) const {
    std::string sql =
        "SELECT id,entity_id,question,answer,status,ask_ch,answer_ch,importance FROM mysteries";
    if (entityId > 0) sql += " WHERE entity_id=?1";
    sql += " ORDER BY importance DESC, id LIMIT " + std::to_string(limit > 0 ? limit : 100);
    auto st = db_->Prepare(sql);
    if (!st) return std::unexpected(st.error());
    if (entityId > 0) (void)st->BindInt(1, entityId);
    std::vector<MysteryRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        MysteryRow r;
        r.id = st->ColumnInt(0);
        r.entity_id = st->ColumnInt(1);
        r.question = st->ColumnText(2);
        r.answer = st->ColumnText(3);
        r.status = st->ColumnText(4);
        r.ask_ch = st->ColumnInt(5);
        r.answer_ch = st->ColumnInt(6);
        r.importance = static_cast<int>(st->ColumnInt(7));
        out.push_back(std::move(r));
    }
    return out;
}

std::expected<RowId, DbError> NovelGraph::UpsertMysteryBeat(const MysteryBeatRow& row) {
    if (row.mystery_id <= 0) return std::unexpected(Err("谜团节拍必须指定 mystery_id"));
    const std::string_view type =
        row.beat_type.empty() ? std::string_view{"hint"} : row.beat_type;
    if (row.id > 0) {
        auto st = db_->Prepare(
            "UPDATE mystery_beats SET mystery_id=?1,beat_type=?2,chapter_id=?3,content=?4,"
            "target_entity_id=?5,ord=?6 WHERE id=?7");
        if (!st) return std::unexpected(st.error());
        (void)st->BindInt(1, row.mystery_id);
        (void)st->BindText(2, type);
        (void)st->BindInt(3, row.chapter_id);
        (void)st->BindText(4, row.content);
        (void)st->BindInt(5, row.target_entity_id);
        (void)st->BindInt(6, row.ord);
        (void)st->BindInt(7, row.id);
        if (auto s = st->Step(); !s) return std::unexpected(s.error());
        return row.id;
    }
    auto st = db_->Prepare(
        "INSERT INTO mystery_beats(mystery_id,beat_type,chapter_id,content,target_entity_id,ord)"
        " VALUES(?1,?2,?3,?4,?5,?6)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.mystery_id);
    (void)st->BindText(2, type);
    (void)st->BindInt(3, row.chapter_id);
    (void)st->BindText(4, row.content);
    (void)st->BindInt(5, row.target_entity_id);
    (void)st->BindInt(6, row.ord);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<std::vector<MysteryBeatRow>, DbError>
NovelGraph::ListMysteryBeats(RowId mysteryId) const {
    auto st = db_->Prepare(
        "SELECT id,mystery_id,beat_type,chapter_id,content,target_entity_id,ord FROM mystery_beats"
        " WHERE mystery_id=?1 ORDER BY ord, id");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, mysteryId);
    std::vector<MysteryBeatRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        MysteryBeatRow r;
        r.id = st->ColumnInt(0);
        r.mystery_id = st->ColumnInt(1);
        r.beat_type = st->ColumnText(2);
        r.chapter_id = st->ColumnInt(3);
        r.content = st->ColumnText(4);
        r.target_entity_id = st->ColumnInt(5);
        r.ord = static_cast<int>(st->ColumnInt(6));
        out.push_back(std::move(r));
    }
    return out;
}

// —— 初始化链（S3-pre）：`10` §2.3 门禁的硬依赖，原先这四张表零写入口 ——

// 角色弧光（character_arcs）——「不同时间段不同性格」的正规落点
std::expected<RowId, DbError> NovelGraph::UpsertArc(const CharacterArcRow& row) {
    if (row.entity_id <= 0) return std::unexpected(Err("弧光必须指定 entity_id"));
    if (row.stage.empty()) return std::unexpected(Err("弧光 stage 不能为空"));
    if (row.id > 0) {
        auto st = db_->Prepare(
            "UPDATE character_arcs SET entity_id=?1,ord=?2,stage=?3,trigger_event_id=?4,note=?5"
            " WHERE id=?6");
        if (!st) return std::unexpected(st.error());
        (void)st->BindInt(1, row.entity_id);
        (void)st->BindInt(2, row.ord);
        (void)st->BindText(3, row.stage);
        (void)st->BindInt(4, row.trigger_event_id);
        (void)st->BindText(5, row.note);
        (void)st->BindInt(6, row.id);
        if (auto s = st->Step(); !s) return std::unexpected(s.error());
        return row.id;
    }
    auto st = db_->Prepare(
        "INSERT INTO character_arcs(entity_id,ord,stage,trigger_event_id,note)"
        " VALUES(?1,?2,?3,?4,?5)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.entity_id);
    (void)st->BindInt(2, row.ord);
    (void)st->BindText(3, row.stage);
    (void)st->BindInt(4, row.trigger_event_id);
    (void)st->BindText(5, row.note);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<std::vector<CharacterArcRow>, DbError> NovelGraph::ListArcs(RowId entityId) const {
    std::string sql = "SELECT id,entity_id,ord,stage,trigger_event_id,note FROM character_arcs";
    if (entityId > 0) sql += " WHERE entity_id=?1";
    sql += " ORDER BY ord, id";
    auto st = db_->Prepare(sql);
    if (!st) return std::unexpected(st.error());
    if (entityId > 0) (void)st->BindInt(1, entityId);
    std::vector<CharacterArcRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        CharacterArcRow r;
        r.id = st->ColumnInt(0);
        r.entity_id = st->ColumnInt(1);
        r.ord = static_cast<int>(st->ColumnInt(2));
        r.stage = st->ColumnText(3);
        r.trigger_event_id = st->ColumnInt(4);
        r.note = st->ColumnText(5);
        out.push_back(std::move(r));
    }
    return out;
}

// 角色级说话方式（dialogue_styles，一人一行；主键就是 entity_id）
std::expected<RowId, DbError> NovelGraph::UpsertDialogueStyle(const DialogueStyleRow& row) {
    if (row.entity_id <= 0) return std::unexpected(Err("说话方式必须指定 entity_id"));
    (void)db_->Exec(fmt::format("DELETE FROM dialogue_styles WHERE entity_id={}", row.entity_id));
    auto st = db_->Prepare(
        "INSERT INTO dialogue_styles(entity_id,sentence_len,vocabulary,catchphrase,taboo_words,"
        "habit) VALUES(?1,?2,?3,?4,?5,?6)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.entity_id);
    (void)st->BindText(2, row.sentence_len);
    (void)st->BindText(3, row.vocabulary);
    (void)st->BindText(4, row.catchphrase);
    (void)st->BindText(5, row.taboo_words);
    (void)st->BindText(6, row.habit);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return row.entity_id;
}

std::expected<DialogueStyleRow, DbError> NovelGraph::GetDialogueStyle(RowId entityId) const {
    auto st = db_->Prepare(
        "SELECT entity_id,sentence_len,vocabulary,catchphrase,taboo_words,habit"
        " FROM dialogue_styles WHERE entity_id=?1");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, entityId);
    auto s = st->Step();
    if (!s) return std::unexpected(s.error());
    if (*s == db::sqlite::StepResult::Done) {
        return std::unexpected(Err("该角色没有说话方式记录"));
    }
    DialogueStyleRow r;
    r.entity_id = st->ColumnInt(0);
    r.sentence_len = st->ColumnText(1);
    r.vocabulary = st->ColumnText(2);
    r.catchphrase = st->ColumnText(3);
    r.taboo_words = st->ColumnText(4);
    r.habit = st->ColumnText(5);
    return r;
}

// 世界级键值（world_meta）——`10` §2.3 N1 的 `book_title` 走这里
std::expected<void, DbError> NovelGraph::SetWorldMeta(std::string_view key,
                                                     std::string_view value) {
    if (key.empty()) return std::unexpected(Err("world_meta key 不能为空"));
    auto st = db_->Prepare(
        "INSERT INTO world_meta(key,value) VALUES(?1,?2)"
        " ON CONFLICT(key) DO UPDATE SET value=excluded.value");
    if (!st) return std::unexpected(st.error());
    (void)st->BindText(1, key);
    (void)st->BindText(2, value);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return {};
}

std::expected<std::string, DbError> NovelGraph::GetWorldMeta(std::string_view key) const {
    auto st = db_->Prepare("SELECT value FROM world_meta WHERE key=?1");
    if (!st) return std::unexpected(st.error());
    (void)st->BindText(1, key);
    auto s = st->Step();
    if (!s) return std::unexpected(s.error());
    if (*s == db::sqlite::StepResult::Done) return std::unexpected(Err("world_meta 键不存在"));
    return st->ColumnText(0);
}

// 主题（themes）
std::expected<RowId, DbError> NovelGraph::UpsertTheme(const ThemeRow& row) {
    if (row.title.empty()) return std::unexpected(Err("主题 title 不能为空"));
    if (row.id > 0) {
        auto st =
            db_->Prepare("UPDATE themes SET title=?1,statement=?2,linked_plot_id=?3 WHERE id=?4");
        if (!st) return std::unexpected(st.error());
        (void)st->BindText(1, row.title);
        (void)st->BindText(2, row.statement);
        (void)st->BindInt(3, row.linked_plot_id);
        (void)st->BindInt(4, row.id);
        if (auto s = st->Step(); !s) return std::unexpected(s.error());
        return row.id;
    }
    auto st = db_->Prepare("INSERT INTO themes(title,statement,linked_plot_id) VALUES(?1,?2,?3)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindText(1, row.title);
    (void)st->BindText(2, row.statement);
    (void)st->BindInt(3, row.linked_plot_id);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<std::vector<ThemeRow>, DbError> NovelGraph::ListThemes() const {
    auto st = db_->Prepare("SELECT id,title,statement,linked_plot_id FROM themes ORDER BY id");
    if (!st) return std::unexpected(st.error());
    std::vector<ThemeRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        ThemeRow r;
        r.id = st->ColumnInt(0);
        r.title = st->ColumnText(1);
        r.statement = st->ColumnText(2);
        r.linked_plot_id = st->ColumnInt(3);
        out.push_back(std::move(r));
    }
    return out;
}

// ———— P1 余下五表（S7）：`entity_versions` / `location_distances` / `writing_style` / `author_rules` ————

std::expected<RowId, DbError> NovelGraph::SaveEntityVersion(const EntityVersionRow& row) {
    if (row.entity_id <= 0) {
        return std::unexpected(Err("快照必须指定 entity_id"));
    }
    if (row.snapshot_json.empty() || row.snapshot_json == "{}") {
        return std::unexpected(Err("快照内容为空：先 SnapshotEntity，或自己填 snapshot_json"));
    }
    int ver = row.ver;
    if (ver <= 0) {
        auto q = db_->Prepare("SELECT COALESCE(MAX(ver),0)+1 FROM entity_versions WHERE entity_id=?1");
        if (!q) return std::unexpected(q.error());
        (void)q->BindInt(1, row.entity_id);
        if (auto s = q->Step(); s && *s == db::sqlite::StepResult::Row) {
            ver = static_cast<int>(q->ColumnInt(0));
        }
        if (ver <= 0) ver = 1;
    }
    auto st = db_->Prepare(
        "INSERT INTO entity_versions(entity_id,ver,snapshot_json,note,created) VALUES(?1,?2,?3,?4,?5)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.entity_id);
    (void)st->BindInt(2, ver);
    (void)st->BindText(3, row.snapshot_json);
    (void)st->BindText(4, row.note);
    (void)st->BindInt(5, NowSec());
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    const RowId id = db_->LastInsertRowId();
    (void)LogAudit("agent", "snapshot_entity", "entity", row.entity_id,
                   fmt::format("ver={} version_id={}", ver, id));
    return id;
}

std::expected<EntityVersionRow, DbError> NovelGraph::GetEntityVersion(RowId id) const {
    auto st = db_->Prepare(
        "SELECT id,entity_id,ver,snapshot_json,note,created FROM entity_versions WHERE id=?1");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, id);
    if (auto s = st->Step(); !s) {
        return std::unexpected(s.error());
    } else if (*s != db::sqlite::StepResult::Row) {
        return std::unexpected(Err(fmt::format("快照 #{} 不存在", id)));
    }
    EntityVersionRow r;
    r.id = st->ColumnInt(0);
    r.entity_id = st->ColumnInt(1);
    r.ver = static_cast<int>(st->ColumnInt(2));
    r.snapshot_json = st->ColumnText(3);
    r.note = st->ColumnText(4);
    r.created = st->ColumnInt(5);
    return r;
}

std::expected<std::vector<EntityVersionRow>, DbError>
NovelGraph::ListEntityVersions(RowId entityId, int limit) const {
    if (entityId <= 0) {
        return std::unexpected(Err("列出快照必须指定 entity_id"));
    }
    if (limit <= 0) limit = 20;
    auto st = db_->Prepare(fmt::format(
        "SELECT id,entity_id,ver,snapshot_json,note,created FROM entity_versions "
        "WHERE entity_id=?1 ORDER BY ver DESC, id DESC LIMIT {}", limit));
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, entityId);
    std::vector<EntityVersionRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        EntityVersionRow r;
        r.id = st->ColumnInt(0);
        r.entity_id = st->ColumnInt(1);
        r.ver = static_cast<int>(st->ColumnInt(2));
        r.snapshot_json = st->ColumnText(3);
        r.note = st->ColumnText(4);
        r.created = st->ColumnInt(5);
        out.push_back(std::move(r));
    }
    return out;
}

std::expected<EntityVersionRow, DbError> NovelGraph::GetLatestEntityVersion(RowId entityId) const {
    auto rows = ListEntityVersions(entityId, 1);
    if (!rows) return std::unexpected(rows.error());
    if (rows->empty()) {
        return std::unexpected(Err(fmt::format("实体 #{} 还没有快照", entityId)));
    }
    return rows->front();
}

std::expected<RowId, DbError> NovelGraph::SnapshotEntity(RowId entityId, std::string_view note) {
    auto entity = GetEntity(entityId);
    if (!entity) return std::unexpected(entity.error());

    EntitySnapshot snap;
    snap.entity = *entity;
    // persona / status 允许缺失（新实体可能还没有）—— 缺就留默认值，不让快照失败
    if (auto persona = GetPersona(entityId); persona) {
        snap.persona = *persona;
    }
    if (auto status = GetLatestCharacterStatus(entityId, 0); status) {
        snap.status = *status;
    }

    EntityVersionRow row;
    row.entity_id = entityId;
    row.snapshot_json = util::reflect::ToJsonString(snap);
    row.note = std::string{note};
    return SaveEntityVersion(row); // 返回的就是快照行 id
}

std::expected<EntitySnapshot, DbError> NovelGraph::LoadEntitySnapshot(RowId versionId) const {
    auto row = GetEntityVersion(versionId);
    if (!row) return std::unexpected(row.error());
    EntitySnapshot snap;
    const std::size_t hit = util::reflect::FromJsonString(row->snapshot_json, snap);
    if (hit == 0) {
        return std::unexpected(Err(fmt::format("快照 #{} 的 JSON 解析不出任何字段", versionId)));
    }
    return snap;
}

std::expected<RowId, DbError> NovelGraph::UpsertLocationDistance(const LocationDistanceRow& row) {
    if (row.from_id <= 0 || row.to_id <= 0) {
        return std::unexpected(Err("地点距离必须给出 from_id 与 to_id"));
    }
    if (row.from_id == row.to_id) {
        return std::unexpected(Err("地点距离的两头不能是同一个地点"));
    }
    RowId target = row.id;
    if (target <= 0) {
        // 「一对地点一行」：同一对（无向）已有就更新，避免 K18 查到两行不确定
        auto q = db_->Prepare(
            "SELECT id FROM location_distances WHERE (from_id=?1 AND to_id=?2) OR (from_id=?2 AND to_id=?1) "
            "ORDER BY id LIMIT 1");
        if (!q) return std::unexpected(q.error());
        (void)q->BindInt(1, row.from_id);
        (void)q->BindInt(2, row.to_id);
        if (auto s = q->Step(); s && *s == db::sqlite::StepResult::Row) {
            target = q->ColumnInt(0);
        }
    }
    if (target > 0) {
        auto st = db_->Prepare(
            "UPDATE location_distances SET from_id=?1,to_id=?2,distance_km=?3,travel_note=?4,"
            "days_estimate=?5 WHERE id=?6");
        if (!st) return std::unexpected(st.error());
        (void)st->BindInt(1, row.from_id);
        (void)st->BindInt(2, row.to_id);
        (void)st->BindDouble(3, row.distance_km);
        (void)st->BindText(4, row.travel_note);
        (void)st->BindDouble(5, row.days_estimate);
        (void)st->BindInt(6, target);
        if (auto s = st->Step(); !s) return std::unexpected(s.error());
        return target;
    }
    auto st = db_->Prepare(
        "INSERT INTO location_distances(from_id,to_id,distance_km,travel_note,days_estimate)"
        " VALUES(?1,?2,?3,?4,?5)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, row.from_id);
    (void)st->BindInt(2, row.to_id);
    (void)st->BindDouble(3, row.distance_km);
    (void)st->BindText(4, row.travel_note);
    (void)st->BindDouble(5, row.days_estimate);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<std::vector<LocationDistanceRow>, DbError>
NovelGraph::ListLocationDistances(RowId fromId, int limit) const {
    if (limit <= 0) limit = 200;
    const bool all = fromId <= 0;
    auto st = db_->Prepare(fmt::format(
        "SELECT id,from_id,to_id,distance_km,travel_note,days_estimate FROM location_distances {} "
        "ORDER BY id LIMIT {}",
        all ? std::string{}
            : std::string{"WHERE from_id=?1 OR to_id=?1"}, // 无向：两头都算
        limit));
    if (!st) return std::unexpected(st.error());
    if (!all) {
        (void)st->BindInt(1, fromId);
    }
    std::vector<LocationDistanceRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        LocationDistanceRow r;
        r.id = st->ColumnInt(0);
        r.from_id = st->ColumnInt(1);
        r.to_id = st->ColumnInt(2);
        r.distance_km = st->ColumnDouble(3);
        r.travel_note = st->ColumnText(4);
        r.days_estimate = st->ColumnDouble(5);
        out.push_back(std::move(r));
    }
    return out;
}

std::expected<double, DbError> NovelGraph::LocationDistanceDays(RowId fromId, RowId toId) const {
    if (fromId <= 0 || toId <= 0) {
        return std::unexpected(Err("查询行程天数必须给出两个地点 id"));
    }
    auto st = db_->Prepare(
        "SELECT days_estimate FROM location_distances WHERE (from_id=?1 AND to_id=?2) "
        "OR (from_id=?2 AND to_id=?1) ORDER BY CASE WHEN from_id=?1 THEN 0 ELSE 1 END, id LIMIT 1");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, fromId);
    (void)st->BindInt(2, toId);
    if (auto s = st->Step(); !s) {
        return std::unexpected(s.error());
    } else if (*s == db::sqlite::StepResult::Row) {
        return st->ColumnDouble(0);
    }
    return -1.0; // 无记录 = 未知（不是错误：`06` K18 见到未知就跳过该条）
}

std::expected<void, DbError> NovelGraph::UpsertWritingStyle(const WritingStyleRow& row) {
    if (row.pov_mode.empty()) {
        return std::unexpected(Err("写作风格必须给 pov_mode"));
    }
    const auto inUnit = [](double v) noexcept { return v >= 0.0 && v <= 1.0; };
    if (!inUnit(row.dialogue_ratio) || !inUnit(row.action_ratio) || !inUnit(row.thought_ratio) ||
        !inUnit(row.env_ratio)) {
        return std::unexpected(Err("写作风格的四个 ratio 必须在 0–1 之间"));
    }
    if (row.humor < 0 || row.humor > 100 || row.serious < 0 || row.serious > 100) {
        return std::unexpected(Err("写作风格的 humor/serious 必须在 0–100 之间"));
    }
    // 全书单行：主键固定 id=1（DDL 有 CHECK(id=1)）
    auto st = db_->Prepare(
        "INSERT OR REPLACE INTO writing_style(id,pov_mode,sentence_len,density,dialogue_ratio,"
        "action_ratio,thought_ratio,env_ratio,humor,serious,pacing,note)"
        " VALUES(1,?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindText(1, row.pov_mode);
    (void)st->BindText(2, row.sentence_len);
    (void)st->BindText(3, row.density);
    (void)st->BindDouble(4, row.dialogue_ratio);
    (void)st->BindDouble(5, row.action_ratio);
    (void)st->BindDouble(6, row.thought_ratio);
    (void)st->BindDouble(7, row.env_ratio);
    (void)st->BindInt(8, row.humor);
    (void)st->BindInt(9, row.serious);
    (void)st->BindText(10, row.pacing);
    (void)st->BindText(11, row.note);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return {};
}

std::expected<WritingStyleRow, DbError> NovelGraph::GetWritingStyle() const {
    auto st = db_->Prepare(
        "SELECT id,pov_mode,sentence_len,density,dialogue_ratio,action_ratio,thought_ratio,env_ratio,"
        "humor,serious,pacing,note FROM writing_style WHERE id=1");
    if (!st) return std::unexpected(st.error());
    if (auto s = st->Step(); !s) {
        return std::unexpected(s.error());
    } else if (*s != db::sqlite::StepResult::Row) {
        return std::unexpected(Err("尚未设置全书写作风格（writing_style 无 id=1 行）"));
    }
    WritingStyleRow r;
    r.id = st->ColumnInt(0);
    r.pov_mode = st->ColumnText(1);
    r.sentence_len = st->ColumnText(2);
    r.density = st->ColumnText(3);
    r.dialogue_ratio = st->ColumnDouble(4);
    r.action_ratio = st->ColumnDouble(5);
    r.thought_ratio = st->ColumnDouble(6);
    r.env_ratio = st->ColumnDouble(7);
    r.humor = static_cast<int>(st->ColumnInt(8));
    r.serious = static_cast<int>(st->ColumnInt(9));
    r.pacing = st->ColumnText(10);
    r.note = st->ColumnText(11);
    return r;
}

std::expected<RowId, DbError> NovelGraph::UpsertAuthorRule(const AuthorRuleRow& row) {
    if (row.rule.empty()) {
        return std::unexpected(Err("作者规则不能为空"));
    }
    const std::string_view sev = row.severity.empty() ? std::string_view{"warn"} : row.severity;
    if (sev != "error" && sev != "warn" && sev != "info") {
        return std::unexpected(Err("作者规则的 severity 只能是 error / warn / info"));
    }
    if (row.id > 0) {
        auto st = db_->Prepare("UPDATE author_rules SET rule=?1,severity=?2,note=?3 WHERE id=?4");
        if (!st) return std::unexpected(st.error());
        (void)st->BindText(1, row.rule);
        (void)st->BindText(2, sev);
        (void)st->BindText(3, row.note);
        (void)st->BindInt(4, row.id);
        if (auto s = st->Step(); !s) return std::unexpected(s.error());
        return row.id;
    }
    auto st = db_->Prepare("INSERT INTO author_rules(rule,severity,note) VALUES(?1,?2,?3)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindText(1, row.rule);
    (void)st->BindText(2, sev);
    (void)st->BindText(3, row.note);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<std::vector<AuthorRuleRow>, DbError>
NovelGraph::ListAuthorRules(std::string_view severity) const {
    const bool all = severity.empty();
    auto st = db_->Prepare(all
                               ? std::string{"SELECT id,rule,severity,note FROM author_rules ORDER BY id"}
                               : std::string{"SELECT id,rule,severity,note FROM author_rules WHERE "
                                             "severity=?1 ORDER BY id"});
    if (!st) return std::unexpected(st.error());
    if (!all) {
        (void)st->BindText(1, severity);
    }
    std::vector<AuthorRuleRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        AuthorRuleRow r;
        r.id = st->ColumnInt(0);
        r.rule = st->ColumnText(1);
        r.severity = st->ColumnText(2);
        r.note = st->ColumnText(3);
        out.push_back(std::move(r));
    }
    return out;
}

bool NovelGraph::RunGraphSelfCheck() {
    // 依赖全量 schema（NovelDb 自检）
    if (!NovelDb::RunSchemaSelfCheck()) {
        return false;
    }
    db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) {
        log::Error("NovelGraph 自检：打开内存库失败");
        return false;
    }
    // 用 NovelDb 的迁移建全表（打开临时文件更干净；这里直接跑内存 + 同一段 schema 由 RunSchemaSelfCheck 覆盖）
    // 为避免重复巨型 SQL：打开一个临时文件库走 Open()
    NovelGraph g(mem);
    // 手工建本自检需要的最小表集（与 v3 列名一致）
    if (auto r = mem.Exec(kMinSchema); !r) {
        log::Error("NovelGraph 自检：建表失败 {}", r.error().message);
        return false;
    }
    auto pid = g.UpsertEntity({.kind = std::string{kind::person}, .name = "林默", .summary = "主角"});
    if (!pid) {
        log::Error("NovelGraph 自检：upsert person 失败 {}", pid.error().message);
        return false;
    }
    auto lid = g.UpsertEntity({.kind = std::string{kind::location}, .name = "黑森林"});
    if (!lid) return false;
    auto rel = g.UpsertRelation({.from_id = *pid, .to_id = *lid, .rel_type = "located_at"});
    if (!rel) return false;
    auto e1 = g.UpsertEvent({.kind = std::string{kind::event}, .name = "初入黑森林"},
                            {.location_id = *lid});
    auto e2 = g.UpsertEvent({.kind = std::string{kind::event}, .name = "发现戒指"}, {});
    if (!e1 || !e2) return false;
    if (!g.UpsertCausalLink({.cause_event_id = *e1, .effect_event_id = *e2, .link_type = "enables"})) {
        return false;
    }
    auto chain = g.GetEventChain(*e1, 2);
    if (!chain || chain->empty()) {
        log::Error("NovelGraph 自检：因果链为空");
        return false;
    }
    auto fs = g.UpsertForeshadow({.title = "黑戒指", .status = "PLANTED"});
    if (!fs) return false;
    auto open = g.ListOpenForeshadows();
    if (!open || open->empty()) return false;
    auto ch = g.UpsertChapter({.ord = 1, .title = "第一章", .body = "雪原上只剩下风声。"});
    if (!ch) return false;
    auto slice = g.GetCharacterSlice(*pid, *ch);
    if (!slice || slice->entity.name != "林默") {
        log::Error("NovelGraph 自检：CharacterSlice 失败");
        return false;
    }
    // —— P0 八表（S2）：每表 Upsert* + List* ——
    auto scene = g.UpsertScene({.chapter_id = *ch, .ord = 1, .title = "雪原"});
    if (!scene) {
        log::Error("NovelGraph 自检：UpsertScene 失败");
        return false;
    }
    // 知情：一条「第 3 章才知」、一条「不知道」→ 规范查询按章过滤
    {
        auto k1 = g.UpsertKnowledge({.entity_id = *pid,
                                     .fact_kind = "secret",
                                     .fact_id = 7,
                                     .fact_text = "戒指的来历",
                                     .knows = 1,
                                     .chapter_known = 3});
        auto k2 = g.UpsertKnowledge({.entity_id = *pid,
                                     .fact_kind = "secret",
                                     .fact_id = 8,
                                     .fact_text = "王的真名",
                                     .knows = 0,
                                     .chapter_known = 0});
        auto kl = g.ListKnowledge(*pid);
        auto klEarly = g.ListKnowledge(*pid, 2);
        if (!k1 || !k2 || !kl || kl->size() != 2 || !klEarly || klEarly->size() != 1 ||
            (*klEarly)[0].fact_id != 8) {
            log::Error("NovelGraph 自检：character_knowledge CRUD/按章过滤失败");
            return false;
        }
        auto kBefore = g.CharacterKnows(*pid, "secret", 7, 2); // 第 2 章：还不知
        auto kAt = g.CharacterKnows(*pid, "secret", 7, 3);     // 第 3 章：已知
        auto kNo = g.CharacterKnows(*pid, "secret", 9, 9);     // 无记录
        if (!kBefore || *kBefore || !kAt || !*kAt || !kNo || *kNo) {
            log::Error("NovelGraph 自检：CharacterKnows 规范查询失败 before={} at={} none={}",
                       kBefore ? *kBefore : false, kAt ? *kAt : false, kNo ? *kNo : false);
            return false;
        }
    }
    // 事件参与（含 K04 用的实体→事件方向）
    {
        auto ep = g.UpsertEventParticipant({.event_id = *e1, .entity_id = *pid, .role = "actor"});
        auto epl = g.ListEventParticipants(*e1);
        auto byEnt = g.ListEntityParticipations(*pid);
        auto byRole = g.ListEntityParticipations(*pid, "victim");
        if (!ep || !epl || epl->size() != 1 || (*epl)[0].role != "actor" || !byEnt ||
            byEnt->size() != 1 || !byRole || !byRole->empty()) {
            log::Error("NovelGraph 自检：event_participants CRUD/双向查询失败");
            return false;
        }
    }
    // 场次在场 / 场次伏笔
    {
        auto sc = g.UpsertSceneCast({.scene_id = *scene, .entity_id = *pid, .role = "pov"});
        auto scl = g.ListSceneCast(*scene);
        if (!sc || !scl || scl->size() != 1 || (*scl)[0].role != "pov") {
            log::Error("NovelGraph 自检：scene_cast CRUD 失败");
            return false;
        }
        auto sf = g.UpsertSceneForeshadow(
            {.scene_id = *scene, .foreshadowing_id = *fs, .action = "plant"});
        auto sfl = g.ListSceneForeshadows(*scene);
        if (!sf || !sfl || sfl->size() != 1 || (*sfl)[0].action != "plant") {
            log::Error("NovelGraph 自检：scene_foreshadows CRUD 失败");
            return false;
        }
    }
    // 剧情线 + 节拍
    {
        auto plot = g.UpsertPlot({.kind = "main", .title = "追查戒指", .target_ch = 12});
        if (!plot) {
            log::Error("NovelGraph 自检：UpsertPlot 失败");
            return false;
        }
        (void)g.UpsertPlotBeat(
            {.plot_id = *plot, .chapter_id = *ch, .ord = 0, .beat_type = "setup", .title = "埋线"});
        (void)g.UpsertPlotBeat({.plot_id = *plot,
                                .chapter_id = *ch,
                                .ord = 1,
                                .beat_type = "rising",
                                .title = "升级"});
        auto got = g.GetPlot(*plot);
        auto pbl = g.ListPlotBeats(*plot);
        auto byKind = g.ListPlots("main");
        auto byOther = g.ListPlots("sub");
        if (!got || got->title != "追查戒指" || !pbl || pbl->size() != 2 || (*pbl)[0].ord != 0 ||
            (*pbl)[1].ord != 1 || !byKind || byKind->size() != 1 || !byOther || !byOther->empty()) {
            log::Error("NovelGraph 自检：plots/plot_beats CRUD 或按 kind 过滤失败");
            return false;
        }
    }
    // 谜团 + 节拍
    {
        auto my = g.UpsertMystery({.question = "戒指是谁的？", .importance = 80});
        if (!my) {
            log::Error("NovelGraph 自检：UpsertMystery 失败");
            return false;
        }
        (void)g.UpsertMysteryBeat({.mystery_id = *my,
                                   .beat_type = "hint",
                                   .chapter_id = *ch,
                                   .content = "旧铜环"});
        auto gm = g.GetMystery(*my);
        auto mbl = g.ListMysteryBeats(*my);
        auto all = g.ListMysteries();
        auto openM = g.ListOpenMysteries();
        if (!gm || gm->status != "open" || !mbl || mbl->size() != 1 || !all || all->size() != 1 ||
            !openM || openM->size() != 1) {
            log::Error("NovelGraph 自检：mysteries/mystery_beats CRUD 失败");
            return false;
        }
        // 推进到 resolved 后应从「开放线索」里消失
        (void)g.UpsertMystery({.id = *my, .question = "戒指是谁的？", .status = "resolved"});
        auto openM2 = g.ListOpenMysteries();
        if (!openM2 || !openM2->empty()) {
            log::Error("NovelGraph 自检：ListOpenMysteries 过滤失败");
            return false;
        }
    }
    // —— 初始化链（S3-pre）：`10` §2.3 门禁的三个硬依赖 ——
    constexpr RowId kGhost = 999999; // 不存在的实体，用于负向断言
    {
        // 角色弧光：门禁要求「主角必须有 arc，stage 至少含起点与终点」
        auto a1 = g.UpsertArc({.entity_id = *pid, .ord = 1, .stage = "懦弱少年"});
        auto a2 = g.UpsertArc({.entity_id = *pid,
                               .ord = 2,
                               .stage = "第一次杀人",
                               .trigger_event_id = *e1,
                               .note = "转折点"});
        auto a3 = g.UpsertArc({.entity_id = *pid, .ord = 3, .stage = "蜕变"});
        auto arcs = g.ListArcs(*pid);
        auto noArcs = g.ListArcs(kGhost);
        if (!a1 || !a2 || !a3 || !arcs || arcs->size() != 3 || (*arcs)[0].stage != "懦弱少年" ||
            (*arcs)[2].stage != "蜕变" || (*arcs)[1].trigger_event_id != *e1 || !noArcs ||
            !noArcs->empty()) {
            log::Error("NovelGraph 自检：character_arcs CRUD / 按 ord 排序 / 按实体过滤失败");
            return false;
        }
        bool hasStart = false;
        bool hasEnd = false;
        for (const auto& a : *arcs) {
            if (a.ord == 1) hasStart = true;
            if (a.ord == 3) hasEnd = true;
        }
        if (!hasStart || !hasEnd) {
            log::Error("NovelGraph 自检：弧光缺起点或终点（`10` §2.3 门禁应可判定）");
            return false;
        }
        // 角色级说话方式（一人一行，重复写覆盖）
        if (!g.UpsertDialogueStyle({.entity_id = *pid,
                                    .sentence_len = "短句",
                                    .vocabulary = "少用形容词",
                                    .catchphrase = "……嗯",
                                    .taboo_words = "绝不称「大人」",
                                    .habit = "先沉默半拍"})) {
            log::Error("NovelGraph 自检：UpsertDialogueStyle 失败");
            return false;
        }
        auto ds = g.GetDialogueStyle(*pid);
        if (!ds || ds->catchphrase != "……嗯" || ds->taboo_words != "绝不称「大人」") {
            log::Error("NovelGraph 自检：GetDialogueStyle 失败");
            return false;
        }
        if (!g.UpsertDialogueStyle({.entity_id = *pid, .catchphrase = "换了口头禅"})) {
            log::Error("NovelGraph 自检：覆盖说话方式失败");
            return false;
        }
        auto ds2 = g.GetDialogueStyle(*pid);
        if (!ds2 || ds2->catchphrase != "换了口头禅" || !ds2->sentence_len.empty()) {
            log::Error("NovelGraph 自检：说话方式未按一人一行覆盖");
            return false;
        }
        if (g.GetDialogueStyle(kGhost)) {
            log::Error("NovelGraph 自检：未写的角色不该有说话方式");
            return false;
        }
        // 世界级键值：`10` §2.3 N1 的 book_title
        if (!g.SetWorldMeta("book_title", "雨的信號")) {
            log::Error("NovelGraph 自检：SetWorldMeta 失败");
            return false;
        }
        if (!g.SetWorldMeta("book_title", "雨的信號·改")) {
            log::Error("NovelGraph 自检：SetWorldMeta 覆盖失败");
            return false;
        }
        auto bt = g.GetWorldMeta("book_title");
        if (!bt || *bt != "雨的信號·改") {
            log::Error("NovelGraph 自检：GetWorldMeta 未拿到覆盖后的值");
            return false;
        }
        if (g.GetWorldMeta("no_such_key")) {
            log::Error("NovelGraph 自检：不存在的 world_meta 键应报错");
            return false;
        }
        // 主题
        auto th = g.UpsertTheme({.title = "身份与代价", .statement = "伪装久了会变成真的"});
        auto th2 = g.UpsertTheme({.title = "忠诚", .statement = "忠诚是选择不是天性"});
        auto themes = g.ListThemes();
        if (!th || !th2 || !themes || themes->size() != 2 || (*themes)[0].title != "身份与代价") {
            log::Error("NovelGraph 自检：themes CRUD 失败");
            return false;
        }
        if (!g.UpsertTheme({.id = *th, .title = "身份与代价", .statement = "改写后的陈述"})) {
            log::Error("NovelGraph 自检：UpsertTheme 同 id 更新失败");
            return false;
        }
        auto thGet = g.ListThemes();
        if (!thGet || thGet->size() != 2 || (*thGet)[0].statement != "改写后的陈述") {
            log::Error("NovelGraph 自检：themes 同 id 更新语义失败");
            return false;
        }
    }

    // —— P1 余下五表（S7）：`entity_versions` / `location_distances` / `writing_style` / `author_rules` ——
    {
        // ① 快照：能写能读（`07` §2.2 的"写前快照"）
        // `values` 是 SQLite 关键字 —— 必须用带引号的列名，否则 prepare 就语法错（见 UpsertPersona 注释）
        auto personaWrite = g.UpsertPersona({.entity_id = *pid,
                                            .appearance = "黑发少年",
                                            .values = "责任高于一切",
                                            .goal = "活下来"});
        if (!personaWrite) {
            log::Error("NovelGraph 自检：persona 写入失败：{}", personaWrite.error().message);
            return false;
        }
        auto personaRead = g.GetPersona(*pid);
        if (!personaRead || personaRead->appearance != "黑发少年" || personaRead->goal != "活下来" ||
            personaRead->values != "责任高于一切") {
            log::Error("NovelGraph 自检：persona 写后读失败（含关键字列 values）");
            return false;
        }
        auto v1 = g.SnapshotEntity(*pid, "写前快照");
        auto v2 = g.SnapshotEntity(*pid, "第二次");
        auto versions = g.ListEntityVersions(*pid);
        if (!v1 || !v2 || *v1 == *v2 || !versions || versions->size() != 2 ||
            (*versions)[0].ver != 2 || (*versions)[1].ver != 1 || (*versions)[0].id != *v2) {
            log::Error("NovelGraph 自检：entity_versions 写入 / ver 递增 / 新→旧排序失败");
            return false;
        }
        auto loaded = g.LoadEntitySnapshot(*v1);
        if (!loaded || loaded->entity.id != *pid || loaded->entity.name != "林默" ||
            loaded->persona.appearance != "黑发少年" || loaded->persona.goal != "活下来") {
            log::Error("NovelGraph 自检：快照读回（entity + persona）失败");
            return false;
        }
        auto latest = g.GetLatestEntityVersion(*pid);
        if (!latest || latest->ver != 2 || latest->note != "第二次" || latest->id != *v2) {
            log::Error("NovelGraph 自检：GetLatestEntityVersion 失败");
            return false;
        }
        if (g.GetLatestEntityVersion(kGhost) || g.GetEntityVersion(999999)) {
            log::Error("NovelGraph 自检：不存在的快照应报错");
            return false;
        }
        if (g.SaveEntityVersion({.entity_id = *pid, .snapshot_json = "{}"}) ||
            g.SaveEntityVersion({.snapshot_json = "{\"a\":1}"})) {
            log::Error("NovelGraph 自检：空快照 / 缺 entity_id 应被拒");
            return false;
        }

        // ② 地点距离：一对地点一行（无向）、能查天数、无记录返回 -1（`06` K18 的数据来源）
        if (!g.UpsertLocationDistance({.from_id = *pid,
                                       .to_id = *lid,
                                       .distance_km = 12.5,
                                       .travel_note = "沿河谷",
                                       .days_estimate = 0.5})) {
            log::Error("NovelGraph 自检：UpsertLocationDistance 失败");
            return false;
        }
        if (!g.UpsertLocationDistance({.from_id = *lid, // 反向再写一次 → 应更新同一行
                                       .to_id = *pid,
                                       .distance_km = 12.5,
                                       .travel_note = "沿河谷（回程）",
                                       .days_estimate = 0.5})) {
            log::Error("NovelGraph 自检：地点距离反向覆盖失败");
            return false;
        }
        auto dists = g.ListLocationDistances(*lid);
        if (!dists || dists->size() != 1 || dists->front().travel_note != "沿河谷（回程）") {
            log::Error("NovelGraph 自检：同一对地点应只有一行（无向覆盖语义）");
            return false;
        }
        auto days = g.LocationDistanceDays(*pid, *lid);
        auto daysReverse = g.LocationDistanceDays(*lid, *pid);
        auto daysUnknown = g.LocationDistanceDays(*pid, kGhost);
        if (!days || *days != 0.5 || !daysReverse || *daysReverse != 0.5 || !daysUnknown ||
            *daysUnknown != -1.0) {
            log::Error("NovelGraph 自检：行程天数（无向查询 / 无记录 -1）失败");
            return false;
        }
        if (g.UpsertLocationDistance({.from_id = *pid, .to_id = *pid})) {
            log::Error("NovelGraph 自检：同一地点的距离应被拒");
            return false;
        }

        // ③ 写作风格：全书单行（id=1）+ 覆盖语义 + 非法值被拒
        if (!g.UpsertWritingStyle({.pov_mode = "first",
                                   .sentence_len = "short",
                                   .density = "medium",
                                   .dialogue_ratio = 0.4,
                                   .action_ratio = 0.3,
                                   .thought_ratio = 0.2,
                                   .env_ratio = 0.1,
                                   .humor = 20,
                                   .serious = 70,
                                   .pacing = "fast",
                                   .note = "冷硬"})) {
            log::Error("NovelGraph 自检：UpsertWritingStyle 失败");
            return false;
        }
        auto ws = g.GetWritingStyle();
        if (!ws || ws->pov_mode != "first" || ws->dialogue_ratio != 0.4 || ws->humor != 20 ||
            ws->pacing != "fast") {
            log::Error("NovelGraph 自检：GetWritingStyle 字段不符");
            return false;
        }
        if (!g.UpsertWritingStyle({.pov_mode = "third_limited"})) {
            log::Error("NovelGraph 自检：写作风格覆盖失败");
            return false;
        }
        auto ws2 = g.GetWritingStyle();
        if (!ws2 || ws2->pov_mode != "third_limited" || !ws2->pacing.empty()) {
            log::Error("NovelGraph 自检：写作风格未按 id=1 单行覆盖");
            return false;
        }
        if (g.UpsertWritingStyle({.pov_mode = ""}) ||
            g.UpsertWritingStyle({.pov_mode = "first", .dialogue_ratio = 1.5})) {
            log::Error("NovelGraph 自检：写作风格非法值应被拒");
            return false;
        }

        // ④ 作者规则：severity 过滤 / 同 id 更新 / 非法值被拒
        auto ruleErr = g.UpsertAuthorRule({.rule = "不许出现现代词汇", .severity = "error"});
        auto ruleWarn = g.UpsertAuthorRule({.rule = "少用感叹号", .severity = "warn"});
        auto errRules = g.ListAuthorRules("error");
        auto allRules = g.ListAuthorRules();
        if (!ruleErr || !ruleWarn || !errRules || errRules->size() != 1 ||
            errRules->front().rule != "不许出现现代词汇" || !allRules || allRules->size() != 2) {
            log::Error("NovelGraph 自检：author_rules 写入/按 severity 过滤失败");
            return false;
        }
        if (!g.UpsertAuthorRule({.id = *ruleWarn, .rule = "绝不写脏话", .severity = "warn"})) {
            log::Error("NovelGraph 自检：author_rules 同 id 更新失败");
            return false;
        }
        auto warnRules = g.ListAuthorRules("warn");
        if (!warnRules || warnRules->size() != 1 || warnRules->front().rule != "绝不写脏话") {
            log::Error("NovelGraph 自检：author_rules 更新后语义不符");
            return false;
        }
        if (g.UpsertAuthorRule({.rule = "x", .severity = "fatal"}) ||
            g.UpsertAuthorRule({.rule = ""})) {
            log::Error("NovelGraph 自检：author_rules 非法 severity / 空规则应被拒");
            return false;
        }
    }
    log::Info("NovelGraph 自检通过（实体/关系/因果/伏笔/章节/切片 + P0 八表 + 初始化链四表 + "
              "P1 四表：快照/地点距离/写作风格/作者规则）");
    {
        const char* path = std::getenv("SHINE_NOVEL_CHECK_OUT");
        if (path && *path) {
            FILE* f = std::fopen(path, "ab");
            if (f) {
                const char* line = "graph:ok\nPASS\n";
                std::fwrite(line, 1, std::strlen(line), f);
                std::fclose(f);
            }
        }
    }
    return true;
}

} // namespace shine::novelcore
