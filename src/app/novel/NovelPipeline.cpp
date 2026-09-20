#include "app/novel/NovelPipeline.h"

#include "core/Log.h"
#include "openai/OpenAIClient.h"
#include "openai/OpenAIConfig.h"
#include "util/Encoding.h"

#include <chrono>
#include <thread>
#include <utility>

#include <fmt/format.h>

namespace shine::app::novel {

agent::LlmCallFn MakeLlmCall(const std::atomic<bool>* cancel) {
    return [cancel](agent::LlmRole role, std::string_view instructions,
                    std::string_view user) -> std::expected<std::string, agent::AgentError> {
        // S16（`09` §2.4）：按阶段选模型 —— planner / writer / critic 三个配置项，
        // 空则回退 `openaiModelDefault`；解析统一在 `openai::ResolveModel`。
        const std::string model = openai::ResolveModel(agent::LlmRoleName(role));
        auto r = openai::LlmComplete(instructions, user, std::chrono::seconds{180}, cancel, model);
        if (!r) {
            return std::unexpected(agent::AgentError{r.error().code, r.error().message});
        }
        return *r;
    };
}

agent::LlmCreateRawFn MakeLlmCreateRaw() {
    return [](std::string_view ins, std::string_view input,
              std::string_view tools) -> std::expected<std::string, std::string> {
        // ★ S62：与 `MakeLlmCall` 同口径按**角色**选模型 —— extractor 走
        // `ResolveModel("extractor")`，不绕过 `09` §2.4 的分层路由。
        const std::string model = openai::ResolveModel(agent::LlmRoleName(agent::LlmRole::Extractor));
        // ★ S62：**网络层退避重试**（与 `NovelVisualStages` 的 `create` 同款 —— S46 就在
        // Agent 工具循环上踩过这个坑：`ssl handshake failed` 偶发，一次抖动会把**整轮**打掉）。
        // ⚠️ 退避**要够长**：S46 实测 1.5s 的退避连续 3 次都过不去，现象是"同一分钟里第 6 次
        //    请求开始被持续拒绝"（像是**服务端按新建连接数短时限流**）⇒ 按 4s / 10s / 20s 走。
        //    2026-09-20 复核：MiniMax 端点在连发时单请求耗时从 0.15s 涨到 1.2-2.5s、复用连接只要
        //    0.4s ⇒ 确实有"新建连接"维度的限流；而 libhv 用的是 **Windows Schannel**
        //    （`WITH_OPENSSL OFF`），慢握手下会**立即**返回 `HSSL_ERROR(-1)`（不是超时）。
        static constexpr int kBackoffMs[] = {4000, 10000, 20000};
        std::string lastErr;
        for (int attempt = 0; attempt < 4; ++attempt) {
            if (attempt > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kBackoffMs[attempt - 1]));
            }
            auto r = openai::LlmCreateRaw(ins, input, tools, model);
            if (r) {
                return *r;
            }
            lastErr = r.error().message;
            log::Warn("LlmCreateRaw 失败（第 {} 次）：{}", attempt + 1, lastErr);
        }
        return std::unexpected(lastErr);
    };
}

bool CrossReviewEffective() {
    return openai::ResolveModel("critic") != openai::ResolveModel("writer");
}

std::string ChapterGenOutcome::Describe() const {
    if (!ok) {
        return "生成失败：" + error;
    }
    return fmt::format("生成成功：章《{}》{} 字 · 修订 {} 次 · 校验重做 {} 次 · LLM 调用 {} 次 · {}",
                       title, body.size(), revisions, validation_retries, llm_calls,
                       state_skipped ? "状态已提交（幂等命中）"
                                     : (state_committed
                                            ? fmt::format("状态已回写{}",
                                                          commit_note.empty()
                                                              ? std::string{}
                                                              : "（" + commit_note + "）")
                                            : "状态未回写"));
}

ChapterGenOutcome
GenerateOneChapter(::shine::db::sqlite::Database& db, std::int64_t chapter_id,
                   const std::filesystem::path& project_dir, int max_revisions,
                   const std::atomic<bool>* cancel,
                   const std::function<void(const agent::GenerateChapterProgress&)>& on_progress,
                   bool resume) {
                   ChapterGenOutcome out;
                   if (chapter_id <= 0) {
        out.error = "需要 chapter_id（> 0）";
        return out;
    }
    if (!db.isOpen()) {
        out.error = "数据库未打开";
        return out;
    }
    // `09` §2.1 前置③：没配 Key 就**别发请求**，直接给可读原因（UI 与 CLI 同一判定）
    const auto profile = openai::ResolveActiveProfile();
    if (profile.apiKey.empty()) {
        out.error = fmt::format("未配置 {} 的 API Key（设置 → LLM）",
                                std::string{openai::ProviderLabel(profile.provider)});
        return out;
    }

    agent::NovelDirector dir(db, MakeLlmCall(cancel));
    agent::GenerateChapterRequest req;
    req.chapter_id = chapter_id;
    req.user_hint = "续写本章";
    req.max_revisions = max_revisions > 0 ? max_revisions : 2;
    // S19（`03` §2.7 P1/P5）：续跑语义由调用方决定（UI 按钮 = false；连跑 = true）
    req.resume = resume;
    if (!project_dir.empty()) {
        // 工程根与快照目录**显式下发**（`work/` 与 `snapshots/` 都按它落盘）
        req.project_dir = util::PathToUtf8(project_dir);
        req.snapshot_dir = util::PathToUtf8(project_dir / "snapshots");
    }

    auto r = dir.GenerateChapter(req, on_progress);
    if (!r) {
        out.error = r.error().message;
        return out;
    }
    out.ok = true;
    out.title = r->title;
    out.body = r->body;
    out.revisions = r->revisions;
    out.validation_retries = r->validation_retries;
    out.llm_calls = r->llm_calls;
    out.state_committed = r->state_committed;
    out.state_skipped = r->state_skipped;
    out.commit_note = r->commit_note;
    return out;
}

void FillPreconditions(novelcore::RunRequest& req, std::int64_t max_total_llm_calls) {
    req.llm_ready = !openai::ResolveActiveProfile().apiKey.empty();
    req.cross_review_ok = CrossReviewEffective();
    req.max_total_llm_calls = max_total_llm_calls;
    // S62：注入"原始响应"通道 → EXTRACT 走工具循环（UI 与 CLI 都从这里过，一处注入两处生效）
    req.create_raw = MakeLlmCreateRaw();
}

novelcore::RunOutcome RunOnce(::shine::db::sqlite::Database& db, const novelcore::RunRequest& req,
                              const std::atomic<bool>* cancel,
                              const std::function<void(const novelcore::RunProgress&)>& on_progress) {
    novelcore::NovelRunLoop loop(db, MakeLlmCall(cancel));
    novelcore::RunRequest r = req;
    if (on_progress) {
        r.on_progress = on_progress;
    }
    if (cancel != nullptr) {
        r.cancel = [cancel]() { return cancel->load(); };
    }
    return loop.Run(r);
}

} // namespace shine::app::novel
