#pragma once
// novel.db 打开与 schema v3 迁移
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

#include "db/sqlite/SqliteDb.h"
#include "db/sqlite/SqliteError.h"

namespace shine::novelcore {

using DbError = db::sqlite::SqliteError;

// 当前打开的工程库（一书一库）。UI 线程只持有句柄；读写在 worker。
class NovelDb {
public:
    static NovelDb& Instance() noexcept;

    // 打开并迁移到 schema v3。路径空则关闭。
    [[nodiscard]] std::expected<void, DbError> Open(const std::filesystem::path& dbPath);
    void Close() noexcept;
    [[nodiscard]] bool isOpen() const noexcept { return db_.isOpen(); }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    [[nodiscard]] db::sqlite::Database& raw() noexcept { return db_; }

    [[nodiscard]] int schemaVersion() const noexcept { return schemaVersion_; }

    // 离线自检：内存库建全 schema + 最小 CRUD
    [[nodiscard]] static bool RunSchemaSelfCheck();

private:
    NovelDb() = default;

    [[nodiscard]] std::expected<void, DbError> Migrate();
    [[nodiscard]] std::expected<void, DbError> ExecAll(std::string_view sql);

    db::sqlite::Database db_;
    std::filesystem::path path_;
    int schemaVersion_ = 0;
};

} // namespace shine::novelcore
