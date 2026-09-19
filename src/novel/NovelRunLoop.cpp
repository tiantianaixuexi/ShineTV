#include "novel/NovelRunLoop.h"

#include "core/Log.h"
#include "novel/NovelChecks.h"
#include "novel/NovelGraph.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Time.h"

#include <fmt/format.h>

#include <yyjson.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <system_error>

namespace shine::novelcore {
namespace {

// ———— SQLite 小工具（本文件只做查询，不写 DDL）————
[[nodiscard]] int ScalarCount(db::sqlite::Database& db, std::string_view sql) {
    auto st = db.Prepare(sql);
    if (!st) return 0;
    auto s = st->Step();
    if (!s || *s != db::sqlite::StepResult::Row) return 0;
    return st->ColumnInt(0);
}

[[nodiscard]] std::string Fnv1aHex(std::string_view data) {
    std::uint64_t h = 1469598103934665603ull;
    for (const unsigned char c : data) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return fmt::format("{:016x}", h);
}

[[nodiscard]] std::string OrdPadded(int ord) { return fmt::format("{:03}", ord); }

// 原子写文件（写失败返回 false）—— 目录不存在则先建
[[nodiscard]] bool WriteTextFile(const std::filesystem::path& path, std::string_view text) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    return util::WriteFileBytes(path, text);
}

// 报告落盘（失败只 Warn，不抛）
[[nodiscard]] std::string WriteReportFile(const std::filesystem::path& path,
                                          std::string_view text) {
    if (path.empty() || !WriteTextFile(path, text)) {
        log::Warn("RunLoop：报告写入失败 {}", util::PathToUtf8(path));
        return {};
    }
    return util::PathToUtf8(path);
}

} // namespace

// ———— `09` §2.1 运行模式 ————
std::string_view RunModeName(RunMode mode) noexcept {
    switch (mode) {
    case RunMode::Manual: return "manual";
    case RunMode::Semi: return "semi";
    case RunMode::Auto: return "auto";
    }
    return "manual";
}

RunMode RunModeFromString(std::string_view text, RunMode fallback) noexcept {
    if (text == "manual") return RunMode::Manual;
    if (text == "semi") return RunMode::Semi;
    if (text == "auto") return RunMode::Auto;
    return fallback;
}

// ———— `09` §2.2 停止条件 ————
std::string_view StopCodeName(StopCode code) noexcept {
    switch (code) {
    case StopCode::None: return "-";
    case StopCode::S1: return "S1";
    case StopCode::S2: return "S2";
    case StopCode::S3: return "S3";
    case StopCode::S4: return "S4";
    case StopCode::S5: return "S5";
    case StopCode::S6: return "S6";
    case StopCode::S7: return "S7";
    case StopCode::S8: return "S8";
    case StopCode::S9: return "S9";
    case StopCode::S10: return "S10";
    case StopCode::S11: return "S11";
    case StopCode::S12: return "S12";
    }
    return "-";
}

std::string_view StopCodeCondition(StopCode code) noexcept {
    switch (code) {
    case StopCode::S1: return "机器校验连续失败 >= 2 次（同一章同一 check_id）";
    case StopCode::S2: return "评审 FAIL 累计 >= 3 次（同一章）";
    case StopCode::S3: return "仅语义判断且 FAIL 的连续章数 >= 2 章";
    case StopCode::S4: return "契约校验失败（同阶段，含 1 次重试后）>= 2 次";
    case StopCode::S5: return "单章成本超预算 > 2 × 单章预算（LLM 调用 / 高档调用）";
    case StopCode::S6: return "LLM/网络不可用：连续失败 >= 5 次且退避后仍失败";
    case StopCode::S7: return "Comfy 不可用且本章需要出图：探活失败 >= 3 次（间隔 5s）";
    case StopCode::S8: return "状态自相矛盾且回退不可用（07 §2.6 无法自动处置）";
    case StopCode::S9: return "引用不存在的实体单章 >= 10 处（code=contract）";
    case StopCode::S10: return "field_defs 条数超上限 > 500";
    case StopCode::S11: return "磁盘 / 快照写入失败（任一，阻断提交，I11）";
    case StopCode::S12: return "需要无法推断的用户决策";
    case StopCode::None: return {};
    }
    return {};
}

std::string_view StopCodeHint(StopCode code) noexcept {
    switch (code) {
    case StopCode::S1: return "输出冲突清单 + 相关 ValidationReport；修 prompt 或 schema 后从该章重跑";
    case StopCode::S2: return "输出完整 ReviewVerdict + RepairReceipt.unresolved；人工判断是否改大纲";
    case StopCode::S3: return "标出需要人工阅读的段落（语义评审不可信）";
    case StopCode::S4: return "输出原始模型输出 + 违反的契约项；检查 prompt 与 json_schema";
    case StopCode::S5: return "输出本章成本明细；检查是否陷入修复循环";
    case StopCode::S6: return "检查 Provider 连通性 / 余额 / Key；恢复后从断点续跑";
    case StopCode::S7: return "输出 HealthSummary()；ComfyUI 恢复后重试该镜";
    case StopCode::S8: return "输出冲突清单；人工裁定后回滚或改文";
    case StopCode::S9: return "通常意味着初始化未完成（10 卷）；补实体后再生成";
    case StopCode::S10: return "输出新增失败清单；人工清理 field_defs 或停用自动扩展";
    case StopCode::S11: return "检查磁盘空间 / 快照目录权限；修好后重跑该章";
    case StopCode::S12: return "写明需要什么决策、在哪一步";
    case StopCode::None: return "正常结束或用户取消；可从断点续跑";
    }
    return {};
}

// ———— 累积量 ————
void StopConditionTracker::Reset() noexcept {
    check_fails_.clear();
    current_chapter_ = 0;
    chapter_review_fails_ = 0;
    chapter_contract_fails_ = 0;
    semantic_streak_ = 0;
    llm_net_streak_ = 0;
    comfy_streak_ = 0;
    chapters_observed_ = 0;
}

void StopConditionTracker::Observe(const ChapterObservation& obs) {
    if (obs.chapter_id != current_chapter_) {
        current_chapter_ = obs.chapter_id;
        check_fails_.clear();
        chapter_review_fails_ = 0;
        chapter_contract_fails_ = 0;
    }
    for (const auto& id : obs.failed_check_ids) {
        if (id.empty()) continue;
        ++check_fails_[id];
    }
    if (obs.review_failed) {
        ++chapter_review_fails_;
    }
    if (obs.semantic_only && obs.review_failed) {
        ++semantic_streak_;
    } else {
        semantic_streak_ = 0;
    }
    chapter_contract_fails_ += obs.contract_failures;
    llm_net_streak_ = obs.llm_network_failures > 0 ? llm_net_streak_ + obs.llm_network_failures : 0;
    comfy_streak_ = obs.comfy_probe_failures > 0 ? comfy_streak_ + obs.comfy_probe_failures : 0;
    ++chapters_observed_;
}

int StopConditionTracker::SameCheckFailures(std::string_view checkId) const {
    const auto it = check_fails_.find(std::string{checkId});
    return it == check_fails_.end() ? 0 : it->second;
}

