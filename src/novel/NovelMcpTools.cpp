#include "novel/NovelMcpTools.h"

#include "agent/AgentKit.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "db/Db.h"
#include "mcp/Schema.h"
#include "novel/NovelDb.h"
#include "novel/NovelFields.h"
#include "novel/NovelGraph.h"
#include "novel/NovelMemory.h"
#include "novel/NovelProjects.h"
#include "util/Encoding.h"
#include "util/Json.h"
#include "util/Time.h"

#include <fmt/format.h>

#include <yyjson.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace shine::novelcore {
namespace {

db::sqlite::Database* g_dbOverride = nullptr;
bool g_mcpAllowWrite = false;
// 自检专用：为 true 时无条件拒绝写（盖过 g_mcpAllowWrite / Settings / 环境变量）
bool g_writeForceDeny = false;

[[nodiscard]] bool EnvWriteOn() {
    if (const char* e = std::getenv("SHINE_MCP_ALLOW_WRITE"); e && *e && *e != '0') {
        return true;
    }
    return false;
}

[[nodiscard]] db::sqlite::Database* ResolveDb() {
    if (g_dbOverride) return g_dbOverride;
    if (!NovelDb::Instance().isOpen()) {
        // Settings.mcpNovelDbPath：stdio/HTTP 在未点开 UI 时也可挂当前工程库
        const auto& raw = Settings().mcpNovelDbPath;
        if (!raw.empty()) {
            auto r = NovelDb::Instance().Open(util::PathFromUtf8(raw));
            if (!r) {
                log::Warn("MCP 挂库失败：{}", r.error().message);
            }
        }
    }
    if (NovelDb::Instance().isOpen()) return &NovelDb::Instance().raw();
    return nullptr;
}

// JSON 字符串转义（控制字符 → \u00XX）
[[nodiscard]] std::string Esc(std::string_view s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (const unsigned char c : s) {
        switch (c) {
        case '"':
            o += "\\\"";
            break;
        case '\\':
            o += "\\\\";
            break;
        case '\n':
            o += "\\n";
            break;
        case '\r':
            o += "\\r";
            break;
        case '\t':
            o += "\\t";
            break;
        default:
            if (c < 0x20) {
                o += fmt::format("\\u{:04x}", static_cast<unsigned>(c));
            } else {
                o += static_cast<char>(c);
            }
            break;
        }
    }
    return o;
}

[[nodiscard]] std::int64_t ArgI64(yyjson_val* args, const char* key, std::int64_t def = 0) {
    if (!args) return def;
    yyjson_val* v = yyjson_obj_get(args, key);
    return (v && yyjson_is_num(v)) ? yyjson_get_sint(v) : def;
}

[[nodiscard]] std::string ArgStr(yyjson_val* args, const char* key) {
    if (!args) return {};
    yyjson_val* v = yyjson_obj_get(args, key);
    return (v && yyjson_is_str(v)) ? std::string{yyjson_get_str(v)} : std::string{};
}

[[nodiscard]] bool ArgBool(yyjson_val* args, const char* key, bool def = false) {
    if (!args) return def;
    yyjson_val* v = yyjson_obj_get(args, key);
    if (!v) return def;
    return yyjson_is_true(v);
}

[[nodiscard]] std::string EmptySchema() {
    auto* doc = mcp::schema::NewDoc();
    auto* s = mcp::schema::Object(doc);
    const std::string out = mcp::schema::ToJsonString(doc, s);
    yyjson_mut_doc_free(doc);
    return out;
}

[[nodiscard]] std::string SchemaWith(auto fill) {
    auto* doc = mcp::schema::NewDoc();
    auto* s = mcp::schema::Object(doc);
    fill(doc, s);
    const std::string out = mcp::schema::ToJsonString(doc, s);
    yyjson_mut_doc_free(doc);
    return out;
}

[[nodiscard]] mcp::CallOutcome NeedDb() {
    return mcp::CallOutcome::Fail(
        mcp::CallStatus::InternalError,
        "小说库未打开：UI 打开工程，或设置 Settings.mcpNovelDbPath / 环境变量 SHINE_NOVEL_DB");
}

[[nodiscard]] mcp::CallOutcome WriteDenied(std::string_view tool) {
    return mcp::CallOutcome::Fail(
        mcp::CallStatus::InternalError,
        fmt::format("写工具 {} 已禁用（默认只读）。开启：设置「允许 MCP 写工具」或 SHINE_MCP_ALLOW_WRITE=1",
                    tool));
}

void AuditWrite(db::sqlite::Database& db, std::string_view action, std::string_view targetKind,
                RowId targetId, std::string_view detail) {
    NovelGraph g(db);
    (void)g.LogAudit("mcp", action, targetKind, targetId, detail);
}

// P10.4 Redis 可选：池未就绪静默跳过（SQLite 权威）
[[nodiscard]] bool RedisGet(std::string_view key, std::string& out) {
    if (!db::redisReady()) return false;
    auto lease = db::AcquireRedis();
    if (!lease) return false;
    auto v = (*lease)->Get(key);
    if (!v || !*v) return false;
    out = **v;
    return true;
}

void RedisSet(std::string_view key, std::string_view value, std::chrono::seconds ttl) {
    if (!db::redisReady()) return;
    auto lease = db::AcquireRedis();
    if (!lease) return;
    (void)(*lease)->Set(key, value);
    if (ttl.count() > 0) {
        (void)(*lease)->Expire(key, ttl);
    }
}

void RedisDel(std::string_view key) {
    if (!db::redisReady()) return;
    auto lease = db::AcquireRedis();
    if (!lease) return;
    (void)(*lease)->Del(key);
}

// ── handlers ────────────────────────────────────────────

mcp::CallOutcome HListAgents(yyjson_val* args) {
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    agent::AgentKit kit(*db, false);
    (void)kit.EnsureSchemaAndSeed();
    auto list = kit.ListAgentDefs(ArgBool(args, "only_enabled", false));
    if (!list) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, list.error().message);
    }
    std::string arr = "[";
    for (std::size_t i = 0; i < list->size(); ++i) {
        const auto& a = (*list)[i];
        if (i) arr += ",";
        arr += fmt::format(
            R"({{"agent_id":"{}","name":"{}","role_tags":"{}","enabled":{},"version":{},"tools":{}}})",
            Esc(a.agent_id), Esc(a.name), Esc(a.role_tags), a.enabled, a.version,
            a.tools_json.empty() ? "[]" : a.tools_json);
    }
    arr += "]";
    return mcp::CallOutcome::Ok(arr);
}

mcp::CallOutcome HGetAgent(yyjson_val* args) {
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    agent::AgentKit kit(*db, false);
    (void)kit.EnsureSchemaAndSeed();
    const auto id = ArgStr(args, "agent_id");
    if (id.empty()) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::BadArguments, "需要 agent_id");
    }
    auto a = kit.GetAgentDef(id);
    if (!a) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::NotFound, a.error().message);
    }
    const auto tools = util::json::ParseStringArray(a->tools_json);
    std::string toolsArr = "[";
    for (std::size_t i = 0; i < tools.size(); ++i) {
        if (i) toolsArr += ",";
        toolsArr += fmt::format("\"{}\"", Esc(tools[i]));
    }
    toolsArr += "]";
    return mcp::CallOutcome::Ok(fmt::format(
        R"({{"agent_id":"{}","name":"{}","role_tags":"{}","enabled":{},"version":{},"tools":{},"output_hint":"{}","system_prompt":"{}"}})",
        Esc(a->agent_id), Esc(a->name), Esc(a->role_tags), a->enabled, a->version, toolsArr,
        Esc(a->output_hint), Esc(a->system_prompt)));
}

