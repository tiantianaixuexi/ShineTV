#include "novel/NovelMemory.h"

#include "core/Log.h"
#include "util/Time.h"

#include <fmt/format.h>

namespace shine::novelcore {

std::expected<RowId, DbError> NovelMemory::Write(const MemoryRow& row) {
    if (row.content.empty() && row.summary.empty()) {
        return std::unexpected(DbError{0, "memory content/summary 全空"});
    }
    auto st = db_->Prepare(
        "INSERT INTO memories(kind,entity_id,chapter_id,content,summary,created)"
        " VALUES(?1,?2,?3,?4,?5,?6)");
    if (!st) return std::unexpected(st.error());
    (void)st->BindText(1, row.kind.empty() ? "short_term" : row.kind);
    (void)st->BindInt(2, row.entity_id);
    (void)st->BindInt(3, row.chapter_id);
    (void)st->BindText(4, row.content);
    (void)st->BindText(5, row.summary);
    (void)st->BindInt(6, row.created > 0 ? row.created : util::NowMillis() / 1000);
    if (auto s = st->Step(); !s) return std::unexpected(s.error());
    return db_->LastInsertRowId();
}

std::expected<std::vector<MemoryRow>, DbError>
NovelMemory::RecentChapterSummaries(int limit) const {
    auto st = db_->Prepare(
        "SELECT id,kind,entity_id,chapter_id,content,summary,created FROM memories"
        " WHERE kind='chapter_summary' ORDER BY chapter_id DESC, id DESC LIMIT ?1");
    if (!st) return std::unexpected(st.error());
    (void)st->BindInt(1, limit > 0 ? limit : 3);
    std::vector<MemoryRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        MemoryRow m;
        m.id = st->ColumnInt(0);
        m.kind = st->ColumnText(1);
        m.entity_id = st->ColumnInt(2);
        m.chapter_id = st->ColumnInt(3);
        m.content = st->ColumnText(4);
        m.summary = st->ColumnText(5);
        m.created = st->ColumnInt(6);
        out.push_back(std::move(m));
    }
    return out;
}

std::expected<std::vector<MemoryRow>, DbError>
NovelMemory::ListByKind(std::string_view kind, int limit) const {
    auto st = db_->Prepare(
        "SELECT id,kind,entity_id,chapter_id,content,summary,created FROM memories"
        " WHERE kind=?1 ORDER BY id DESC LIMIT ?2");
    if (!st) return std::unexpected(st.error());
    (void)st->BindText(1, kind);
    (void)st->BindInt(2, limit > 0 ? limit : 50);
    std::vector<MemoryRow> out;
    for (;;) {
        auto s = st->Step();
        if (!s) return std::unexpected(s.error());
        if (*s == db::sqlite::StepResult::Done) break;
        MemoryRow m;
        m.id = st->ColumnInt(0);
        m.kind = st->ColumnText(1);
        m.entity_id = st->ColumnInt(2);
        m.chapter_id = st->ColumnInt(3);
        m.content = st->ColumnText(4);
        m.summary = st->ColumnText(5);
        m.created = st->ColumnInt(6);
        out.push_back(std::move(m));
    }
    return out;
}

} // namespace shine::novelcore
