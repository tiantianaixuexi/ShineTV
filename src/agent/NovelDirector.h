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
    // S8（`07` §2.4）：章级快照落盘目录（UTF-8）。空 → 尝试从 `NovelDb` 单例推工程根；
    // 仍为空 → G3 不过、**状态回写被拒绝**（不变式 I11）。
    std::string snapshot_dir;
    // S12：工程根（UTF-8）。`work/ch<NNN>/12_state_diff.json` 与降级账都按它落盘；
    // 空 → 退回 `NovelDb` 单例的父目录（调用方知道工程根时应显式传，别靠单例猜）。
    std::string project_dir;
    // S9（`07` §2.5）：manual（默认，写 PROPOSED）| auto（门禁 G1–G5 全满足才写 CANON）
    std::string canon_mode = "manual";
    // S12（`03` §2.6）：机器校验失败**回到 EXTRACT 重做**的次数上限（`max_validate_retry`）
    int max_validate_retries = 2;
    // S19（`03` §2.7 P1/P2/P5）：断点续跑 —— true 时先读 `work/ch<NNN>/` 的阶段产物，
    // **哈希一致就跳过该阶段**（省掉一次 LLM）。默认 false（保持"每次都全跑"的既有行为，
    // 自检与 UI 的默认路径不受影响）。
    bool resume = false;
    // S9（`09` §2.3）：网络重试与退避（在 `CallLlm` 内生效；参数由 `novelcore::RunLimits` 下发）
    int network_retries = 4;
    int backoff_base_ms = 2000;
    int rate_limit_backoff_ms = 10000;
    int min_request_interval_ms = 200;
};

// S9（`09` §2.4）：逐次 LLM 调用记录 —— 单章成本账（`cost_report.json`）的数据来源
struct LlmCallRecord {
    std::string stage; // PLAN / WRITE / REVIEW / REVISION / EXTRACT
    std::string role;  // planner / writer / critic / extractor
    std::string tier;  // high / mid / low（`09` §2.4 的档位）
    int attempt = 0;   // 含重试：1 = 首次
    bool ok = false;
    bool rate_limited = false;
    std::int64_t ms = 0;
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
    // S8：状态回写的结果（`false` + `commit_note` 非空 = 被门禁拒绝，原因可读）
    bool state_committed = false;
    bool state_skipped = false; // 幂等命中（同一 diff 已提交过）
    std::string commit_note;
    // —— S9：成本与停止条件的观测（`09` §2.3 / §2.4）——
    std::vector<std::string> stages;  // 本报告走过的 Phase（写 `_manifest.json`）
    std::vector<LlmCallRecord> calls; // 逐次调用（含重试）
    int llm_calls = 0;                // 含重试的调用次数（单章硬上限 40）
    int high_tier_calls = 0;          // 「高」档调用次数（writer / critic，上限 8）
    int images = 0;                   // 本管线不出图 → 恒 0（V0 / 场景图未接入）
    bool review_passed = false;       // 最终评审结论（G1）
    bool semantic_only = false;       // 仅语义判断且 FAIL（`06` §2.7 M2）
    int contract_failures = 0;        // 契约校验失败（含 1 次重试后）
    int missing_entity_refs = 0;      // `code=contract` 的缺失引用处数（`09` §2.2 S9）
    // S11：G2 的 K01–K29 报告里**最终仍未通过**的 check_id（重复条目 = 失败次数，供 `09` §2.2 S1 计数）
    std::vector<std::string> failed_check_ids;
    // S12：因机器校验未过而重做 EXTRACT 的次数（`03` §2.6 的 `max_validate_retry`）
    int validation_retries = 0;
};

struct AgentError {
    std::string code;
    std::string message;
};

// S16（`09` §2.4 模型分层）：调用角色 —— 决定**用哪个模型**（planner/writer/critic 三档）
// 与**记账档位**（`cost_report.json` 的 high/mid/low）。调用方（UI）按它解析模型名；
// `novel`/`agent` 层不知道也不需要知道模型名。
enum class LlmRole { Planner, Writer, Critic, Extractor };
[[nodiscard]] std::string_view LlmRoleName(LlmRole role) noexcept; // "planner"|"writer"|"critic"|"extractor"

// LLM 调用抽象（可 mock）。role+instructions+user → 完整响应 JSON（含 output_text）
using LlmCallFn = std::function<std::expected<std::string, AgentError>(
    LlmRole role, std::string_view instructions, std::string_view user)>;

// 可流式 Writer：on_delta 收增量；返回完整正文
using LlmStreamFn = std::function<std::expected<std::string, AgentError>(
    LlmRole role, std::string_view instructions, std::string_view user,
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
    // S9：同 Provider 请求间隔（`09` §2.3 ≥ 200ms）—— 上次调用结束的单调毫秒
    std::int64_t last_call_ms_ = 0;

    [[nodiscard]] std::string LoadPrompt(std::string_view name) const;
    // S9：带重试/退避（`09` §2.3）并把逐次调用记进 `out`
    [[nodiscard]] std::expected<std::string, AgentError>
    CallLlm(std::string_view role, std::string_view user, std::string_view stage,
            const GenerateChapterRequest& req, std::vector<LlmCallRecord>& out);
};

// 从模型 JSON 里抠 output_text / 或直接当正文
[[nodiscard]] std::string ExtractOutputText(std::string_view responseJson);

// 内置默认 prompt（prompts/*.md 缺失时）
[[nodiscard]] std::string DefaultPrompt(std::string_view role);

} // namespace shine::agent
