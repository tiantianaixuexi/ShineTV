#include "novel/NovelFields.h"

#include "core/Log.h"
#include "util/Time.h"

#include <fmt/format.h>

#include <yyjson.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace shine::novelcore {
namespace {

constexpr std::string_view kSchemaV5Fields = R"SQL(
CREATE TABLE IF NOT EXISTS field_defs(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  scope TEXT NOT NULL DEFAULT 'entity',
  entity_kind TEXT NOT NULL DEFAULT '',
  field_key TEXT NOT NULL,
  title TEXT NOT NULL DEFAULT '',
  value_type TEXT NOT NULL DEFAULT 'text',
  enum_json TEXT NOT NULL DEFAULT '[]',
  description TEXT NOT NULL DEFAULT '',
  created_by TEXT NOT NULL DEFAULT '',
  is_system INTEGER NOT NULL DEFAULT 0,
  updated INTEGER NOT NULL DEFAULT 0);
CREATE UNIQUE INDEX IF NOT EXISTS idx_field_defs_key ON field_defs(scope, entity_kind, field_key);

CREATE TABLE IF NOT EXISTS entity_fields(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  entity_id INTEGER NOT NULL DEFAULT 0,
  field_key TEXT NOT NULL,
  value_text TEXT NOT NULL DEFAULT '',
  value_json TEXT NOT NULL DEFAULT 'null',
  chapter_scope INTEGER NOT NULL DEFAULT 0,
  chapter_to INTEGER NOT NULL DEFAULT 0,
  layer TEXT NOT NULL DEFAULT 'global',
  note TEXT NOT NULL DEFAULT '',
  created_by TEXT NOT NULL DEFAULT '',
  updated INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS idx_ef_entity ON entity_fields(entity_id);
CREATE INDEX IF NOT EXISTS idx_ef_key ON entity_fields(field_key);
CREATE UNIQUE INDEX IF NOT EXISTS idx_ef_uniq
  ON entity_fields(entity_id, field_key, chapter_scope, layer);
)SQL";

[[nodiscard]] std::string Esc(std::string_view s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (const char c : s) {
        if (c == '\'') {
            o += "''";
        } else {
            o += c;
        }
    }
    return o;
}

[[nodiscard]] FieldDefRow ReadFieldDef(db::sqlite::Statement& st) {
    FieldDefRow r;
    r.id = st.ColumnInt(0);
    r.scope = st.ColumnText(1);
    r.entity_kind = st.ColumnText(2);
    r.field_key = st.ColumnText(3);
    r.title = st.ColumnText(4);
    r.value_type = st.ColumnText(5);
    r.enum_json = st.ColumnText(6);
    r.description = st.ColumnText(7);
    r.created_by = st.ColumnText(8);
    r.is_system = st.ColumnInt(9);
    r.updated = st.ColumnInt(10);
    return r;
}

[[nodiscard]] EntityFieldRow ReadEntityField(db::sqlite::Statement& st) {
    EntityFieldRow r;
    r.id = st.ColumnInt(0);
    r.entity_id = st.ColumnInt(1);
    r.field_key = st.ColumnText(2);
    r.value_text = st.ColumnText(3);
    r.value_json = st.ColumnText(4);
    r.chapter_scope = st.ColumnInt(5);
    r.chapter_to = st.ColumnInt(6);
    r.layer = st.ColumnText(7);
    r.note = st.ColumnText(8);
    r.created_by = st.ColumnText(9);
    r.updated = st.ColumnInt(10);
    return r;
}

} // namespace

std::expected<RowId, DbError> NovelFields::UpsertFieldDef(const FieldDefRow& row) {
    if (row.field_key.empty()) {
        return std::unexpected(DbError{0, "field_key 不能为空"});
    }
    const auto now = util::NowMillis() / 1000;
    const std::string scope = row.scope.empty() ? "entity" : row.scope;
    // 先删同键再插，保证唯一
    (void)db_->Exec(fmt::format(
        "DELETE FROM field_defs WHERE scope='{}' AND entity_kind='{}' AND field_key='{}'",
        Esc(scope), Esc(row.entity_kind), Esc(row.field_key)));
    auto st = db_->Prepare(
        "INSERT INTO field_defs(scope,entity_kind,field_key,title,value_type,enum_json,"
        "description,created_by,is_system,updated) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10)");
    if (!st) {
        return std::unexpected(st.error());
    }
    (void)st->BindText(1, scope);
    (void)st->BindText(2, row.entity_kind);
    (void)st->BindText(3, row.field_key);
    (void)st->BindText(4, row.title);
    (void)st->BindText(5, row.value_type.empty() ? "text" : row.value_type);
    (void)st->BindText(6, row.enum_json.empty() ? "[]" : row.enum_json);
    (void)st->BindText(7, row.description);
    (void)st->BindText(8, row.created_by);
    (void)st->BindInt(9, row.is_system);
    (void)st->BindInt(10, static_cast<std::int64_t>(now));
    if (auto s = st->Step(); !s || *s != db::sqlite::StepResult::Done) {
        return std::unexpected(DbError{0, "insert field_defs 失败"});
    }
    return db_->LastInsertRowId();
}

