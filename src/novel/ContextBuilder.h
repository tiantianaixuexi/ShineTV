#pragma once
// ContextBuilder：L1–L4 组装中文 prompt 附加段（契约 S2.4）
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "db/sqlite/SqliteDb.h"
#include "novel/NovelDb.h"
#include "novel/NovelTypes.h"

namespace shine::agent {

struct ContextBuildInput {
    std::int64_t chapter_id = 0;
    std::string task;
    std::optional<std::int64_t> pov_entity_id;
    // 字符预算（按 UTF-8 字节）；0 = 默认 12000
    std::size_t maxBytes = 0;
};

struct ContextBuildOutput {
    std::string text;
    std::vector<std::int64_t> used_entity_ids;
};

class ContextBuilder {
public:
    explicit ContextBuilder(db::sqlite::Database& db) noexcept;

    [[nodiscard]] std::expected<ContextBuildOutput, novelcore::DbError>
    Build(const ContextBuildInput& in) const;

    // 离线自检：样例库 → Build，检查段落齐全 + POV 秘密过滤
    [[nodiscard]] static bool RunSelfCheck();

private:
    db::sqlite::Database* db_;

    [[nodiscard]] std::string BuildPrefix(std::int64_t chapterId) const;
    [[nodiscard]] std::string BuildL2(std::int64_t chapterId) const;
    [[nodiscard]] std::string BuildL3(std::int64_t chapterId, std::int64_t povId,
                                      std::vector<std::int64_t>& used) const;
    [[nodiscard]] std::string BuildL4(std::int64_t povId, std::vector<std::int64_t>& used) const;
};

// UTF-8 安全截断：不超过 maxBytes，且不切断多字节序列
[[nodiscard]] std::string TruncateUtf8(std::string_view s, std::size_t maxBytes);

} // namespace shine::agent
