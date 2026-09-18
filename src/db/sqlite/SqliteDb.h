#pragma once
// 本地嵌入式库封装。路径用 std::filesystem::path（中文安全）。
// 一书一库：业务为每本书 Open 一个 Database 实例。
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string_view>

#include "db/sqlite/SqliteError.h"
#include "db/sqlite/SqliteStmt.h"

struct sqlite3;

namespace shine::db::sqlite {

struct OpenOptions {
    std::filesystem::path path; // 空路径 + memory=true → ":memory:"
    bool readOnly = false;
    bool create = true;
    bool memory = false;
};

class Database {
public:
    Database() = default;
    ~Database();
    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    [[nodiscard]] std::expected<void, SqliteError> Open(const OpenOptions& opt);
    void Close() noexcept;
    [[nodiscard]] bool isOpen() const noexcept { return db_ != nullptr; }

    // 执行无结果集 SQL（可多语句，分号分隔）
    [[nodiscard]] std::expected<void, SqliteError> Exec(std::string_view sql);

    [[nodiscard]] std::expected<Statement, SqliteError> Prepare(std::string_view sql);

    [[nodiscard]] std::expected<void, SqliteError> Begin();
    [[nodiscard]] std::expected<void, SqliteError> Commit();
    [[nodiscard]] std::expected<void, SqliteError> Rollback();

    // 最近一次插入 rowid
    [[nodiscard]] std::int64_t LastInsertRowId() const noexcept;

private:
    sqlite3* db_ = nullptr;
};

} // namespace shine::db::sqlite
