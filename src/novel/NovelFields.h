#pragma once
// 动态字段：AI/Agent 自行扩展设定维度，不写死小说体系
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "novel/NovelDb.h"
#include "novel/NovelTypes.h"

namespace shine::novelcore {

// 字段定义（可被 FieldAgent / AgentMetaAgent 扩展）
struct FieldDefRow {
    RowId id = 0;
    std::string scope;      // entity|world|chapter|agent|custom
    std::string entity_kind; // 仅 scope=entity 时约束，空=不限
    std::string field_key;  // 稳定键，如 multiverse_layer / true_identity
    std::string title;      // 展示名
    std::string value_type; // text|number|json|enum
    std::string enum_json;  // value_type=enum 时的可选值 ["a","b"]
    std::string description;
    std::string created_by; // agent id
    int is_system = 0;      // 1=系统最小集，Agent 可读不可删
    std::int64_t updated = 0;
};

// 挂在实体上的字段值（支持分章 / 分身份层）
struct EntityFieldRow {
    RowId id = 0;
    RowId entity_id = 0;
    std::string field_key;
    std::string value_text;
    std::string value_json = "null";
    RowId chapter_scope = 0; // 0=全局；>0=该章起可见
    RowId chapter_to = 0;    // 0=至今
    std::string layer;        // global|public|mask|true|private|custom…
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
