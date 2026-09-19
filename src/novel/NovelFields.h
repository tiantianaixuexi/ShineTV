#pragma once
// 动态字段：AI/Agent 自行扩展设定维度，不写死小说体系
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "novel/NovelDb.h"
#include "novel/NovelTypes.h"

namespace shine::novelcore {

// 字段定义（可被 FieldAgent / AgentMetaAgent 扩展）——「契约」，必须登记、有类型、可枚举
struct FieldDefRow {
    RowId id = 0;
    std::string scope;      // entity|world|chapter|agent|custom
    std::string entity_kind; // 仅 scope=entity 时约束，空=不限
    std::string field_key;  // 稳定键，如 multiverse_layer / true_identity（归一后 ^[a-z][a-z0-9_]{1,39}$）
    std::string title;      // 展示名
    std::string value_type; // text|number|json|enum
    std::string enum_json;  // value_type=enum 时的可选值 ["a","b"]
    std::string description;
    std::string created_by; // agent id
    int is_system = 0;      // 1=系统最小集，Agent 可读不可删
    // v8（S2b）：PROPOSED|CANON。空 = 由 UpsertFieldDef 决定（系统种子→CANON，AI 提案→PROPOSED）。
    std::string status;
    std::int64_t updated = 0;
};

// 挂在实体上的字段值（支持分章 / 分身份层）——「值」，AI 可自由填写，但必须在已登记键上
struct EntityFieldRow {
    RowId id = 0;
    RowId entity_id = 0;
    std::string field_key;
    std::string value_text;
    std::string value_json = "null";
    RowId chapter_scope = 0; // 0=全局；>0=该章起可见
    RowId chapter_to = 0;    // 0=至今
    std::string layer;       // 枚举：global|public|mask|true|private（`08` §2.6，写入门禁校验）
    std::string note;
    std::string created_by;
    std::int64_t updated = 0;
};

class NovelFields {
public:
    explicit NovelFields(db::sqlite::Database& db) noexcept : db_(&db) {}

    // —— 字段定义 ——
    [[nodiscard]] std::expected<RowId, DbError> UpsertFieldDef(const FieldDefRow& row);
    [[nodiscard]] std::expected<FieldDefRow, DbError> GetFieldDef(RowId id) const;
    [[nodiscard]] std::expected<std::vector<FieldDefRow>, DbError>
    ListFieldDefs(std::string_view scope = {}, std::string_view entityKind = {}) const;
    [[nodiscard]] std::expected<void, DbError> DeleteFieldDef(RowId id, bool force = false);

    // —— S2b 字段门禁（规格 `Doc/小说系统/08` §2.2 / §2.4 / §2.6）——
    // 键归一化：trim → lower → 空白/连字符转下划线 → 折叠连续下划线 → 去首尾下划线
    [[nodiscard]] static std::string NormalizeKey(std::string_view raw);
    // ^[a-z][a-z0-9_]{1,39}$（长度 2..40，首字符小写字母）
    [[nodiscard]] static bool IsValidKey(std::string_view key);
    // global|public|mask|true|private
    [[nodiscard]] static bool IsValidLayer(std::string_view layer);
    static constexpr std::string_view kLayerEnum = "global|public|mask|true|private";
    static constexpr std::string_view kValueTypeEnum = "text|number|json|enum";
    // `08` §2.3 硬上限：单个工程 field_defs 条数。只约束「新增键」，更新既有键不受限。
    static constexpr int kMaxFieldDefs = 500;

    // 字段表的**唯一定义来源**：建 field_defs / entity_fields / field_aliases + 旧库 ALTER 回填。
    // 规格 `08` §2.3。`NovelDb::Migrate`、`AgentKit::EnsureSchemaAndSeed`、自检都调这个 ——
    // 原先三处各写一份 DDL，漏更新一处就静默丢种子（S2b 已踩）。幂等，可重复调用。
    [[nodiscard]] static std::expected<void, DbError> EnsureSchema(db::sqlite::Database& db);

    // 别名：读取与写入都先过别名表（`08` §2.4）。命中 → 用规范键。
    [[nodiscard]] std::expected<void, DbError> UpsertFieldAlias(std::string_view alias,
                                                                std::string_view canonicalKey,
                                                                std::string_view note = {});
    [[nodiscard]] std::expected<std::string, DbError> ResolveAlias(std::string_view key) const;
    [[nodiscard]] std::expected<std::vector<std::pair<std::string, std::string>>, DbError>
    ListFieldAliases() const;

    // —— 实体字段值 ——
    [[nodiscard]] std::expected<RowId, DbError> UpsertEntityField(const EntityFieldRow& row);
    [[nodiscard]] std::expected<std::vector<EntityFieldRow>, DbError>
    ListEntityFields(RowId entityId, RowId chapterId = 0, std::string_view layer = {}) const;
    [[nodiscard]] std::expected<void, DbError> DeleteEntityField(RowId id);

    // 按 key 读（chapter 过滤后优先取匹配层）
    [[nodiscard]] std::expected<EntityFieldRow, DbError>
    GetEntityFieldByKey(RowId entityId, std::string_view key, RowId chapterId = 0,
                        std::string_view layer = {}) const;

    // —— 世界级动态字段（挂在 meta / field_values 简化：entity_id=0）——
    [[nodiscard]] std::expected<RowId, DbError> UpsertWorldField(std::string_view key,
                                                                 std::string_view valueText,
                                                                 std::string_view valueJson = {},
                                                                 std::string_view layer = "global",
                                                                 std::string_view createdBy = {});
    [[nodiscard]] std::expected<std::vector<EntityFieldRow>, DbError>
    ListWorldFields(RowId chapterId = 0) const;

    // 给 MCP / Context：实体的全部动态字段 JSON 文本
    [[nodiscard]] std::expected<std::string, DbError>
    EntityFieldsJson(RowId entityId, RowId chapterId = 0) const;

    // 种子：系统最小字段集（多宇宙/身份层等）
    static void SeedBuiltinFieldDefs(NovelFields& fields);

    [[nodiscard]] static bool RunSelfCheck();

private:
    db::sqlite::Database* db_;
};

} // namespace shine::novelcore
