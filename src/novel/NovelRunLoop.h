#pragma once
// shine::novelcore —— **无人值守运行循环**（规格 `Doc/小说系统/09-无人值守运行规范.md`）
//
// 「连跑几百章不停」的主线。本模块收口 `09` 的六件事：
//   ① 运行模式 `manual` / `semi` / `auto`（§2.1）—— 每次运行写 `audit_logs(action='run_mode')`；
//      `auto` 不满足前置条件时**拒绝启动并给原因**（`07` §2.5 C5：禁止跳门禁）。
//   ② 停止条件 `S1–S12` 的**量化阈值**与判定（§2.2）—— `EvaluateStop` 是**纯函数**，
//      输入是 `ChapterObservation`，所以每条条件都能离线构造触发（自检即如此）。
//   ③ 重试退避与单章上限参数（§2.3 / §2.4）—— `RunLimits`；实际退避在 `NovelDirector::CallLlm`。
//   ④ 检查点（§2.5，默认每 10 章）：`checkpoint_ch<A>_<B>.md` + `08` §2.3 的
//      `PROPOSED`→`CANON` 四条件自动升格（`NovelFields::PromoteProposedFields`）。
//   ⑤ 断点续跑（§2.6）：已 `done` 的章**不重跑**；`work/ch<NNN>/_manifest.json` 记阶段与状态指纹。
//   ⑥ 可观测性（§2.7）：`cost_report.json` / `_manifest.json` / `checkpoint_*.md` / `stop_report.md`。
//
// ⚠️ 本步（S9-auto-run）**不做**：模型分层路由（09-7/09-8）、并发限流（09-11）、全书预算估算（09-12）。
// ⚠️ 粒度偏差（已在 `Plan/证据.md` 记明）：`03` 的 T1–T17 细阶段机在本仓尚不存在，`GenerateChapter`
//    只报 11 个粗阶段 → 本循环的**记账 / 续跑粒度是"章"**，`_manifest.json` 的"产物文件"列当前为空
//    （`03` §2.7 的 `01_outline.json` 那套阶段产物未落地）。续跑只保证"不从头重跑全书"。
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agent/NovelDirector.h"
#include "db/sqlite/SqliteDb.h"
#include "novel/NovelDb.h"
#include "novel/NovelFields.h"