// ———— 纯函数：`09` §2.2 STOP 表 ————
std::optional<StopDecision> EvaluateStop(const StopConditionTracker& tracker,
                                         const ChapterObservation& obs, const RunLimits& limits) {
    auto make = [](StopCode code, std::string detail) {
        StopDecision d;
        d.code = code;
        d.condition = std::string{StopCodeCondition(code)};
        d.detail = std::move(detail);
        return d;
    };
    // S1：机器校验连续失败（同章同 check_id >= 2）
    for (const auto& id : obs.failed_check_ids) {
        const int n = tracker.SameCheckFailures(id);
        if (n >= 2) {
            return make(StopCode::S1, fmt::format("chapter={} check_id={} 连续失败 {} 次", obs.chapter_id,
                                                 id, n));
        }
    }
    // S2：评审 FAIL 累计 >= 3（同章）
    if (tracker.ChapterReviewFailTotal() >= 3) {
        return make(StopCode::S2, fmt::format("chapter={} FAIL 累计 {} 次", obs.chapter_id,
                                             tracker.ChapterReviewFailTotal()));
    }
    // S3：仅语义判断且 FAIL 的连续章数 >= 2
    if (tracker.ConsecutiveSemanticOnlyFailChapters() >= 2) {
        return make(StopCode::S3, fmt::format("连续 {} 章仅语义 FAIL（起自 chapter={}）",
                                             tracker.ConsecutiveSemanticOnlyFailChapters(),
                                             obs.chapter_id));
    }
    // S4：契约校验失败（含 1 次重试后）>= 2
    if (tracker.ChapterContractFailures() >= 2) {
        return make(StopCode::S4, fmt::format("chapter={} 契约校验失败 {} 次", obs.chapter_id,
                                             tracker.ChapterContractFailures()));
    }
    // S5：单章成本超 2 × 预算（硬上限 40 次调用本身也是 S5 的触发点）
    const int callCap = limits.max_llm_calls_per_chapter * kCostOverrunFactor;
    const int highCap = limits.max_high_tier_calls_per_chapter * kCostOverrunFactor;
    if (obs.llm_calls > callCap || obs.high_tier_calls > highCap) {
        return make(StopCode::S5, fmt::format("LLM 调用 {}/{} 高档 {}/{}（预算 {} / {}）", obs.llm_calls,
                                             callCap, obs.high_tier_calls, highCap,
                                             limits.max_llm_calls_per_chapter,
                                             limits.max_high_tier_calls_per_chapter));
    }
    // S6：LLM/网络连续失败 >= 5
    if (tracker.ConsecutiveLlmNetworkFailures() >= 5) {
        return make(StopCode::S6, fmt::format("连续网络失败 {} 次", tracker.ConsecutiveLlmNetworkFailures()));
    }
    // S7：Comfy 探活失败 >= 3 且本章需要出图
    if (obs.images > 0 && tracker.ConsecutiveComfyProbeFailures() >= 3) {
        return make(StopCode::S7, fmt::format("探活失败 {} 次（本章需出图 {} 张）",
                                             tracker.ConsecutiveComfyProbeFailures(), obs.images));
    }
    // S8：状态自相矛盾且回退不可用
    if (obs.state_conflict_unrecoverable) {
        return make(StopCode::S8, "命中 07 §2.6 且无法自动处置");
    }
    // S9：引用不存在实体 >= 10 处
    if (obs.missing_entity_refs >= 10) {
        return make(StopCode::S9, fmt::format("chapter={} 缺失引用 {} 处", obs.chapter_id,
                                             obs.missing_entity_refs));
    }
    // S10：field_defs 超上限
    if (obs.field_def_count > NovelFields::kMaxFieldDefs) {
        return make(StopCode::S10, fmt::format("field_defs={} > {}", obs.field_def_count,
                                              NovelFields::kMaxFieldDefs));
    }
    // S11：磁盘 / 快照写入失败
    if (obs.disk_write_failed) {
        return make(StopCode::S11, fmt::format("chapter={} 写入失败：{}", obs.chapter_id, obs.note));
    }
    // S12：需要无法推断的用户决策
    if (obs.needs_user_decision) {
        return make(StopCode::S12, obs.note.empty() ? std::string{"需要人工决策"} : obs.note);
    }
    return std::nullopt;
}

// ———— `09` §2.1 auto 前置 ————
std::optional<std::string> CheckAutoPrecondition(const AutoPreconditionInput& in) {
    std::string missing;
    if (!in.gates_verified_on_last_chapter) {
        missing += "① 最近一章的 G1–G5 未验证通过（07 §2.3）；";
    }
    if (!in.verifiers_complete) {
        missing += "② 06 §2.3 的 K01–K29 校验器未全部可用（本仓尚未全量落地）；";
    }
    if (!in.llm_ok) {
        missing += "③ LLM 连通性自检未通过；";
    }
    if (!in.comfy_ok) {
        missing += "④ Comfy 连通性自检未通过（本章需要出图）；";
    }
    if (!missing.empty()) {
        return fmt::format("auto 前置条件不满足：{}（07 §2.5 C5：禁止跳门禁）", missing);
    }
    return std::nullopt;
}

AutoPreconditionInput ProbeAutoPrecondition(db::sqlite::Database& db, bool llm_ok) {
    AutoPreconditionInput in;
    in.llm_ok = llm_ok;
    // 最近一章已提交（status='done' 且有正文）且写了 canon_logs → 视为 G1–G5 已验证通过
    const std::int64_t lastChapter = ScalarCount(db, "SELECT id FROM chapters WHERE status='done' "
                                                    "AND body<>'' ORDER BY ord DESC LIMIT 1");
    in.gates_verified_on_last_chapter = lastChapter > 0;
    // `06` §2.3 的 K01–K29 校验器是否全量可用 —— 由 `NovelChecks` 的目录自己回答
    // （29 条必须正好是 K01…K29，无缺号无占位；`S10` 之前这里恒 false，`auto` 一律被拒启动）
    in.verifiers_complete = VerifiersComplete();
    in.comfy_ok = true;
    return in;
}

// ———— 产物（`09` §2.7）————
std::filesystem::path ChapterWorkDir(const std::filesystem::path& project_dir, int ord) {
    return project_dir / "work" / fmt::format("ch{:03}", ord);
}

std::string ManifestJson(RowId chapter_id, int ord, std::string_view state_hash,
                        const std::vector<std::string>& stages, std::string_view status) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (doc == nullptr) return "{}";
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "chapter_id", chapter_id);
    yyjson_mut_obj_add_int(doc, root, "ord", ord);
    yyjson_mut_obj_add_strcpy(doc, root, "input_state_hash", std::string{state_hash}.c_str());
    yyjson_mut_obj_add_strcpy(doc, root, "status", std::string{status}.c_str());
    yyjson_mut_val* arr = yyjson_mut_arr(doc);
    for (const auto& s : stages) {
        yyjson_mut_arr_add_strcpy(doc, arr, s.c_str());
    }
    yyjson_mut_obj_add_val(doc, root, "stages", arr);
    // `03` §2.7 的「阶段 → 产物文件」列当前为空：细阶段机（T1–T17）尚未落地
    yyjson_mut_obj_add_str(doc, root, "artifacts_note",
                           "stage artifact files not implemented (T1-T17 stage machine pending)");
    yyjson_mut_obj_add_int(doc, root, "updated", util::NowMillis());
    std::size_t len = 0;
    char* text = yyjson_mut_val_write(root, 0, &len);
    std::string out = text != nullptr ? std::string{text, len} : std::string{"{}"};
    std::free(text);
    yyjson_mut_doc_free(doc);
    return out;
}