std::expected<FieldDefRow, DbError> NovelFields::GetFieldDef(RowId id) const {
    auto st = db_->Prepare(
        "SELECT id,scope,entity_kind,field_key,title,value_type,enum_json,description,"
        "created_by,is_system,updated FROM field_defs WHERE id=?1");
    if (!st) {
        return std::unexpected(st.error());
    }
    (void)st->BindInt(1, id);
    if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
        return ReadFieldDef(*st);
    }
    return std::unexpected(DbError{0, "field_def 不存在"});
}

std::expected<std::vector<FieldDefRow>, DbError>
NovelFields::ListFieldDefs(std::string_view scope, std::string_view entityKind) const {
    std::string sql =
        "SELECT id,scope,entity_kind,field_key,title,value_type,enum_json,description,"
        "created_by,is_system,updated FROM field_defs WHERE 1=1";
    if (!scope.empty()) {
        sql += fmt::format(" AND scope='{}'", Esc(scope));
    }
    if (!entityKind.empty()) {
        sql += fmt::format(" AND entity_kind IN ('','{}')", Esc(entityKind));
    }
    sql += " ORDER BY is_system DESC, scope, field_key";
    auto st = db_->Prepare(sql);
    if (!st) {
        return std::unexpected(st.error());
    }
    std::vector<FieldDefRow> out;
    while (auto s = st->Step()) {
        if (*s != db::sqlite::StepResult::Row) break;
        out.push_back(ReadFieldDef(*st));
    }
    return out;
}

std::expected<void, DbError> NovelFields::DeleteFieldDef(RowId id, bool force) {
    auto cur = GetFieldDef(id);
    if (!cur) {
        return std::unexpected(cur.error());
    }
    if (cur->is_system && !force) {
        return std::unexpected(DbError{0, "系统字段需 force 才能删除"});
    }
    if (auto r = db_->Exec(fmt::format("DELETE FROM field_defs WHERE id={}", id)); !r) {
        return r;
    }
    return {};
}

std::expected<RowId, DbError> NovelFields::UpsertEntityField(const EntityFieldRow& row) {
    if (row.field_key.empty()) {
        return std::unexpected(DbError{0, "field_key 不能为空"});
    }
    const auto now = util::NowMillis() / 1000;
    const std::string layer = row.layer.empty() ? "global" : row.layer;
    const std::string vj = row.value_json.empty() ? "null" : row.value_json;
    (void)db_->Exec(fmt::format(
        "DELETE FROM entity_fields WHERE entity_id={} AND field_key='{}' AND chapter_scope={} "
        "AND layer='{}'",
        row.entity_id, Esc(row.field_key), row.chapter_scope, Esc(layer)));
    auto st = db_->Prepare(
        "INSERT INTO entity_fields(entity_id,field_key,value_text,value_json,chapter_scope,"
        "chapter_to,layer,note,created_by,updated) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10)");
    if (!st) {
        return std::unexpected(st.error());
    }
    (void)st->BindInt(1, row.entity_id);
    (void)st->BindText(2, row.field_key);
    (void)st->BindText(3, row.value_text);
    (void)st->BindText(4, vj);
    (void)st->BindInt(5, row.chapter_scope);
    (void)st->BindInt(6, row.chapter_to);
    (void)st->BindText(7, layer);
    (void)st->BindText(8, row.note);
    (void)st->BindText(9, row.created_by);
    (void)st->BindInt(10, static_cast<std::int64_t>(now));
    if (auto s = st->Step(); !s || *s != db::sqlite::StepResult::Done) {
        return std::unexpected(DbError{0, "insert entity_fields 失败"});
    }
    return db_->LastInsertRowId();
}

std::expected<std::vector<EntityFieldRow>, DbError>
NovelFields::ListEntityFields(RowId entityId, RowId chapterId, std::string_view layer) const {
    std::string sql = fmt::format(
        "SELECT id,entity_id,field_key,value_text,value_json,chapter_scope,chapter_to,layer,note,"
        "created_by,updated FROM entity_fields WHERE entity_id={} "
        "AND (chapter_scope=0 OR chapter_scope<={}) "
        "AND (chapter_to=0 OR chapter_to>={})",
        entityId, chapterId, chapterId);
    if (!layer.empty()) {
        sql += fmt::format(" AND layer='{}'", Esc(layer));
    }
    sql += " ORDER BY field_key, chapter_scope DESC, layer";
    auto st = db_->Prepare(sql);
    if (!st) {
        return std::unexpected(st.error());
    }
    std::vector<EntityFieldRow> out;
    while (auto s = st->Step()) {
        if (*s != db::sqlite::StepResult::Row) break;
        out.push_back(ReadEntityField(*st));
    }
    return out;
}