namespace shine::novelcore {

// ———— `09` §2.1 运行模式 ————
enum class RunMode { Manual, Semi, Auto };
[[nodiscard]] std::string_view RunModeName(RunMode mode) noexcept;
[[nodiscard]] RunMode RunModeFromString(std::string_view text,
                                        RunMode fallback = RunMode::Manual) noexcept;

// ———— `09` §2.2 停止条件编号 ————
enum class StopCode : int { None = 0, S1, S2, S3, S4, S5, S6, S7, S8, S9, S10, S11, S12 };
[[nodiscard]] std::string_view StopCodeName(StopCode code) noexcept;
// 该条件的中文阈值原文（写 `stop_report.md`；`None` 返回空）
[[nodiscard]] std::string_view StopCodeCondition(StopCode code) noexcept;
// 停下后该做什么（`09` §2.2「停下后输出」列）
[[nodiscard]] std::string_view StopCodeHint(StopCode code) noexcept;

struct StopDecision {
    StopCode code = StopCode::None;
    std::string condition; // 触发条件（含数字阈值）
    std::string detail;    // 现场数字
};

// ———— `09` §2.3 / §2.4 阈值 ————
struct RunLimits {
    int max_llm_calls_per_chapter = 40;       // §2.3「每章最大 LLM 调用数」，超出即 S5
    int max_high_tier_calls_per_chapter = 8;  // §2.4 单章预算「高档调用」
    int max_images_per_chapter = 2;           // §2.4 单章预算「出图次数」
    std::int64_t chapter_wall_clock_ms = 30 * 60 * 1000; // §2.4 单章墙钟（超时告警，不阻断）
    int contract_retries = 1;      // §2.3 契约重试
    int validation_retries = 2;    // §2.3 校验重试
    int repair_rounds = 3;         // §2.3 修复轮次
    int network_retries = 4;       // §2.3 网络重试（2s → 4s → 8s → 16s，±20% 抖动）
    int backoff_base_ms = 2000;
    int rate_limit_backoff_ms = 10000; // 429（无 Retry-After）从 10s 起
    int min_request_interval_ms = 200; // 同 Provider 请求间隔
};
// `09` §2.2 S5：单章成本超预算 `> 2 ×`
inline constexpr int kCostOverrunFactor = 2;

// ———— 一章的观测（停止条件的唯一输入）————
struct ChapterObservation {
    RowId chapter_id = 0;
    int chapter_ord = 0;
    bool review_failed = false; // 本章最终评审 FAIL
    bool semantic_only = false; // `06` §2.7 M2：仅语义判断且 FAIL
    std::vector<std::string> failed_check_ids; // 机器校验失败项（同章同 check_id 累计 → S1）
    int contract_failures = 0;     // 契约校验失败次数（含 1 次重试后）→ S4
    int llm_calls = 0;             // → S5
    int high_tier_calls = 0;       // → S5
    int images = 0;                // → S7（本章需要出图）
    std::int64_t wall_ms = 0;      // 超 `chapter_wall_clock_ms` 只告警，不阻断
    int llm_network_failures = 0;  // 本章连续网络失败 → S6
    int comfy_probe_failures = 0;  // 探活失败次数（间隔 5s）→ S7
    int missing_entity_refs = 0;   // `code=contract` 的缺失引用 → S9
    int field_def_count = 0;       // → S10
    bool disk_write_failed = false;         // → S11
    bool state_conflict_unrecoverable = false; // → S8
    bool needs_user_decision = false;       // → S12
    std::string note;
};

// 累积量（同章同 check_id 的失败、连续语义失败章数、连续网络失败……）
class StopConditionTracker {
public:
    void Reset() noexcept;
    void Observe(const ChapterObservation& obs);
    [[nodiscard]] int SameCheckFailures(std::string_view checkId) const;
    [[nodiscard]] int ChapterReviewFailTotal() const noexcept { return chapter_review_fails_; }
    [[nodiscard]] int ConsecutiveSemanticOnlyFailChapters() const noexcept {
        return semantic_streak_;
    }
    [[nodiscard]] int ChapterContractFailures() const noexcept { return chapter_contract_fails_; }
    [[nodiscard]] int ConsecutiveLlmNetworkFailures() const noexcept { return llm_net_streak_; }
    [[nodiscard]] int ConsecutiveComfyProbeFailures() const noexcept { return comfy_streak_; }
    [[nodiscard]] int ChaptersObserved() const noexcept { return chapters_observed_; }

private:
    std::map<std::string, int> check_fails_;
    RowId current_chapter_ = 0;
    int chapter_review_fails_ = 0;
    int chapter_contract_fails_ = 0;
    int semantic_streak_ = 0;
    int llm_net_streak_ = 0;
    int comfy_streak_ = 0;
    int chapters_observed_ = 0;
};

// 纯函数：是否必须停下（`09` §2.2 STOP 表逐条；`CONTINUE` 表的情形不产生决策）
[[nodiscard]] std::optional<StopDecision> EvaluateStop(const StopConditionTracker& tracker,
                                                       const ChapterObservation& obs,
                                                       const RunLimits& limits);

// ———— 跑完一章的结果（runner → 循环）————
struct ChapterRunInfo {
    bool ok = false;
    std::string error;
    bool state_committed = false;
    bool state_skipped = false;
    bool review_passed = false;
    bool semantic_only = false;
    int llm_calls = 0;
    int high_tier_calls = 0;
    int images = 0;
    int contract_failures = 0;
    int missing_entity_refs = 0;
    int llm_network_failures = 0;
    int field_def_count = 0;
    std::vector<std::string> stages; // `GenerateChapter` 报告的 Phase（写 `_manifest.json`）
    std::string note;
};

// runner：默认实现走 `NovelDirector::GenerateChapter`；自检注入 mock（离线跑全流程）
using ChapterRunner = std::function<std::expected<ChapterRunInfo, agent::AgentError>(
    RowId chapter_id, const RunLimits& limits, RunMode mode,
    const std::function<void(const agent::GenerateChapterProgress&)>& on_progress)>;

struct RunProgress {
    int chapters_done = 0;
    int chapters_total = 0;
    RowId chapter_id = 0;
    int chapter_ord = 0;
    std::string phase;
    std::string note;
};

struct RunRequest {
    std::filesystem::path project_dir; // 工程根：`work/`、`snapshots/`、报告都落这里
    RowId from_ord = 0;                // 0 = 从第一个未完成章
    int max_chapters = 0;              // 0 = 不限（`auto_create_chapters` 时必须 > 0）
    RunMode mode = RunMode::Manual;
    RunLimits limits{};
    int checkpoint_every = 10;         // `09` §2.5（0 = 关）
    bool resume = true;                // `09` §2.6
    bool auto_create_chapters = false; // 无非完成章时自动建下一章（`03` CHAPTER_GOAL 未实现前的替代）
    std::function<void(const RunProgress&)> on_progress;
    std::function<bool()> cancel;      // true = 用户取消（保存进度，不算失败）
};

struct RunOutcome {
    bool started = false;
    std::string refuse_reason; // 被拒启动（auto 前置不满足 / 参数非法）
    int chapters_attempted = 0;
    int chapters_done = 0;
    int chapters_resumed_skipped = 0;
    int llm_calls_total = 0;
    std::vector<RowId> completed_chapters;
    std::optional<StopDecision> stop;
    std::string stop_report_path;
    std::string last_error;
    std::vector<std::string> checkpoints; // 写出的 `checkpoint_ch<A>_<B>.md`
    std::string mode_note;
};

// ———— `09` §2.1 `auto` 的三条前置 ————
struct AutoPreconditionInput {
    bool gates_verified_on_last_chapter = false; // 最近一章 G1–G5 已验证通过（库里可查）
    bool verifiers_complete = false;             // `06` K01–K29 校验器**全部可用**
    bool llm_ok = false;
    bool comfy_ok = true; // 本章需要出图时才要求
};
// 空 = 允许 `auto`；有值 = 拒绝启动的原因（逐条列出未满足项）
[[nodiscard]] std::optional<std::string> CheckAutoPrecondition(const AutoPreconditionInput& in);
// 从库里探测（gates / verifiers；LLM 连通性由调用方给）。`verifiers_complete` 依据是
// `06` §2.3 的 K01–K29 是否全量落地 —— 本仓当前**未全量**，故恒 false。
[[nodiscard]] AutoPreconditionInput ProbeAutoPrecondition(db::sqlite::Database& db, bool llm_ok);

// ———— 检查点（`09` §2.5）————
struct CheckpointInput {
    int from_ord = 0;
    int to_ord = 0;
    std::vector<std::string> high_issues;        // 本区间 high issue
    std::vector<std::string> semantic_only_ords; // 仅语义判断且 FAIL 的章
    std::vector<std::string> promoted_fields;    // 本检查点自动升 CANON 的字段
    std::int64_t llm_calls = 0;
    std::int64_t images = 0;
};

// ———— 产物序列化（`09` §2.7；暴露出来是为了可离线断言内容）————
[[nodiscard]] std::string StopReportMarkdown(const RunOutcome& out, const RunRequest& req);
[[nodiscard]] std::string CostReportJson(RowId chapter_id, int ord, const ChapterRunInfo& info,
                                         const ChapterObservation& obs, const RunLimits& limits);
[[nodiscard]] std::string ManifestJson(RowId chapter_id, int ord, std::string_view state_hash,
                                       const std::vector<std::string>& stages,
                                       std::string_view status);
[[nodiscard]] std::filesystem::path ChapterWorkDir(const std::filesystem::path& project_dir,
                                                  int ord);

class NovelRunLoop {
public:
    NovelRunLoop(db::sqlite::Database& db, agent::LlmCallFn call, agent::LlmStreamFn stream = nullptr);

