#include "novel/NovelFields.h"

#include "core/Log.h"
#include "novel/NovelGraph.h" // S9：升格写 audit_logs(action='promote_field')
#include "util/Time.h"

#include <fmt/format.h>

#include <yyjson.h>

#include <cctype>
#include <charconv>
#include <cstdio>
#include "util/Json.h" // S43：JsonQuote 的唯一来源

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
  status TEXT NOT NULL DEFAULT 'PROPOSED',
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

-- v8（S2b）别名表。**本常量是本组表 DDL 的唯一来源**（`08` §2.3）——
-- NovelDb::Migrate / AgentKit::EnsureSchemaAndSeed / 各处自检一律调 NovelFields::EnsureSchema。
CREATE TABLE IF NOT EXISTS field_aliases(
  alias TEXT PRIMARY KEY,
  canonical_key TEXT NOT NULL DEFAULT '',
  note TEXT NOT NULL DEFAULT '');
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
    r.status = st.ColumnText(11); // v8（S2b）
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

// —— S2b 字段门禁（规格 `08` §2.2 / §2.5）——

// JSON 字符串字面量（含引号）。`08` §2.5：手拼 JSON 不转义是确定性 bug ——
// value_text / note 含 `"` 或换行会产出非法 JSON，直接喂给 Agent。
// S43：合并到 `util::json::JsonQuote`（**唯一来源**）。原先这里自己用 yyjson 写了一遍 ——
// 功能正确，但属"同一件事多处实现"（连同 `NovelImageGen` 一起收口）。
// ⚠️ 丢掉了原先"非法 UTF-8 时 `log::Warn`"那一条：util 版静默退化为空串（与 `NovelImageGen` 一致）。
[[nodiscard]] std::string JsonQuote(std::string_view s) { return util::json::JsonQuote(s); }

// value_json 原样嵌入，但必须先能解析；非法则退化为 null（不产出非法 JSON）
[[nodiscard]] std::string JsonRawOrNull(std::string_view s) {
    if (s.empty()) {
        return "null";
    }
    yyjson_doc* d = yyjson_read(s.data(), s.size(), 0);
    if (d == nullptr) {
        return "null";
    }
    yyjson_doc_free(d);
    return std::string{s};
}

// 取定义（`08` §2.2 ②）：按 field_key 查，`entity_kind` 允许 ''=不限，优先 scope 精确匹配。
// 返回 id==0 表示「未登记」；只有 SQL 失败才返回 error。
[[nodiscard]] std::expected<FieldDefRow, DbError> FindDef(db::sqlite::Database& db, RowId entityId,
                                                          std::string_view key,
                                                          std::string_view scopeHint) {
    std::string kind;
    if (entityId > 0) {
        if (auto st = db.Prepare("SELECT kind FROM entities WHERE id=?1")) {
            (void)st->BindInt(1, entityId);
            if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                kind = st->ColumnText(0);
            }
        }
    }
    auto st = db.Prepare(
        "SELECT id,scope,entity_kind,field_key,title,value_type,enum_json,description,created_by,"
        "is_system,updated,status FROM field_defs "
        "WHERE field_key=?1 AND (entity_kind='' OR entity_kind=?2) "
        "ORDER BY (scope=?3) DESC, is_system DESC, id LIMIT 1");
    if (!st) {
        return std::unexpected(st.error());
    }
    (void)st->BindText(1, key);
    (void)st->BindText(2, kind);
    (void)st->BindText(3, scopeHint);
    auto s = st->Step();
    if (!s) {
        return std::unexpected(s.error());
    }
    if (*s == db::sqlite::StepResult::Row) {
        return ReadFieldDef(*st);
    }
    return FieldDefRow{}; // id==0 → 未登记
}

