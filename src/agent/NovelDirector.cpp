#include "agent/NovelDirector.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "novel/NovelGraph.h"
#include "novel/NovelMemory.h"
#include "util/File.h"
#include "util/Json.h"
#include "util/Time.h"

#include <fmt/format.h>

#include <yyjson.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
NovelDirector::CallLlm(std::string_view role, std::string_view user) const {
    if (!call_) {
        return std::unexpected(AgentError{"no_llm", "未配置 LLM 回调"});
    }
    const std::string instructions = LoadPrompt(role);
    return call_(instructions, user);
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
    novelcore::NovelGraph g(*db_);
    novelcore::NovelMemory mem(*db_);
    ContextBuilder cb(*db_);

    Report(on_progress, Phase::Analyze, 5, "读取章节");
    auto ch = g.GetChapter(req.chapter_id);
    if (!ch) {
        return std::unexpected(AgentError{"not_found", ch.error().message});
    }

    // RETRIEVE
    Report(on_progress, Phase::Retrieve, 15, "组装上下文");
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
    Report(on_progress, Phase::Plan, 30, "Planner");
    const std::string planUser = fmt::format("{}\n\n【任务】\n{}", ctx->text, req.user_hint);
    auto planResp = CallLlm("planner", planUser);
    if (!planResp) {
        return std::unexpected(planResp.error());
    }
    result.plan_json = ExtractOutputText(*planResp);
    if (result.plan_json.empty()) {
        // 解析失败重试 1 次
        planResp = CallLlm("planner", planUser);
        if (!planResp) return std::unexpected(planResp.error());
        result.plan_json = ExtractOutputText(*planResp);
    }
    if (result.plan_json.empty()) {
        return std::unexpected(AgentError{"plan", "Planner 未返回 JSON"});
    }

    // WRITE（可 stream）
    Report(on_progress, Phase::Write, 50, "Writer");
    const std::string writeUser =
        fmt::format("{}\n\n【章节计划 JSON】\n{}\n\n【写出正文】", ctx->text, result.plan_json);
    std::string body;
    if (stream_) {
        auto onDelta = [&](std::string_view d) {
            if (on_progress) {
                GenerateChapterProgress pr;
                pr.phase = Phase::Write;
                pr.percent = 55;
                pr.text_delta = std::string{d};
                on_progress(pr);
            }
        };
        auto wr = stream_(LoadPrompt("writer"), writeUser, onDelta);
        if (!wr) return std::unexpected(wr.error());
        body = ExtractOutputText(*wr);
        if (body.empty()) body = *wr;
    } else {
        auto wr = CallLlm("writer", writeUser);
        if (!wr) return std::unexpected(wr.error());
        body = ExtractOutputText(*wr);
    }
    if (body.empty()) {
        return std::unexpected(AgentError{"write", "Writer 未返回正文"});
    }

    // REVIEW + REVISION
    int revisions = 0;
    std::string criticJson;
    while (revisions <= req.max_revisions) {
        Report(on_progress, revisions == 0 ? Phase::Review : Phase::Revision,
               70 + revisions * 5, fmt::format("Critic 第{}轮", revisions + 1));
        const std::string criticUser =
            fmt::format("{}\n\n【计划】\n{}\n\n【正文】\n{}\n\n请审校并输出 JSON。", ctx->text,
                        result.plan_json, body);
        auto cr = CallLlm("critic", criticUser);
        if (!cr) break; // Critic 失败不阻断保存
        criticJson = ExtractOutputText(*cr);
        result.critic_json = criticJson;
        const bool passed =
            criticJson.find("\"passed\":true") != std::string::npos ||
            criticJson.find("\"passed\": true") != std::string::npos;
        if (passed || revisions >= req.max_revisions) break;
        ++revisions;
        // 改稿
        const std::string revUser = fmt::format(
            "{}\n\n【计划】\n{}\n\n【原稿】\n{}\n\n【审校意见】\n{}\n\n请输出修订后的完整正文。", ctx->text,
            result.plan_json, body, criticJson);
        auto rr = CallLlm("writer", revUser);
        if (!rr) break;
        const auto newBody = ExtractOutputText(*rr);
        if (!newBody.empty()) body = newBody;
    }
    result.revisions = revisions;
    result.body = body;
    result.title = ch->title;

    // EXTRACT
    Report(on_progress, Phase::Extract, 90, "Extractor");
    const std::string exUser = fmt::format("【计划】\n{}\n\n【正文】\n{}", result.plan_json, body);
    auto er = CallLlm("extractor", exUser);
    std::string summary = body.substr(0, std::min<std::size_t>(body.size(), 80));
    if (er) {
        const auto ej = ExtractOutputText(*er);
        yyjson_doc* doc = yyjson_read(ej.data(), ej.size(), 0);
        if (doc) {
            yyjson_val* root = yyjson_doc_get_root(doc);
            const auto s = util::json::GetStrCopy(root, "summary");
            if (!s.empty()) summary = s;
            yyjson_doc_free(doc);
        }
    }

    // SAVE
    Report(on_progress, Phase::Save, 95, "写入章节");
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

    Report(on_progress, Phase::Done, 100, "完成");
    log::Info("GenerateChapter 完成：章={} 正文 {} 字 修订 {}", req.chapter_id, body.size(),
              revisions);
    return result;
}

