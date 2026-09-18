#include "db/sqlite/SqliteDb.h"

#include <sqlite3.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <string>

namespace shine::db::sqlite {
namespace {

[[nodiscard]] SqliteError MakeErr(sqlite3* db, int code, const char* fallback) {
    const char* msg = db ? sqlite3_errmsg(db) : nullptr;
    return SqliteError{.code = code, .message = msg ? std::string{msg} : std::string{fallback ? fallback : "sqlite error"}};
}

// Windows 路径：sqlite3_open_v2 要 UTF-8
[[nodiscard]] std::string PathToUtf8(const std::filesystem::path& p) {
#ifdef _WIN32
    const auto& w = p.native();
    if (w.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
#else
    return p.string();
#endif
}

} // namespace

Database::~Database() {
    Close();
}

Database::Database(Database&& other) noexcept : db_(other.db_) {
    other.db_ = nullptr;
}

Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        Close();
        db_ = other.db_;
        other.db_ = nullptr;
    }
    return *this;
}

void Database::Close() noexcept {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

std::expected<void, SqliteError> Database::Open(const OpenOptions& opt) {
    Close();

    std::string pathUtf8 = opt.memory ? std::string{":memory:"} : PathToUtf8(opt.path);
    if (pathUtf8.empty() && !opt.memory) {
        return std::unexpected(SqliteError{.code = SQLITE_MISUSE, .message = "数据库路径为空"});
    }

    int flags = SQLITE_OPEN_URI;
    if (opt.readOnly) {
        flags |= SQLITE_OPEN_READONLY;
    } else {
        flags |= SQLITE_OPEN_READWRITE;
        if (opt.create) {
            flags |= SQLITE_OPEN_CREATE;
        }
    }

    sqlite3* raw = nullptr;
    const int rc = sqlite3_open_v2(pathUtf8.c_str(), &raw, flags, nullptr);
    if (rc != SQLITE_OK) {
        auto err = MakeErr(raw, rc, "open");
        if (raw) {
            sqlite3_close(raw);
        }
        return std::unexpected(std::move(err));
    }
    db_ = raw;
    // 忙等待：单进程内多连接写同一文件时更稳
    sqlite3_busy_timeout(db_, 3000);
    return {};
}

std::expected<void, SqliteError> Database::Exec(std::string_view sql) {
    if (!db_) {
        return std::unexpected(SqliteError{.code = SQLITE_MISUSE, .message = "数据库未打开"});
    }
    char* errMsg = nullptr;
    const std::string sqlStr{sql};
    const int rc = sqlite3_exec(db_, sqlStr.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        SqliteError err{.code = rc, .message = errMsg ? std::string{errMsg} : std::string{"exec failed"}};
        if (errMsg) {
            sqlite3_free(errMsg);
        }
        return std::unexpected(std::move(err));
    }
    return {};
}

std::expected<Statement, SqliteError> Database::Prepare(std::string_view sql) {
    if (!db_) {
        return std::unexpected(SqliteError{.code = SQLITE_MISUSE, .message = "数据库未打开"});
    }
    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (rc != SQLITE_OK || !stmt) {
        return std::unexpected(MakeErr(db_, rc, "prepare"));
    }
    return Statement(db_, stmt);
}

std::expected<void, SqliteError> Database::Begin() {
    return Exec("BEGIN");
}

std::expected<void, SqliteError> Database::Commit() {
    return Exec("COMMIT");
}

std::expected<void, SqliteError> Database::Rollback() {
    return Exec("ROLLBACK");
}

std::int64_t Database::LastInsertRowId() const noexcept {
    return db_ ? sqlite3_last_insert_rowid(db_) : 0;
}

} // namespace shine::db::sqlite
