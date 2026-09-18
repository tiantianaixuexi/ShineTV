#pragma once
// Director 状态机 + 四角色（契约 S2.5 / P5-PLAN）
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "agent/ContextBuilder.h"
#include "agent/ToolRegistry.h"
#include "db/sqlite/SqliteDb.h"
#include "novel/NovelDb.h"

namespace shine::agent {

enum class Phase {
    Idle,
    Analyze,
    Plan,
    Retrieve,
    Write,
    Review,
    Revision,
    Extract,
    Save,
    Done,
    Failed,
};

[[nodiscard]] std::string_view PhaseName(Phase p) noexcept;

struct GenerateChapterRequest {
    std::int64_t chapter_id = 0;
    std::string user_hint;
    int max_revisions = 3;
};

struct GenerateChapterProgress {
    Phase phase = Phase::Idle;
    std::string text_delta;
    int percent = 0;
    std::string note;
};

struct GenerateChapterResult {
    std::int64_t chapter_id = 0;
    std::string title;
    std::string body;
    std::string plan_json;
    std::string critic_json;
    int revisions = 0;
    std::vector<std::string> callLog;
};

struct AgentError {
    std::string code;
    std::string message;
};

// LLM 调用抽象（可 mock）。instructions+user → 完整响应 JSON（含 output_text）
using LlmCallFn = std::function<std::expected<std::string, AgentError>(
    std::string_view instructions, std::string_view user)>;

// 可流式 Writer：on_delta 收增量；返回完整正文
using LlmStreamFn = std::function<std::expected<std::string, AgentError>(
    std::string_view instructions, std::string_view user,
    const std::function<void(std::string_view)>& on_delta)>;

class NovelDirector {
public:
    NovelDirector(db::sqlite::Database& db, LlmCallFn call, LlmStreamFn stream = nullptr);

    // 同步跑完管线（worker 线程）
    [[nodiscard]] std::expected<GenerateChapterResult, AgentError>
    GenerateChapter(const GenerateChapterRequest& req,
                    const std::function<void(const GenerateChapterProgress&)>& on_progress = {});

    // prompts 目录（空 = 可执行文件旁 prompts/ 或内置默认）
    void SetPromptsDir(const std::filesystem::path& dir) noexcept { promptsDir_ = dir; }

    [[nodiscard]] static bool RunSelfCheck();

private:
    db::sqlite::Database* db_;
    LlmCallFn call_;
    LlmStreamFn stream_;
    std::filesystem::path promptsDir_;

    [[nodiscard]] std::string LoadPrompt(std::string_view name) const;
    [[nodiscard]] std::expected<std::string, AgentError>
    CallLlm(std::string_view role, std::string_view user) const;
};

// 从模型 JSON 里抠 output_text / 或直接当正文
[[nodiscard]] std::string ExtractOutputText(std::string_view responseJson);

// 内置默认 prompt（prompts/*.md 缺失时）
[[nodiscard]] std::string DefaultPrompt(std::string_view role);

} // namespace shine::agent