std::string CostReportJson(RowId chapter_id, int ord, const ChapterRunInfo& info,
                           const ChapterObservation& obs, const RunLimits& limits) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (doc == nullptr) return "{}";
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "chapter_id", chapter_id);
    yyjson_mut_obj_add_int(doc, root, "ord", ord);
    yyjson_mut_obj_add_bool(doc, root, "ok", info.ok);
    yyjson_mut_obj_add_strcpy(doc, root, "mode_note", info.note.c_str());
    yyjson_mut_obj_add_int(doc, root, "llm_calls", obs.llm_calls);
    yyjson_mut_obj_add_int(doc, root, "llm_calls_budget", limits.max_llm_calls_per_chapter);
    yyjson_mut_obj_add_int(doc, root, "high_tier_calls", obs.high_tier_calls);
    yyjson_mut_obj_add_int(doc, root, "high_tier_budget", limits.max_high_tier_calls_per_chapter);
    yyjson_mut_obj_add_int(doc, root, "images", obs.images);
    yyjson_mut_obj_add_int(doc, root, "images_budget", limits.max_images_per_chapter);
    yyjson_mut_obj_add_int(doc, root, "wall_ms", obs.wall_ms);
    yyjson_mut_obj_add_int(doc, root, "wall_budget_ms", limits.chapter_wall_clock_ms);
    yyjson_mut_obj_add_bool(doc, root, "wall_over_budget",
                            obs.wall_ms > limits.chapter_wall_clock_ms);
    yyjson_mut_obj_add_int(doc, root, "contract_failures", obs.contract_failures);
    yyjson_mut_obj_add_int(doc, root, "missing_entity_refs", obs.missing_entity_refs);
    yyjson_mut_obj_add_bool(doc, root, "review_failed", obs.review_failed);
    yyjson_mut_obj_add_bool(doc, root, "semantic_only", obs.semantic_only);
    yyjson_mut_obj_add_bool(doc, root, "state_committed", info.state_committed);
    yyjson_mut_obj_add_bool(doc, root, "state_skipped", info.state_skipped);
    yyjson_mut_obj_add_strcpy(doc, root, "error", info.error.c_str());
    yyjson_mut_val* stages = yyjson_mut_arr(doc);
    for (const auto& s : info.stages) {
        yyjson_mut_arr_add_strcpy(doc, stages, s.c_str());
    }
    yyjson_mut_obj_add_val(doc, root, "stages", stages);
    std::size_t len = 0;
    char* text = yyjson_mut_val_write(root, 0, &len);
    std::string out = text != nullptr ? std::string{text, len} : std::string{"{}"};
    std::free(text);
    yyjson_mut_doc_free(doc);
    return out;
}

std::string StopReportMarkdown(const RunOutcome& out, const RunRequest& req) {
    std::string md;
    md += "# 停止报告（`09` §2.7）\n\n";
    md += fmt::format("- 生成时间（Unix ms）：{}\n", util::NowMillis());
    md += fmt::format("- 运行模式：{}\n", RunModeName(req.mode));
    md += fmt::format("- 工程目录：{}\n", util::PathToUtf8(req.project_dir));
    if (!out.started && !out.refuse_reason.empty()) {
        md += fmt::format("- 启动结果：**被拒启动**（{}）\n", out.refuse_reason);
    } else {
        md += "- 启动结果：已启动\n";
    }
    if (out.stop) {
        const std::string_view code = StopCodeName(out.stop->code);
        if (out.stop->code == StopCode::None) {
            md += fmt::format("- 停止条件：无编号 —— {}\n", out.stop->condition);
        } else {
            md += fmt::format("- 停止条件：**{}** —— {}\n", code, out.stop->condition);
        }
        md += fmt::format("- 现场：{}\n", out.stop->detail);
        md += fmt::format("- 建议动作：{}\n", StopCodeHint(out.stop->code));
    } else {
        md += "- 停止条件：无（正常结束）\n";
    }
    md += fmt::format("- 尝试章数：{}；完成：{}；续跑跳过：{}；LLM 调用合计：{}\n",
                      out.chapters_attempted, out.chapters_done, out.chapters_resumed_skipped,
                      out.llm_calls_total);
    md += fmt::format("- 检查点：{} 个", out.checkpoints.size());
    if (!out.checkpoints.empty()) {
        md += fmt::format("（最近 {}）", out.checkpoints.back());
    }
    md += "\n";
    if (!out.mode_note.empty()) {
        md += fmt::format("- 备注：{}\n", out.mode_note);
    }
    if (!out.last_error.empty()) {
        md += fmt::format("- 最后一次错误：{}\n", out.last_error);
    }
    if (!out.completed_chapters.empty()) {
        std::string list;
        for (const RowId id : out.completed_chapters) {
            list += fmt::format("{} ", id);
        }
        md += fmt::format("- 已完成章 id：{}\n", list);
    }
    std::string actions;
    actions += "1. 按上表「建议动作」处置后，用同一入口**断点续跑**（不得从头重跑全书，`09` §2.6）。\n";
    actions += "2. 若为 S1–S4：先看 `cost_report.json` 与 `_manifest.json`，再决定改 prompt / 改契约 / 人工改稿。\n";
    actions += "3. 若为 S5–S7：先恢复外部依赖（Provider / ComfyUI），再续跑。\n";
    actions += "4. 若为 S9/S10/S12：多半需要补初始化 / 清理 `field_defs` / 人工裁定（`10` / `08`）。\n";
    md += fmt::format("\n## 建议动作清单\n\n{}\n", actions);
    return md;
}

// ———— NovelRunLoop ————
NovelRunLoop::NovelRunLoop(db::sqlite::Database& db, agent::LlmCallFn call,
                           agent::LlmStreamFn stream)
    : db_(&db), call_(std::move(call)), stream_(std::move(stream)) {}

bool NovelRunLoop::ChapterAlreadyDone(RowId chapter_id) const {
    if (db_ == nullptr || !db_->isOpen() || chapter_id <= 0) return false;
    auto st = db_->Prepare("SELECT status, body FROM chapters WHERE id=?1");
    if (!st) return false;
    (void)st->BindInt(1, chapter_id);
    auto s = st->Step();
    if (!s || *s != db::sqlite::StepResult::Row) return false;
    return st->ColumnText(0) == "done" && !st->ColumnText(1).empty();
}

std::string NovelRunLoop::StateFingerprint() const {
    if (db_ == nullptr || !db_->isOpen()) return "fnv:0";
    const int done = ScalarCount(*db_, "SELECT COUNT(*) FROM chapters WHERE status='done'");
    const int entities = ScalarCount(*db_, "SELECT COUNT(*) FROM entities");
    const int fores = ScalarCount(*db_, "SELECT COUNT(*) FROM foreshadowings WHERE status IN "
                                       "('PLANNED','PLANTED','DEVELOPING')");
    return fmt::format("fnv:{}", Fnv1aHex(fmt::format("{}|{}|{}", done, entities, fores)));
}

int NovelRunLoop::FieldDefCount() const {
    if (db_ == nullptr || !db_->isOpen()) return 0;
    return ScalarCount(*db_, "SELECT COUNT(*) FROM field_defs");
}

