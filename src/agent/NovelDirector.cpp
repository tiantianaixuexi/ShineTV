#include "agent/NovelDirector.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "novel/NovelChecks.h"    // S19：ComputeInputStateHash（`04` §2.5 哈希，唯一来源）
#include "novel/NovelCommit.h"    // S8：StateDiff + 提交门禁 + 14 块事务
#include "novel/NovelGraph.h"
#include "novel/NovelMemory.h"
#include "novel/NovelStageLedger.h" // S19：阶段产物落盘与续跑（`03` §2.7 P1/P2/P5）
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Json.h"
#include "util/Strings.h"
#include "util/Time.h"

#include <fmt/format.h>

#include <yyjson.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <random>
#include <system_error>
#include <thread>

namespace shine::agent {

std::string_view LlmRoleName(LlmRole role) noexcept {
    switch (role) {
    case LlmRole::Planner: return "planner";
    case LlmRole::Writer: return "writer";
    case LlmRole::Critic: return "critic";
    case LlmRole::Extractor: return "extractor";
    }
    return "planner";
}

namespace {

// S42：委托给 `util::json::JsonEscape`（**唯一来源**）—— 原先手写且漏了 `<0x20` 的控制字符。
[[nodiscard]] std::string EscapeJson(std::string_view s) { return util::json::JsonEscape(s); }

void Report(const std::function<void(const GenerateChapterProgress&)>& cb, Phase p, int pct,
            std::string_view note = {}) {
    if (!cb) return;
    GenerateChapterProgress pr;
    pr.phase = p;
    pr.percent = pct;
    pr.note = std::string{note};
    cb(pr);
}

// —— S9/S16（`09` §2.4 模型分层 / §2.3 重试退避）——
// S16（09-7）：role 现在**真的**参与路由 —— 它随 `LlmCallFn` 传给调用方，由调用方按
// `openai::ResolveModel(role)` 选模型（planner/writer/critic 三个配置项，空则回退 default）。
[[nodiscard]] LlmRole LlmRoleOf(std::string_view role) noexcept {
    if (role == "writer") return LlmRole::Writer;
    if (role == "critic") return LlmRole::Critic;
    if (role == "extractor" || role == "extract") return LlmRole::Extractor;
    return LlmRole::Planner;
}

// 档位（`09` §2.4）：planner=中、writer/critic=高、extractor=低。用于记账与「高档 ≤ 8」。
[[nodiscard]] std::string_view TierOfRole(std::string_view role) noexcept {
    if (role == "writer" || role == "critic") return "high";
    if (role == "planner") return "mid";
    return "low"; // extractor
}

// `09` §2.3：哪些错误可以重试（网络 5xx / 429 / 超时）；本地配置类错误不重试（重试也没用）
[[nodiscard]] bool IsRetryable(const AgentError& e) {
    if (e.code == "cancelled" || e.code == "no_llm" || e.code == "bad_args" ||
        e.code == "no_key" || e.code == "auth" || e.code == "unsupported") {
        return false;
    }
    return true;
}

[[nodiscard]] bool IsRateLimited(const AgentError& e) {
    return e.code.find("429") != std::string::npos || e.code.find("rate") != std::string::npos ||
           e.message.find("429") != std::string::npos;
}

} // namespace

std::string_view PhaseName(Phase p) noexcept {
    switch (p) {
    case Phase::Idle: return "IDLE";
    case Phase::Analyze: return "ANALYZE";
    case Phase::Plan: return "PLAN";
    case Phase::Retrieve: return "RETRIEVE";
    case Phase::Write: return "WRITE";
    case Phase::Review: return "REVIEW";
    case Phase::Revision: return "REVISION";
    case Phase::Extract: return "EXTRACT";
    case Phase::Save: return "SAVE";
    case Phase::Done: return "DONE";
    case Phase::Failed: return "FAILED";
    }
    return "?";
}

std::string ExtractOutputText(std::string_view responseJson) {
    if (responseJson.empty()) return {};
    yyjson_doc* doc = yyjson_read(responseJson.data(), responseJson.size(), 0);
    if (!doc) {
        return std::string{responseJson}; // 已是纯文本
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    std::string text = util::json::GetStrCopy(root, "output_text");
    if (text.empty()) {
        if (yyjson_val* output = util::json::GetArr(root, "output")) {
            size_t i = 0, n = 0;
            yyjson_val* item = nullptr;
            yyjson_arr_foreach(output, i, n, item) {
                if (util::json::GetStr(item, "type") != "message") continue;
                if (yyjson_val* content = util::json::GetArr(item, "content")) {
                    size_t ci = 0, cn = 0;
                    yyjson_val* c = nullptr;
                    yyjson_arr_foreach(content, ci, cn, c) {
                        if (util::json::GetStr(c, "type") == "output_text") {
                            text += util::json::GetStrCopy(c, "text");
                        }
                    }
                }
            }
        }
    }
    yyjson_doc_free(doc);
    // S38：**兜底** —— 走到这里说明它既不是 Responses API 的 `output_text` / `output[]` 结构，
    // 也不是非法 JSON。那就是**别的协议**的合法 JSON（如 Chat Completions 那条路返回的
    // `{"shots":[...]}` —— `ChatComplete` 早就提取过一层 content 了，这里是**双重提取**）。
    // ⚠️ 原先这里**返回空** ⇒ 调用方只看到一句"未返回内容"，完全无从下手。
    //    真实跑 V9 就是卡在这：日志说"Storyboard 未返回内容"，而 dump 出来的响应
    //    明明是 `status=200 finish_reason=stop` + 一大段合法 JSON。**返回原文**，让上层去解析、
    //    去报"缺 shots 数组"这种**有信息量**的错。
    if (text.empty()) {
        return std::string{responseJson};
    }
    return text;
}

std::string DefaultPrompt(std::string_view role) {
    if (role == "planner") {
        return
            "你是小说规划器。根据上下文为本章输出 JSON（不要其它文字）：\n"
            "{\"chapter_title\":\"…\",\"goal\":\"…\","
            "\"scenes\":[{\"ord\":1,\"location\":\"…\",\"cast\":[\"…\"],\"goal\":\"…\","
            "\"conflict\":\"…\",\"result\":\"…\",\"emotion\":\"…\"}],"
            "\"foreshadowing\":[\"plant|x\"],\"ending_hook\":\"…\"}\n"
            "场景 2–4 个；冲突具体；结尾留钩子。";
    }
    if (role == "writer") {
        return
            "你是小说写手。严格按计划与上下文写本章正文。遵守文风与作者硬规则；"
            "POV 未知情的秘密不得写穿；不要输出 JSON 或解释，只输出正文。";
    }
    if (role == "critic") {
        return
            "你是小说审校。检查人设/世界观/时间线/能力/POV/伏笔/作者规则。"
            "输出 JSON：{\"passed\":true|false,\"issues\":[{\"type\":\"…\",\"severity\":\"high|mid|low\","
            "\"description\":\"…\"}]}。无问题则 passed=true 且 issues 为空。";
    }
    if (role == "extractor") {
        return
            "你是信息抽取器。从正文与计划中抽取摘要，输出 JSON：\n"
            "{\"summary\":\"本章 2–3 句摘要\",\"new_entities\":[{\"kind\":\"person|location|item\",\"name\":\"…\"}],"
            "\"events\":[{\"title\":\"…\"}],\"foreshadow_updates\":[{\"title\":\"…\",\"status\":\"PLANTED|DEVELOPING|REVEALED\"}]}\n"
            "没有则空数组。";
    }
    return "你是小说助手。";
}

NovelDirector::NovelDirector(db::sqlite::Database& db, LlmCallFn call, LlmStreamFn stream)
    : db_(&db), call_(std::move(call)), stream_(std::move(stream)) {}

std::string NovelDirector::LoadPrompt(std::string_view name) const {
    if (!promptsDir_.empty()) {
        const auto p = promptsDir_ / (std::string{name} + ".md");
        if (std::error_code ec; std::filesystem::exists(p, ec)) {
            if (auto bytes = util::ReadFileBytes(p); bytes && !bytes->empty()) {
                return *bytes;
            }
        }
    }
    return DefaultPrompt(name);
}

std::expected<std::string, AgentError>
NovelDirector::CallLlm(std::string_view role, std::string_view user, std::string_view stage,
                       const GenerateChapterRequest& req, std::vector<LlmCallRecord>& out) {
    if (!call_) {
        return std::unexpected(AgentError{"no_llm", "未配置 LLM 回调"});
    }
    const std::string instructions = LoadPrompt(role);
    const std::string tier{TierOfRole(role)};
    const LlmRole llmRole = LlmRoleOf(role);
    // `09` §2.3：同 Provider 请求间隔 ≥ 200ms（防限流）；LLM 并发 = 1（串行，见 `09` §2.3）
    if (req.min_request_interval_ms > 0 && last_call_ms_ > 0) {
        const std::int64_t wait = req.min_request_interval_ms - util::ElapsedMillis(last_call_ms_);
        if (wait > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds{wait});
        }
    }
    const int maxAttempts = 1 + std::max(0, req.network_retries);
    AgentError last{"unknown", "未知错误"};
    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        const std::int64_t t0 = util::MonotonicMillis();
        auto r = call_(llmRole, instructions, user);
        last_call_ms_ = util::MonotonicMillis();
        LlmCallRecord rec;
        rec.stage = std::string{stage};
        rec.role = std::string{role};
        rec.tier = tier;
        rec.attempt = attempt;
        rec.ok = r.has_value();
        rec.ms = util::ElapsedMillis(t0);
        if (r) {
            out.push_back(std::move(rec));
            return r;
        }
        last = r.error();
        rec.rate_limited = IsRateLimited(last);
        out.push_back(std::move(rec));
        if (!IsRetryable(last) || attempt >= maxAttempts) {
            break;
        }
        // `09` §2.3：2s → 4s → 8s → 16s（±20% 抖动）；429（无 Retry-After）从 10s 起
        std::int64_t delay = rec.rate_limited
                                 ? req.rate_limit_backoff_ms
                                 : static_cast<std::int64_t>(req.backoff_base_ms) << (attempt - 1);
        static thread_local std::mt19937 rng{std::random_device{}()};
        const double jitter = std::uniform_real_distribution<double>{0.8, 1.2}(rng);
        delay = static_cast<std::int64_t>(static_cast<double>(delay) * jitter);
        log::Warn("LLM {}（{}）第 {}/{} 次失败：{} —— {}ms 后重试", stage, role, attempt,
                  maxAttempts, last.message, delay);
        std::this_thread::sleep_for(std::chrono::milliseconds{delay});
    }
    return std::unexpected(last);
}

