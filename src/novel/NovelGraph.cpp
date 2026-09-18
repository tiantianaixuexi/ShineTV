#include "novel/NovelGraph.h"

#include "core/Log.h"
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
    auto st = db_->Prepare(
        "INSERT INTO entity_personas(entity_id,age,appearance,personality,background,values,"
        "desire,goal,fear,weakness,strength,ability_note,knowledge_note,memory_note)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14)"
        " ON CONFLICT(entity_id) DO UPDATE SET age=excluded.age,appearance=excluded.appearance,"
        "personality=excluded.personality,background=excluded.background,values=excluded.values,"
        "desire=excluded.desire,goal=excluded.goal,fear=excluded.fear,weakness=excluded.weakness,"
        "strength=excluded.strength,ability_note=excluded.ability_note,"
        "knowledge_note=excluded.knowledge_note,memory_note=excluded.memory_note");
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
        "SELECT entity_id,age,appearance,personality,background,values,desire,goal,fear,"
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
    log::Info("NovelGraph 自检通过（实体/关系/因果/伏笔/章节/切片）");
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