mcp::CallOutcome HRouteTask(yyjson_val* args) {
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    agent::AgentKit kit(*db, false);
    (void)kit.EnsureSchemaAndSeed();
    const auto task = ArgStr(args, "task");
    if (task.empty()) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::BadArguments, "需要 task");
    }
    auto r = kit.Route(task);
    if (!r) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, r.error().message);
    }
    return mcp::CallOutcome::Ok(fmt::format(R"({{"agent_id":"{}"}})", Esc(*r)));
}

mcp::CallOutcome HAgentTools(yyjson_val* args) {
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    agent::AgentKit kit(*db, false);
    (void)kit.EnsureSchemaAndSeed();
    auto id = ArgStr(args, "agent_id");
    if (id.empty()) {
        const auto task = ArgStr(args, "task");
        if (auto r = kit.Route(task); r) {
            id = *r;
        } else {
            return mcp::CallOutcome::Fail(mcp::CallStatus::BadArguments, "需要 agent_id 或 task");
        }
    }
    // 确保 novel MCP 工具在进程注册表中（外部 list 与内部解析一致）
    RegisterMcpTools(mcp::ToolRegistry::Instance());

    const auto resolved = kit.ResolveTools(id);
    std::string allowed = "[";
    std::string local = "[";
    std::string viaMcp = "[";
    std::string missing = "[";
    bool fA = true, fL = true, fM = true, fX = true;
    for (const auto& t : resolved) {
        if (!fA) allowed += ",";
        fA = false;
        allowed += fmt::format("\"{}\"", Esc(t.name));
        if (t.source == "local") {
            if (!fL) local += ",";
            fL = false;
            local += fmt::format("\"{}\"", Esc(t.mcpName));
        } else if (t.source == "mcp") {
            if (!fM) viaMcp += ",";
            fM = false;
            viaMcp += fmt::format("\"{}\"", Esc(t.mcpName));
        } else {
            if (!fX) missing += ",";
            fX = false;
            missing += fmt::format("\"{}\"", Esc(t.name));
        }
    }
    allowed += "]";
    local += "]";
    viaMcp += "]";
    missing += "]";

    // 全量 MCP novel 工具名（诊断用，不注入 Agent）
    const auto allMcp = mcp::ToolRegistry::Instance().ToolNames("novel");
    std::string allArr = "[";
    for (std::size_t i = 0; i < allMcp.size(); ++i) {
        if (i) allArr += ",";
        allArr += fmt::format("\"{}\"", Esc(allMcp[i]));
    }
    allArr += "]";

    return mcp::CallOutcome::Ok(fmt::format(
        R"({{"agent_id":"{}","whitelist":{},"resolved_local":{},"resolved_mcp":{},"missing":{},"mcp_module_all":{},"note":"Agent 只应调用 whitelist∩(local∪mcp)，mcp_module_all 仅供诊断，禁止一股脑注入"}})",
        Esc(id), allowed, local, viaMcp, missing, allArr));
}

mcp::CallOutcome HAgentInvoke(yyjson_val* args) {
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    agent::AgentKit kit(*db, McpWriteAllowed());
    (void)kit.EnsureSchemaAndSeed();
    std::string agentId = ArgStr(args, "agent_id");
    const auto task = ArgStr(args, "task");
    const auto chapterId = ArgI64(args, "chapter_id");
    if (agentId.empty() && task.empty()) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::BadArguments, "需要 agent_id 或 task");
    }
    if (agentId.empty()) {
        if (auto r = kit.Route(task); r) {
            agentId = *r;
        }
    }
    auto pkg = kit.BuildInvokePackage(agentId, task, chapterId);
    if (!pkg) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, pkg.error().message);
    }
    // 外部 LLM 应：1) 使用 system_prompt 2) 只 tools/call pkg.tools 里的名称
    return mcp::CallOutcome::Ok(fmt::format(
        R"({{"agent_id":"{}","routing_hint":"{}","system_prompt":"{}","allowed_tools":{},"task":"{}"}})",
        Esc(pkg->agent_id), Esc(pkg->routing_hint), Esc(pkg->system_prompt), pkg->tools_json,
        Esc(task)));
}

mcp::CallOutcome HListFieldDefs(yyjson_val* args) {
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    NovelFields fields(*db);
    auto list = fields.ListFieldDefs(ArgStr(args, "scope"), ArgStr(args, "entity_kind"));
    if (!list) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, list.error().message);
    }
    std::string arr = "[";
    for (std::size_t i = 0; i < list->size(); ++i) {
        const auto& d = (*list)[i];
        if (i) arr += ",";
        // enum_json 是数组，原样作为 JSON 嵌入（已转义外层字符串字段）
        std::string enumJson = d.enum_json.empty() ? "[]" : d.enum_json;
        if (enumJson.front() != '[') {
            enumJson = "[]";
        }
        arr += fmt::format(
            R"({{"id":{},"scope":"{}","entity_kind":"{}","key":"{}","title":"{}","value_type":"{}","enum":{},"description":"{}","is_system":{}}})",
            d.id, Esc(d.scope), Esc(d.entity_kind), Esc(d.field_key), Esc(d.title),
            Esc(d.value_type), enumJson, Esc(d.description), d.is_system);
    }
    arr += "]";
    return mcp::CallOutcome::Ok(arr);
}

mcp::CallOutcome HListEntityFields(yyjson_val* args) {
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    NovelFields fields(*db);
    const auto eid = ArgI64(args, "entity_id");
    const auto ch = ArgI64(args, "chapter_id");
    auto list = fields.ListEntityFields(eid, ch, ArgStr(args, "layer"));
    if (!list) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, list.error().message);
    }
    std::string arr = "[";
    for (std::size_t i = 0; i < list->size(); ++i) {
        const auto& f = (*list)[i];
        if (i) arr += ",";
        std::string vj = f.value_json.empty() ? "null" : f.value_json;
        // value_json 嵌入前确保是合法 JSON 值
        if (util::json::Classify(vj) == util::json::ValueKind::Invalid) {
            vj = "null";
        }
        // 数组解析摘要（给外部模型，避免它自己瞎切字符串）
        const auto brief = util::json::ValueBrief(f.value_json, 120);
        arr += fmt::format(
            R"({{"id":{},"entity_id":{},"key":"{}","value_text":"{}","value_json":{},"value_kind":"{}","value_brief":"{}","layer":"{}","chapter_scope":{},"chapter_to":{},"created_by":"{}"}})",
            f.id, f.entity_id, Esc(f.field_key), Esc(f.value_text), vj,
            util::json::KindLabel(util::json::Classify(f.value_json)), Esc(brief), Esc(f.layer),
            f.chapter_scope, f.chapter_to, Esc(f.created_by));
    }
    arr += "]";
    return mcp::CallOutcome::Ok(arr);
}