std::expected<GenerateChapterResult, AgentError> NovelDirector::GenerateChapter(
    const GenerateChapterRequest& req,
    const std::function<void(const GenerateChapterProgress&)>& on_progress) {
    if (!db_ || !db_->isOpen()) {
        return std::unexpected(AgentError{"no_db", "数据库未打开"});
    }
    if (req.chapter_id <= 0) {
        return std::unexpected(AgentError{"bad_args", "需要 chapter_id"});
    }

    GenerateChapterResult result;
    result.chapter_id = req.chapter_id;
    // S9：记录走过的阶段（写 `work/ch<NNN>/_manifest.json`），对外回调行为不变
    std::vector<std::string> stages;
    auto progressCb = [&](const GenerateChapterProgress& p) {
        const std::string name{PhaseName(p.phase)};
        if (stages.empty() || stages.back() != name) {
            stages.push_back(name);
        }
        if (on_progress) on_progress(p);
    };
    novelcore::NovelGraph g(*db_);
    novelcore::NovelMemory mem(*db_);
    ContextBuilder cb(*db_);

    Report(progressCb, Phase::Analyze, 5, "读取章节");
    auto ch = g.GetChapter(req.chapter_id);
    if (!ch) {
        return std::unexpected(AgentError{"not_found", ch.error().message});
    }

    // RETRIEVE
    Report(progressCb, Phase::Retrieve, 15, "组装上下文");
    auto ctx = cb.Build({.chapter_id = req.chapter_id,
                         .task = req.user_hint.empty() ? fmt::format("写第{}章", ch->ord)
                                                       : req.user_hint,
                         .pov_entity_id = ch->pov_entity_id > 0
                                              ? std::optional<std::int64_t>{ch->pov_entity_id}
                                              : std::nullopt});
    if (!ctx) {
        return std::unexpected(AgentError{"context", ctx.error().message});
    }

    // S19（`03` §2.7）：阶段产物目录与"当前状态哈希"（`04` §2.5，chain=text）—— 落盘与续跑共用
    const std::filesystem::path workDir =
        req.project_dir.empty() ? std::filesystem::path{} : util::PathFromUtf8(req.project_dir);
    const int ord = ch->ord;
    const auto stateHash = [this, &req](std::string_view stage) {
        return novelcore::ComputeInputStateHash(*db_, req.chapter_id, "text", stage);
    };
    // T5 CONTEXT_ASSEMBLY（`03` §2.2）→ `04_context_pack.json` + 库账（S20：K23 的受检对象）
    (void)novelcore::RecordStageArtifact(*db_, req.chapter_id, ord, workDir, "CONTEXT_ASSEMBLY",
                                         ctx->text, stateHash("CONTEXT_ASSEMBLY"));

    // PLAN（T2–T10 合并执行 —— `03` §2.2 明确允许"T2–T9 是规划细化，可合并"；合并不改契约）
    // S19（P1/P2）：`resume` 且盘上 `09_chapter_plan.json` 的哈希与当前一致 → **复用**，
    // 不再请求 planner（"崩在 WRITE 之后，重跑不再花 plan 那份钱"）。
    if (req.resume && !workDir.empty()) {
        if (const auto art = novelcore::ReadStageArtifact(workDir, ord, "SCENE_EVENT_ORDER");
            art && !art->payload.empty() && art->input_state_hash == stateHash("SCENE_EVENT_ORDER")) {
            result.plan_json = art->payload;
            Report(progressCb, Phase::Plan, 30, "Planner（复用盘上产物）");
            log::Info("阶段续跑（P1）：复用 09_chapter_plan.json（{} 字节），Planner 未再请求 LLM",
                      art->payload.size());
        }
    }
    if (result.plan_json.empty()) {
        Report(progressCb, Phase::Plan, 30, "Planner");
        const std::string planUser = fmt::format("{}\n\n【任务】\n{}", ctx->text, req.user_hint);
        auto planResp = CallLlm("planner", planUser, "PLAN", req, result.calls);
        if (!planResp) {
            return std::unexpected(planResp.error());
        }
        result.plan_json = ExtractOutputText(*planResp);
        if (result.plan_json.empty()) {
            // 解析失败重试 1 次（`09` §2.3「契约重试 = 1」）—— 计一次契约失败（`09` §2.2 S4 的输入）
            ++result.contract_failures;
            planResp = CallLlm("planner", planUser, "PLAN", req, result.calls);
            if (!planResp) return std::unexpected(planResp.error());
            result.plan_json = ExtractOutputText(*planResp);
        }
        if (result.plan_json.empty()) {
            return std::unexpected(AgentError{"plan", "Planner 未返回 JSON"});
        }
        (void)novelcore::RecordStageArtifact(*db_, req.chapter_id, ord, workDir, "SCENE_EVENT_ORDER",
                                             result.plan_json, stateHash("SCENE_EVENT_ORDER"));
    }

    // WRITE（可 stream）。T11 的产物就是 `chapters.body`（`03` §2.7 的 work 表里**没有**草稿文件）
    // S19（P4/P5）：`resume` 且正文已落库（非 done）→ **复用**，跳过 Writer
    std::string body;
    // P4 的语义就是"正文一旦写进 `chapters.body` 就算该阶段落盘"——与章状态无关
    // （已 done 的章本该被 `ChapterAlreadyDone` 挡在上游，不该走到这里）。
    if (req.resume && !ch->body.empty()) {
        body = ch->body;
        Report(progressCb, Phase::Write, 50, "Writer（复用已落库正文）");
        log::Info("阶段续跑（P5）：复用 chapters.body（{} 字），Writer 未再请求 LLM", body.size());
    } else {
    Report(progressCb, Phase::Write, 50, "Writer");
    const std::string writeUser =
        fmt::format("{}\n\n【章节计划 JSON】\n{}\n\n【写出正文】", ctx->text, result.plan_json);
    if (stream_) {
        auto onDelta = [&](std::string_view d) {
            GenerateChapterProgress pr;
            pr.phase = Phase::Write;
            pr.percent = 55;
            pr.text_delta = std::string{d};
            progressCb(pr);
        };
        const std::int64_t t0 = util::MonotonicMillis();
        auto wr = stream_(LlmRole::Writer, LoadPrompt("writer"), writeUser, onDelta);
        last_call_ms_ = util::MonotonicMillis();
        {
            LlmCallRecord rec;
            rec.stage = "WRITE";
            rec.role = "writer";
            rec.tier = "high";
            rec.attempt = 1;
            rec.ok = wr.has_value();
            rec.ms = util::ElapsedMillis(t0);
            result.calls.push_back(std::move(rec));
        }
        if (!wr) return std::unexpected(wr.error());
        body = ExtractOutputText(*wr);
        if (body.empty()) body = *wr;
    } else {
        auto wr = CallLlm("writer", writeUser, "WRITE", req, result.calls);
        if (!wr) return std::unexpected(wr.error());
        body = ExtractOutputText(*wr);
    }
    if (body.empty()) {
        return std::unexpected(AgentError{"write", "Writer 未返回正文"});
    }
    } // S19：`else`（未复用已落库正文）的收束

    // REVIEW + REVISION
    int revisions = 0;
    std::string criticJson;
    bool reviewPassed = false; // 提交门禁 G1 要用的**最终评审结论**（循环外可见）
    while (revisions <= req.max_revisions) {
        Report(progressCb, revisions == 0 ? Phase::Review : Phase::Revision,
               70 + revisions * 5, fmt::format("Critic 第{}轮", revisions + 1));
        const std::string criticUser =
            fmt::format("{}\n\n【计划】\n{}\n\n【正文】\n{}\n\n请审校并输出 JSON。", ctx->text,
                        result.plan_json, body);
        auto cr = CallLlm("critic", criticUser, "REVIEW", req, result.calls);
        if (!cr) break; // Critic 失败不阻断保存
        criticJson = ExtractOutputText(*cr);
        result.critic_json = criticJson;
        // T12 CHAPTER_REVIEW（`03` §2.2）→ `10_review.json`（`03` §2.7）+ 库账
        (void)novelcore::RecordStageArtifact(*db_, req.chapter_id, ord, workDir, "CHAPTER_REVIEW",
                                             criticJson, stateHash("CHAPTER_REVIEW"));
        const bool passed =
            criticJson.find("\"passed\":true") != std::string::npos ||
            criticJson.find("\"passed\": true") != std::string::npos;
        reviewPassed = passed; // ⚠️ 目前是子串判定（`06` §2.4 的 ReviewVerdict 落地后换掉）
        if (passed || revisions >= req.max_revisions) break;
        ++revisions;
        // 改稿
        const std::string revUser = fmt::format(
            "{}\n\n【计划】\n{}\n\n【原稿】\n{}\n\n【审校意见】\n{}\n\n请输出修订后的完整正文。", ctx->text,
            result.plan_json, body, criticJson);
        auto rr = CallLlm("writer", revUser, "REVISION", req, result.calls);
        if (!rr) break;
        const auto newBody = ExtractOutputText(*rr);
        if (!newBody.empty()) body = newBody;
        // T13 CHAPTER_REPAIR（`03` §2.2）→ `11_repair_receipt.json`（`03` §2.7）+ 库账
        (void)novelcore::RecordStageArtifact(
            *db_, req.chapter_id, ord, workDir, "CHAPTER_REPAIR",
            fmt::format(R"({{"round":{},"revised":{}}})", revisions,
                        newBody.empty() ? "false" : "true"),
            stateHash("CHAPTER_REPAIR"));
    }
    result.revisions = revisions;
    result.body = body;
    result.title = ch->title;

    // SAVE（正文落盘 —— 正文不随「状态校验重做」而变，故在重做循环之前写一次）
    Report(progressCb, Phase::Save, 95, "写入章节");
    novelcore::ChapterRow row = *ch;
    row.body = body;
    row.words = static_cast<int>(body.size());
    row.status = "review";
    if (auto id = g.UpsertChapter(row); !id) {
        return std::unexpected(AgentError{"save", id.error().message});
    }

    // ———— EXTRACT → 提交门禁（`06` §2.6：机器校验失败**回到产出该对象的阶段重做**）————
    // `07` §2.2 的 ②「模型提取」+ ③「差分校验」在这里成环：extractor 产出 StateDiff → 提交门禁
    // （G2 = `06` §2.3 K01–K29）→ 被机器校验挡下则**重跑 extractor**（把失败清单喂回去让它针对性修），
    // 上限 `03` §2.6 的 `max_validate_retry`（默认 2）。基线仍由代码读（`07` §2.2 ①），不交给模型。
    std::string summary = body.substr(0, std::min<std::size_t>(body.size(), 80));
    novelcore::CommitResult commit;
    bool hasDiff = false;
    const int maxValidateRetries = req.max_validate_retries > 0 ? req.max_validate_retries : 2;
    // 同章同 check_id 的累计失败次数 → `09` §2.2 S1 的输入（重复条目 = 次数）
    std::map<std::string, int> checkFailCounts;
    std::string lastChecksDescribe; // S19：T15 的产物要记"最后一次尝试"的机器校验摘要

    // S55：上一次**解析失败**的提示（与"机器校验不过"分开 —— 两种毛病的处方完全不同：
    // 前者是"JSON 都不合法"（多半引号没转义），后者是"JSON 合法但字段/取值不对"）。
    std::string parseFailHint;
    for (int attempt = 0; attempt <= maxValidateRetries; ++attempt) {
        Report(progressCb, Phase::Extract, 90,
               attempt == 0 ? std::string{"Extractor"}
                            : fmt::format("Extractor 重做（第 {} 次 · `06` §2.6 回产出阶段）", attempt));
        std::string exUser = fmt::format("【计划】\n{}\n\n【正文】\n{}", result.plan_json, body);
        if (!parseFailHint.empty()) {
            exUser += parseFailHint;
        } else if (attempt > 0) {
            exUser += fmt::format(
                "\n\n【上一版 StateDiff 未通过机器校验（`06` §2.3），请**只修这些问题**后重新输出"
                "完整 StateDiff】\n{}\n",
                commit.checks_describe);
        }
        auto er = CallLlm("extractor", exUser, "EXTRACT", req, result.calls);
        novelcore::StateDiff diff;
        hasDiff = false;
        if (er) {
            // S53：**先做宽容提取**（与阶段链 `ExtractJsonObject` 同款）—— Extractor 同样会吐
            // ` ```json ` 围栏，而它直接决定"**状态回写**"：回写不了 ⇒ `canon_logs` 空 ⇒
            // **auto 前置①（G1–G5）永远过不了** ⇒ `--novel-run auto` 永远被拒。
            // 真跑实测：`Extractor 输出不是合法的 StateDiff JSON（本章不回写状态）：```json`。
            // ⚠️ 下面 `yyjson_read(ej...)` 与 `StateDiffFromJson(ej, ...)` **共用同一个 `ej`** ——
            //    在这里统一净化，两处一起受益。
            const std::string ej = util::json::ExtractJsonObject(ExtractOutputText(*er));
            yyjson_doc* doc = yyjson_read(ej.data(), ej.size(), 0);
            if (doc) {
                yyjson_val* root = yyjson_doc_get_root(doc);
                const auto s = util::json::GetStrCopy(root, "summary");
                if (!s.empty()) summary = s;
                yyjson_doc_free(doc);
            }
            // S8：把 extractor 的**完整** StateDiff 解出来（契约 `02` §2.5）—— 原先这里只取 summary，
            // 于是章节生成完**从不回写世界状态**（`07` 差距 07-1，本章不改变世界，长篇必崩）。
            if (novelcore::StateDiffFromJson(ej, diff)) {
                if (diff.chapter_id <= 0) {
                    diff.chapter_id = req.chapter_id;
                }
                hasDiff = true;
            } else if (!util::Trim(ej).empty()) {
                // S55：**把"截断"和"格式错"分开** —— 原先只有 `substr(0, 200)`，两种情况的
                // 日志长得一样，导致误判（我自己就先误判成"围栏"，其实是别的原因）。
                // 判据：JSON 正常收尾是 `}`（提取后）或原文以 `}` 结尾；没有 ⇒ 几乎肯定是
                // `max_output_tokens` 截断 ⇒ 该**调大上限 / 精简输出**，而不是去怪模型"格式不听话"。
                const std::string t{util::Trim(ej)};
                const bool looksTruncated = t.empty() || (t.back() != '}' && t.back() != ']');
                log::Warn("Extractor 输出不是合法的 StateDiff JSON（本章不回写状态）"
                          "· 总长 {} 字 · {}：\n{}",
                          t.size(),
                          looksTruncated ? "**疑似被 max_output_tokens 截断**（结尾不是 } ）"
                                         : "结尾正常，疑似格式/字段问题",
                          t.substr(0, 800));
            }
        }
        if (!hasDiff) {
            // S55：**解析失败也重试**（原先直接 `break` ⇒ `max_validate_retries` 形同虚设）。
            // 实测：模型这一轮吐出**未转义的英文引号**（中文对话里直接写 `"`）⇒ `yyjson` 语法失败。
            // 模型有随机性，重试往往就过；而"不重试"的代价是**整章状态不回写** ⇒
            // `canon_logs` 空 ⇒ **auto 前置①永远过不了**（`--novel-run auto` 永远被拒）。
            if (attempt < maxValidateRetries) {
                parseFailHint =
                    "\n\n【上一版输出**不是合法 JSON**（解析失败）。最常见原因是**字符串里的英文引号"
                    "没有转义** —— 中文对话中的引号请写成 `\\\"`，或直接改用中文引号「」；"
                    "另外不要输出 markdown 代码块/```json 围栏。请**重新输出完整、合法的 JSON**】\n";
                continue;
            }
            break; // 用完重试次数仍不合法 → 不提交（重做也没用）
        }
        parseFailHint.clear(); // 解析成功 → 提示作废

        // S8（闭环回写）：门禁 G1–G5 + 14 块事务。**门禁拒绝 ≠ 生成失败**：正文已落盘
        // （`07` §2.3 的失败处理）；其中 G2 的机器校验失败则回到本阶段重做。
        novelcore::CommitContext cctx;
        cctx.chapter_id = req.chapter_id;
        cctx.review_pass = reviewPassed;
        cctx.review_verdict = reviewPassed ? "PASS" : "FAIL";
        // S9：`auto` 模式（门禁 G1–G5 全满足才写 CANON）；manual 默认写 PROPOSED
        cctx.canon_mode = req.canon_mode.empty() ? "manual" : req.canon_mode;
        cctx.chapter_summary = summary;
        cctx.snapshot_dir = req.snapshot_dir;
        // S12：工程根优先用调用方给的（`NovelRunLoop` 知道它）；否则退回 novel.db 的父目录
        std::filesystem::path projectRoot;
        if (!req.project_dir.empty()) {
            projectRoot = util::PathFromUtf8(req.project_dir);
        } else if (novelcore::NovelDb::Instance().isOpen()) {
            projectRoot = novelcore::NovelDb::Instance().path().parent_path();
        }
        if (!projectRoot.empty()) {
            if (cctx.snapshot_dir.empty()) {
                cctx.snapshot_dir = util::PathToUtf8(projectRoot / "snapshots");
            }
            // S11：K 校验要用它找 `work/`（K12）与降级账（K28）
            cctx.project_dir = util::PathToUtf8(projectRoot);
        }
        commit = novelcore::CommitChapterState(*db_, diff, cctx);
        if (commit.ok) {
            break;
        }
        // 只有**机器校验**（G2 的 K01–K29）挡下才值得重做提取：G1（评审）/G3（快照）/G4（契约）
        // 类原因重跑 extractor 不会变好（`07` §2.3 的失败处理），别浪费调用。
        if (commit.failed_check_ids.empty()) {
            break;
        }
        for (const std::string& id : commit.failed_check_ids) {
            ++checkFailCounts[id];
        }
        if (attempt >= maxValidateRetries) {
            break;
        }
        ++result.validation_retries;
    }

    (void)mem.Write({.kind = "chapter_summary",
                     .chapter_id = req.chapter_id,
                     .content = summary,
                     .summary = summary});
    (void)g.LogAudit("agent", "generate_chapter", "chapter", req.chapter_id,
                     fmt::format("revisions={} validate_retries={}", revisions,
                                 result.validation_retries));

    if (hasDiff) {
        // S9（`09` §2.2 S4/S9）：契约类问题的观测量 —— 缺失引用处数 + G4（契约非空/合法）失败。
        // 只认**最后一次尝试**的结论（重做的中间轮不重复计数）。
        for (const auto& iss : commit.gates.issues) {
            if (iss.code == "contract") {
                ++result.missing_entity_refs;
            } else if (iss.code.starts_with("K02")) {
                // S11：K02 `entity.exists` 的失败同样是「引用不存在实体」，计入 S9 的观测量
                ++result.missing_entity_refs;
            }
        }
        if (!commit.ok && !commit.gates.g4_diff_valid) {
            ++result.contract_failures;
        }
        result.state_committed = commit.ok && !commit.skipped;
        result.state_skipped = commit.skipped;
        lastChecksDescribe = commit.checks_describe;
        result.commit_note = commit.ok ? (commit.skipped ? "已提交过（幂等跳过）" : "状态已回写")
                                       : commit.error;
        if (!commit.ok) {
            log::Warn("章节 {} 的状态回写未提交：{}", req.chapter_id, commit.error);
        } else if (!commit.skipped) {
            log::Info("章节 {} 状态已回写：{}", req.chapter_id,
                      commit.applied.size() > 0 ? commit.applied.front()
                                                : std::string{"（无变化块）"});
        }
        // S11/S12：把「最终仍未通过」的机器校验项交给运行循环（`09` §2.2 S1 的唯一数据来源）。
        // 重做后通过的**不算失败**（否则会误报停止条件）。
        if (commit.ok) {
            result.failed_check_ids.clear();
        } else {
            result.failed_check_ids.clear();
            for (const auto& [id, n] : checkFailCounts) {
                for (int i = 0; i < n; ++i) {
                    result.failed_check_ids.push_back(id);
                }
            }
            if (!result.failed_check_ids.empty()) {
                log::Warn("章节 {} 的 K01–K29 校验未通过（重做 {} 次）：{}", req.chapter_id,
                          result.validation_retries, commit.checks_describe);
            }
        }
    } else {
        result.commit_note = "extractor 没有给出 StateDiff，未回写状态";
    }

    // S9（`09` §2.4）：单章成本账（含重试）—— `cost_report.json` 与停止条件 S5 的数据来源
    for (const auto& c : result.calls) {
        ++result.llm_calls;
        if (c.tier == "high") {
            ++result.high_tier_calls;
        }
    }
    result.stages = stages;
    result.review_passed = reviewPassed;
    // S19：T15 STATE_VALIDATE 的产物 → `13_validation.json`（`03` §2.7）——
    // 记门禁结论 + 机器校验摘要（`06` §2.3），供崩溃恢复与事后复核看"当时为什么放行/拒绝"
    {
        std::string ids = "[";
        for (std::size_t i = 0; i < result.failed_check_ids.size(); ++i) {
            ids += (i == 0 ? "" : ",") + std::string{"\""} + EscapeJson(result.failed_check_ids[i]) +
                   "\"";
        }
        ids += "]";
        (void)novelcore::WriteStageArtifact(
            workDir, ord, "STATE_VALIDATE",
            fmt::format(R"({{"committed":{},"skipped":{},"validate_retries":{},"failed_check_ids":{},"checks_describe":"{}"}})",
                        result.state_committed ? "true" : "false",
                        result.state_skipped ? "true" : "false", result.validation_retries, ids,
                        EscapeJson(lastChecksDescribe)),
            stateHash("STATE_VALIDATE"));
    }
    result.semantic_only = !reviewPassed && result.contract_failures == 0 &&
                           result.missing_entity_refs == 0;
    Report(progressCb, Phase::Done, 100, "完成");
    log::Info("GenerateChapter 完成：章={} 正文 {} 字 修订 {}", req.chapter_id, body.size(),
              revisions);
    return result;
}