    // 注入 runner（离线自检 / 未来换阶段机）；空 = 用内置的 `NovelDirector` 路径
    void SetChapterRunner(ChapterRunner runner) noexcept { runner_ = std::move(runner); }

    [[nodiscard]] RunOutcome Run(const RunRequest& req);

    // 检查点：自动升格 `08` §2.3 的四条件字段 + 产出 `checkpoint_ch<A>_<B>.md`
    [[nodiscard]] std::expected<std::string, DbError> WriteCheckpoint(const RunRequest& req,
                                                                     CheckpointInput input);

    // 断点续跑（`09` §2.6）：该章是否已完成（`chapters.status='done'` 且有正文）
    [[nodiscard]] bool ChapterAlreadyDone(RowId chapter_id) const;
    // 状态指纹（轻量：已完成章数 / 实体数 / 开放伏笔数）—— 不是 `04` §2.5 的完整 `input_state_hash`
    [[nodiscard]] std::string StateFingerprint() const;
    [[nodiscard]] int FieldDefCount() const;

    [[nodiscard]] static bool RunSelfCheck();

private:
    db::sqlite::Database* db_;
    agent::LlmCallFn call_;
    agent::LlmStreamFn stream_;
    ChapterRunner runner_;
};

} // namespace shine::novelcore
