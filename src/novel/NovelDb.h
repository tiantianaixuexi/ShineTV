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

    // 把**规范 schema**（v3–v8 的表/索引）建到**任意**库上 —— **DDL 的唯一来源**就是本文件里的
    // `kSchemaV*` 常量 + `NovelFields::EnsureSchema`。
    // 用途：各处自检夹具（内存库）。原先 9 个夹具各自手抄一份建表 SQL（合计 121 条 CREATE TABLE），
    // 已经抄漏过一次：夹具里的 `writing_style` 只有 4 列（规范 12 列）、`entity_personas` 的
    // 关键字列 `values` 也在两处抄法不同。幂等（全部 IF NOT EXISTS）；**不做**版本迁移与 ALTER
    // （那是 `Migrate()` 的事）。
    [[nodiscard]] static std::expected<void, DbError> ApplyCanonicalSchema(db::sqlite::Database& db);

    // 把**任意库**升到当前目标 schema：建规范表（`ApplyCanonicalSchema`）+ 补旧库缺列
    // （`AddShotStateColumns` / `AddPromptArtifactColumns`）+ 字段表（`NovelFields::EnsureSchema`）。
    // **用途**：不走 `NovelDb` 单例、**自己开库**的入口（CLI / 自检夹具）—— 它们原先从不迁移，
    // 于是"新加的列"在旧库上永远补不出来（S26 实测：CLI 跑 V10 报
    // `table prompt_artifacts has no column named version`，而同一个库用 GUI 打开却是好的）。
    [[nodiscard]] static std::expected<void, DbError> EnsureSchemaUpToDate(db::sqlite::Database& db);

    // 只建 **agent 相关表**（`agent_defs`）：给 `AgentKit::EnsureSchemaAndSeed()` 这种**高频**入口用
    // （它在每次 MCP 工具分发前都会调一次，不能跑整套建表）。DDL 同样以 NovelDb.cpp 的常量为唯一来源。
    [[nodiscard]] static std::expected<void, DbError> EnsureAgentSchema(db::sqlite::Database& db);

    // v9（S13）：给旧库的 `shots` 补 `start_state_json` / `end_state_json` / `timeline_json`
    //（`12` §1.4 缺的就是这三列：K09 连续性、K24 Beat 时间轴的受检对象）。
    // 新库由 `kSchemaV4Visual` 的建表直接带这三列，本函数只服务旧库；**列已存在即忽略**。
    // **唯一来源**：`Migrate()` 与 `RunSchemaSelfCheck` 都调它（别再各抄一遍 ALTER）。
    static void AddShotStateColumns(db::sqlite::Database& db);

    // v10（S26）：给旧库的 `prompt_artifacts` 补 `version` / `generation_ref`
    //（`13` §2.7 PV3–PV5 的载体：多版本保留、版本自增、生成结果关联）。
    // 新库由 `kSchemaV9PromptArtifacts` 的建表直接带这两列；**列已存在即忽略**。
    // **唯一来源**：`Migrate()` 与 `RunSchemaSelfCheck` 都调它。
    static void AddPromptArtifactColumns(db::sqlite::Database& db);

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