std::expected<void, DbError> NovelFields::DeleteEntityField(RowId id) {
    return db_->Exec(fmt::format("DELETE FROM entity_fields WHERE id={}", id));
}

std::expected<EntityFieldRow, DbError>
NovelFields::GetEntityFieldByKey(RowId entityId, std::string_view key, RowId chapterId,
                                 std::string_view layer) const {
    auto list = ListEntityFields(entityId, chapterId, layer);
    if (!list) {
        return std::unexpected(list.error());
    }
    for (const auto& r : *list) {
        if (r.field_key == key) {
            return r;
        }
    }
    return std::unexpected(DbError{0, "字段不存在"});
}

std::expected<RowId, DbError> NovelFields::UpsertWorldField(std::string_view key,
                                                             std::string_view valueText,
                                                             std::string_view valueJson,
                                                             std::string_view layer,
                                                             std::string_view createdBy) {
    EntityFieldRow row;
    row.entity_id = 0; // world 挂在 entity_id=0
    row.field_key = std::string{key};
    row.value_text = std::string{valueText};
    row.value_json = valueJson.empty() ? "null" : std::string{valueJson};
    row.layer = layer.empty() ? "global" : std::string{layer};
    row.created_by = std::string{createdBy};
    return UpsertEntityField(row);
}

std::expected<std::vector<EntityFieldRow>, DbError>
NovelFields::ListWorldFields(RowId chapterId) const {
    return ListEntityFields(0, chapterId, {});
}

std::expected<std::string, DbError> NovelFields::EntityFieldsJson(RowId entityId,
                                                                  RowId chapterId) const {
    auto list = ListEntityFields(entityId, chapterId, {});
    if (!list) {
        return std::unexpected(list.error());
    }
    std::string arr = "[";
    bool first = true;
    for (const auto& f : *list) {
        if (!first) arr += ",";
        first = false;
        arr += fmt::format(
            R"({{"key":"{}","value":"{}","value_json":{},"layer":"{}","chapter_scope":{},"note":"{}"}})",
            f.field_key, f.value_text, f.value_json, f.layer, f.chapter_scope, f.note);
    }
    arr += "]";
    return arr;
}

void NovelFields::SeedBuiltinFieldDefs(NovelFields& fields) {
    struct Seed {
        std::string_view scope;
        std::string_view entityKind;
        std::string_view key;
        std::string_view title;
        std::string_view valueType;
        std::string_view desc;
    };
    constexpr Seed kSeeds[] = {
        {"world", "", "active_universe", "当前宇宙", "text",
         "本卷/本章激活的宇宙名；可多宇宙并存"},
        {"world", "", "universe_layers", "宇宙层列表", "json",
         "如 [{\"id\":\"main\",\"name\":\"主宇宙\"}]"},
        {"world", "", "world_rules_active", "激活规则", "json",
         "当前生效的 world_rule 实体 id 或自定义规则"},
        {"entity", "person", "identity_layers", "身份层", "json",
         "多面身份：[{\"layer\":\"mask\",\"label\":\"反派\",\"faction\":1},{\"layer\":\"true\",\"label\":\"卧底\"}]"},
        {"entity", "person", "true_faction", "真实效忠", "text", "背地效忠的势力名/ id"},
        {"entity", "person", "public_mask", "公开伪装", "text", "众人眼中的身份"},
        {"entity", "person", "power_profile", "力量画像", "json", "AI 自定义体系字段，不限 skill/spell"},
        {"entity", "", "custom_traits", "自定义特质", "json", "任意扩展：键值由 Agent 生成"},
        {"chapter", "", "pov_knowledge_scope", "本章知情范围", "json", "本章 POV 可见的字段/秘密"},
        {"chapter", "", "universe_of_chapter", "本章宇宙", "text", "不同章节可不同世界观"},
        {"agent", "", "routing_hints", "路由提示", "json", "影响 Router 的关键词"},
    };
    for (const auto& s : kSeeds) {
        FieldDefRow row;
        row.scope = std::string{s.scope};
        row.entity_kind = std::string{s.entityKind};
        row.field_key = std::string{s.key};
        row.title = std::string{s.title};
        row.value_type = std::string{s.valueType};
        row.description = std::string{s.desc};
        row.created_by = "system";
        row.is_system = 1;
        (void)fields.UpsertFieldDef(row);
    }
}