// 按 value_type 校值（`08` §2.2 ③）。任一步失败 → 拒绝，错误信息含字段名与期望类型。
[[nodiscard]] std::expected<void, DbError> ValidateFieldValue(const FieldDefRow& def,
                                                              const EntityFieldRow& row) {
    const std::string& vt = def.value_type;
    if (vt == "text") {
        if (row.value_text.size() > 4096) {
            return std::unexpected(DbError{0, fmt::format(
                "validate：字段 '{}'（text）长度 {} 超过 4096", def.field_key,
                row.value_text.size())});
        }
        for (char c : row.value_text) {
            const auto u = static_cast<unsigned char>(c);
            if (u < 0x20 && c != '\n' && c != '\t') {
                return std::unexpected(DbError{0, fmt::format(
                    "validate：字段 '{}'（text）含控制字符 0x{:02X}", def.field_key,
                    static_cast<int>(u))});
            }
        }
        return {};
    }
    if (vt == "number") {
        const std::string& s = row.value_text;
        double out = 0.0;
        const auto* b = s.data();
        const auto* e = s.data() + s.size();
        const auto [p, ec] = std::from_chars(b, e, out);
        if (s.empty() || ec != std::errc{} || p != e) {
            return std::unexpected(DbError{0, fmt::format(
                "validate：字段 '{}'（number）'{}' 不是合法数字", def.field_key, s)});
        }
        return {};
    }
    if (vt == "json") {
        const std::string vj = row.value_json.empty() ? "null" : row.value_json;
        yyjson_doc* d = yyjson_read(vj.c_str(), vj.size(), 0);
        if (d == nullptr) {
            return std::unexpected(DbError{0, fmt::format(
                "validate：字段 '{}'（json）不是合法 JSON", def.field_key)});
        }
        yyjson_val* root = yyjson_doc_get_root(d);
        const bool shaped = yyjson_is_obj(root) || yyjson_is_arr(root);
        yyjson_doc_free(d);
        if (!shaped) {
            return std::unexpected(DbError{0, fmt::format(
                "validate：字段 '{}'（json）必须是对象或数组（不允许裸标量）", def.field_key)});
        }
        return {};
    }
    if (vt == "enum") {
        yyjson_doc* d = yyjson_read(def.enum_json.c_str(), def.enum_json.size(), 0);
        if (d == nullptr) {
            return std::unexpected(DbError{0, fmt::format(
                "contract：字段 '{}' 的 enum_json 不是合法 JSON", def.field_key)});
        }
        bool hit = false;
        yyjson_val* root = yyjson_doc_get_root(d);
        if (yyjson_is_arr(root)) {
            const size_t n = yyjson_arr_size(root);
            for (size_t i = 0; i < n; ++i) {
                yyjson_val* v = yyjson_arr_get(root, i);
                if (v != nullptr && yyjson_is_str(v) && row.value_text == yyjson_get_str(v)) {
                    hit = true;
                    break;
                }
            }
        }
        yyjson_doc_free(d);
        if (!hit) {
            return std::unexpected(DbError{0, fmt::format(
                "validate：字段 '{}'（enum）值 '{}' 不在 enum_json 内", def.field_key,
                row.value_text)});
        }
        return {};
    }
    return std::unexpected(DbError{0, fmt::format(
        "contract：字段 '{}' 的 value_type '{}' 未知（应为 {}）", def.field_key, vt,
        NovelFields::kValueTypeEnum)});
}

} // namespace

// —— S2b：键归一化 / 枚举校验（`08` §2.2 ①、§2.6）——