mcp::CallOutcome HGetEntity(yyjson_val* args) {
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    NovelGraph g(*db);
    const auto id = ArgI64(args, "id");
    const auto name = ArgStr(args, "name");
    EntityRow e;
    if (id > 0) {
        auto r = g.GetEntity(id);
        if (!r) return mcp::CallOutcome::Fail(mcp::CallStatus::NotFound, r.error().message);
        e = *r;
    } else if (!name.empty()) {
        auto list = g.ListEntities({}, name, 5);
        if (!list || list->empty()) {
            return mcp::CallOutcome::Fail(mcp::CallStatus::NotFound, "未找到实体");
        }
        e = list->front();
    } else {
        return mcp::CallOutcome::Fail(mcp::CallStatus::BadArguments, "需要 id 或 name");
    }
    NovelFields fields(*db);
    std::string fieldArr = "[]";
    if (auto fr = fields.EntityFieldsJson(e.id, 0); fr) {
        fieldArr = *fr;
    }
    const std::string payload = fmt::format(
        R"({{"id":{},"kind":"{}","name":"{}","summary":"{}","status":"{}","meta_json":{},"dynamic_fields":{}}})",
        e.id, Esc(e.kind), Esc(e.name), Esc(e.summary), Esc(e.status),
        e.meta_json.empty() ? "{}" : e.meta_json, fieldArr);
    RedisSet(fmt::format("novel:cache:entity:{}", e.id), payload, std::chrono::seconds{60});
    return mcp::CallOutcome::Ok(payload);
}

mcp::CallOutcome HListEntities(yyjson_val* args) {
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    NovelGraph g(*db);
    auto list = g.ListEntities(ArgStr(args, "kind"), ArgStr(args, "filter"),
                               static_cast<int>(ArgI64(args, "limit", 50)));
    if (!list) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, list.error().message);
    }
    std::string arr = "[";
    for (std::size_t i = 0; i < list->size(); ++i) {
        const auto& e = (*list)[i];
        if (i) arr += ",";
        arr += fmt::format(R"({{"id":{},"kind":"{}","name":"{}","summary":"{}"}})", e.id,
                           Esc(e.kind), Esc(e.name), Esc(e.summary));
    }
    arr += "]";
    return mcp::CallOutcome::Ok(arr);
}

mcp::CallOutcome HUpsertEntity(yyjson_val* args) {
    if (!McpWriteAllowed()) return WriteDenied("novel_upsert_entity");
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    NovelGraph g(*db);
    EntityRow row;
    row.id = ArgI64(args, "id");
    row.kind = ArgStr(args, "kind");
    row.name = ArgStr(args, "name");
    row.summary = ArgStr(args, "summary");
    if (row.kind.empty() || row.name.empty()) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::BadArguments, "需要 kind 与 name");
    }
    auto id = g.UpsertEntity(row);
    if (!id) return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, id.error().message);
    (void)g.SetCanon(row.kind, *id, "PROPOSED", "mcp");
    AuditWrite(*db, "upsert_entity", row.kind.empty() ? "entity" : row.kind, *id, "mcp PROPOSED");
    RedisDel(fmt::format("novel:cache:entity:{}", *id));
    return mcp::CallOutcome::Ok(fmt::format(R"({{"id":{},"canon":"PROPOSED"}})", *id));
}

mcp::CallOutcome HUpsertFieldDef(yyjson_val* args) {
    if (!McpWriteAllowed()) return WriteDenied("novel_upsert_field_def");
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    NovelFields fields(*db);
    FieldDefRow row;
    row.scope = ArgStr(args, "scope");
    row.entity_kind = ArgStr(args, "entity_kind");
    row.field_key = ArgStr(args, "field_key");
    row.title = ArgStr(args, "title");
    row.value_type = ArgStr(args, "value_type");
    row.description = ArgStr(args, "description");
    row.created_by = ArgStr(args, "created_by");
    if (row.created_by.empty()) row.created_by = "mcp";
    if (row.field_key.empty()) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::BadArguments, "需要 field_key");
    }
    auto id = fields.UpsertFieldDef(row);
    if (!id) return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, id.error().message);
    AuditWrite(*db, "upsert_field_def", "field_def", *id, row.field_key);
    return mcp::CallOutcome::Ok(fmt::format(R"({{"id":{}}})", *id));
}

mcp::CallOutcome HUpsertEntityField(yyjson_val* args) {
    if (!McpWriteAllowed()) return WriteDenied("novel_upsert_entity_field");
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    NovelFields fields(*db);
    EntityFieldRow row;
    row.entity_id = ArgI64(args, "entity_id");
    row.field_key = ArgStr(args, "field_key");
    row.value_text = ArgStr(args, "value_text");
    // value_json 可以是对象/数组——必须原样保留，不能当字符串塞 value_text
    if (args) {
        if (yyjson_val* v = yyjson_obj_get(args, "value_json"); v) {
            if (yyjson_is_str(v)) {
                row.value_json = yyjson_get_str(v);
            } else {
                size_t len = 0;
                if (char* t = yyjson_val_write(v, 0, &len)) {
                    row.value_json.assign(t, len);
                    std::free(t);
                }
            }
        }
    }
    row.chapter_scope = ArgI64(args, "chapter_scope");
    row.chapter_to = ArgI64(args, "chapter_to");
    row.layer = ArgStr(args, "layer");
    row.created_by = ArgStr(args, "created_by");
    row.note = ArgStr(args, "note");
    if (row.created_by.empty()) row.created_by = "mcp";
    if (row.field_key.empty()) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::BadArguments, "需要 field_key");
    }
    auto id = fields.UpsertEntityField(row);
    if (!id) return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, id.error().message);
    AuditWrite(*db, "upsert_entity_field", "entity", row.entity_id, row.field_key);
    RedisDel(fmt::format("novel:cache:entity:{}", row.entity_id));
    return mcp::CallOutcome::Ok(fmt::format(R"({{"id":{}}})", *id));
}

// P10 外部 Agent 写章节：正文由模型代笔，经 MCP 落库（PROPOSED/draft）
mcp::CallOutcome HUpsertChapter(yyjson_val* args) {
    if (!McpWriteAllowed()) return WriteDenied("novel_upsert_chapter");
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    NovelGraph g(*db);
    ChapterRow row;
    row.id = ArgI64(args, "id");
    row.volume_id = ArgI64(args, "volume_id");
    row.ord = static_cast<int>(ArgI64(args, "ord"));
    row.title = ArgStr(args, "title");
    row.status = ArgStr(args, "status");
    if (row.status.empty()) row.status = "draft";
    row.summary = ArgStr(args, "summary");
    row.body = ArgStr(args, "body");
    row.pov_entity_id = ArgI64(args, "pov_entity_id");
    row.words = static_cast<int>(ArgI64(args, "words"));
    if (row.words <= 0) row.words = static_cast<int>(row.body.size());
    if (row.title.empty() && row.id <= 0) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::BadArguments, "需要 title（新建）或 id（更新）");
    }
    auto id = g.UpsertChapter(row);
    if (!id) return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, id.error().message);
    (void)g.SetCanon("chapter", *id, "PROPOSED", "mcp");
    AuditWrite(*db, "upsert_chapter", "chapter", *id,
               fmt::format("ord={} title={} words={}", row.ord, row.title, row.words));
    return mcp::CallOutcome::Ok(fmt::format(R"({{"id":{},"words":{},"status":"{}","canon":"PROPOSED"}})",
                                             *id, row.words, Esc(row.status)));
}

