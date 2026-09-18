#pragma once
// 记忆读写：chapter_summary / short_term 等（表结构见 NovelDb v3 memories）
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "novel/NovelDb.h"
#include "novel/NovelTypes.h"

namespace shine::novelcore {

struct MemoryRow {
    RowId id = 0;
    std::string kind = "chapter_summary"; // short_term|long_term|semantic|episodic|chapter_summary|…
    RowId entity_id = 0;
    RowId chapter_id = 0;
    std::string content;
    std::string summary;
    std::int64_t created = 0;
};

class NovelMemory {
public:
    explicit NovelMemory(db::sqlite::Database& db) noexcept : db_(&db) {}

    [[nodiscard]] std::expected<RowId, DbError> Write(const MemoryRow& row);
    // 最近 N 条 chapter_summary（按 chapter_id 降序）
    [[nodiscard]] std::expected<std::vector<MemoryRow>, DbError>
    RecentChapterSummaries(int limit = 3) const;
    // 按 kind 列表
    [[nodiscard]] std::expected<std::vector<MemoryRow>, DbError>
    ListByKind(std::string_view kind, int limit = 50) const;

private:
    db::sqlite::Database* db_;
};

} // namespace shine::novelcore