std::string NovelFields::NormalizeKey(std::string_view raw) {
    std::string folded;
    folded.reserve(raw.size());
    for (unsigned char c : raw) {
        if (std::isspace(c) != 0 || c == '-') {
            folded.push_back('_');
        } else {
            folded.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    std::string out;
    out.reserve(folded.size());
    for (char c : folded) {
        if (c == '_' && (out.empty() || out.back() == '_')) {
            continue; // 折叠连续下划线 + 去掉前导下划线
        }
        out.push_back(c);
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out;
}

bool NovelFields::IsValidKey(std::string_view key) {
    if (key.size() < 2 || key.size() > 40) {
        return false;
    }
    if (key[0] < 'a' || key[0] > 'z') {
        return false;
    }
    for (char c : key) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool NovelFields::IsValidLayer(std::string_view layer) {
    return layer == "global" || layer == "public" || layer == "mask" || layer == "true" ||
           layer == "private";
}

// —— S2b：别名表（`08` §2.4）——

std::expected<void, DbError> NovelFields::UpsertFieldAlias(std::string_view alias,
                                                           std::string_view canonicalKey,
                                                           std::string_view note) {
    const std::string a = NormalizeKey(alias);
    const std::string c = NormalizeKey(canonicalKey);
    if (!IsValidKey(a) || !IsValidKey(c)) {
        return std::unexpected(
            DbError{0, "contract：别名与规范键都必须满足 ^[a-z][a-z0-9_]{1,39}$"});
    }
    if (a == c) {
        return std::unexpected(DbError{0, "contract：别名与规范键相同，无需登记"});
    }
    (void)db_->Exec(fmt::format("DELETE FROM field_aliases WHERE alias='{}'", Esc(a)));
    auto st = db_->Prepare("INSERT INTO field_aliases(alias,canonical_key,note) VALUES(?1,?2,?3)");
    if (!st) {
        return std::unexpected(st.error());
    }
    (void)st->BindText(1, a);
    (void)st->BindText(2, c);
    (void)st->BindText(3, note);
    if (auto s = st->Step(); !s || *s != db::sqlite::StepResult::Done) {
        return std::unexpected(DbError{0, "insert field_aliases 失败"});
    }
    return {};
}

std::expected<std::string, DbError> NovelFields::ResolveAlias(std::string_view key) const {
    std::string out{key};
    auto st = db_->Prepare("SELECT canonical_key FROM field_aliases WHERE alias=?1");
    if (!st) {
        return out; // 表缺失等 → 视为无别名，不阻断写入
    }
    (void)st->BindText(1, key);
    if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
        const std::string c = st->ColumnText(0);
        if (!c.empty()) {
            out = c;
        }
    }
    return out;
}

std::expected<std::vector<std::pair<std::string, std::string>>, DbError>
NovelFields::ListFieldAliases() const {
    auto st = db_->Prepare("SELECT alias,canonical_key FROM field_aliases ORDER BY alias");
    if (!st) {
        return std::unexpected(st.error());
    }
    std::vector<std::pair<std::string, std::string>> out;
    while (auto s = st->Step()) {
        if (*s != db::sqlite::StepResult::Row) break;
        out.emplace_back(st->ColumnText(0), st->ColumnText(1));
    }
    return out;
}

// —— 字段表 DDL 的**唯一定义来源**（`08` §2.3）——
std::expected<void, DbError> NovelFields::EnsureSchema(db::sqlite::Database& db) {
    if (auto r = db.Exec(kSchemaV5Fields); !r) {
        return r;
    }
    // 旧库：`CREATE TABLE IF NOT EXISTS` 不会改已存在的表 → 补 `status` 列并回填。
    // 列已存在则 ALTER 报错，忽略（SQLite 没有 ADD COLUMN IF NOT EXISTS）。
    (void)db.Exec("ALTER TABLE field_defs ADD COLUMN status TEXT NOT NULL DEFAULT 'PROPOSED'");
    (void)db.Exec("UPDATE field_defs SET status='CANON' WHERE is_system=1");
    return {};
}

std::expected<RowId, DbError> NovelFields::UpsertFieldDef(const FieldDefRow& row) {
    const std::string key = NormalizeKey(row.field_key);
    if (!IsValidKey(key)) {
        return std::unexpected(DbError{0, fmt::format(
            "contract：field_key '{}' 归一后为 '{}'，不满足 ^[a-z][a-z0-9_]{{1,39}}$",
            row.field_key, key)});
    }
    const std::string vt = row.value_type.empty() ? "text" : row.value_type;
    if (vt != "text" && vt != "number" && vt != "json" && vt != "enum") {
        return std::unexpected(DbError{0, fmt::format(
            "contract：value_type '{}' 不在枚举内（{}）", vt, kValueTypeEnum)});
    }
    const std::string scope = row.scope.empty() ? "entity" : row.scope;
    // `08` §2.3：AI/Agent 的提案一律 PROPOSED；is_system=1 的种子写 CANON；人工可显式传。
    int isSystem = row.is_system;
    std::string status = row.status.empty() ? (isSystem != 0 ? "CANON" : "PROPOSED") : row.status;
    // 既有键检查：① 系统种子不可被后写降级（AI 重复 Upsert 同键不许把它从 CANON 打成 PROPOSED）；
    // ② `08` §2.3 的硬上限只约束「新增键」，更新既有键不受限。
    bool exists = false;
    if (auto ex = db_->Prepare("SELECT is_system FROM field_defs WHERE scope=?1 AND "
                               "entity_kind=?2 AND field_key=?3")) {
        (void)ex->BindText(1, scope);
        (void)ex->BindText(2, row.entity_kind);
        (void)ex->BindText(3, key);
        if (auto s = ex->Step(); s && *s == db::sqlite::StepResult::Row) {
            exists = true;
            if (ex->ColumnInt(0) != 0) {
                isSystem = 1;
                status = "CANON";
            }
        }
    }
    if (!exists) {
        auto cnt = db_->Prepare("SELECT COUNT(*) FROM field_defs");
        if (cnt && cnt->Step() && cnt->ColumnInt(0) >= kMaxFieldDefs) {
            return std::unexpected(DbError{0, fmt::format(
                "contract：field_defs 已达单工程上限 {}，新增 '{}' 被拒（`08` §2.3，进人工复核清单）",
                kMaxFieldDefs, key)});
        }
    }
    if (status != "PROPOSED" && status != "CANON") {
        return std::unexpected(DbError{0, fmt::format(
            "contract：status '{}' 只能是 PROPOSED / CANON", status)});
    }
    const auto now = util::NowMillis() / 1000;
    // 先删同键再插，保证唯一
    (void)db_->Exec(fmt::format(
        "DELETE FROM field_defs WHERE scope='{}' AND entity_kind='{}' AND field_key='{}'",
        Esc(scope), Esc(row.entity_kind), Esc(key)));
    auto st = db_->Prepare(
        "INSERT INTO field_defs(scope,entity_kind,field_key,title,value_type,enum_json,"
        "description,created_by,is_system,updated,status) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)");
    if (!st) {
        return std::unexpected(st.error());
    }
    (void)st->BindText(1, scope);
    (void)st->BindText(2, row.entity_kind);
    (void)st->BindText(3, key);
    (void)st->BindText(4, row.title);
    (void)st->BindText(5, vt);
    (void)st->BindText(6, row.enum_json.empty() ? "[]" : row.enum_json);
    (void)st->BindText(7, row.description);
    (void)st->BindText(8, row.created_by);
    (void)st->BindInt(9, isSystem);
    (void)st->BindInt(10, static_cast<std::int64_t>(now));
    (void)st->BindText(11, status);
    if (auto s = st->Step(); !s || *s != db::sqlite::StepResult::Done) {
        return std::unexpected(DbError{0, "insert field_defs 失败"});
    }
    return db_->LastInsertRowId();
}

std::expected<FieldDefRow, DbError> NovelFields::GetFieldDef(RowId id) const {
    auto st = db_->Prepare(
        "SELECT id,scope,entity_kind,field_key,title,value_type,enum_json,description,"
        "created_by,is_system,updated,status FROM field_defs WHERE id=?1");
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
        "created_by,is_system,updated,status FROM field_defs WHERE 1=1";
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
    // ① 归一化：trim → lower → 空白/连字符 → 下划线；再查别名表（`08` §2.2 ① / §2.4）
    std::string key = NormalizeKey(row.field_key);
    if (auto alias = ResolveAlias(key); alias) {
        key = *alias;
    }
    if (!IsValidKey(key)) {
        return std::unexpected(DbError{0, fmt::format(
            "contract：field_key '{}' 归一后为 '{}'，不满足 ^[a-z][a-z0-9_]{{1,39}}$",
            row.field_key, key)});
    }
    // ①b layer 枚举化（`08` §2.6）
    const std::string layer = row.layer.empty() ? "global" : row.layer;
    if (!IsValidLayer(layer)) {
        return std::unexpected(DbError{0, fmt::format("contract：layer '{}' 不在枚举内（{}）", layer,
                                                      kLayerEnum)});
    }
    // ② 查定义（`08` §2.2 ②）：未登记 → 拒绝，并把提案落 PROPOSED（§2.3）
    const std::string scopeHint = row.entity_id > 0 ? "entity" : "world";
    auto found = FindDef(*db_, row.entity_id, key, scopeHint);
    if (!found) {
        return std::unexpected(found.error());
    }
    if (found->id == 0) {
        FieldDefRow proposal; // is_system=0 → status=PROPOSED
        proposal.scope = scopeHint;
        proposal.field_key = key;
        proposal.title = key;
        proposal.value_type = "text";
        proposal.description = "字段门禁自动登记的未登记键提案（待人工 / `09` §2.5 检查点复核）";
        proposal.created_by = row.created_by;
        (void)UpsertFieldDef(proposal); // 失败不掩盖主错误
        return std::unexpected(DbError{0, fmt::format(
            "field_unregistered：'{}' 未在 field_defs 登记；已落 PROPOSED 提案，"
            "复核（或再写一次）后即可写入（`08` §2.2）", key)});
    }
    const FieldDefRow def = *found;
    std::string note = row.note;
    if (def.status == "PROPOSED" && note.find("unapproved_field") == std::string::npos) {
        note = note.empty() ? "unapproved_field" : note + ";unapproved_field";
    }
    // ③ 校值（`08` §2.2 ③）：拒绝即失败，不改默认、不静默纠正
    if (auto v = ValidateFieldValue(def, row); !v) {
        return std::unexpected(v.error());
    }
    const auto now = util::NowMillis() / 1000;
    const std::string vj = row.value_json.empty() ? "null" : row.value_json;
    (void)db_->Exec(fmt::format(
        "DELETE FROM entity_fields WHERE entity_id={} AND field_key='{}' AND chapter_scope={} "
        "AND layer='{}'",
        row.entity_id, Esc(key), row.chapter_scope, Esc(layer)));
    auto st = db_->Prepare(
        "INSERT INTO entity_fields(entity_id,field_key,value_text,value_json,chapter_scope,"
        "chapter_to,layer,note,created_by,updated) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10)");
    if (!st) {
        return std::unexpected(st.error());
    }
    (void)st->BindInt(1, row.entity_id);
    (void)st->BindText(2, key);
    (void)st->BindText(3, row.value_text);
    (void)st->BindText(4, vj);
    (void)st->BindInt(5, row.chapter_scope);
    (void)st->BindInt(6, row.chapter_to);
    (void)st->BindText(7, layer);
    (void)st->BindText(8, note);
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
        if (!first) {
            arr += ",";
        }
        first = false;
        // `08` §2.5：逐个转义 —— 输出必须是**合法 JSON**。原来手拼且不转义，
        // value_text / note 含 `"` 或换行即产出非法串，再直接喂给 Agent（静默吞数据）。
        arr += fmt::format(
            R"({{"key":{},"value":{},"value_json":{},"layer":{},"chapter_scope":{},"note":{}}})",
            JsonQuote(f.field_key), JsonQuote(f.value_text), JsonRawOrNull(f.value_json),
            JsonQuote(f.layer), f.chapter_scope, JsonQuote(f.note));
    }
    arr += "]";
    return arr;
}

// —— S9（`08` §2.3）：检查点的 PROPOSED→CANON 自动升格 ——
namespace {

// 值形状是否与 value_type 一致（条件 ② 值类型始终一致）—— 与写入侧 ValidateFieldValue 同口径。
[[nodiscard]] bool ValueShapeOk(std::string_view valueType, const std::string& valueText,
                               const std::string& valueJson, const std::string& enumJson) {
    if (valueType == "text") {
        return !valueText.empty() && valueText.size() <= 4096;
    }
    if (valueType == "number") {
        double out = 0.0;
        const auto* b = valueText.data();
        const auto* e = b + valueText.size();
        const auto [p, ec] = std::from_chars(b, e, out);
        return !valueText.empty() && ec == std::errc{} && p == e;
    }
    if (valueType == "json") {
        yyjson_doc* d = yyjson_read(valueJson.c_str(), valueJson.size(), 0);
        if (d == nullptr) return false;
        yyjson_val* root = yyjson_doc_get_root(d);
        const bool shaped = yyjson_is_obj(root) || yyjson_is_arr(root);
        yyjson_doc_free(d);
        return shaped;
    }
    if (valueType == "enum") {
        yyjson_doc* d = yyjson_read(enumJson.c_str(), enumJson.size(), 0);
        if (d == nullptr) return false;
        bool hit = false;
        yyjson_val* root = yyjson_doc_get_root(d);
        if (yyjson_is_arr(root)) {
            const std::size_t n = yyjson_arr_size(root);
            for (std::size_t i = 0; i < n; ++i) {
                yyjson_val* v = yyjson_arr_get(root, i);
                if (v != nullptr && yyjson_is_str(v) && valueText == yyjson_get_str(v)) {
                    hit = true;
                    break;
                }
            }
        }
        yyjson_doc_free(d);
        return hit;
    }
    return false;
}

} // namespace

std::expected<std::vector<NovelFields::FieldPromotionCandidate>, DbError>
NovelFields::EvaluateFieldPromotion(RowId atChapterOrd) const {
    auto defs = ListFieldDefs();
    if (!defs) {
        return std::unexpected(defs.error());
    }
    std::vector<FieldPromotionCandidate> out;
    for (const auto& def : *defs) {
        if (def.status != "PROPOSED") {
            continue;
        }
        FieldPromotionCandidate c;
        c.id = def.id;
        c.field_key = def.field_key;
        c.value_type = def.value_type;
        // ① 已出现 ≥ 5 次
        if (auto st = db_->Prepare("SELECT COUNT(*) FROM entity_fields WHERE field_key=?1")) {
            (void)st->BindText(1, def.field_key);
            if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                c.usages = st->ColumnInt(0);
            }
        }
        // ② 值类型始终一致（至少一行，且每行形状都对）
        bool any = false;
        bool stable = true;
        if (auto st = db_->Prepare("SELECT value_text,value_json FROM entity_fields WHERE field_key=?1")) {
            (void)st->BindText(1, def.field_key);
            while (auto s = st->Step()) {
                if (*s != db::sqlite::StepResult::Row) break;
                any = true;
                if (!ValueShapeOk(def.value_type, st->ColumnText(0), st->ColumnText(1),
                                  def.enum_json)) {
                    stable = false;
                    break;
                }
            }
        }
        c.type_stable = any && stable;
        // ③ 无同义键冲突（`08` §2.4 别名表；语义重叠本身不自动合并，只挡已登记的别名冲突）
        int aliasConflicts = 0;
        if (auto st = db_->Prepare("SELECT COUNT(*) FROM field_aliases WHERE (alias=?1 AND "
                                   "canonical_key<>?1) OR (canonical_key=?1 AND alias<>?1)")) {
            (void)st->BindText(1, def.field_key);
            if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                aliasConflicts = st->ColumnInt(0);
            }
        }
        c.no_synonym = aliasConflicts == 0;
        // ④ 最近 3 章内仍在使用（chapter_scope/chapter_to 视窗近似）
        const RowId from = atChapterOrd > kPromoteRecentWindow
                               ? atChapterOrd - kPromoteRecentWindow + 1
                               : 0;
        int recent = 0;
        if (auto st = db_->Prepare("SELECT COUNT(*) FROM entity_fields WHERE field_key=?1 AND "
                                   "(chapter_scope=0 OR chapter_scope<=?2) AND (chapter_to=0 OR "
                                   "chapter_to>=?3)")) {
            (void)st->BindText(1, def.field_key);
            (void)st->BindInt(2, atChapterOrd);
            (void)st->BindInt(3, from);
            if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                recent = st->ColumnInt(0);
            }
        }
        c.recently_used = recent > 0;
        std::string why;
        if (c.usages < kPromoteMinUsages) {
            why += fmt::format("出现次数 {}/{}；", c.usages, kPromoteMinUsages);
        }
        if (!c.type_stable) why += "值类型不一致；";
        if (!c.no_synonym) why += "存在同义键冲突（别名表）；";
        if (!c.recently_used) why += "最近 3 章未使用；";
        c.reject = why;
        out.push_back(std::move(c));
    }
    return out;
}