mcp::CallOutcome HLinkRelation(yyjson_val* args) {
    if (!McpWriteAllowed()) return WriteDenied("novel_link_relation");
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    NovelGraph g(*db);
    RelationRow r;
    r.from_id = ArgI64(args, "from_id");
    r.to_id = ArgI64(args, "to_id");
    r.rel_type = ArgStr(args, "rel_type");
    r.strength = static_cast<int>(ArgI64(args, "strength", 50));
    r.reason = ArgStr(args, "reason");
    auto id = g.UpsertRelation(r);
    if (!id) return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, id.error().message);
    (void)g.SetCanon("relation", *id, "PROPOSED", "mcp");
    AuditWrite(*db, "link_relation", "relation", *id,
               fmt::format("{}->{} {}", r.from_id, r.to_id, r.rel_type));
    return mcp::CallOutcome::Ok(fmt::format(R"({{"id":{},"canon":"PROPOSED"}})", *id));
}

// —— P10.2：与 P4 Local Tools 对齐的只读图谱工具 ——

mcp::CallOutcome HGetRelations(yyjson_val* args) {
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    NovelGraph g(*db);
    const auto id = ArgI64(args, "entity_id");
    if (id <= 0) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::BadArguments, "需要 entity_id");
    }
    auto rels = g.GetRelations(id, true);
    if (!rels) return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, rels.error().message);
    std::string arr = "[";
    for (std::size_t i = 0; i < rels->size(); ++i) {
        const auto& r = (*rels)[i];
        if (i) arr += ",";
        arr += fmt::format(
            R"({{"from":{},"to":{},"type":"{}","strength":{},"status":"{}","reason":"{}","from_ch":{},"to_ch":{}}})",
            r.from_id, r.to_id, Esc(r.rel_type), r.strength, Esc(r.status), Esc(r.reason),
            r.from_chapter, r.to_chapter);
    }
    arr += "]";
    return mcp::CallOutcome::Ok(arr);
}

mcp::CallOutcome HGetChapter(yyjson_val* args) {
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    NovelGraph g(*db);
    const auto id = ArgI64(args, "id");
    if (id <= 0) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::BadArguments, "需要 id");
    }
    auto c = g.GetChapter(id);
    if (!c) return mcp::CallOutcome::Fail(mcp::CallStatus::NotFound, c.error().message);
    std::string body = c->body;
    if (body.size() > 800) body = body.substr(0, 800) + "…";
    return mcp::CallOutcome::Ok(fmt::format(
        R"({{"id":{},"title":"{}","summary":"{}","body":"{}","words":{},"status":"{}","pov_entity_id":{}}})",
        c->id, Esc(c->title), Esc(c->summary), Esc(body), c->words, Esc(c->status),
        c->pov_entity_id));
}

mcp::CallOutcome HGetRecentChapters(yyjson_val* args) {
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    NovelGraph g(*db);
    int k = static_cast<int>(ArgI64(args, "k", 3));
    if (k <= 0) k = 3;
    auto list = g.ListChapters(k);
    if (!list) return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, list.error().message);
    std::string arr = "[";
    for (std::size_t i = 0; i < list->size(); ++i) {
        const auto& c = (*list)[i];
        if (i) arr += ",";
        arr += fmt::format(R"({{"id":{},"title":"{}","summary":"{}","status":"{}","words":{}}})",
                           c.id, Esc(c.title), Esc(c.summary), Esc(c.status), c.words);
    }
    arr += "]";
    return mcp::CallOutcome::Ok(arr);
}

mcp::CallOutcome HGetForeshadows(yyjson_val*) {
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    NovelGraph g(*db);
    auto list = g.ListOpenForeshadows();
    if (!list) return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, list.error().message);
    std::string arr = "[";
    for (std::size_t i = 0; i < list->size(); ++i) {
        const auto& f = (*list)[i];
        if (i) arr += ",";
        arr += fmt::format(
            R"({{"id":{},"title":"{}","content":"{}","status":"{}","setup_ch":{},"payoff_ch":{},"importance":{}}})",
            f.id, Esc(f.title), Esc(f.content), Esc(f.status), f.setup_ch, f.payoff_ch,
            f.importance);
    }
    arr += "]";
    return mcp::CallOutcome::Ok(arr);
}

mcp::CallOutcome HGetSecretsFor(yyjson_val* args) {
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    NovelGraph g(*db);
    const auto id = ArgI64(args, "entity_id");
    const auto ch = ArgI64(args, "chapter_id");
    if (id <= 0) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::BadArguments, "需要 entity_id");
    }
    auto list = g.GetSecretsFor(id, ch);
    if (!list) return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, list.error().message);
    std::string arr = "[";
    for (std::size_t i = 0; i < list->size(); ++i) {
        const auto& s = (*list)[i];
        if (i) arr += ",";
        arr += fmt::format(
            R"({{"id":{},"content":"{}","truth":"{}","reveal_ch":{},"scope":"{}"}})", s.id,
            Esc(s.content), Esc(s.truth), s.reveal_ch, Esc(s.scope));
    }
    arr += "]";
    return mcp::CallOutcome::Ok(arr);
}

mcp::CallOutcome HGetEventChain(yyjson_val* args) {
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    NovelGraph g(*db);
    const auto id = ArgI64(args, "event_id");
    const auto depth = ArgI64(args, "depth", 3);
    if (id <= 0) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::BadArguments, "需要 event_id");
    }
    auto edges = g.GetEventChain(id, static_cast<int>(depth <= 0 ? 3 : depth));
    if (!edges) return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, edges.error().message);
    std::string arr = "[";
    for (std::size_t i = 0; i < edges->size(); ++i) {
        const auto& e = (*edges)[i];
        if (i) arr += ",";
        arr += fmt::format(R"({{"cause":{},"effect":{},"type":"{}","note":"{}"}})",
                           e.cause_event_id, e.effect_event_id, Esc(e.link_type), Esc(e.note));
    }
    arr += "]";
    return mcp::CallOutcome::Ok(arr);
}

mcp::CallOutcome HGetOwnership(yyjson_val* args) {
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    NovelGraph g(*db);
    const auto id = ArgI64(args, "entity_id");
    if (id <= 0) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::BadArguments, "需要 entity_id");
    }
    auto list = g.GetOwnerships(id);
    if (!list) return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, list.error().message);
    std::string arr = "[";
    for (std::size_t i = 0; i < list->size(); ++i) {
        const auto& o = (*list)[i];
        if (i) arr += ",";
        arr += fmt::format(R"({{"item_id":{},"from_ch":{},"to_ch":{},"how":"{}","note":"{}"}})",
                           o.item_id, o.from_chapter, o.to_chapter, Esc(o.how), Esc(o.note));
    }
    arr += "]";
    return mcp::CallOutcome::Ok(arr);
}

mcp::CallOutcome HLinkCausal(yyjson_val* args) {
    if (!McpWriteAllowed()) return WriteDenied("novel_link_causal");
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    NovelGraph g(*db);
    CausalLinkRow r;
    r.cause_event_id = ArgI64(args, "cause_event_id");
    r.effect_event_id = ArgI64(args, "effect_event_id");
    r.link_type = ArgStr(args, "link_type");
    r.note = ArgStr(args, "note");
    if (r.link_type.empty()) r.link_type = "causes";
    if (r.cause_event_id <= 0 || r.effect_event_id <= 0) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::BadArguments, "需要 cause_event_id 与 effect_event_id");
    }
    auto id = g.UpsertCausalLink(r);
    if (!id) return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, id.error().message);
    AuditWrite(*db, "link_causal", "event", r.cause_event_id,
               fmt::format("->{} {}", r.effect_event_id, r.link_type));
    return mcp::CallOutcome::Ok(fmt::format(R"({{"id":{}}})", *id));
}