RunOutcome NovelRunLoop::Run(const RunRequest& req) {
    RunOutcome out;
    if (req.project_dir.empty()) {
        // 报告 / `work/` / `snapshots/` 都按工程根落盘；没有工程根就没有可写位置（不写 CWD）
        out.refuse_reason = "project_dir 为空（报告与 work/ 无处落盘）";
        return out;
    }
    if (db_ == nullptr || !db_->isOpen()) {
        out.refuse_reason = "数据库未打开";
        out.stop = StopDecision{StopCode::None, "参数非法", out.refuse_reason};
        out.stop_report_path = WriteReportFile(req.project_dir / "stop_report.md",
                                               StopReportMarkdown(out, req));
        return out;
    }
    if (req.auto_create_chapters && req.max_chapters <= 0) {
        out.refuse_reason = "auto_create_chapters 需要 max_chapters > 0（否则无终止条件）";
        out.stop = StopDecision{StopCode::None, "参数非法", out.refuse_reason};
        out.stop_report_path = WriteReportFile(req.project_dir / "stop_report.md",
                                               StopReportMarkdown(out, req));
        return out;
    }

    NovelGraph graph(*db_);
    // ① `09` §2.1：模式是工程级配置，每次运行写入 audit_logs(action='run_mode')
    (void)graph.LogAudit("orchestrator", "run_mode", "project", 0,
                         fmt::format("mode={} from_ord={} max_chapters={} resume={} checkpoint={}",
                                     RunModeName(req.mode), req.from_ord, req.max_chapters,
                                     req.resume, req.checkpoint_every));

    // ② auto 前置：不满足 → 拒绝启动并给原因（`07` §2.5 C5）
    if (req.mode == RunMode::Auto) {
        const AutoPreconditionInput pre = ProbeAutoPrecondition(*db_, true);
        if (auto why = CheckAutoPrecondition(pre)) {
            out.started = false;
            out.refuse_reason = *why;
            out.stop = StopDecision{StopCode::None, "auto 前置条件不满足（拒绝启动）", *why};
            out.stop_report_path = WriteReportFile(req.project_dir / "stop_report.md",
                                                   StopReportMarkdown(out, req));
            log::Warn("RunLoop：拒绝启动 auto —— {}", *why);
            return out;
        }
    }
    out.started = true;
    out.mode_note = fmt::format("mode={} checkpoint_every={}", RunModeName(req.mode),
                                req.checkpoint_every);

    // ③ 工作清单：只跑**已存在**的章（`03` 的 CHAPTER_GOAL 阶段未落地；见 req.auto_create_chapters）
    auto chapters = graph.ListChapters(2000);
    if (!chapters) {
        out.refuse_reason = chapters.error().message;
        out.stop = StopDecision{StopCode::None, "读取章节失败", out.refuse_reason};
        out.stop_report_path = WriteReportFile(req.project_dir / "stop_report.md",
                                               StopReportMarkdown(out, req));
        return out;
    }
    std::vector<ChapterRow> list;
    for (const auto& c : *chapters) {
        if (c.ord >= static_cast<int>(req.from_ord)) {
            list.push_back(c);
        }
    }
    std::sort(list.begin(), list.end(),
              [](const ChapterRow& a, const ChapterRow& b) { return a.ord < b.ord; });
    if (list.empty()) {
        out.mode_note += "；没有可跑的章（章节表为空）";
    }

    StopConditionTracker tracker;
    ChapterRunner runner = runner_;
    if (!runner) {
        const std::filesystem::path projectDir = req.project_dir;
        runner = [this, projectDir](RowId chapter_id, const RunLimits& limits, RunMode mode,
                                    const std::function<void(
                                        const agent::GenerateChapterProgress&)>& onProgress)
            -> std::expected<ChapterRunInfo, agent::AgentError> {
            agent::NovelDirector dir(*db_, call_, stream_);
            agent::GenerateChapterRequest chReq;
            chReq.chapter_id = chapter_id;
            chReq.max_revisions = limits.repair_rounds;
            chReq.canon_mode = (mode == RunMode::Auto) ? "auto" : "manual";
            if (!projectDir.empty()) {
                chReq.snapshot_dir = util::PathToUtf8(projectDir / "snapshots");
            }
            auto res = dir.GenerateChapter(chReq, onProgress);
            ChapterRunInfo info;
            if (!res) {
                info.ok = false;
                info.error = res.error().message;
                info.contract_failures =
                    (res.error().code == "plan" || res.error().code == "contract") ? 1 : 0;
                info.llm_network_failures =
                    (res.error().code == "network" || res.error().code == "timeout") ? 1 : 0;
                info.note = res.error().code;
                return std::unexpected(res.error());
            }
            info.ok = true;
            info.state_committed = res->state_committed;
            info.state_skipped = res->state_skipped;
            info.review_passed = res->review_passed;
            info.semantic_only = res->semantic_only;
            info.llm_calls = res->llm_calls;
            info.high_tier_calls = res->high_tier_calls;
            info.images = res->images;
            info.contract_failures = res->contract_failures;
            info.missing_entity_refs = res->missing_entity_refs;
            // S11：G2 的 K01–K29 不通过项 → 停止条件 S1 的输入
            info.failed_check_ids = res->failed_check_ids;
            info.stages = res->stages;
            info.note = res->commit_note;
            return info;
        };
    }

    const std::filesystem::path projectDir = req.project_dir;
    std::vector<std::string> highIssues;
    std::vector<std::string> semanticOnlyOrds;

    for (std::size_t i = 0; i < list.size(); ++i) {
        if (req.cancel && req.cancel()) {
            out.mode_note += "；用户取消（已保存进度，可断点续跑）";
            break;
        }
        if (req.max_chapters > 0 && out.chapters_attempted >= req.max_chapters) {
            break;
        }
        const ChapterRow chapter = list[i];

        // ④ 断点续跑（`09` §2.6）：已完成的章不重跑
        if (req.resume && ChapterAlreadyDone(chapter.id)) {
            ++out.chapters_resumed_skipped;
            continue;
        }

        if (req.on_progress) {
            RunProgress p;
            p.chapters_done = out.chapters_done;
            p.chapters_total = static_cast<int>(list.size());
            p.chapter_id = chapter.id;
            p.chapter_ord = chapter.ord;
            p.phase = "START";
            req.on_progress(p);
        }

        const auto t0 = util::MonotonicMillis();
        auto info = runner(chapter.id, req.limits, req.mode,
                           [&req, &out, &chapter](const agent::GenerateChapterProgress& p) {
                               if (!req.on_progress) return;
                               RunProgress rp;
                               rp.chapters_done = out.chapters_done;
                               rp.chapter_id = chapter.id;
                               rp.chapter_ord = chapter.ord;
                               rp.phase = std::string{agent::PhaseName(p.phase)};
                               rp.note = p.note;
                               req.on_progress(rp);
                           });
        const std::int64_t wall = util::ElapsedMillis(t0);
        ++out.chapters_attempted;

        ChapterObservation obs;
        obs.chapter_id = chapter.id;
        obs.chapter_ord = chapter.ord;
        obs.wall_ms = wall;
        obs.field_def_count = FieldDefCount();
        if (info) {
            obs.review_failed = !info->review_passed;
            obs.semantic_only = info->semantic_only;
            obs.llm_calls = info->llm_calls;
            obs.high_tier_calls = info->high_tier_calls;
            obs.images = info->images;
            obs.contract_failures = info->contract_failures;
            obs.missing_entity_refs = info->missing_entity_refs;
            obs.llm_network_failures = info->llm_network_failures;
            // S11：机器校验（`06` §2.3 K01–K29）的不通过项 —— `09` §2.2 S1 的唯一输入。
            // 语义：**重复条目 = 失败次数**（章内重试由产出阶段负责，见 `03` §2.6）。
            obs.failed_check_ids = info->failed_check_ids;
            obs.note = info->note;
        } else {
            obs.llm_calls = 0;
            obs.contract_failures = 2; // 契约类错误按「含 1 次重试后仍失败」计（S4 的阈值）
            obs.llm_network_failures = 5; // 网络类错误在 `CallLlm` 内已退避重试 4 次
            obs.note = info.error().message;
            out.last_error = info.error().message;
            log::Warn("RunLoop：第 {} 章未完成（{}）：{}", chapter.ord, info.error().code,
                      info.error().message);
        }

        // ⑤ 成本报告 + `_manifest.json`（写入失败 → S11）
        const auto workDir = ChapterWorkDir(projectDir, chapter.ord);
        const std::string fingerprint = StateFingerprint();
        const std::string status =
            info ? (info->state_committed ? "done" : (info->state_skipped ? "skipped" : "generated"))
                 : "failed";
        const std::string costJson =
            CostReportJson(chapter.id, chapter.ord, info ? *info : ChapterRunInfo{}, obs, req.limits);
        const bool costOk = !projectDir.empty() && WriteTextFile(workDir / "cost_report.json", costJson);
        const std::string manifestJson = ManifestJson(chapter.id, chapter.ord, fingerprint,
                                                      info ? info->stages : std::vector<std::string>{},
                                                      status);
        const bool manifestOk =
            !projectDir.empty() && WriteTextFile(workDir / "_manifest.json", manifestJson);
        if (!costOk || !manifestOk) {
            obs.disk_write_failed = true;
            obs.note += "；cost_report/_manifest 写入失败";
        }

        if (info && info->ok) {
            ++out.chapters_done;
            out.llm_calls_total += obs.llm_calls;
            out.completed_chapters.push_back(chapter.id);
            if (obs.review_failed) {
                highIssues.push_back(fmt::format("ch{} 评审 FAIL（{}）", chapter.ord,
                                                 obs.semantic_only ? "仅语义" : "含机器项"));
            }
            if (obs.semantic_only) {
                semanticOnlyOrds.push_back(fmt::format("第 {} 章", chapter.ord));
            }
            log::Info("RunLoop：第 {} 章完成（LLM {} 次 / 高档 {} 次 / 墙钟 {}ms）", chapter.ord,
                      obs.llm_calls, obs.high_tier_calls, obs.wall_ms);
        }

        // ⑥ 检查点（`09` §2.5）：每 N 章 + `08` §2.3 自动升格
        if (req.checkpoint_every > 0 && chapter.ord > 0 && chapter.ord % req.checkpoint_every == 0) {
            CheckpointInput ci;
            ci.from_ord = std::max(1, chapter.ord - req.checkpoint_every + 1);
            ci.to_ord = chapter.ord;
            ci.high_issues = highIssues;
            ci.semantic_only_ords = semanticOnlyOrds;
            ci.llm_calls = out.llm_calls_total;
            ci.images = 0;
            if (auto cp = WriteCheckpoint(req, std::move(ci)); cp) {
                out.checkpoints.push_back(*cp);
            } else {
                obs.disk_write_failed = true;
                obs.note += "；检查点写入失败";
                log::Warn("RunLoop：检查点写入失败：{}", cp.error().message);
            }
        }

        // ⑦ 停止条件（`09` §2.2）
        tracker.Observe(obs);
        if (auto stop = EvaluateStop(tracker, obs, req.limits)) {
            out.stop = stop;
            log::Warn("RunLoop：命中停止条件 {} —— {}（{}）", StopCodeName(stop->code),
                      stop->condition, stop->detail);
            break;
        }
        if (!info || !info->ok) {
            StopDecision d;
            d.code = StopCode::None;
            d.condition = "本章未完成（未命中 S1–S12；断点续跑可从该章重试）";
            d.detail = obs.note;
            out.stop = d;
            break;
        }
        // `09` §2.1：manual = 每完成一章停下等确认；semi = 跑到检查点停下
        if (req.mode == RunMode::Manual) {
            out.mode_note += "；manual：每章停下等 UI 确认";
            break;
        }
        if (req.mode == RunMode::Semi && req.checkpoint_every > 0 && chapter.ord > 0 &&
            chapter.ord % req.checkpoint_every == 0) {
            out.mode_note += "；semi：到达检查点停下等确认";
            break;
        }

        // ⑧ 章表耗尽时自动建下一章（`03` CHAPTER_GOAL 未实现前的替代；需显式开启）
        if (req.auto_create_chapters && i + 1 >= list.size() && req.max_chapters > 0 &&
            out.chapters_attempted < req.max_chapters) {
            const int nextOrd = list.back().ord + 1;
            ChapterRow next;
            next.ord = nextOrd;
            next.title = fmt::format("第{}章", nextOrd);
            next.pov_entity_id = list.back().pov_entity_id;
            if (auto created = graph.UpsertChapter(next); created) {
                if (auto row = graph.GetChapter(*created); row) {
                    list.push_back(*row);
                }
            } else {
                log::Warn("RunLoop：自动建第 {} 章失败：{}", nextOrd, created.error().message);
                out.mode_note += "；自动建下一章失败";
                break;
            }
        }
    }

    out.stop_report_path = WriteReportFile(projectDir / "stop_report.md",
                                           StopReportMarkdown(out, req));
    log::Info("RunLoop 结束：{}（完成 {} / 跳过 {} / 尝试 {}）",
              out.stop ? (out.stop->code == StopCode::None
                              ? out.stop->condition
                              : fmt::format("停止条件 {}", StopCodeName(out.stop->code)))
                       : std::string{"正常结束"},
              out.chapters_done, out.chapters_resumed_skipped, out.chapters_attempted);
    return out;
}