bool NovelFields::RunSelfCheck() {
    db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) {
        log::Error("Fields 自检：打开内存库失败");
        return false;
    }
    // 最小表
    if (auto r = mem.Exec(R"SQL(
CREATE TABLE IF NOT EXISTS entities(id INTEGER PRIMARY KEY AUTOINCREMENT,kind TEXT,name TEXT,summary TEXT,status TEXT,meta_json TEXT,created_chapter INTEGER,updated INTEGER);
)SQL"); !r) {
        return false;
    }
    if (auto r = mem.Exec(std::string{kSchemaV5Fields}); !r) {
        log::Error("Fields 自检：建表失败 {}", r.error().message);
        return false;
    }

    NovelFields fields(mem);
    SeedBuiltinFieldDefs(fields);

    auto defs = fields.ListFieldDefs("entity", "person");
    if (!defs || defs->empty()) {
        log::Error("Fields 自检：种子 field_defs 失败");
        return false;
    }

    // 插入人物 + 动态身份字段
    auto ins = mem.Prepare("INSERT INTO entities(kind,name) VALUES('person','林默')");
    if (!ins || !ins->Step()) {
        return false;
    }
    const auto pid = mem.LastInsertRowId();

    EntityFieldRow mask;
    mask.entity_id = pid;
    mask.field_key = "public_mask";
    mask.value_text = "反派军师";
    mask.layer = "mask";
    mask.created_by = "character";
    if (!fields.UpsertEntityField(mask)) {
        log::Error("Fields 自检：写 mask 失败");
        return false;
    }

    EntityFieldRow truth;
    truth.entity_id = pid;
    truth.field_key = "true_faction";
    truth.value_text = "北境卧底网";
    truth.layer = "true";
    truth.created_by = "character";
    if (!fields.UpsertEntityField(truth)) {
        log::Error("Fields 自检：写 true 失败");
        return false;
    }

    // 分章宇宙
    if (!fields.UpsertWorldField("active_universe", "主宇宙", "{}", "global", "world")) {
        return false;
    }
    EntityFieldRow chU;
    chU.entity_id = 0;
    chU.field_key = "universe_of_chapter";
    chU.value_text = "镜像宇宙";
    chU.chapter_scope = 50;
    chU.layer = "global";
    chU.created_by = "world";
    if (!fields.UpsertEntityField(chU)) {
        return false;
    }

    // 第 10 章：不应看到第 50 章的镜像宇宙字段
    auto early = fields.ListWorldFields(10);
    if (!early) {
        return false;
    }
    bool sawMirrorEarly = false;
    for (const auto& f : *early) {
        if (f.field_key == "universe_of_chapter") sawMirrorEarly = true;
    }
    if (sawMirrorEarly) {
        log::Error("Fields 自检：chapter_scope 过滤失败（第10章看到了第50章字段）");
        return false;
    }
    auto late = fields.ListWorldFields(50);
    if (!late) {
        return false;
    }
    bool sawMirrorLate = false;
    for (const auto& f : *late) {
        if (f.field_key == "universe_of_chapter" && f.value_text == "镜像宇宙") {
            sawMirrorLate = true;
        }
    }
    if (!sawMirrorLate) {
        log::Error("Fields 自检：第50章应看到镜像宇宙字段");
        return false;
    }

    // 身份层读取
    auto maskGot = fields.GetEntityFieldByKey(pid, "public_mask", 0, "mask");
    auto trueGot = fields.GetEntityFieldByKey(pid, "true_faction", 0, "true");
    if (!maskGot || maskGot->value_text != "反派军师" || !trueGot ||
        trueGot->value_text != "北境卧底网") {
        log::Error("Fields 自检：身份层读取失败");
        return false;
    }

    auto json = fields.EntityFieldsJson(pid, 0);
    if (!json || json->find("true_faction") == std::string::npos) {
        log::Error("Fields 自检：EntityFieldsJson 失败");
        return false;
    }

    // AI 新增自定义字段（非系统）
    FieldDefRow custom;
    custom.scope = "entity";
    custom.entity_kind = "person";
    custom.field_key = "bloodline_seal";
    custom.title = "血脉封印";
    custom.value_type = "text";
    custom.created_by = "field_builder";
    custom.is_system = 0;
    if (!fields.UpsertFieldDef(custom)) {
        return false;
    }
    EntityFieldRow blood;
    blood.entity_id = pid;
    blood.field_key = "bloodline_seal";
    blood.value_text = "第三层未解";
    blood.layer = "true";
    blood.created_by = "field_builder";
    if (!fields.UpsertEntityField(blood)) {
        return false;
    }

    log::Info("NovelFields 自检通过（字段定义/身份层/分章宇宙/自定义扩展）");
    if (const char* path = std::getenv("SHINE_NOVEL_CHECK_OUT"); path && *path) {
        if (FILE* f = std::fopen(path, "ab")) {
            const char* line = "fields:ok\n";
            std::fwrite(line, 1, std::strlen(line), f);
            std::fclose(f);
        }
    }
    return true;
}

} // namespace shine::novelcore