mcp::CallOutcome HGetCharacterSlice(yyjson_val* args) {
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    NovelGraph g(*db);
    const auto id = ArgI64(args, "entity_id");
    const auto ch = ArgI64(args, "chapter_id");
    if (id <= 0) {
        return mcp::CallOutcome::Fail(mcp::CallStatus::BadArguments, "需要 entity_id");
    }
    auto sl = g.GetCharacterSlice(id, ch);
    if (!sl) return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, sl.error().message);
    std::string rel = "[";
    for (std::size_t i = 0; i < sl->relations.size(); ++i) {
        const auto& r = sl->relations[i];
        if (i) rel += ",";
        rel += fmt::format(R"({{"from":{},"to":{},"type":"{}","strength":{}}})", r.from_id,
                           r.to_id, Esc(r.rel_type), r.strength);
    }
    rel += "]";
    std::string fs = "[";
    for (std::size_t i = 0; i < sl->openForeshadows.size(); ++i) {
        const auto& f = sl->openForeshadows[i];
        if (i) fs += ",";
        fs += fmt::format(R"({{"id":{},"title":"{}","status":"{}"}})", f.id, Esc(f.title),
                          Esc(f.status));
    }
    fs += "]";
    return mcp::CallOutcome::Ok(fmt::format(
        R"({{"entity":{{"id":{},"kind":"{}","name":"{}","summary":"{}"}},"persona":{{"age":"{}","personality":"{}","goal":"{}","fear":"{}"}},"relations":{},"open_foreshadows":{},"knowledge_count":{}}})",
        sl->entity.id, Esc(sl->entity.kind), Esc(sl->entity.name), Esc(sl->entity.summary),
        Esc(sl->persona.age), Esc(sl->persona.personality), Esc(sl->persona.goal),
        Esc(sl->persona.fear), rel, fs, sl->knowledge.size()));
}

mcp::CallOutcome HGetWorldSlice(yyjson_val*) {
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    NovelGraph g(*db);
    auto sl = g.GetWorldSlice();
    if (!sl) return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, sl.error().message);
    auto arr = [](const std::vector<EntityRow>& rows) {
        std::string a = "[";
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (i) a += ",";
            a += fmt::format(R"({{"id":{},"kind":"{}","name":"{}","summary":"{}"}})", rows[i].id,
                             Esc(rows[i].kind), Esc(rows[i].name), Esc(rows[i].summary));
        }
        a += "]";
        return a;
    };
    return mcp::CallOutcome::Ok(fmt::format(
        R"({{"rules":{},"locations":{},"factions":{},"power_systems":{}}})", arr(sl->rules),
        arr(sl->locations), arr(sl->factions), arr(sl->powerSystems)));
}

mcp::CallOutcome HSearchMemory(yyjson_val* args) {
    db::sqlite::Database* db = ResolveDb();
    if (!db) return NeedDb();
    NovelMemory mem(*db);
    const auto kind = ArgStr(args, "kind");
    int limit = static_cast<int>(ArgI64(args, "limit", 20));
    if (limit <= 0) limit = 20;
    auto list = kind.empty() || kind == "chapter_summary"
                    ? mem.RecentChapterSummaries(limit)
                    : mem.ListByKind(kind, limit);
    if (!list) return mcp::CallOutcome::Fail(mcp::CallStatus::InternalError, list.error().message);
    const auto filter = ArgStr(args, "filter");
    std::string arr = "[";
    bool first = true;
    for (const auto& m : *list) {
        if (!filter.empty() && m.content.find(filter) == std::string::npos &&
            m.summary.find(filter) == std::string::npos) {
            continue;
        }
        if (!first) arr += ",";
        first = false;
        std::string sum = m.summary;
        if (sum.size() > 200) sum = sum.substr(0, 200) + "…";
        arr += fmt::format(
            R"({{"id":{},"kind":"{}","entity_id":{},"chapter_id":{},"summary":"{}"}})", m.id,
            Esc(m.kind), m.entity_id, m.chapter_id, Esc(sum));
    }
    arr += "]";
    return mcp::CallOutcome::Ok(arr);
}

} // namespace

void SetMcpDbOverride(db::sqlite::Database* db) noexcept { g_dbOverride = db; }

void SetMcpAllowWrite(bool allow) noexcept { g_mcpAllowWrite = allow; }

void SetMcpWriteForceDeny(bool deny) noexcept { g_writeForceDeny = deny; }

bool McpWriteAllowed() noexcept {
    if (g_writeForceDeny) return false; // 自检期间无条件拒绝（盖过设置与环境变量）
    return g_mcpAllowWrite || Settings().mcpAllowWrite || EnvWriteOn();
}

std::string ResolveAgentMcpToolsJson(std::string_view agentId, const mcp::ToolRegistry& reg,
                                     std::string_view toolsJson) {
    const auto whitelist = util::json::ParseStringArray(toolsJson);
    std::string allowed = "[";
    std::string mcpHit = "[";
    std::string missing = "[";
    bool fA = true, fM = true, fX = true;
    for (const auto& logicRaw : whitelist) {
        std::string logic = logicRaw;
        if (logic.starts_with("novel_")) {
            logic = logic.substr(6);
        }
        if (!fA) allowed += ",";
        fA = false;
        allowed += fmt::format("\"{}\"", Esc(logic));
        const std::string mcpName = "novel_" + logic;
        if (reg.Find(mcpName) || reg.Find(logic)) {
            if (!fM) mcpHit += ",";
            fM = false;
            mcpHit += fmt::format("\"{}\"", Esc(mcpName));
        } else {
            if (!fX) missing += ",";
            fX = false;
            missing += fmt::format("\"{}\"", Esc(logic));
        }
    }
    allowed += "]";
    mcpHit += "]";
    missing += "]";
    return fmt::format(
        R"({{"agent_id":"{}","allowed":{},"mcp_tools":{},"missing":{}}})", Esc(agentId), allowed,
        mcpHit, missing);
}