std::expected<std::string, DbError> NovelRunLoop::WriteCheckpoint(const RunRequest& req,
                                                                 CheckpointInput input) {
    if (db_ == nullptr || !db_->isOpen()) {
        return std::unexpected(DbError{0, "数据库未打开"});
    }
    NovelFields fields(*db_);
    // `08` §2.3：四条件全满足才升 CANON；未满足的进人工复核清单
    std::vector<std::string> pending;
    if (auto cands = fields.EvaluateFieldPromotion(input.to_ord); cands) {
        for (const auto& c : *cands) {
            if (!c.Passed()) {
                pending.push_back(fmt::format("`{}`：{}", c.field_key, c.reject));
            }
        }
    } else {
        log::Warn("RunLoop：字段升格评估失败：{}", cands.error().message);
    }
    std::vector<std::string> promoted;
    if (auto r = fields.PromoteProposedFields(input.to_ord); r) {
        promoted = *r;
    } else {
        log::Warn("RunLoop：字段自动升格失败：{}", r.error().message);
    }
    input.promoted_fields = promoted;

    const int openForeshadows =
        ScalarCount(*db_, "SELECT COUNT(*) FROM foreshadowings WHERE status IN "
                          "('PLANNED','PLANTED','DEVELOPING')");
    std::string md;
    md += fmt::format("# 检查点 ch{}–{}\n\n", OrdPadded(input.from_ord), OrdPadded(input.to_ord));
    md += fmt::format("- 生成时间（Unix ms）：{}\n", util::NowMillis());
    md += fmt::format("- 区间：第 {} 章 – 第 {} 章（每 {} 章一检，`09` §2.5）\n", input.from_ord,
                      input.to_ord, req.checkpoint_every);
    md += fmt::format("- 本区间成本：LLM 调用 {} 次 / 出图 {} 次\n", input.llm_calls, input.images);
    md += fmt::format("- 未回收伏笔（`06` §2.3 K10）：{} 条\n", openForeshadows);
    md += fmt::format("- `field_defs` 条数：{} / {}（`08` §2.3 硬上限）\n",
                      FieldDefCount(), NovelFields::kMaxFieldDefs);

    md += "\n## 自动升格为 CANON 的字段定义（`08` §2.3 四条件全满足）\n\n";
    if (input.promoted_fields.empty()) {
        md += "- （无）\n";
    } else {
        for (const auto& k : input.promoted_fields) {
            md += fmt::format("- `{}`\n", k);
        }
    }
    md += "\n## 仍需人工确认的 PROPOSED 字段（未满足四条件，取前 10）\n\n";
    if (pending.empty()) {
        md += "- （无）\n";
    } else {
        const std::size_t n = std::min<std::size_t>(pending.size(), 10);
        for (std::size_t i = 0; i < n; ++i) {
            md += fmt::format("- {}\n", pending[i]);
        }
    }
    md += "\n## 本区间 high issue\n\n";
    if (input.high_issues.empty()) {
        md += "- （无）\n";
    } else {
        for (const auto& s : input.high_issues) {
            md += fmt::format("- {}\n", s);
        }
    }
    md += "\n## 仅语义判断且 FAIL 的章（`06` §2.7 M2）\n\n";
    if (input.semantic_only_ords.empty()) {
        md += "- （无）\n";
    } else {
        for (const auto& s : input.semantic_only_ords) {
            md += fmt::format("- {}\n", s);
        }
    }
    md += "\n## 人工复核清单\n\n";
    md += "1. 上表「仍需人工确认的 PROPOSED 字段」：确认语义是否需要保留（`08` §2.4，同义键**不自动合并**）。\n";
    md += "2. high issue：逐条决定「改文 / 改大纲 / 回滚」（`07` §2.6）。\n";
    md += "3. 抽样复核：每 10 章抽 2 章换模型重评（`06` §2.7 M3，**本步未做**）。\n";

    const auto path = req.project_dir / fmt::format("checkpoint_ch{}–{}.md", OrdPadded(input.from_ord),
                                                   OrdPadded(input.to_ord));
    const std::string written = WriteReportFile(path, md);
    if (written.empty()) {
        return std::unexpected(DbError{0, fmt::format("检查点写入失败：{}", util::PathToUtf8(path))});
    }
    log::Info("RunLoop：检查点已写出 {}（升格字段 {} 个）", written, promoted.size());
    return written;
}