bool NovelDirector::RunSelfCheck() {
    db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) return false;
    // 最小表
    if (auto r = mem.Exec(R"SQL(
CREATE TABLE IF NOT EXISTS entities(id INTEGER PRIMARY KEY AUTOINCREMENT,kind TEXT,name TEXT,summary TEXT,status TEXT,meta_json TEXT,created_chapter INTEGER,updated INTEGER);
CREATE TABLE IF NOT EXISTS chapters(id INTEGER PRIMARY KEY AUTOINCREMENT,volume_id INTEGER,ord INTEGER,title TEXT,status TEXT,summary TEXT,body TEXT,pov_entity_id INTEGER,words INTEGER,updated INTEGER);
CREATE TABLE IF NOT EXISTS scenes(id INTEGER PRIMARY KEY AUTOINCREMENT,chapter_id INTEGER,ord INTEGER,title TEXT,location_id INTEGER,time_label TEXT,pov_entity_id INTEGER,conflict_id INTEGER,goal TEXT,action TEXT,conflict TEXT,result TEXT,emotion TEXT,info_reveal TEXT,hook TEXT,body TEXT);
CREATE TABLE IF NOT EXISTS foreshadowings(id INTEGER PRIMARY KEY AUTOINCREMENT,title TEXT,content TEXT,status TEXT,setup_ch INTEGER,payoff_ch INTEGER,importance INTEGER,truth TEXT,entity_ids_json TEXT);
CREATE TABLE IF NOT EXISTS secrets(id INTEGER PRIMARY KEY AUTOINCREMENT,content TEXT,truth TEXT,reveal_ch INTEGER,reveal_condition TEXT,entity_id INTEGER,scope TEXT);
CREATE TABLE IF NOT EXISTS secret_knowledge(id INTEGER PRIMARY KEY AUTOINCREMENT,secret_id INTEGER,entity_id INTEGER,knows INTEGER,chapter_known INTEGER);
CREATE TABLE IF NOT EXISTS memories(id INTEGER PRIMARY KEY AUTOINCREMENT,kind TEXT,entity_id INTEGER,chapter_id INTEGER,content TEXT,summary TEXT,embedding_blob BLOB,created INTEGER);
CREATE TABLE IF NOT EXISTS character_status(id INTEGER PRIMARY KEY AUTOINCREMENT,entity_id INTEGER,chapter_id INTEGER,location_id INTEGER,body_state TEXT,mind_state TEXT,emotion_json TEXT,goal TEXT,relation_note TEXT,resource_note TEXT,secret_note TEXT,updated INTEGER);
CREATE TABLE IF NOT EXISTS entity_personas(entity_id INTEGER PRIMARY KEY,age TEXT,appearance TEXT,personality TEXT,background TEXT,"values" TEXT,desire TEXT,goal TEXT,fear TEXT,weakness TEXT,strength TEXT,ability_note TEXT,knowledge_note TEXT,memory_note TEXT);
CREATE TABLE IF NOT EXISTS relations(id INTEGER PRIMARY KEY AUTOINCREMENT,from_id INTEGER,to_id INTEGER,rel_type TEXT,strength INTEGER,from_chapter INTEGER,to_chapter INTEGER,reason TEXT,status TEXT);
CREATE TABLE IF NOT EXISTS world_meta(key TEXT PRIMARY KEY,value TEXT);
CREATE TABLE IF NOT EXISTS writing_style(id INTEGER PRIMARY KEY,pov_mode TEXT,sentence_len TEXT,note TEXT);
CREATE TABLE IF NOT EXISTS author_rules(id INTEGER PRIMARY KEY AUTOINCREMENT,rule TEXT,severity TEXT,note TEXT);
CREATE TABLE IF NOT EXISTS themes(id INTEGER PRIMARY KEY AUTOINCREMENT,title TEXT,statement TEXT,linked_plot_id INTEGER);
CREATE TABLE IF NOT EXISTS mysteries(id INTEGER PRIMARY KEY AUTOINCREMENT,entity_id INTEGER,question TEXT,answer TEXT,status TEXT,ask_ch INTEGER,answer_ch INTEGER,importance INTEGER);
CREATE TABLE IF NOT EXISTS audit_logs(id INTEGER PRIMARY KEY AUTOINCREMENT,actor TEXT,action TEXT,target_kind TEXT,target_id INTEGER,detail TEXT,created INTEGER);
CREATE TABLE IF NOT EXISTS canon_logs(id INTEGER PRIMARY KEY AUTOINCREMENT,target_kind TEXT,target_id INTEGER,status TEXT,note TEXT,created INTEGER);
)SQL"); !r) {
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
            return std::string{R"({"output_text":"{\"summary\":\"林默在雪原负伤前行。\",\"new_entities\":[],\"events\":[],\"foreshadow_updates\":[]}"})"};
        }
        // writer
        return std::string{R"({"output_text":"雪原上只剩下风声。林默按住伤口，继续向前。"})"};
    };

    NovelDirector dir(mem, mock);
    std::vector<Phase> phases;
    auto out = dir.GenerateChapter(
        {.chapter_id = *ch, .user_hint = "写第一章", .max_revisions = 3},
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
    if (!hasBody || !hasRev || !hasPhases || !savedOk || !memOk) {
        log::Error("Director 自检失败：body={} rev={} phases={} saved={} mem={}", hasBody, hasRev,
                   hasPhases, savedOk, memOk);
        return false;
    }
    log::Info("Director 自检通过（Plan→Write→Review→Revise→Extract→Save）");
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