void RegisterMcpTools(mcp::ToolRegistry& reg) {
    reg.EnsureModule(mcp::ModuleInfo{.id = "novel", .title = "小说 Agent / 图谱 / 动态字段"});

    const auto regTool = [&](mcp::Tool t) { reg.Register(std::move(t)); };

    regTool(mcp::Tool{
        .name = "novel_list_agents",
        .title = "列出小说 Agent",
        .description = "返回 Agent 定义摘要。tools 字段是该 Agent 的白名单（JSON 数组，已解析）。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddBoolean(d, s, "only_enabled", "仅启用", false);
        }),
        .handler = HListAgents,
    });

    regTool(mcp::Tool{
        .name = "novel_get_agent",
        .title = "读取 Agent 定义",
        .description = "含 system_prompt 与 tools 白名单。调度时先 route 再 get_agent/agent_tools。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddString(d, s, "agent_id", "Agent id，如 character", true);
        }),
        .handler = HGetAgent,
    });

    regTool(mcp::Tool{
        .name = "novel_route_task",
        .title = "任务路由到 Agent",
        .description = "按任务文本返回 agent_id。不要把全部 MCP 工具塞给模型，应按路由结果收窄。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddString(d, s, "task", "任务描述", true);
        }),
        .handler = HRouteTask,
    });

    regTool(mcp::Tool{
        .name = "novel_agent_tools",
        .title = "解析 Agent 可用工具",
        .description =
            "返回该 Agent 白名单与 MCP/本地工具的交集。mcp_module_all 仅诊断，禁止注入 Agent 上下文。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddString(d, s, "agent_id", "Agent id（与 task 二选一）", false);
            mcp::schema::AddString(d, s, "task", "任务描述（可路由）", false);
        }),
        .handler = HAgentTools,
    });

    regTool(mcp::Tool{
        .name = "novel_agent_invoke",
        .title = "组装 Agent 执行包",
        .description =
            "返回 system_prompt + allowed_tools（仅白名单）。外部模型只应调用 allowed_tools 中的名称。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddString(d, s, "agent_id", "Agent id，可空则按 task 路由", false);
            mcp::schema::AddString(d, s, "task", "任务描述", false);
            mcp::schema::AddInteger(d, s, "chapter_id", "章节 id", false);
        }),
        .handler = HAgentInvoke,
    });

    regTool(mcp::Tool{
        .name = "novel_list_field_defs",
        .title = "列出动态字段定义",
        .description = "AI 可扩展的设定维度。enum 为 JSON 数组，调用方按数组解析。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddString(d, s, "scope", "entity|world|chapter|agent", false);
            mcp::schema::AddString(d, s, "entity_kind", "如 person", false);
        }),
        .handler = HListFieldDefs,
    });

    regTool(mcp::Tool{
        .name = "novel_list_entity_fields",
        .title = "列出实体动态字段",
        .description =
            "value_json 按 JSON 值返回（可为数组/对象）；value_kind/value_brief 已预解析，勿当纯文本切。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddInteger(d, s, "entity_id", "实体 id；0=世界级", true);
            mcp::schema::AddInteger(d, s, "chapter_id", "章 id 过滤", false);
            mcp::schema::AddString(d, s, "layer", "global|mask|true|…", false);
        }),
        .handler = HListEntityFields,
    });

    regTool(mcp::Tool{
        .name = "novel_get_entity",
        .title = "查询实体",
        .description = "含 meta_json 与 dynamic_fields 数组。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddInteger(d, s, "id", "实体 id", false);
            mcp::schema::AddString(d, s, "name", "名称（与 id 二选一）", false);
        }),
        .handler = HGetEntity,
    });

    regTool(mcp::Tool{
        .name = "novel_list_entities",
        .title = "列出实体",
        .description = "按 kind/名称过滤。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddString(d, s, "kind", "实体 kind", false);
            mcp::schema::AddString(d, s, "filter", "名称子串", false);
            mcp::schema::AddInteger(d, s, "limit", "上限", false, 50);
        }),
        .handler = HListEntities,
    });

    // —— P10.2：与 P4 对齐的只读图谱工具 ——
    regTool(mcp::Tool{
        .name = "novel_get_relations",
        .title = "查询关系边",
        .description = "与本地 get_relations 同源：实体出/入边。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddInteger(d, s, "entity_id", "实体 id", true);
        }),
        .handler = HGetRelations,
    });
    regTool(mcp::Tool{
        .name = "novel_get_chapter",
        .title = "获取章节",
        .description = "标题/摘要/截断正文（800 字）。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddInteger(d, s, "id", "章节 id", true);
        }),
        .handler = HGetChapter,
    });
    regTool(mcp::Tool{
        .name = "novel_get_recent_chapters",
        .title = "最近章节摘要",
        .description = "与本地 get_recent_chapters 对齐。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddInteger(d, s, "k", "条数，默认 3", false, 3);
        }),
        .handler = HGetRecentChapters,
    });
    regTool(mcp::Tool{
        .name = "novel_get_foreshadows",
        .title = "未回收伏笔",
        .description = "PLANNED|PLANTED|DEVELOPING。",
        .moduleId = "novel",
        .schemaJson = EmptySchema(),
        .handler = HGetForeshadows,
    });
    regTool(mcp::Tool{
        .name = "novel_get_secrets_for",
        .title = "角色知情秘密",
        .description = "按知情过滤，不知情不返回内容。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddInteger(d, s, "entity_id", "人物 id", true);
            mcp::schema::AddInteger(d, s, "chapter_id", "章 id 过滤", false);
        }),
        .handler = HGetSecretsFor,
    });
    regTool(mcp::Tool{
        .name = "novel_get_event_chain",
        .title = "事件因果链",
        .description = "BFS depth 跳，返回 causes/enables/… 边。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddInteger(d, s, "event_id", "事件 id", true);
            mcp::schema::AddInteger(d, s, "depth", "深度，默认 3", false, 3);
        }),
        .handler = HGetEventChain,
    });
    regTool(mcp::Tool{
        .name = "novel_get_ownership",
        .title = "持有物品",
        .description = "与本地 get_ownership 对齐。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddInteger(d, s, "entity_id", "持有者 id", true);
        }),
        .handler = HGetOwnership,
    });
    regTool(mcp::Tool{
        .name = "novel_get_character_slice",
        .title = "人物切片",
        .description = "人设+关系+未回收伏笔摘要（L3 材料）。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddInteger(d, s, "entity_id", "人物 id", true);
            mcp::schema::AddInteger(d, s, "chapter_id", "章 id", false);
        }),
        .handler = HGetCharacterSlice,
    });
    regTool(mcp::Tool{
        .name = "novel_get_world_slice",
        .title = "世界切片",
        .description = "规则/地点/势力/能力体系摘要。",
        .moduleId = "novel",
        .schemaJson = EmptySchema(),
        .handler = HGetWorldSlice,
    });
    regTool(mcp::Tool{
        .name = "novel_search_memory",
        .title = "检索记忆",
        .description = "按 kind 列记忆摘要；filter 子串过滤 content/summary。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddString(d, s, "kind", "chapter_summary|short_term|…", false);
            mcp::schema::AddString(d, s, "filter", "子串", false);
            mcp::schema::AddInteger(d, s, "limit", "上限", false, 20);
        }),
        .handler = HSearchMemory,
    });

    // 写工具（默认拒绝）
    regTool(mcp::Tool{
        .name = "novel_upsert_entity",
        .title = "写入实体（需允许写）",
        .description = "默认禁用。写成功 canon=PROPOSED。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddString(d, s, "kind", "person|item|…", true);
            mcp::schema::AddString(d, s, "name", "名称", true);
            mcp::schema::AddString(d, s, "summary", "摘要", false);
            mcp::schema::AddInteger(d, s, "id", "更新时的 id", false);
        }),
        .handler = HUpsertEntity,
    });

    regTool(mcp::Tool{
        .name = "novel_upsert_field_def",
        .title = "新增字段定义（需允许写）",
        .description = "field_builder / 外部 AI 扩展小说体系用。默认禁用。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddString(d, s, "scope", "entity|world|chapter|agent", true);
            mcp::schema::AddString(d, s, "entity_kind", "person 等", false);
            mcp::schema::AddString(d, s, "field_key", "稳定键", true);
            mcp::schema::AddString(d, s, "title", "展示名", false);
            mcp::schema::AddString(d, s, "value_type", "text|json|…", false);
            mcp::schema::AddString(d, s, "description", "说明", false);
            mcp::schema::AddString(d, s, "created_by", "agent id", false);
        }),
        .handler = HUpsertFieldDef,
    });

    regTool(mcp::Tool{
        .name = "novel_upsert_chapter",
        .title = "写入章节正文（需允许写）",
        .description = "外部 Agent/novel_writer 经 MCP 落库章节；写成功 canon=PROPOSED。默认禁用。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddInteger(d, s, "id", "更新时的章节 id", false);
            mcp::schema::AddInteger(d, s, "volume_id", "卷 id", false);
            mcp::schema::AddInteger(d, s, "ord", "章序", false);
            mcp::schema::AddString(d, s, "title", "章标题（新建必填）", false);
            mcp::schema::AddString(d, s, "status", "draft|writing|review|done", false);
            mcp::schema::AddString(d, s, "summary", "摘要", false);
            mcp::schema::AddString(d, s, "body", "正文", false);
            mcp::schema::AddInteger(d, s, "pov_entity_id", "POV 人物实体 id", false);
            mcp::schema::AddInteger(d, s, "words", "字数；空则按 body 长度", false);
        }),
        .handler = HUpsertChapter,
    });

    regTool(mcp::Tool{
        .name = "novel_upsert_entity_field",
        .title = "写入实体动态字段（需允许写）",
        .description =
            "value_json 可传数组/对象（如 identity_layers）。layer 支持 mask/true。默认禁用。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddInteger(d, s, "entity_id", "实体 id；0=世界级", true);
            mcp::schema::AddString(d, s, "field_key", "字段键", true);
            mcp::schema::AddString(d, s, "value_text", "文本值", false);
            mcp::schema::AddString(d, s, "value_json", "JSON 值（字符串或对象序列化）", false);
            mcp::schema::AddInteger(d, s, "chapter_scope", "生效起始章", false);
            mcp::schema::AddInteger(d, s, "chapter_to", "结束章，0=至今", false);
            mcp::schema::AddString(d, s, "layer", "global|mask|true|…", false);
            mcp::schema::AddString(d, s, "created_by", "agent id", false);
        }),
        .handler = HUpsertEntityField,
    });

    regTool(mcp::Tool{
        .name = "novel_link_relation",
        .title = "新增关系边（需允许写）",
        .description = "rel_type 可用词表外类型。默认禁用写。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddInteger(d, s, "from_id", "起点", true);
            mcp::schema::AddInteger(d, s, "to_id", "终点", true);
            mcp::schema::AddString(d, s, "rel_type", "关系类型", true);
            mcp::schema::AddInteger(d, s, "strength", "0-100", false, 50);
            mcp::schema::AddString(d, s, "reason", "原因", false);
        }),
        .handler = HLinkRelation,
    });

    regTool(mcp::Tool{
        .name = "novel_link_causal",
        .title = "新增因果边（需允许写）",
        .description = "与本地 link_causal 对齐。默认禁用写。",
        .moduleId = "novel",
        .schemaJson = SchemaWith([](yyjson_mut_doc* d, yyjson_mut_val* s) {
            mcp::schema::AddInteger(d, s, "cause_event_id", "因", true);
            mcp::schema::AddInteger(d, s, "effect_event_id", "果", true);
            mcp::schema::AddString(d, s, "link_type",
                                   "causes|enables|prevents|escalates|reveals", false);
            mcp::schema::AddString(d, s, "note", "说明", false);
        }),
        .handler = HLinkCausal,
    });

    log::Info("mcp novel 模块已注册 tools={}", reg.ToolNames("novel").size());
}