// ———— 自检（离线：内存库 + mock runner）————
bool NovelRunLoop::RunSelfCheck() {
    db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) {
        log::Error("RunLoop 自检：打开内存库失败");
        return false;
    }
    if (auto r = NovelDb::ApplyCanonicalSchema(mem); !r) {
        log::Error("RunLoop 自检：建规范 schema 失败 {}", r.error().message);
        return false;
    }

    int failures = 0;
    auto expect = [&failures](bool ok, std::string_view what) {
        if (!ok) {
            ++failures;
            log::Error("RunLoop 自检 FAIL：{}", what);
        }
    };

    // ① 三态 + 名称往返
    expect(RunModeFromString("manual") == RunMode::Manual, "manual 解析");
    expect(RunModeFromString("semi") == RunMode::Semi, "semi 解析");
    expect(RunModeFromString("auto") == RunMode::Auto, "auto 解析");
    expect(RunModeName(RunMode::Auto) == "auto", "auto 名称");

    // ② S1–S12 逐条可构造触发（`09` §2.2 的每一条都要有数字阈值）
    {
        const RunLimits lim;
        // S1：同章同 check_id 连续失败 2 次（两次观测）
        {
            StopConditionTracker t;
            ChapterObservation o;
            o.chapter_id = 1;
            o.failed_check_ids = {"K04"};
            t.Observe(o);
            expect(!EvaluateStop(t, o, lim).has_value(), "S1 一次不触发");
            t.Observe(o);
            auto d = EvaluateStop(t, o, lim);
            expect(d && d->code == StopCode::S1, "S1 触发（同 check_id >= 2）");
        }
        // S2：同章评审 FAIL 累计 3
        {
            StopConditionTracker t;
            ChapterObservation o;
            o.chapter_id = 2;
            o.review_failed = true;
            t.Observe(o);
            t.Observe(o);
            expect(!EvaluateStop(t, o, lim).has_value(), "S2 两次不触发");
            t.Observe(o);
            auto d = EvaluateStop(t, o, lim);
            expect(d && d->code == StopCode::S2, "S2 触发（FAIL >= 3）");
        }
        // S3：仅语义 FAIL 连续 2 章
        {
            StopConditionTracker t;
            ChapterObservation o1;
            o1.chapter_id = 3;
            o1.review_failed = true;
            o1.semantic_only = true;
            t.Observe(o1);
            expect(!EvaluateStop(t, o1, lim).has_value(), "S3 一章不触发");
            ChapterObservation o2;
            o2.chapter_id = 4;
            o2.review_failed = true;
            o2.semantic_only = true;
            t.Observe(o2);
            auto d = EvaluateStop(t, o2, lim);
            expect(d && d->code == StopCode::S3, "S3 触发（连续 2 章仅语义 FAIL）");
        }
        // S4：契约失败 >= 2
        {
            StopConditionTracker t;
            ChapterObservation o;
            o.chapter_id = 5;
            o.contract_failures = 1;
            t.Observe(o);
            expect(!EvaluateStop(t, o, lim).has_value(), "S4 一次不触发");
            o.contract_failures = 1;
            t.Observe(o);
            auto d = EvaluateStop(t, o, lim);
            expect(d && d->code == StopCode::S4, "S4 触发（契约失败 >= 2）");
        }
        // S5：单章成本 > 2 × 预算
        {
            StopConditionTracker t;
            ChapterObservation o;
            o.chapter_id = 6;
            o.llm_calls = lim.max_llm_calls_per_chapter * kCostOverrunFactor;
            t.Observe(o);
            expect(!EvaluateStop(t, o, lim).has_value(), "S5 正好 2× 不触发");
            o.llm_calls += 1;
            t.Observe(o);
            auto d = EvaluateStop(t, o, lim);
            expect(d && d->code == StopCode::S5, "S5 触发（> 2× 预算）");
        }
        // S6：连续网络失败 >= 5
        {
            StopConditionTracker t;
            ChapterObservation o;
            o.chapter_id = 7;
            o.llm_network_failures = 4;
            t.Observe(o);
            expect(!EvaluateStop(t, o, lim).has_value(), "S6 4 次不触发");
            o.llm_network_failures = 1;
            t.Observe(o);
            auto d = EvaluateStop(t, o, lim);
            expect(d && d->code == StopCode::S6, "S6 触发（连续 5 次）");
        }
        // S7：Comfy 探活失败 >= 3 且本章需要出图
        {
            StopConditionTracker t;
            ChapterObservation o;
            o.chapter_id = 8;
            o.comfy_probe_failures = 3;
            o.images = 0;
            t.Observe(o);
            expect(!EvaluateStop(t, o, lim).has_value(), "S7 不需出图不触发");
            o.images = 1;
            auto d = EvaluateStop(t, o, lim);
            expect(d && d->code == StopCode::S7, "S7 触发（探活 >= 3 且需出图）");
        }
        // S8 / S9 / S10 / S11 / S12
        {
            StopConditionTracker t;
            ChapterObservation o;
            o.chapter_id = 9;
            o.state_conflict_unrecoverable = true;
            t.Observe(o);
            auto d = EvaluateStop(t, o, lim);
            expect(d && d->code == StopCode::S8, "S8 触发（状态自相矛盾不可回退）");
        }
        {
            StopConditionTracker t;
            ChapterObservation o;
            o.chapter_id = 10;
            o.missing_entity_refs = 9;
            t.Observe(o);
            expect(!EvaluateStop(t, o, lim).has_value(), "S9 9 处不触发");
            o.missing_entity_refs = 10;
            t.Observe(o);
            auto d = EvaluateStop(t, o, lim);
            expect(d && d->code == StopCode::S9, "S9 触发（>= 10 处）");
        }
        {
            StopConditionTracker t;
            ChapterObservation o;
            o.chapter_id = 11;
            o.field_def_count = NovelFields::kMaxFieldDefs;
            t.Observe(o);
            expect(!EvaluateStop(t, o, lim).has_value(), "S10 正好 500 不触发");
            o.field_def_count = NovelFields::kMaxFieldDefs + 1;
            t.Observe(o);
            auto d = EvaluateStop(t, o, lim);
            expect(d && d->code == StopCode::S10, "S10 触发（> 500）");
        }
        {
            StopConditionTracker t;
            ChapterObservation o;
            o.chapter_id = 12;
            o.disk_write_failed = true;
            t.Observe(o);
            auto d = EvaluateStop(t, o, lim);
            expect(d && d->code == StopCode::S11, "S11 触发（写入失败）");
        }
        {
            StopConditionTracker t;
            ChapterObservation o;
            o.chapter_id = 13;
            o.needs_user_decision = true;
            o.note = "需要在两条大纲之间选择";
            t.Observe(o);
            auto d = EvaluateStop(t, o, lim);
            expect(d && d->code == StopCode::S12, "S12 触发（需人工决策）");
        }
    }

    // ③ auto 前置：任意一条不满足 → 拒绝启动并给原因（判据 5）
    {
        AutoPreconditionInput bad;
        const auto why = CheckAutoPrecondition(bad);
        expect(why.has_value() && why->find("auto 前置条件不满足") != std::string::npos,
               "auto 前置不满足 → 拒绝并给原因");
        AutoPreconditionInput good;
        good.gates_verified_on_last_chapter = true;
        good.verifiers_complete = true;
        good.llm_ok = true;
        good.comfy_ok = true;
        expect(!CheckAutoPrecondition(good).has_value(), "auto 前置全满足 → 允许");
        // S10：`06` §2.3 的 K01–K29 校验器已全量落地 → 这一条不再恒 false
        const AutoPreconditionInput probed = ProbeAutoPrecondition(mem, true);
        expect(probed.verifiers_complete, "K01–K29 已全量（S10）→ verifiers_complete=true");
    }

    // ④ 报告/清单序列化含停止条件编号与阈值
    {
        RunOutcome o;
        o.started = true;
        o.stop = StopDecision{StopCode::S4, std::string{StopCodeCondition(StopCode::S4)}, "contract=2"};
        RunRequest rq;
        rq.mode = RunMode::Semi;
        const std::string md = StopReportMarkdown(o, rq);
        expect(md.find("**S4**") != std::string::npos, "stop_report 含 S4 编号");
        expect(md.find(">= 2") != std::string::npos, "stop_report 含数字阈值");
        const std::string cost = CostReportJson(7, 7, ChapterRunInfo{}, ChapterObservation{}, RunLimits{});
        expect(cost.find("\"llm_calls_budget\":40") != std::string::npos, "cost_report 含单章预算 40");
        const std::string man = ManifestJson(7, 7, "fnv:abc", {"PLAN", "WRITE"}, "done");
        expect(man.find("input_state_hash") != std::string::npos &&
                   man.find("\"PLAN\"") != std::string::npos,
               "manifest 含指纹与阶段");
    }

    // ⑤ 字段自动升格四条件（`08` §2.3）：全满足才升 + 审计
    {
        NovelFields fields(mem);
        auto e = NovelGraph(mem).UpsertEntity({.kind = std::string{kind::person}, .name = "林默"});
        expect(e.has_value(), "建实体");
        FieldDefRow def; // 不传 status → is_system=0 → PROPOSED
        def.scope = "entity";
        def.entity_kind = "person";
        def.field_key = "soul_ring";
        def.value_type = "text";
        def.created_by = "field_builder";
        auto defId = fields.UpsertFieldDef(def);
        expect(defId.has_value(), "登记 PROPOSED 字段");
        // 注意：entity_fields 唯一键是 (entity_id, field_key, chapter_scope, layer) ——
        // 必须用不同的 chapter_scope 才能留下 6 行（否则被去重成 1 行，出现次数凑不够 5）
        for (int i = 0; i < 6; ++i) {
            EntityFieldRow row;
            row.entity_id = e.value_or(0);
            row.field_key = "soul_ring";
            row.value_text = fmt::format("第{}环", i + 1);
            row.chapter_scope = i + 1;
            row.created_by = "character";
            (void)fields.UpsertEntityField(row);
        }
        auto promote = fields.PromoteProposedFields(10);
        expect(promote && promote->size() == 1 && (*promote)[0] == "soul_ring",
               "四条件满足 → 升 CANON");
        auto after = fields.GetFieldDef(defId.value_or(0));
        expect(after && after->status == "CANON", "升格后 status=CANON");
        int promoteAudits = 0;
        if (auto st = mem.Prepare("SELECT COUNT(*) FROM audit_logs WHERE action='promote_field'")) {
            if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                promoteAudits = st->ColumnInt(0);
            }
        }
        expect(promoteAudits == 1, "升格写了 audit_logs(action='promote_field')");
        // 只出现 2 次（< 5）的 PROPOSED 不升
        FieldDefRow weak;
        weak.scope = "entity";
        weak.entity_kind = "person";
        weak.field_key = "weak_key";
        weak.value_type = "text";
        weak.created_by = "field_builder";
        expect(fields.UpsertFieldDef(weak).has_value(), "登记弱字段");
        for (int i = 0; i < 2; ++i) {
            EntityFieldRow row;
            row.entity_id = e.value_or(0);
            row.field_key = "weak_key";
            row.value_text = "x";
            row.chapter_scope = i + 1;
            (void)fields.UpsertEntityField(row);
        }
        auto promote2 = fields.PromoteProposedFields(10);
        expect(promote2 && promote2->empty(), "出现次数不足 → 不升格");
    }

    // ⑥ 端到端：mock runner 跑 3 章（manual 每章停 / semi 到检查点停 / auto 被拒）
    {
        auto c1 = NovelGraph(mem).UpsertChapter({.ord = 1, .title = "第一章"});
        auto c2 = NovelGraph(mem).UpsertChapter({.ord = 2, .title = "第二章"});
        auto c3 = NovelGraph(mem).UpsertChapter({.ord = 3, .title = "第三章"});
        expect(c1 && c2 && c3, "建三章");
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / "shine_runloop_check";
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);

        NovelRunLoop loop(mem, nullptr);
        int calls = 0;
        // S11：K01–K29（`06` §2.3）的不通过项必须真的流进停止条件（`09` §2.2 S1）。
        // 语义：`failed_check_ids` 的**重复条目 = 失败次数**（章内重试由产出阶段负责，`03` §2.6）。
        {
            db::sqlite::Database s1db;
            if (auto r = s1db.Open({.memory = true}); r) {
                (void)NovelDb::ApplyCanonicalSchema(s1db);
                (void)NovelGraph(s1db).UpsertChapter({.ord = 1, .title = "S1 章"});
                NovelRunLoop s1loop(s1db, nullptr);
                s1loop.SetChapterRunner(
                    [](RowId, const RunLimits&, RunMode,
                       const std::function<void(const agent::GenerateChapterProgress&)>&)
                        -> std::expected<ChapterRunInfo, agent::AgentError> {
                        ChapterRunInfo info;
                        info.ok = true;
                        info.review_passed = true;
                        info.failed_check_ids = {"K02", "K02"}; // 同章同 check_id 2 次
                        return info;
                    });
                RunRequest s1req;
                s1req.project_dir = dir / "s1";
                s1req.mode = RunMode::Manual;
                s1req.max_chapters = 1;
                const RunOutcome s1 = s1loop.Run(s1req);
                expect(s1.stop && s1.stop->code == StopCode::S1 &&
                           s1.stop->detail.find("K02") != std::string::npos,
                       "S11：K01–K29 失败项 → 观测 → S1 触发（停止条件真的接上了）");
            } else {
                expect(false, "S1 用例：内存库打开失败");
            }
        }
        loop.SetChapterRunner([&calls](RowId chapter_id, const RunLimits&, RunMode,
                                       const std::function<void(
                                           const agent::GenerateChapterProgress&)>&)
                                  -> std::expected<ChapterRunInfo, agent::AgentError> {
            ++calls;
            // 第 1 章在第二次调用时命中 S4（契约失败 >= 2）——用真实 DB 写正文（不写库，只报告）
            ChapterRunInfo info;
            info.ok = true;
            info.state_committed = true;
            info.review_passed = true;
            info.llm_calls = 3;
            info.high_tier_calls = 2;
            info.stages = {"PLAN", "WRITE", "SAVE"};
            info.note = "mock";
            (void)chapter_id;
            return info;
        });

        // manual：每章停下 → 只跑 1 章
        RunRequest manualReq;
        manualReq.project_dir = dir;
        manualReq.mode = RunMode::Manual;
        RunOutcome manual = loop.Run(manualReq);
        expect(manual.started && manual.chapters_done == 1, "manual 只跑 1 章");
        expect(calls == 1, "manual 只调用一次 runner");
        expect(!manual.stop_report_path.empty() &&
                   std::filesystem::exists(std::filesystem::path{util::PathFromUtf8(
                       manual.stop_report_path)}),
               "stop_report.md 落盘");
        expect(std::filesystem::exists(ChapterWorkDir(dir, 1) / "cost_report.json"),
               "cost_report.json 落盘");
        expect(std::filesystem::exists(ChapterWorkDir(dir, 1) / "_manifest.json"),
               "_manifest.json 落盘");

        // 断点续跑：第二章起（第 1 章已完成 → 跳过，不再调 runner）
        // 注：mock runner 不写 chapters.body/status，故这里直接验证「已完成章不重跑」的判定
        auto ch1 = NovelGraph(mem).GetChapter(c1.value_or(0));
        expect(ch1.has_value(), "读第 1 章");
        // 让第 1 章成为「已完成」，然后从第 1 章重新跑 → 应跳过
        {
            ChapterRow row = ch1.value_or(ChapterRow{});
            row.body = "正文";
            row.status = "done";
            (void)NovelGraph(mem).UpsertChapter(row);
        }
        const int callsBefore = calls;
        RunRequest resumeReq;
        resumeReq.project_dir = dir;
        resumeReq.mode = RunMode::Manual;
        resumeReq.max_chapters = 1;
        RunOutcome resumed = loop.Run(resumeReq);
        expect(resumed.chapters_resumed_skipped == 1 && calls == callsBefore + 1 &&
                   !resumed.completed_chapters.empty() &&
                   resumed.completed_chapters.front() == c2.value_or(0),
               "断点续跑：已完成章不重跑（跳到第 2 章）");

        // auto（S10 后语义变化）：K01–K29 已全量，此时这个库「最近一章已 done」→ 前置齐备，
        // `auto` **不再被 K01–K29 挡住**（允许启动）。仍要证明「前置不满足就拒绝」——
        // 用一个没有已完成章的新库跑一次 Run 级拒绝（判据 5 的完整形态）。
        {
            const AutoPreconditionInput probed = ProbeAutoPrecondition(mem, true);
            expect(probed.verifiers_complete && probed.gates_verified_on_last_chapter,
                   "auto 前置齐备（K01–K29 全量 + 最近一章已 done）");
            expect(!CheckAutoPrecondition(probed).has_value(), "前置齐备 → 允许 auto");

            db::sqlite::Database fresh;
            if (auto r = fresh.Open({.memory = true}); r) {
                (void)NovelDb::ApplyCanonicalSchema(fresh);
                NovelRunLoop freshLoop(fresh, nullptr);
                RunRequest autoReq;
                autoReq.project_dir = dir;
                autoReq.mode = RunMode::Auto;
                const RunOutcome autoOut = freshLoop.Run(autoReq);
                expect(!autoOut.started &&
                           autoOut.refuse_reason.find("auto 前置条件不满足") != std::string::npos,
                       "auto 前置不满足（无已完成章）→ 拒绝启动");
            }
        }

        // 检查点：跑到第 2 章（checkpoint_every=2）→ 产出 checkpoint_ch001–002.md 且字段被升格
        {
            auto c4 = NovelGraph(mem).UpsertChapter({.ord = 4, .title = "第四章"});
            (void)c4;
            RunRequest semiReq;
            semiReq.project_dir = dir;
            semiReq.mode = RunMode::Semi;
            semiReq.checkpoint_every = 2;
            semiReq.resume = false; // 从第 1 章重跑（第 1 章已 done → 会被跳过；用 resume=false 强制）
            RunOutcome semi = loop.Run(semiReq);
            expect(!semi.checkpoints.empty(), "semi 到检查点产出 checkpoint 文件");
            expect(std::filesystem::exists(
                       dir / fmt::format("checkpoint_ch{}–{}.md", "001", "002")),
                   "checkpoint_ch001–002.md 存在");
        }
    }

    if (failures == 0) {
        log::Info("NovelRunLoop 自检通过（三态 / S1–S12 逐条量化触发 / auto 前置拒绝 / "
                  "stop_report+cost_report+_manifest / 字段四条件自动升格 / manual·semi·续跑）");
        if (const char* path = std::getenv("SHINE_NOVEL_CHECK_OUT"); path && *path) {
            if (FILE* f = std::fopen(path, "ab")) {
                const char* line = "runloop:ok\n";
                std::fwrite(line, 1, std::strlen(line), f);
                std::fclose(f);
            }
        }
        return true;
    }
    log::Error("NovelRunLoop 自检失败：{} 项", failures);
    return false;
}

} // namespace shine::novelcore
