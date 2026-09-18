#pragma once
// SQLite 预处理语句。一个 Statement 绑定一个 Database；Database 关闭前必须先析构全部 Statement。
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "db/sqlite/SqliteError.h"

struct sqlite3;
struct sqlite3_stmt;

namespace shine::db::sqlite {

enum class StepResult { Row, Done };

class Statement {
public:
    Statement() = default;
    Statement(sqlite3* db, sqlite3_stmt* stmt) noexcept;
    ~Statement();
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    [[nodiscard]] bool valid() const noexcept { return stmt_ != nullptr; }

    // 参数下标从 1 开始（与 SQLite 一致）
    [[nodiscard]] std::expected<void, SqliteError> BindInt(int idx, std::int64_t v);
    [[nodiscard]] std::expected<void, SqliteError> BindDouble(int idx, double v);
    [[nodiscard]] std::expected<void, SqliteError> BindText(int idx, std::string_view v);
    [[nodiscard]] std::expected<void, SqliteError> BindBlob(int idx, std::string_view v);
    [[nodiscard]] std::expected<void, SqliteError> BindNull(int idx);
    [[nodiscard]] std::expected<void, SqliteError> Reset();

    [[nodiscard]] std::expected<StepResult, SqliteError> Step();

    [[nodiscard]] int ColumnCount() const noexcept;
    [[nodiscard]] std::int64_t ColumnInt(int col) const noexcept;
    [[nodiscard]] double ColumnDouble(int col) const noexcept;
    [[nodiscard]] std::string ColumnText(int col) const;
    [[nodiscard]] bool ColumnIsNull(int col) const noexcept;

private:
    void FinalizeNoThrow() noexcept;

    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

} // namespace shine::db::sqlite
