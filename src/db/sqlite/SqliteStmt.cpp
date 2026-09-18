#include "db/sqlite/SqliteStmt.h"

#include <sqlite3.h>

namespace shine::db::sqlite {
namespace {

[[nodiscard]] SqliteError MakeErr(sqlite3* db, int code, const char* fallback) {
    const char* msg = db ? sqlite3_errmsg(db) : nullptr;
    return SqliteError{.code = code, .message = msg ? std::string{msg} : std::string{fallback ? fallback : "sqlite error"}};
}

} // namespace

Statement::Statement(sqlite3* db, sqlite3_stmt* stmt) noexcept : db_(db), stmt_(stmt) {}

Statement::~Statement() {
    FinalizeNoThrow();
}

Statement::Statement(Statement&& other) noexcept : db_(other.db_), stmt_(other.stmt_) {
    other.db_ = nullptr;
    other.stmt_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        FinalizeNoThrow();
        db_ = other.db_;
        stmt_ = other.stmt_;
        other.db_ = nullptr;
        other.stmt_ = nullptr;
    }
    return *this;
}

void Statement::FinalizeNoThrow() noexcept {
    if (stmt_) {
        sqlite3_finalize(stmt_);
        stmt_ = nullptr;
    }
    db_ = nullptr;
}

std::expected<void, SqliteError> Statement::BindInt(int idx, std::int64_t v) {
    if (!stmt_) {
        return std::unexpected(SqliteError{.code = SQLITE_MISUSE, .message = "语句无效"});
    }
    const int rc = sqlite3_bind_int64(stmt_, idx, v);
    if (rc != SQLITE_OK) {
        return std::unexpected(MakeErr(db_, rc, "bind int"));
    }
    return {};
}

std::expected<void, SqliteError> Statement::BindDouble(int idx, double v) {
    if (!stmt_) {
        return std::unexpected(SqliteError{.code = SQLITE_MISUSE, .message = "语句无效"});
    }
    const int rc = sqlite3_bind_double(stmt_, idx, v);
    if (rc != SQLITE_OK) {
        return std::unexpected(MakeErr(db_, rc, "bind double"));
    }
    return {};
}

std::expected<void, SqliteError> Statement::BindText(int idx, std::string_view v) {
    if (!stmt_) {
        return std::unexpected(SqliteError{.code = SQLITE_MISUSE, .message = "语句无效"});
    }
    const int rc = sqlite3_bind_text(stmt_, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        return std::unexpected(MakeErr(db_, rc, "bind text"));
    }
    return {};
}

std::expected<void, SqliteError> Statement::BindBlob(int idx, std::string_view v) {
    if (!stmt_) {
        return std::unexpected(SqliteError{.code = SQLITE_MISUSE, .message = "语句无效"});
    }
    const int rc = sqlite3_bind_blob(stmt_, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        return std::unexpected(MakeErr(db_, rc, "bind blob"));
    }
    return {};
}

std::expected<void, SqliteError> Statement::BindNull(int idx) {
    if (!stmt_) {
        return std::unexpected(SqliteError{.code = SQLITE_MISUSE, .message = "语句无效"});
    }
    const int rc = sqlite3_bind_null(stmt_, idx);
    if (rc != SQLITE_OK) {
        return std::unexpected(MakeErr(db_, rc, "bind null"));
    }
    return {};
}

std::expected<void, SqliteError> Statement::Reset() {
    if (!stmt_) {
        return std::unexpected(SqliteError{.code = SQLITE_MISUSE, .message = "语句无效"});
    }
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
    return {};
}

std::expected<StepResult, SqliteError> Statement::Step() {
    if (!stmt_) {
        return std::unexpected(SqliteError{.code = SQLITE_MISUSE, .message = "语句无效"});
    }
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        return StepResult::Row;
    }
    if (rc == SQLITE_DONE) {
        return StepResult::Done;
    }
    return std::unexpected(MakeErr(db_, rc, "step"));
}

int Statement::ColumnCount() const noexcept {
    return stmt_ ? sqlite3_column_count(stmt_) : 0;
}

std::int64_t Statement::ColumnInt(int col) const noexcept {
    return stmt_ ? sqlite3_column_int64(stmt_, col) : 0;
}

double Statement::ColumnDouble(int col) const noexcept {
    return stmt_ ? sqlite3_column_double(stmt_, col) : 0.0;
}

std::string Statement::ColumnText(int col) const {
    if (!stmt_) {
        return {};
    }
    const auto* p = sqlite3_column_text(stmt_, col);
    const int n = sqlite3_column_bytes(stmt_, col);
    if (!p || n <= 0) {
        return {};
    }
    return std::string{reinterpret_cast<const char*>(p), static_cast<std::size_t>(n)};
}

bool Statement::ColumnIsNull(int col) const noexcept {
    return stmt_ ? sqlite3_column_type(stmt_, col) == SQLITE_NULL : true;
}

} // namespace shine::db::sqlite
