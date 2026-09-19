#include "agent/NovelDirector.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "novel/NovelCommit.h" // S8：StateDiff + 提交门禁 + 14 块事务
#include "novel/NovelGraph.h"
#include "novel/NovelMemory.h"
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
#include <random>
#include <system_error>
#include <thread>

namespace shine::agent {
namespace {

[[nodiscard]] std::string EscapeJson(std::string_view s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (const char c : s) {
        if (c == '"' || c == '\\') {
            o += '\\';
            o += c;
        } else if (c == '\n') {
            o += "\\n";
        } else if (c == '\r') {
            o += "\\r";
        } else if (c == '\t') {
            o += "\\t";
        } else {
            o += c;
        }
    }
    return o;
}

void Report(const std::function<void(const GenerateChapterProgress&)>& cb, Phase p, int pct,
            std::string_view note = {}) {
    if (!cb) return;
    GenerateChapterProgress pr;
    pr.phase = p;
    pr.percent = pct;
    pr.note = std::string{note};
    cb(pr);
}

// —— S9（`09` §2.4 模型分层 / §2.3 重试退避）——
// 档位只用于**记账**（`cost_report.json` 与单章「高档调用 ≤ 8」）：按阶段路由是 09-7，本步不做。
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
    // `09` §2.3：同 Provider 请求间隔 ≥ 200ms（防限流）
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
        auto r = call_(instructions, user);
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

    // PLAN
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

    // WRITE（可 stream）
    Report(progressCb, Phase::Write, 50, "Writer");
    const std::string writeUser =
        fmt::format("{}\n\n【章节计划 JSON】\n{}\n\n【写出正文】", ctx->text, result.plan_json);
    std::string body;
    if (stream_) {
        auto onDelta = [&](std::string_view d) {
            GenerateChapterProgress pr;
            pr.phase = Phase::Write;
            pr.percent = 55;
            pr.text_delta = std::string{d};
            progressCb(pr);
        };
        const std::int64_t t0 = util::MonotonicMillis();
        auto wr = stream_(LoadPrompt("writer"), writeUser, onDelta);
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
    }
    result.revisions = revisions;
    result.body = body;
    result.title = ch->title;

    // EXTRACT（`07` §2.2 的 ②）：LLM 只负责"从正文里看出变化"，**基线由代码读**
    Report(progressCb, Phase::Extract, 90, "Extractor");
    const std::string exUser = fmt::format("【计划】\n{}\n\n【正文】\n{}", result.plan_json, body);
    auto er = CallLlm("extractor", exUser, "EXTRACT", req, result.calls);
    std::string summary = body.substr(0, std::min<std::size_t>(body.size(), 80));
    novelcore::StateDiff diff;
    bool hasDiff = false;
    if (er) {
        const auto ej = ExtractOutputText(*er);
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
            log::Warn("Extractor 输出不是合法的 StateDiff JSON（本章不回写状态）：{}",
                      util::Trim(ej).substr(0, 200));
        }
    }

    // SAVE
    Report(progressCb, Phase::Save, 95, "写入章节");
    novelcore::ChapterRow row = *ch;
    row.body = body;
    row.words = static_cast<int>(body.size());
    row.status = "review";
    if (auto id = g.UpsertChapter(row); !id) {
        return std::unexpected(AgentError{"save", id.error().message});
    }
    (void)mem.Write({.kind = "chapter_summary",
                     .chapter_id = req.chapter_id,
                     .content = summary,
                     .summary = summary});
    (void)g.LogAudit("agent", "generate_chapter", "chapter", req.chapter_id,
                     fmt::format("revisions={}", revisions));

    // S8（闭环回写）：门禁 G1–G5 + 14 块事务。G1 = 本章评审结论；G2 = 提交模块自己能跑的机器校验
    // （`06` 的 K01–K29 尚未全量实现，见 `NovelCommit.h` 的说明）。**门禁拒绝 ≠ 生成失败**：
    // 正文已落盘，状态回写留给修订后重提（`07` §2.3 的失败处理）。
    if (hasDiff) {
        novelcore::CommitContext cctx;
        cctx.chapter_id = req.chapter_id;
        cctx.review_pass = reviewPassed;
        cctx.review_verdict = reviewPassed ? "PASS" : "FAIL";
        // S9：`auto` 模式（门禁 G1–G5 全满足才写 CANON）；manual 默认写 PROPOSED
        cctx.canon_mode = req.canon_mode.empty() ? "manual" : req.canon_mode;
        cctx.chapter_summary = summary;
        cctx.snapshot_dir = req.snapshot_dir;
        if (cctx.snapshot_dir.empty() && novelcore::NovelDb::Instance().isOpen()) {
            // 工程根 = novel.db 的父目录（与 `NovelImageStore::ProjectDirOfDb` 同口径）
            cctx.snapshot_dir =
                util::PathToUtf8(novelcore::NovelDb::Instance().path().parent_path() / "snapshots");
        }
        const novelcore::CommitResult commit = novelcore::CommitChapterState(*db_, diff, cctx);
        // S9（`09` §2.2 S4/S9）：契约类问题的观测量 —— 缺失引用处数 + G4（契约非空/合法）失败
        for (const auto& iss : commit.gates.issues) {
            if (iss.code == "contract") {
                ++result.missing_entity_refs;
            }
        }
        if (!commit.ok && !commit.gates.g4_diff_valid) {
            ++result.contract_failures;
        }
        result.state_committed = commit.ok && !commit.skipped;
        result.state_skipped = commit.skipped;
        result.commit_note = commit.ok ? (commit.skipped ? "已提交过（幂等跳过）" : "状态已回写")
                                       : commit.error;
        if (!commit.ok) {
            log::Warn("章节 {} 的状态回写未提交：{}", req.chapter_id, commit.error);
        } else if (!commit.skipped) {
            log::Info("章节 {} 状态已回写：{}", req.chapter_id, commit.applied.size() > 0
                                                          ? commit.applied.front()
                                                          : std::string{"（无变化块）"});
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
    LlmCallFn mock = [&](std::string_view instructions, std::string_view)
        -> std::expected<std::string, AgentError> {
        const std::string ins{instructions};
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
            novelcore::StateDiff d;
            d.chapter_id = *ch;
            d.input_state_hash = "sha1:director-selfcheck";
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
        return std::string{R"({"output_text":"雪原上只剩下风声。林默按住伤口，继续向前。"})"};
    };

    NovelDirector dir(mem, mock);
    std::vector<Phase> phases;
    const std::filesystem::path snapRoot =
        std::filesystem::temp_directory_path() / "shine_director_commit_check";
    std::error_code ec;
    std::filesystem::remove_all(snapRoot, ec);
    auto out = dir.GenerateChapter(
        {.chapter_id = *ch,
         .user_hint = "写第一章",
         .max_revisions = 3,
         .snapshot_dir = util::PathToUtf8(snapRoot)},
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
    if (!hasBody || !hasRev || !hasPhases || !savedOk || !memOk || !committed) {
        log::Error("Director 自检失败：body={} rev={} phases={} saved={} mem={} committed={}（{}）",
                   hasBody, hasRev, hasPhases, savedOk, memOk, committed, out->commit_note);
        return false;
    }
    log::Info("Director 自检通过（Plan→Write→Review→Revise→Extract→**Commit 回写**→Save）");
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