bool NovelDirector::RunSelfCheck() {
    db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) return false;
    // 最小表
    if (auto r = ::shine::novelcore::NovelDb::ApplyCanonicalSchema(mem); !r) {
        log::Error("Director 自检：建表失败 {}", r.error().message);
        return false;
    }

    novelcore::NovelGraph g(mem);
    auto pov = g.UpsertEntity({.kind = std::string{novelcore::kind::person}, .name = "林默"});
    auto ch = g.UpsertChapter({.ord = 1, .title = "第一章", .pov_entity_id = pov.value_or(0)});
    if (!ch) return false;

    // Mock LLM：按 role 返回不同 JSON
    int planCalls = 0;
    int extractCalls = 0;   // S12：第 1 版故意不合规，用来验证「回 EXTRACT 重做」
    bool retryHintSeen = false; // S12：重做时把失败清单喂回给 extractor 了吗
    int roleMask = 0;           // S16：哪些 LlmRole 真的到过回调（09-7 的证据）
    int writerCalls = 0;        // S19：Writer 被请求了几次（验 P4 的正文复用）
    LlmCallFn mock = [&](LlmRole role, std::string_view instructions, std::string_view user)
        -> std::expected<std::string, AgentError> {
        const std::string ins{instructions};
        // S16（09-7）：role 是**真传**过来的（不是从 prompt 文本猜的）—— 逐个记下来，
        // 结尾断言四个 role 都到过（否则「按阶段路由」只是空话）
        roleMask |= 1 << static_cast<int>(role);
        if (ins.find("规划器") != std::string::npos || ins.find("planner") != std::string::npos) {
            ++planCalls;
            return std::string{R"({"output_text":"{\"chapter_title\":\"雪原\",\"goal\":\"逃脱\",\"scenes\":[{\"ord\":1,\"location\":\"黑森林\",\"cast\":[\"林默\"],\"goal\":\"活下来\",\"conflict\":\"追兵\",\"result\":\"受伤\",\"emotion\":\"恐惧\"}],\"foreshadowing\":[\"plant|黑戒指\"],\"ending_hook\":\"林中异响\"}"})"};
        }
        if (ins.find("审校") != std::string::npos || ins.find("critic") != std::string::npos) {
            // 第一次 fail 触发 revision，之后 pass
            static int n = 0;
            if (n++ == 0) {
                return std::string{R"({"output_text":"{\"passed\":false,\"issues\":[{\"type\":\"pov\",\"severity\":\"high\",\"description\":\"疑似越界\"}]}"})"};
            }
            return std::string{R"({"output_text":"{\"passed\":true,\"issues\":[]}"})"};
        }
        if (ins.find("抽取") != std::string::npos || ins.find("extractor") != std::string::npos) {
            // S8：extractor 的产物是**完整 StateDiff**（`02` §2.5），不再是只有 summary 的壳。
            // 这里造最小一份：一条角色状态 + 一条伏笔 → 提交后库里应能查到（端到端判据）。
            ++extractCalls;
            if (extractCalls >= 2) {
                // 重做轮的 user 消息里必须带上上一轮的失败清单（否则"重做"等于盲猜）
                retryHintSeen = std::string{user}.find("未通过机器校验") != std::string::npos;
            }
            novelcore::StateDiff d;
            d.chapter_id = *ch;
            d.input_state_hash = "sha1:director-selfcheck";
            if (extractCalls == 1) {
                // S12：第 1 版引用不存在的实体（K02 + D1）→ 提交门禁拒绝 → 必须回到本阶段重做；
                // 顺带断言重做时把失败清单喂了回来（`ins` 里应含 K01–K29 的报告）
                d.characters.push_back({.entity_id = 999999,
                                        .body_state = "不该存在",
                                        .mind_state = "不该存在",
                                        .reason = "S12 自检：故意引用不存在的实体"});
            } else {
                d.characters.push_back({.entity_id = *pov,
                                        .body_state = "旧伤",
                                        .mind_state = "警觉",
                                        .goal = "活着走出雪原",
                                        .reason = "自检：雪原负伤前行"});
                d.foreshadows.push_back({.op = "new",
                                         .title = "黑戒指",
                                         .content = "雪原上捡到的黑戒指",
                                         .status = "PLANTED",
                                         .setup_ch = *ch,
                                         .importance = 70});
            }
            const std::string inner = novelcore::StateDiffToJson(d);
            yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
            yyjson_mut_val* root = yyjson_mut_obj(doc);
            yyjson_mut_doc_set_root(doc, root);
            yyjson_mut_obj_add_strncpy(doc, root, "output_text", inner.data(), inner.size());
            std::size_t len = 0;
            char* text = yyjson_mut_val_write(root, 0, &len);
            yyjson_mut_doc_free(doc);
            std::string resp = text != nullptr ? std::string{text, len} : std::string{"{}"};
            std::free(text);
            return resp;
        }
        // writer
        ++writerCalls;
        return std::string{R"({"output_text":"雪原上只剩下风声。林默按住伤口，继续向前。"})"};
    };

    NovelDirector dir(mem, mock);
    std::vector<Phase> phases;
    const std::filesystem::path snapRoot =
        std::filesystem::temp_directory_path() / "shine_director_commit_check";
    // S19：阶段产物的工程根（`03` §2.7 的 `work/ch<NNN>/` 落在这里）
    const std::filesystem::path stageRoot =
        std::filesystem::temp_directory_path() / "shine_director_stage_check";
    std::error_code ec;
    std::filesystem::remove_all(snapRoot, ec);
    std::filesystem::remove_all(stageRoot, ec);
    auto out = dir.GenerateChapter(
        {.chapter_id = *ch,
         .user_hint = "写第一章",
         .max_revisions = 3,
         .snapshot_dir = util::PathToUtf8(snapRoot),
         .project_dir = util::PathToUtf8(stageRoot)},
        [&](const GenerateChapterProgress& p) { phases.push_back(p.phase); });
    if (!out) {
        log::Error("Director 自检：Generate 失败 {}", out.error().message);
        return false;
    }
    const bool hasBody = out->body.find("雪原") != std::string::npos;
    const bool hasRev = out->revisions >= 1;
    const bool hasPhases =
        std::find(phases.begin(), phases.end(), Phase::Plan) != phases.end() &&
        std::find(phases.begin(), phases.end(), Phase::Write) != phases.end() &&
        std::find(phases.begin(), phases.end(), Phase::Done) != phases.end();
    auto saved = g.GetChapter(*ch);
    const bool savedOk = saved && saved->body.find("雪原") != std::string::npos;
    auto summaries = novelcore::NovelMemory(mem).RecentChapterSummaries(1);
    const bool memOk = summaries && !summaries->empty();
    // S8：**生成一章后世界状态确有变化**（本 S 的判据）—— 端到端经 GenerateChapter 走一遍
    auto statusAfter = g.GetLatestCharacterStatus(*pov, *ch);
    auto openFores = g.ListOpenForeshadows();
    const bool committed = out->state_committed && statusAfter && statusAfter->body_state == "旧伤" &&
                           openFores && !openFores->empty();
    // S12：第 1 版 StateDiff 故意不合规 → 必须**回到 EXTRACT 重做**且重做时带上失败清单，
    // 第 2 版才提交成功；重做后通过 ⇒ 不得把失败算到 `failed_check_ids`（否则 S1 会误停）
    const bool retried = out->validation_retries >= 1 && retryHintSeen && extractCalls >= 2;
    const bool noStaleFailures = out->failed_check_ids.empty();
    // ———— S19（`03` §2.7）：阶段产物落盘 + 断点续跑（P1/P2/P4/P5）————
    bool stagesOnDisk = true;
    for (const std::string_view stage : {std::string_view{"CONTEXT_ASSEMBLY"},
                                         std::string_view{"SCENE_EVENT_ORDER"},
                                         std::string_view{"CHAPTER_REVIEW"},
                                         std::string_view{"STATE_VALIDATE"}}) {
        std::error_code e2;
        if (!std::filesystem::exists(
                novelcore::ChapterWorkDir(stageRoot, 1) /
                    std::string{novelcore::StageFileName(stage)},
                e2)) {
            stagesOnDisk = false;
        }
    }
    // P1：把**当前**哈希写进盘上（模拟"状态没变的重跑"）→ 该阶段应判为可跳过
    const std::string_view oneStage[] = {"CONTEXT_ASSEMBLY"};
    const std::string currentHash =
        novelcore::ComputeInputStateHash(mem, *ch, "text", "CONTEXT_ASSEMBLY");
    (void)novelcore::WriteStageArtifact(stageRoot, 1, "CONTEXT_ASSEMBLY", "{}", currentHash);
    const bool skipWhenHashMatches =
        novelcore::FindResumeIndex(mem, *ch, 1, stageRoot, oneStage) == std::size(oneStage);
    // P2：哈希不一致（状态变了）→ 该阶段及其下游必须重跑
    (void)novelcore::WriteStageArtifact(stageRoot, 1, "CONTEXT_ASSEMBLY", "{}", "deadbeef");
    const bool redoWhenHashDiffers =
        novelcore::FindResumeIndex(mem, *ch, 1, stageRoot, oneStage) == 0;
    // P5：产物缺失 → 从该阶段重跑
    std::filesystem::remove(novelcore::ChapterWorkDir(stageRoot, 1) /
                                std::string{novelcore::StageFileName("CONTEXT_ASSEMBLY")},
                            ec);
    const bool redoWhenMissing =
        novelcore::FindResumeIndex(mem, *ch, 1, stageRoot, oneStage) == 0;
    // 端到端：resume 重跑一次 —— `chapters.body` 已落库 ⇒ **Writer 不该再被请求**（P4）；
    // 而正文提交后世界状态已变 ⇒ 哈希不一致 ⇒ **Planner 必须重跑**（P2）。
    const int writerBefore = writerCalls;
    const int planBefore = planCalls;
    // ⚠️ `retryHintSeen` 会被第二次调用**重写**（mock 的检测是赋值语义）→ 日志要打快照
    const bool hintSeenAfterFirst = retryHintSeen;
    const auto out2 = dir.GenerateChapter({.chapter_id = *ch,
                                           .user_hint = "写第一章",
                                           .max_revisions = 3,
                                           .snapshot_dir = util::PathToUtf8(snapRoot),
                                           .project_dir = util::PathToUtf8(stageRoot),
                                           .resume = true},
                                          nullptr);
    const bool bodyReused = out2.has_value() && writerCalls == writerBefore;
    const bool planRedone = out2.has_value() && planCalls > planBefore;
    // S20（差距 03-11）：阶段产物**落库**（`prompt_artifacts`，chain=text）——
    // 这是 K23（`prompt.state_hash_match`）的受检对象；此前该表没有写入方 ⇒ K23 恒 `n/a`。
    const auto stageRec = novelcore::LoadStageHashRecord(mem, *ch);
    const bool promptRecorded =
        stageRec.has_value() && !stageRec->stage.empty() && !stageRec->input_state_hash.empty();

    // S16（09-7）：四个 role 都必须**真的到过回调**（Planner=1, Writer=2, Critic=4, Extractor=8）
    const int kAllRoles = (1 << static_cast<int>(LlmRole::Planner)) |
                          (1 << static_cast<int>(LlmRole::Writer)) |
                          (1 << static_cast<int>(LlmRole::Critic)) |
                          (1 << static_cast<int>(LlmRole::Extractor));
    const bool rolesOk = roleMask == kAllRoles;
    if (!hasBody || !hasRev || !hasPhases || !savedOk || !memOk || !committed || !retried ||
        !noStaleFailures || !rolesOk || !stagesOnDisk || !skipWhenHashMatches ||
        !redoWhenHashDiffers || !redoWhenMissing || !bodyReused || !planRedone ||
        !promptRecorded) {
        log::Error("Director 自检失败：body={} rev={} phases={} saved={} mem={} committed={} "
                   "retried={} hint={} extractCalls={} staleFail={} roles=0b{:04b}（期望 0b{:04b}）"
                   " stagesOnDisk={} skipMatch={} redoDiffers={} redoMissing={} bodyReused={} "
                   "planRedone={} promptRecorded={}（{}）",
                   hasBody, hasRev, hasPhases, savedOk, memOk, committed, retried,
                   hintSeenAfterFirst, extractCalls, !noStaleFailures, roleMask, kAllRoles,
                   stagesOnDisk,
                   skipWhenHashMatches, redoWhenHashDiffers, redoWhenMissing, bodyReused, planRedone,
                   promptRecorded, out->commit_note);
        return false;
    }
    log::Info("Director 自检通过（Plan→Write→Review→Revise→Extract→**Commit 回写**→Save；"
              "S12 机器校验失败→回 EXTRACT 重做 {} 次；S16 四个 LlmRole 均到回调 0b{:04b}；"
              "S19 阶段产物落盘 + P1/P2/P5 断点判定 + P4 正文复用；"
              "S20 阶段产物落库 prompt_artifacts（K23 的受检对象））",
              out->validation_retries, roleMask);
    {
        const char* path = std::getenv("SHINE_NOVEL_CHECK_OUT");
        if (path && *path) {
            FILE* f = std::fopen(path, "ab");
            if (f) {
                const char* line = "director:ok\n";
                std::fwrite(line, 1, std::strlen(line), f);
                std::fclose(f);
            }
        }
    }
    return true;
}

} // namespace shine::agent
