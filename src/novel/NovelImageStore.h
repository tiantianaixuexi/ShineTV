#pragma once
// P9.2–P9.4：出图任务队列、generated_images 落盘、简单 checklist
#include "db/sqlite/SqliteDb.h"
#include "novel/NovelDb.h"
#include "novel/NovelImageGen.h"
#include "novel/NovelTypes.h"

namespace shine::novelcore {

struct GeneratedImageRow {
    RowId id = 0;
    std::string job_id;
    std::string backend;
    std::string model;
    std::string prompt;
    std::string negative;
    int width = 0;
    int height = 0;
    int steps = 0;
    std::string rel_path; // 相对工程根
    std::string source_kind; // asset|shot|entity|manual
    RowId source_id = 0;
    RowId chapter_id = 0;
    RowId scene_id = 0;
    std::string status = "QUEUED"; // QUEUED|RUNNING|PROPOSED|CANON|NON_CANON|FAILED|DISCARDED
    std::string error;
    std::string raw_meta = "{}";
    std::string checklist_json = "{}";
    std::int64_t created = 0;
    std::int64_t updated = 0;
};

struct ImageJobInput {
    std::string prompt; // 非空则直接用；空则从 asset/shot Assemble
    std::string negative;
    RowId asset_id = 0;
    RowId shot_id = 0;
    RowId entity_id = 0; // 无 asset 时按 entity 找资产
    RowId chapter_id = 0;
    RowId scene_id = 0;
    int width = 0;  // 0 = Settings
    int height = 0;
    int steps = 0;
    std::string model;
};

struct ImageChecklistItem {
    std::string key;
    bool ok = true;
    std::string note;
};

// worker：assemble → queue 行 → Backend.Generate → PROPOSED/FAILED + checklist
[[nodiscard]] std::expected<GeneratedImageRow, ImageGenError>
RunImageJob(db::sqlite::Database& db, const ImageJobInput& in);

// 查询最近生成图
[[nodiscard]] std::expected<std::vector<GeneratedImageRow>, DbError>
ListGeneratedImages(db::sqlite::Database& db, int limit = 50);

[[nodiscard]] std::expected<GeneratedImageRow, DbError>
GetGeneratedImage(db::sqlite::Database& db, RowId id);

// status: CANON | NON_CANON | DISCARDED | PROPOSED
[[nodiscard]] std::expected<void, DbError>
SetGeneratedImageStatus(db::sqlite::Database& db, RowId id, std::string_view status);

// 工程目录：db 路径的父目录
[[nodiscard]] std::filesystem::path ProjectDirOfDb(const std::filesystem::path& dbPath);

// 工程相对路径 → 绝对路径
[[nodiscard]] std::filesystem::path
ResolveImageAbsPath(const std::filesystem::path& projectDir, std::string_view relPath);

// 简单启发式 checklist（P9.4）
[[nodiscard]] std::vector<ImageChecklistItem>
EvaluateImageChecklist(const GeneratedImageRow& row, bool fileExists);

[[nodiscard]] std::string ChecklistToJson(const std::vector<ImageChecklistItem>& items);

// 离线自检：内存库 + mock 跑通队列 → PROPOSED → CANON
[[nodiscard]] bool RunImageQueueSelfCheck();

} // namespace shine::novelcore