std::expected<void, DbError> NovelFields::SetFieldDefStatus(RowId id, std::string_view status) {
    if (status != "PROPOSED" && status != "CANON") {
        return std::unexpected(
            DbError{0, fmt::format("contract：status '{}' 只能是 PROPOSED / CANON", status)});
    }
    return db_->Exec(fmt::format("UPDATE field_defs SET status='{}', updated={} WHERE id={}",
                                 Esc(status), util::NowMillis() / 1000, id));
}

std::expected<std::vector<std::string>, DbError>
NovelFields::PromoteProposedFields(RowId atChapterOrd) {
    auto cands = EvaluateFieldPromotion(atChapterOrd);
    if (!cands) {
        return std::unexpected(cands.error());
    }
    std::vector<std::string> promoted;
    for (const auto& c : *cands) {
        if (!c.Passed()) {
            continue;
        }
        if (auto r = SetFieldDefStatus(c.id, "CANON"); !r) {
            log::Warn("Fields：'{}' 自动升格失败：{}", c.field_key, r.error().message);
            continue;
        }
        NovelGraph g(*db_);
        if (auto a = g.LogAudit("orchestrator", "promote_field", "field_def", c.id,
                                fmt::format("usages={} at_ch={}", c.usages, atChapterOrd));
            !a) {
            log::Warn("Fields：'{}' 升格审计失败：{}", c.field_key, a.error().message);
        }
        log::Info("Fields：PROPOSED→CANON 自动升格 '{}'（出现 {} 次）", c.field_key, c.usages);
        promoted.push_back(c.field_key);
    }
    return promoted;
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
        // ★ S63：**幂等必须落到"不写"** —— `UpsertFieldDef` 是**先删后插**且无条件把 `updated`
        // 刷成 now（见本文件 `UpsertFieldDef` 的 `BindInt(10, now)`）。于是**每次**调用本函数，
        // 11 条内置字段的 `updated` 全被刷新 —— 而 `field_defs.updated` **正是
        // `ComputeInputStateHash`（`04` §2.5）的输入之一** ⇒ **章节中途调一次本函数，输入状态哈希
        // 就漂移** ⇒ K23（不变式 I9 `prompt.state_hash_match`）**必然 fail** ⇒ 提交被挡。
        // 🔴 真跑实证（第 5 章）：`CHAPTER_REVIEW` 产物记于 t=1789899034，而 field_defs 被整体改写到
        //    t=1789899106（**72 秒后**）—— 那次写入正是 S62 工具循环里新加的 `EnsureSchemaAndSeed`。
        // 判据：内容（title / value_type / enum / description / CANON / is_system）全一致 ⇒ **跳过**。
        if (auto ex = fields.db_->Prepare("SELECT title,value_type,enum_json,description,status,is_system "
                                          "FROM field_defs WHERE scope=?1 AND entity_kind=?2 AND "
                                          "field_key=?3")) {
            (void)ex->BindText(1, row.scope);
            (void)ex->BindText(2, row.entity_kind);
            (void)ex->BindText(3, row.field_key);
            if (auto st = ex->Step(); st && *st == db::sqlite::StepResult::Row) {
                const std::string wantEnum = row.enum_json.empty() ? "[]" : row.enum_json;
                const bool identical = ex->ColumnText(0) == row.title &&
                                       ex->ColumnText(1) == row.value_type &&
                                       ex->ColumnText(2) == wantEnum &&
                                       ex->ColumnText(3) == row.description &&
                                       ex->ColumnText(4) == "CANON" && ex->ColumnInt(5) != 0;
                if (identical) {
                    continue; // 无变化 → 不写 → 不动 `updated`（保住哈希稳定性）
                }
            }
        }
        // 种子失败必须可见：原先是 (void) 吞掉 —— 「field_defs 缺列 → 全部种子静默丢失 →
        // 之后写入一律 field_unregistered」是最难查的一类故障（S2b 已踩）。
        if (auto r = fields.UpsertFieldDef(row); !r) {
            log::Warn("Fields 种子写入失败 key={}：{}", s.key, r.error().message);
        }
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
    // 字段表 DDL 走唯一来源（同时验证 EnsureSchema 本身可用）
    if (auto r = NovelFields::EnsureSchema(mem); !r) {
        log::Error("Fields 自检：EnsureSchema 失败 {}", r.error().message);
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

    // —— S2b 字段门禁（`08` §2.2 三步 / §2.4 别名 / §2.6 layer 枚举 / §2.5 JSON 转义）——
    {
        // ① 归一化：'  Public MASK  ' → public_mask（trim / lower / 空白→下划线 / 折叠）
        EntityFieldRow mixed;
        mixed.entity_id = pid;
        mixed.field_key = "  Public MASK  ";
        mixed.value_text = "归一化写入";
        mixed.layer = "mask";
        if (!fields.UpsertEntityField(mixed)) {
            log::Error("Fields 自检：归一化写入失败");
            return false;
        }
        auto norm = fields.GetEntityFieldByKey(pid, "public_mask", 0, "mask");
        if (!norm || norm->value_text != "归一化写入") {
            log::Error("Fields 自检：' Public MASK ' 未归一为 public_mask");
            return false;
        }
        // ① 别名：blood_seal → bloodline_seal（读与写都过别名表）
        if (auto a = fields.UpsertFieldAlias("blood_seal", "bloodline_seal", "自检别名"); !a) {
            log::Error("Fields 自检：写别名失败 {}", a.error().message);
            return false;
        }
        EntityFieldRow aliased;
        aliased.entity_id = pid;
        aliased.field_key = "blood_seal";
        aliased.value_text = "第五层";
        aliased.layer = "true";
        if (!fields.UpsertEntityField(aliased)) {
            log::Error("Fields 自检：别名写入失败");
            return false;
        }
        auto viaAlias = fields.GetEntityFieldByKey(pid, "bloodline_seal", 0, "true");
        if (!viaAlias || viaAlias->value_text != "第五层") {
            log::Error("Fields 自检：别名未归一为 bloodline_seal");
            return false;
        }
        // bloodline_seal 是 is_system=0 → status=PROPOSED：可写，但必须带 unapproved_field
        if (viaAlias->note.find("unapproved_field") == std::string::npos) {
            log::Error("Fields 自检：PROPOSED 字段 note 应含 unapproved_field，got '{}'",
                       viaAlias->note);
            return false;
        }
        // ② 未登记 → 拒绝 + 提案落 PROPOSED
        EntityFieldRow unreg;
        unreg.entity_id = pid;
        unreg.field_key = "never_registered_key";
        unreg.value_text = "x";
        unreg.layer = "global";
        unreg.created_by = "check";
        auto rej = fields.UpsertEntityField(unreg);
        if (rej) {
            log::Error("Fields 自检：未登记键竟然写入成功");
            return false;
        }
        if (rej.error().message.find("field_unregistered") == std::string::npos) {
            log::Error("Fields 自检：未登记键错误应含 field_unregistered，got {}",
                       rej.error().message);
            return false;
        }
        auto defs2 = fields.ListFieldDefs("entity");
        if (!defs2) {
            return false;
        }
        bool sawProposal = false;
        for (const auto& d : *defs2) {
            if (d.field_key == "never_registered_key" && d.status == "PROPOSED" &&
                d.is_system == 0) {
                sawProposal = true;
            }
            if (d.is_system == 1 && d.status != "CANON") {
                log::Error("Fields 自检：系统种子 '{}' 的 status 应为 CANON，got '{}'", d.field_key,
                           d.status);
                return false;
            }
        }
        if (!sawProposal) {
            log::Error("Fields 自检：未登记键的提案未落 PROPOSED");
            return false;
        }
        // ③ 校值：text 超长 / layer 非枚举
        EntityFieldRow tooLong;
        tooLong.entity_id = pid;
        tooLong.field_key = "public_mask";
        tooLong.value_text = std::string(4097, 'a');
        tooLong.layer = "mask";
        if (fields.UpsertEntityField(tooLong)) {
            log::Error("Fields 自检：text 超长（4097）未被拒");
            return false;
        }
        EntityFieldRow badLayer;
        badLayer.entity_id = pid;
        badLayer.field_key = "public_mask";
        badLayer.value_text = "ok";
        badLayer.layer = "custom";
        if (fields.UpsertEntityField(badLayer)) {
            log::Error("Fields 自检：layer 非枚举值 'custom' 未被拒");
            return false;
        }
        // ③ 校值：number
        FieldDefRow numDef;
        numDef.scope = "entity";
        numDef.entity_kind = "person";
        numDef.field_key = "power_rank";
        numDef.value_type = "number";
        numDef.created_by = "check";
        numDef.status = "CANON";
        if (!fields.UpsertFieldDef(numDef)) {
            log::Error("Fields 自检：登记 number 字段失败");
            return false;
        }
        EntityFieldRow num;
        num.entity_id = pid;
        num.field_key = "power_rank";
        num.value_text = "第七层";
        num.layer = "global";
        if (fields.UpsertEntityField(num)) {
            log::Error("Fields 自检：number 非数字未被拒");
            return false;
        }
        num.value_text = "7.5";
        if (!fields.UpsertEntityField(num)) {
            log::Error("Fields 自检：number 合法值 '7.5' 被拒");
            return false;
        }
        // ③ 校值：json（种子 power_profile）裸标量被拒、数组通过
        EntityFieldRow rawJson;
        rawJson.entity_id = pid;
        rawJson.field_key = "power_profile";
        rawJson.value_json = "123";
        rawJson.layer = "global";
        if (fields.UpsertEntityField(rawJson)) {
            log::Error("Fields 自检：json 裸标量未被拒");
            return false;
        }
        rawJson.value_json = "[{\"k\":1}]";
        if (!fields.UpsertEntityField(rawJson)) {
            log::Error("Fields 自检：json 数组被拒");
            return false;
        }
        // ③ 校值：enum
        FieldDefRow enumDef;
        enumDef.scope = "entity";
        enumDef.entity_kind = "person";
        enumDef.field_key = "mood_tag";
        enumDef.value_type = "enum";
        enumDef.enum_json = "[\"calm\",\"angry\"]";
        enumDef.created_by = "check";
        enumDef.status = "CANON";
        if (!fields.UpsertFieldDef(enumDef)) {
            log::Error("Fields 自检：登记 enum 字段失败");
            return false;
        }
        EntityFieldRow mood;
        mood.entity_id = pid;
        mood.field_key = "mood_tag";
        mood.value_text = "weird";
        mood.layer = "global";
        if (fields.UpsertEntityField(mood)) {
            log::Error("Fields 自检：enum 未命中未被拒");
            return false;
        }
        mood.value_text = "calm";
        if (!fields.UpsertEntityField(mood)) {
            log::Error("Fields 自检：enum 命中值 'calm' 被拒");
            return false;
        }
        // ④ `08` §2.5：含引号与换行的 value_text 必须产出**合法 JSON**
        EntityFieldRow tricky;
        tricky.entity_id = pid;
        tricky.field_key = "public_mask";
        tricky.value_text = "他说\"你好\"\n第二行";
        tricky.layer = "mask";
        if (!fields.UpsertEntityField(tricky)) {
            log::Error("Fields 自检：含引号/换行的 text 写入失败");
            return false;
        }
        const auto trickyJson = fields.EntityFieldsJson(pid, 0);
        yyjson_doc* jd =
            trickyJson ? yyjson_read(trickyJson->c_str(), trickyJson->size(), 0) : nullptr;
        if (jd == nullptr) {
            log::Error("Fields 自检：EntityFieldsJson 不是合法 JSON（`08` §2.5 未修）");
            return false;
        }
        yyjson_doc_free(jd);
    }

    // —— `08` §2.3 硬上限（放最后：会把 field_defs 塞满到 500）——
    {
        int added = 0;
        for (int i = 0; i < 700; ++i) {
            FieldDefRow fill;
            fill.scope = "custom";
            fill.field_key = fmt::format("fill_{}", i);
            fill.value_type = "text";
            fill.created_by = "check";
            if (!fields.UpsertFieldDef(fill)) {
                break; // 到上限
            }
            ++added;
        }
        if (added <= 0) {
            log::Error("Fields 自检：500 上限测试没能插入任何键（上限事先已满？）");
            return false;
        }
        FieldDefRow over;
        over.scope = "custom";
        over.field_key = "over_limit_key";
        over.value_type = "text";
        over.created_by = "check";
        auto overRes = fields.UpsertFieldDef(over);
        if (overRes) {
            log::Error("Fields 自检：超过 {} 条后新增仍成功（硬上限未生效）", kMaxFieldDefs);
            return false;
        }
        if (overRes.error().message.find("上限") == std::string::npos) {
            log::Error("Fields 自检：超限错误应含「上限」，got {}", overRes.error().message);
            return false;
        }
        // 满额时「更新既有键」不受上限约束
        FieldDefRow upd;
        upd.scope = "entity";
        upd.entity_kind = "person";
        upd.field_key = "public_mask";
        upd.value_type = "text";
        upd.is_system = 1;
        if (!fields.UpsertFieldDef(upd)) {
            log::Error("Fields 自检：满额时更新既有键被误拒（上限应只挡新增）");
            return false;
        }
        auto cnt = mem.Prepare("SELECT COUNT(*) FROM field_defs");
        if (!cnt || !cnt->Step() || cnt->ColumnInt(0) != kMaxFieldDefs) {
            log::Error("Fields 自检：字段定义最终条数应为 {}", kMaxFieldDefs);
            return false;
        }
    }

    log::Info("NovelFields 自检通过（字段定义/身份层/分章宇宙/自定义扩展 + 门禁：归一化/别名/"
              "未登记拒绝/按类型校值/layer 枚举/JSON 转义 + {} 条硬上限）",
              kMaxFieldDefs);
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
