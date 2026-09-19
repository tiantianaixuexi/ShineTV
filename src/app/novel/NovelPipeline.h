#pragma once
// S17：小说侧「生成一章 / 连跑」的**共用入口** —— UI（`NovelView`）与 headless CLI
// （`NovelCli`）走同一条路：同一份 LLM 回调（按 `LlmRole` 选模型）、同一份前置判定、
// 同一份结果结构。此前 UI 内联 worker、CLI 得另写一份 —— 那是最容易「两条路行为不一致」
// 的地方（S15 抽 `StartChapterGeneration` 只解了 UI 内部的重用，这一层才跨载体）。
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "agent/NovelDirector.h"
#include "db/sqlite/SqliteDb.h"
#include "novel/NovelRunLoop.h"

namespace shine::app::novel {

namespace novelcore = ::shine::novelcore;

// 真实 LLM 回调：按 `LlmRole` 解析模型（`09` §2.4 分层），`agent` 层只知道 role。
[[nodiscard]] agent::LlmCallFn MakeLlmCall(const std::atomic<bool>* cancel);

// `09` §2.4 验收判据：`critic ≠ writer`（判**解析后的生效模型**，不是「配置非空」）。
[[nodiscard]] bool CrossReviewEffective();

// 生成一章（含状态回写）。`project_dir` 决定 `work/` 与 `snapshots/` 的落点。
struct ChapterGenOutcome {
    bool ok = false;
    std::string error; // ok=false 时的可读原因
    std::string title;
    std::string body;
    int revisions = 0;
    int validation_retries = 0;
    int llm_calls = 0;
    bool state_committed = false;
    bool state_skipped = false; // 幂等命中
    std::string commit_note;
    [[nodiscard]] std::string Describe() const;
};

// 生成一章。`max_revisions <= 0` → 用契约默认（2）。
// `resume`（S19，`03` §2.7 P1/P5）：true = **续跑语义** —— 盘上阶段产物哈希一致就跳过
// 该阶段（含 `chapters.body` 已落库时不再请求 Writer）。默认 **false**：
// UI 的「生成本章」与 MCP 工具都是"我要这一章（重）写"，不是"把没写完的补完"。
[[nodiscard]] ChapterGenOutcome GenerateOneChapter(
    ::shine::db::sqlite::Database& db, std::int64_t chapter_id,
    const std::filesystem::path& project_dir, int max_revisions, const std::atomic<bool>* cancel,
    const std::function<void(const agent::GenerateChapterProgress&)>& on_progress,
    bool resume = false);

// 把「调用方能判的前置」一次填好（LLM 可用 / 交叉复核 / 全书预算上限）—— UI 与 CLI
// 都不该漏填（漏一个就等于该前置失效）。
void FillPreconditions(novelcore::RunRequest& req, std::int64_t max_total_llm_calls);

// 连跑（`09` §2.1）：拼好 LLM 回调与取消钩子后转发 `NovelRunLoop::Run`。
[[nodiscard]] novelcore::RunOutcome
RunOnce(::shine::db::sqlite::Database& db, const novelcore::RunRequest& req,
        const std::atomic<bool>* cancel,
        const std::function<void(const novelcore::RunProgress&)>& on_progress);

} // namespace shine::app::novel