namespace {

// 自检期间**无条件**拒绝写工具；析构复位，覆盖所有 return 路径。
struct ScopedWriteForceDeny {
    ScopedWriteForceDeny() noexcept { SetMcpWriteForceDeny(true); }
    ~ScopedWriteForceDeny() noexcept { SetMcpWriteForceDeny(false); }
    ScopedWriteForceDeny(const ScopedWriteForceDeny&) = delete;
    ScopedWriteForceDeny& operator=(const ScopedWriteForceDeny&) = delete;
};

} // namespace

bool RunNovelMcpSelfCheck() {
    ScopedWriteForceDeny denyGuard; // 自检期间写工具必须被拒，且不受 Settings/环境变量影响
    bool pass = true;
    auto fail = [&](std::string_view why) {
        log::Error("novel MCP 自检 FAIL：{}", why);
        pass = false;
        if (const char* path = std::getenv("SHINE_NOVEL_CHECK_OUT"); path && *path) {
            if (FILE* f = std::fopen(path, "ab")) {
                const std::string line = fmt::format("novelmcp:fail {}\n", why);
                std::fwrite(line.data(), 1, line.size(), f);
                std::fclose(f);
            }
        }
    };

    db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) {
        fail("打开内存库失败");
        return false;
    }
    if (auto r = mem.Exec(R"SQL(
CREATE TABLE IF NOT EXISTS entities(id INTEGER PRIMARY KEY AUTOINCREMENT,kind TEXT,name TEXT,summary TEXT,status TEXT,meta_json TEXT,created_chapter INTEGER,updated INTEGER);
CREATE TABLE IF NOT EXISTS relations(id INTEGER PRIMARY KEY AUTOINCREMENT,from_id INTEGER,to_id INTEGER,rel_type TEXT,strength INTEGER,from_chapter INTEGER,to_chapter INTEGER,reason TEXT,status TEXT);
CREATE TABLE IF NOT EXISTS chapters(id INTEGER PRIMARY KEY AUTOINCREMENT,volume_id INTEGER,ord INTEGER,title TEXT,status TEXT,summary TEXT,body TEXT,pov_entity_id INTEGER,words INTEGER,updated INTEGER);
CREATE TABLE IF NOT EXISTS canon_logs(id INTEGER PRIMARY KEY AUTOINCREMENT,target_kind TEXT,target_id INTEGER,status TEXT,note TEXT,created INTEGER);
CREATE TABLE IF NOT EXISTS audit_logs(id INTEGER PRIMARY KEY AUTOINCREMENT,actor TEXT,action TEXT,target_kind TEXT,target_id INTEGER,detail TEXT,created INTEGER);
)SQL"); !r) {
        fail("建表失败");
        return false;
    }

    SetMcpDbOverride(&mem);
    SetMcpAllowWrite(false);

    mcp::ToolRegistry local;
    RegisterMcpTools(local);
    const auto names = local.ToolNames("novel");
    if (names.size() < 10) {
        fail(fmt::format("novel 工具过少 {}", names.size()));
    }
    auto has = [&](std::string_view n) {
        for (const auto& x : names) {
            if (x == n) return true;
        }
        return false;
    };
    for (const char* req : {"novel_list_agents", "novel_route_task", "novel_agent_tools",
                            "novel_agent_invoke", "novel_list_field_defs",
                            "novel_list_entity_fields", "novel_get_entity", "novel_get_relations",
                            "novel_get_chapter", "novel_get_recent_chapters",
                            "novel_get_foreshadows", "novel_get_secrets_for",
                            "novel_get_event_chain", "novel_get_ownership",
                            "novel_get_character_slice", "novel_get_world_slice",
                            "novel_search_memory", "novel_link_causal"}) {
        if (!has(req)) {
            fail(fmt::format("缺少工具 {}", req));
        }
    }

    // tools/list 是数组，调用方解析数组
    const std::string list = local.BuildToolsListJson();
    yyjson_doc* ldoc = yyjson_read(list.data(), list.size(), 0);
    if (!ldoc) {
        fail("tools/list 非法 JSON");
    } else {
        yyjson_val* root = yyjson_doc_get_root(ldoc);
        yyjson_val* tools = yyjson_obj_get(root, "tools");
        if (!tools || !yyjson_is_arr(tools)) {
            fail("tools/list 缺 tools 数组");
        } else {
            std::size_t novelCount = 0;
            std::size_t novelWithProps = 0;
            bool getEntityOk = false;
            size_t i = 0, n = 0;
            yyjson_val* item = nullptr;
            yyjson_arr_foreach(tools, i, n, item) {
                yyjson_val* name = yyjson_obj_get(item, "name");
                if (!(name && yyjson_is_str(name))) continue;
                const std::string_view nm{yyjson_get_str(name)};
                if (!nm.starts_with("novel_")) continue;
                ++novelCount;
                // G22 回归：inputSchema 曾因 key 悬空全部退化成 {"type":"object"}（properties 丢失）
                yyjson_val* sch = yyjson_obj_get(item, "inputSchema");
                yyjson_val* props = sch != nullptr ? yyjson_obj_get(sch, "properties") : nullptr;
                if (props != nullptr && yyjson_is_obj(props) && yyjson_obj_size(props) > 0) {
                    ++novelWithProps;
                    if (nm == "novel_get_entity" && yyjson_obj_get(props, "name") != nullptr) {
                        getEntityOk = true;
                    }
                }
            }
            if (novelCount < 16) fail(fmt::format("tools/list 中 novel_* 数量 {}", novelCount));
            if (novelWithProps < 8) {
                fail(fmt::format("novel_* 中 inputSchema 带 properties 的只有 {}（疑似 schema 退化）",
                                 novelWithProps));
            }
            if (!getEntityOk) {
                fail("novel_get_entity 的 inputSchema.properties 缺 name（schema 退化）");
            }
        }
        yyjson_doc_free(ldoc);
    }

    // seed + 路由
    {
        agent::AgentKit kit(mem, true);
        if (auto r = kit.EnsureSchemaAndSeed(); !r) {
            fail("seed 失败");
        } else {
            auto callJson = [&](std::string_view tool, const std::string& argsJson) {
                yyjson_doc* d =
                    argsJson.empty() ? nullptr : yyjson_read(argsJson.data(), argsJson.size(), 0);
                auto out = local.Call(tool, d ? yyjson_doc_get_root(d) : nullptr);
                if (d) yyjson_doc_free(d);
                return out;
            };

            auto route = callJson("novel_route_task", R"({"task":"创建一个卧底人物"})");
            if (!route.ok() || route.text.find("character") == std::string::npos) {
                fail(fmt::format("route 结果异常 {}", route.text));
            }

            auto tools = callJson("novel_agent_tools", R"({"agent_id":"character"})");
            if (!tools.ok()) {
                fail(fmt::format("agent_tools 失败 {}", tools.text));
            } else if (tools.text.find("whitelist") == std::string::npos ||
                       tools.text.find("mcp_module_all") == std::string::npos) {
                fail("agent_tools 应返回 whitelist + mcp_module_all");
            } else if (tools.text.find("upsert_entity") == std::string::npos) {
                fail("character 白名单应含 upsert_entity");
            } else {
                const auto pos = tools.text.find("\"whitelist\":");
                const auto pos2 = tools.text.find("\"resolved_local\":");
                if (pos != std::string::npos && pos2 != std::string::npos && pos2 > pos) {
                    const auto seg = tools.text.substr(pos, pos2 - pos);
                    if (seg.find("upsert_agent") != std::string::npos) {
                        fail("character 白名单不应含 upsert_agent");
                    }
                }
            }

            auto inv = callJson("novel_agent_invoke",
                                R"({"task":"添加一个字段表示血脉封印"})");
            if (!inv.ok()) {
                fail(fmt::format("agent_invoke 失败 {}", inv.text));
            } else if (inv.text.find("field_builder") == std::string::npos ||
                       inv.text.find("allowed_tools") == std::string::npos ||
                       inv.text.find("system_prompt") == std::string::npos) {
                fail("agent_invoke 应路由到 field_builder 且含 allowed_tools");
            } else if (inv.text.find("novel_list_agents") != std::string::npos) {
                fail("field_builder 执行包不应包含 novel_list_agents（禁止全量注入）");
            }

            auto write = callJson("novel_upsert_entity", R"({"kind":"person","name":"测试"})");
            if (write.ok() || write.text.find("禁用") == std::string::npos) {
                fail(fmt::format("写工具应默认拒绝，got ok={} text={}", write.ok(), write.text));
            }

            NovelGraph g(mem);
            auto pid = g.UpsertEntity({.kind = "person", .name = "林默"});
            NovelFields fields(mem);
            EntityFieldRow truth;
            truth.entity_id = pid.value_or(0);
            truth.field_key = "true_faction";
            truth.value_text = "北境";
            truth.layer = "true";
            truth.value_json = R"([{"id":"north"}])";
            (void)fields.UpsertEntityField(truth);

            auto ef = callJson("novel_list_entity_fields",
                               fmt::format(R"({{"entity_id":{}}})", pid.value_or(0)));
            if (!ef.ok()) {
                fail(fmt::format("list_entity_fields 失败 {}", ef.text));
            } else if (ef.text.find("true_faction") == std::string::npos ||
                       ef.text.find("\"value_kind\":\"array\"") == std::string::npos) {
                fail(fmt::format("动态字段应解析 array kind，got {}", ef.text));
            }

            agent::AgentKit kit2(mem, false);
            yyjson_doc* badArgs = yyjson_read(R"({"agent_id":"character"})", 24, 0);
            auto denied =
                kit2.CallToolAsAgent("character", "list_agents", yyjson_doc_get_root(badArgs));
            yyjson_doc_free(badArgs);
            if (denied) {
                fail("character 调用 list_agents 应被白名单拒绝");
            }

            auto pkg = kit2.BuildInvokePackage("agent_meta", "更新 writer prompt");
            if (!pkg) {
                fail("agent_meta BuildInvokePackage 失败");
            } else if (pkg->tools_json.find("novel_list_agents") == std::string::npos) {
                fail("agent_meta 执行包应含 novel_list_agents");
            } else if (pkg->tools_json.find("novel_upsert_entity_field") != std::string::npos) {
                fail("agent_meta 执行包不应含 upsert_entity_field");
            }
        }
    }

    SetMcpDbOverride(nullptr);
    SetMcpAllowWrite(false);

    if (pass) {
        log::Info("novel MCP 自检通过（注册/路由/白名单调度/数组字段/写保护）");
        if (const char* path = std::getenv("SHINE_NOVEL_CHECK_OUT"); path && *path) {
            if (FILE* f = std::fopen(path, "ab")) {
                const char* line = "novelmcp:ok\n";
                std::fwrite(line, 1, std::strlen(line), f);
                std::fclose(f);
            }
        }
    }
    return pass;
}

} // namespace shine::novelcore
