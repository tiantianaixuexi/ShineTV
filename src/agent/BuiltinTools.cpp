#include "agent/ToolRegistry.h"

#include "core/Log.h"
#include "novel/NovelGraph.h"
#include "util/Json.h"

#include <fmt/format.h>

#include <yyjson.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

namespace shine::agent {
namespace {

[[nodiscard]] yyjson_doc* DocFromJson(std::string_view json) {
    return yyjson_read(json.data(), json.size(), 0);
}

[[nodiscard]] yyjson_doc* OkDoc(std::string_view jsonBody) {
    const std::string j = fmt::format(R"({{"ok":true,"data":{}}})", jsonBody);
    return DocFromJson(j);
}

[[nodiscard]] yyjson_doc* ErrDoc(std::string_view code, std::string_view msg) {
    // 手工转义
    auto esc = [](std::string_view s) {
        std::string o;
        for (const char c : s) {
            if (c == '"' || c == '\\') o += '\\';
            o += c;
        }
        return o;
    };
    const std::string j =
        fmt::format(R"({{"ok":false,"code":"{}","message":"{}"}})", esc(code), esc(msg));
    return DocFromJson(j);
}

[[nodiscard]] std::int64_t ArgI64(yyjson_val* args, std::string_view key, std::int64_t def = 0) {
    if (!args) return def;
    yyjson_val* v = yyjson_obj_getn(args, key.data(), key.size());
    return (v && yyjson_is_num(v)) ? yyjson_get_sint(v) : def;
}

[[nodiscard]] std::string ArgStr(yyjson_val* args, std::string_view key) {
    if (!args) return {};
    yyjson_val* v = yyjson_obj_getn(args, key.data(), key.size());
    return (v && yyjson_is_str(v)) ? std::string{yyjson_get_str(v)} : std::string{};
}

// ---- 基类：持有 shared Graph ----
class GraphTool : public Tool {
public:
    explicit GraphTool(std::shared_ptr<novelcore::NovelGraph> g) noexcept : g_(std::move(g)) {}

protected:
    std::shared_ptr<novelcore::NovelGraph> g_;

    [[nodiscard]] novelcore::NovelGraph& G() const { return *g_; }

    [[nodiscard]] static yyjson_mut_val* ObjType(yyjson_mut_doc* doc) {
        yyjson_mut_val* o = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strcpy(doc, o, "type", "object");
        yyjson_mut_val* props = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_val(doc, o, "properties", props);
        yyjson_mut_val* req = yyjson_mut_arr(doc);
        yyjson_mut_obj_add_val(doc, o, "required", req);
        return o;
    }

    static void AddIntProp(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* name,
                           const char* desc) {
        yyjson_mut_val* props = yyjson_mut_obj_get(obj, "properties");
        if (!props) return;
        yyjson_mut_val* p = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strcpy(doc, p, "type", "integer");
        yyjson_mut_obj_add_strcpy(doc, p, "description", desc);
        yyjson_mut_obj_add_val(doc, props, name, p);
    }

    static void AddStrProp(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* name,
                           const char* desc) {
        yyjson_mut_val* props = yyjson_mut_obj_get(obj, "properties");
        if (!props) return;
        yyjson_mut_val* p = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strcpy(doc, p, "type", "string");
        yyjson_mut_obj_add_strcpy(doc, p, "description", desc);
        yyjson_mut_obj_add_val(doc, props, name, p);
    }
};

class GetEntityTool final : public GraphTool {
public:
    using GraphTool::GraphTool;
    std::string_view Name() const override { return "get_entity"; }
    std::string_view Description() const override { return "按 id 或 name 查询实体"; }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddIntProp(doc, o, "id", "实体 id");
        AddStrProp(doc, o, "name", "实体名（与 id 二选一）");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        const auto id = ArgI64(args, "id");
        const auto name = ArgStr(args, "name");
        if (id <= 0 && name.empty()) {
            return ErrDoc("bad_args", "需要 id 或 name");
        }
        novelcore::EntityRow e;
        if (id > 0) {
            auto r = G().GetEntity(id);
            if (!r) return ErrDoc("not_found", r.error().message);
            e = *r;
        } else {
            auto list = G().ListEntities({}, name, 5);
            if (!list || list->empty()) return ErrDoc("not_found", "未找到实体");
            e = list->front();
        }
        const auto j = fmt::format(
            R"({{"id":{},"kind":"{}","name":"{}","summary":"{}","status":"{}"}})", e.id, e.kind,
            e.name, e.summary, e.status);
        return OkDoc(j);
    }
};

class ListEntitiesTool final : public GraphTool {
public:
    using GraphTool::GraphTool;
    std::string_view Name() const override { return "list_entities"; }
    std::string_view Description() const override { return "按 kind/名称过滤列出实体"; }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddStrProp(doc, o, "kind", "实体 kind，如 person/location");
        AddStrProp(doc, o, "filter", "名称子串");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        auto list = G().ListEntities(ArgStr(args, "kind"), ArgStr(args, "filter"), 50);
        if (!list) return ErrDoc("db", list.error().message);
        std::string arr = "[";
        for (std::size_t i = 0; i < list->size(); ++i) {
            const auto& e = (*list)[i];
            if (i) arr += ",";
            arr += fmt::format(R"({{"id":{},"kind":"{}","name":"{}"}})", e.id, e.kind, e.name);
        }
        arr += "]";
        return OkDoc(arr);
    }
};

class GetRelationsTool final : public GraphTool {
public:
    using GraphTool::GraphTool;
    std::string_view Name() const override { return "get_relations"; }
    std::string_view Description() const override { return "查询实体关系边"; }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddIntProp(doc, o, "entity_id", "实体 id");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        const auto id = ArgI64(args, "entity_id");
        if (id <= 0) return ErrDoc("bad_args", "需要 entity_id");
        auto rels = G().GetRelations(id, true);
        if (!rels) return ErrDoc("db", rels.error().message);
        std::string arr = "[";
        for (std::size_t i = 0; i < rels->size(); ++i) {
            const auto& r = (*rels)[i];
            if (i) arr += ",";
            arr += fmt::format(R"({{"from":{},"to":{},"type":"{}","strength":{},"status":"{}"}})",
                               r.from_id, r.to_id, r.rel_type, r.strength, r.status);
        }
        arr += "]";
        return OkDoc(arr);
    }
};

class GetChapterTool final : public GraphTool {
public:
    using GraphTool::GraphTool;
    std::string_view Name() const override { return "get_chapter"; }
    std::string_view Description() const override { return "按 id 获取章节标题/摘要/截断正文"; }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddIntProp(doc, o, "id", "章节 id");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        const auto id = ArgI64(args, "id");
        if (id <= 0) return ErrDoc("bad_args", "需要 id");
        auto c = G().GetChapter(id);
        if (!c) return ErrDoc("not_found", c.error().message);
        std::string body = c->body;
        if (body.size() > 800) body = body.substr(0, 800) + "…";
        const auto j = fmt::format(
            R"({{"id":{},"title":"{}","summary":"{}","body":"{}","words":{}}})", c->id, c->title,
            c->summary, body, c->words);
        return OkDoc(j);
    }
};

class GetRecentChaptersTool final : public GraphTool {
public:
    using GraphTool::GraphTool;
    std::string_view Name() const override { return "get_recent_chapters"; }
    std::string_view Description() const override { return "最近 k 章摘要"; }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddIntProp(doc, o, "k", "条数，默认 3");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        int k = static_cast<int>(ArgI64(args, "k", 3));
        if (k <= 0) k = 3;
        auto list = G().ListChapters(k);
        if (!list) return ErrDoc("db", list.error().message);
        std::string arr = "[";
        for (std::size_t i = 0; i < list->size(); ++i) {
            const auto& c = (*list)[i];
            if (i) arr += ",";
            arr += fmt::format(R"({{"id":{},"title":"{}","summary":"{}"}})", c.id, c.title,
                               c.summary);
        }
        arr += "]";
        return OkDoc(arr);
    }
};

class GetForeshadowsTool final : public GraphTool {
public:
    using GraphTool::GraphTool;
    std::string_view Name() const override { return "get_foreshadows"; }
    std::string_view Description() const override { return "列出未回收伏笔"; }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override { return ObjType(doc); }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val*) override {
        auto list = G().ListOpenForeshadows();
        if (!list) return ErrDoc("db", list.error().message);
        std::string arr = "[";
        for (std::size_t i = 0; i < list->size(); ++i) {
            const auto& f = (*list)[i];
            if (i) arr += ",";
            arr += fmt::format(R"({{"id":{},"title":"{}","status":"{}"}})", f.id, f.title,
                               f.status);
        }
        arr += "]";
        return OkDoc(arr);
    }
};

class GetSecretsForTool final : public GraphTool {
public:
    using GraphTool::GraphTool;
    std::string_view Name() const override { return "get_secrets_for"; }
    std::string_view Description() const override {
        return "按知情过滤：某角色已知的秘密（不知情的不返回内容）";
    }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddIntProp(doc, o, "entity_id", "人物 id");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        const auto id = ArgI64(args, "entity_id");
        if (id <= 0) return ErrDoc("bad_args", "需要 entity_id");
        auto list = G().GetSecretsFor(id, 0);
        if (!list) return ErrDoc("db", list.error().message);
        std::string arr = "[";
        for (std::size_t i = 0; i < list->size(); ++i) {
            const auto& s = (*list)[i];
            if (i) arr += ",";
            arr += fmt::format(R"({{"id":{},"content":"{}"}})", s.id, s.content);
        }
        arr += "]";
        return OkDoc(arr);
    }
};

class GetEventChainTool final : public GraphTool {
public:
    using GraphTool::GraphTool;
    std::string_view Name() const override { return "get_event_chain"; }
    std::string_view Description() const override { return "事件因果链（depth 跳）"; }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddIntProp(doc, o, "event_id", "事件实体 id");
        AddIntProp(doc, o, "depth", "深度，默认 3");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        const auto id = ArgI64(args, "event_id");
        const auto depth = ArgI64(args, "depth", 3);
        if (id <= 0) return ErrDoc("bad_args", "需要 event_id");
        auto edges = G().GetEventChain(id, static_cast<int>(depth));
        if (!edges) return ErrDoc("db", edges.error().message);
        std::string arr = "[";
        for (std::size_t i = 0; i < edges->size(); ++i) {
            const auto& e = (*edges)[i];
            if (i) arr += ",";
            arr += fmt::format(R"({{"cause":{},"effect":{},"type":"{}"}})", e.cause_event_id,
                               e.effect_event_id, e.link_type);
        }
        arr += "]";
        return OkDoc(arr);
    }
};

class GetOwnershipTool final : public GraphTool {
public:
    using GraphTool::GraphTool;
    std::string_view Name() const override { return "get_ownership"; }
    std::string_view Description() const override { return "实体持有物品"; }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddIntProp(doc, o, "entity_id", "持有者 id");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        const auto id = ArgI64(args, "entity_id");
        if (id <= 0) return ErrDoc("bad_args", "需要 entity_id");
        auto list = G().GetOwnerships(id);
        if (!list) return ErrDoc("db", list.error().message);
        std::string arr = "[";
        for (std::size_t i = 0; i < list->size(); ++i) {
            const auto& o = (*list)[i];
            if (i) arr += ",";
            arr += fmt::format(R"({{"item_id":{},"from_ch":{},"note":"{}"}})", o.item_id,
                               o.from_chapter, o.note);
        }
        arr += "]";
        return OkDoc(arr);
    }
};

// ---- 写工具（PROPOSED）----
class UpsertEntityTool final : public GraphTool {
public:
    using GraphTool::GraphTool;
    bool IsWrite() const noexcept override { return true; }
    std::string_view Name() const override { return "upsert_entity"; }
    std::string_view Description() const override {
        return "新建/更新实体（写库，状态 PROPOSED 待作者确认）";
    }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddStrProp(doc, o, "kind", "实体 kind");
        AddStrProp(doc, o, "name", "名称");
        AddStrProp(doc, o, "summary", "摘要");
        AddIntProp(doc, o, "id", "已有实体 id（更新时）");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        novelcore::EntityRow row;
        row.id = ArgI64(args, "id");
        row.kind = ArgStr(args, "kind");
        row.name = ArgStr(args, "name");
        row.summary = ArgStr(args, "summary");
        auto id = G().UpsertEntity(row);
        if (!id) return ErrDoc("db", id.error().message);
        (void)G().SetCanon(row.kind.empty() ? "entity" : row.kind, *id, "PROPOSED", "agent 提议");
        return OkDoc(fmt::format(R"({{"id":{}}})", *id));
    }
};

class LinkRelationTool final : public GraphTool {
public:
    using GraphTool::GraphTool;
    bool IsWrite() const noexcept override { return true; }
    std::string_view Name() const override { return "link_relation"; }
    std::string_view Description() const override { return "新增关系边（PROPOSED）"; }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddIntProp(doc, o, "from_id", "起点");
        AddIntProp(doc, o, "to_id", "终点");
        AddStrProp(doc, o, "rel_type", "关系类型");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        novelcore::RelationRow r;
        r.from_id = ArgI64(args, "from_id");
        r.to_id = ArgI64(args, "to_id");
        r.rel_type = ArgStr(args, "rel_type");
        auto id = G().UpsertRelation(r);
        if (!id) return ErrDoc("db", id.error().message);
        (void)G().SetCanon("relation", *id, "PROPOSED", "agent 提议");
        return OkDoc(fmt::format(R"({{"id":{}}})", *id));
    }
};

class LinkCausalTool final : public GraphTool {
public:
    using GraphTool::GraphTool;
    bool IsWrite() const noexcept override { return true; }
    std::string_view Name() const override { return "link_causal"; }
    std::string_view Description() const override { return "新增因果边（PROPOSED）"; }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddIntProp(doc, o, "cause_event_id", "因");
        AddIntProp(doc, o, "effect_event_id", "果");
        AddStrProp(doc, o, "link_type", "causes|enables|prevents|escalates|reveals");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        novelcore::CausalLinkRow r;
        r.cause_event_id = ArgI64(args, "cause_event_id");
        r.effect_event_id = ArgI64(args, "effect_event_id");
        r.link_type = ArgStr(args, "link_type");
        auto id = G().UpsertCausalLink(r);
        if (!id) return ErrDoc("db", id.error().message);
        return OkDoc(fmt::format(R"({{"id":{}}})", *id));
    }
};

} // namespace

void RegisterBuiltinTools(ToolRegistry& reg, db::sqlite::Database& db, bool allowWrite) {
    auto g = std::make_shared<novelcore::NovelGraph>(db);
    auto add = [&](std::unique_ptr<Tool> t) { reg.Register(std::move(t)); };
    add(std::make_unique<GetEntityTool>(g));
    add(std::make_unique<ListEntitiesTool>(g));
    add(std::make_unique<GetRelationsTool>(g));
    add(std::make_unique<GetChapterTool>(g));
    add(std::make_unique<GetRecentChaptersTool>(g));
    add(std::make_unique<GetForeshadowsTool>(g));
    add(std::make_unique<GetSecretsForTool>(g));
    add(std::make_unique<GetEventChainTool>(g));
    add(std::make_unique<GetOwnershipTool>(g));
    if (allowWrite) {
        add(std::make_unique<UpsertEntityTool>(g));
        add(std::make_unique<LinkRelationTool>(g));
        add(std::make_unique<LinkCausalTool>(g));
    }
}

bool RunToolsSelfCheck() {
    db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) {
        log::Error("Tools 自检：打开内存库失败");
        return false;
    }
    // 最小表
    if (auto r = mem.Exec(R"SQL(
CREATE TABLE IF NOT EXISTS entities(id INTEGER PRIMARY KEY AUTOINCREMENT,kind TEXT,name TEXT,summary TEXT,status TEXT,meta_json TEXT,created_chapter INTEGER,updated INTEGER);
CREATE TABLE IF NOT EXISTS relations(id INTEGER PRIMARY KEY AUTOINCREMENT,from_id INTEGER,to_id INTEGER,rel_type TEXT,strength INTEGER,from_chapter INTEGER,to_chapter INTEGER,reason TEXT,status TEXT);
CREATE TABLE IF NOT EXISTS chapters(id INTEGER PRIMARY KEY AUTOINCREMENT,volume_id INTEGER,ord INTEGER,title TEXT,status TEXT,summary TEXT,body TEXT,pov_entity_id INTEGER,words INTEGER,updated INTEGER);
CREATE TABLE IF NOT EXISTS foreshadowings(id INTEGER PRIMARY KEY AUTOINCREMENT,title TEXT,content TEXT,status TEXT,setup_ch INTEGER,payoff_ch INTEGER,importance INTEGER,truth TEXT,entity_ids_json TEXT);
CREATE TABLE IF NOT EXISTS secrets(id INTEGER PRIMARY KEY AUTOINCREMENT,content TEXT,truth TEXT,reveal_ch INTEGER,reveal_condition TEXT,entity_id INTEGER,scope TEXT);
CREATE TABLE IF NOT EXISTS secret_knowledge(id INTEGER PRIMARY KEY AUTOINCREMENT,secret_id INTEGER,entity_id INTEGER,knows INTEGER,chapter_known INTEGER);
CREATE TABLE IF NOT EXISTS entity_ownerships(id INTEGER PRIMARY KEY AUTOINCREMENT,owner_id INTEGER,item_id INTEGER,from_chapter INTEGER,to_chapter INTEGER,how TEXT,note TEXT);
CREATE TABLE IF NOT EXISTS event_details(entity_id INTEGER PRIMARY KEY,time_label TEXT,location_id INTEGER,cause_note TEXT,result_note TEXT);
CREATE TABLE IF NOT EXISTS causal_links(id INTEGER PRIMARY KEY AUTOINCREMENT,cause_event_id INTEGER,effect_event_id INTEGER,link_type TEXT,note TEXT,ord INTEGER);
CREATE TABLE IF NOT EXISTS canon_logs(id INTEGER PRIMARY KEY AUTOINCREMENT,target_kind TEXT,target_id INTEGER,status TEXT,note TEXT,created INTEGER);
CREATE TABLE IF NOT EXISTS audit_logs(id INTEGER PRIMARY KEY AUTOINCREMENT,actor TEXT,action TEXT,target_kind TEXT,target_id INTEGER,detail TEXT,created INTEGER);
)SQL"); !r) {
        log::Error("Tools 自检：建表失败 {}", r.error().message);
        return false;
    }

    ToolRegistry reg;
    RegisterBuiltinTools(reg, mem, /*allowWrite=*/true);
    if (reg.size() < 10) {
        log::Error("Tools 自检：注册数量不足 {}", reg.size());
        return false;
    }

    // Export schema
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* tools = reg.ExportOpenAiTools(doc);
    size_t len = 0;
    char* tjson = yyjson_mut_val_write(tools, 0, &len);
    const bool exportOk = tjson && len > 10;
    if (tjson) std::free(tjson);
    yyjson_mut_doc_free(doc);
    if (!exportOk) {
        log::Error("Tools 自检：ExportOpenAiTools 失败");
        return false;
    }

    // 预置数据
    novelcore::NovelGraph g(mem);
    auto pid = g.UpsertEntity({.kind = std::string{novelcore::kind::person}, .name = "林默"});
    auto ch = g.UpsertChapter({.ord = 1, .title = "第一章", .summary = "开端", .body = "正文"});
    if (!pid || !ch) return false;

    // Execute get_entity by name
    {
        yyjson_doc* adoc = yyjson_read(R"({"name":"林默"})", 13, 0);
        auto r = reg.Execute("get_entity", yyjson_doc_get_root(adoc));
        bool ok = static_cast<bool>(r);
        if (ok) yyjson_doc_free(*r);
        yyjson_doc_free(adoc);
        if (!ok) {
            log::Error("Tools 自检：get_entity 失败");
            return false;
        }
    }
    // get_chapter
    {
        const auto j = fmt::format(R"({{"id":{}}})", *ch);
        yyjson_doc* adoc = yyjson_read(j.data(), j.size(), 0);
        auto r = reg.Execute("get_chapter", yyjson_doc_get_root(adoc));
        bool ok = static_cast<bool>(r);
        if (ok) yyjson_doc_free(*r);
        yyjson_doc_free(adoc);
        if (!ok) return false;
    }
    // 循环保护
    reg.ResetLoopState();
    std::string reason;
    for (int i = 0; i < 3; ++i) {
        reason = reg.CheckLoopGuard("get_entity", R"({"name":"林默"})");
        reg.NoteCall("get_entity", R"({"name":"林默"})");
    }
    if (reason.empty()) {
        log::Error("Tools 自检：重复调用保护未触发");
        return false;
    }

    // 循环：mock create 返回 function_call 一次再完成
    {
        ToolRegistry reg2;
        RegisterBuiltinTools(reg2, mem, false);
        int n = 0;
        auto create = [&](std::string_view, std::string_view, std::string_view)
            -> std::expected<std::string, std::string> {
            if (n++ == 0) {
                return std::string{R"({"output":[{"type":"function_call","call_id":"c1","name":"get_entity","arguments":"{\"name\":\"林默\"}"}]})"};
            }
            return std::string{R"({"output_text":"林默是主角。"})"};
        };
        ToolLoopStats stats;
        auto out = RunToolLoop(reg2, "sys", "介绍林默", create, &stats);
        if (!out || out->find("林默是主角") == std::string::npos || stats.steps < 1) {
            log::Error("Tools 自检：ToolLoop 失败 out={} steps={}", out ? *out : out.error(),
                       stats.steps);
            return false;
        }
    }

    log::Info("Tools 自检通过（注册/Export/Execute/循环保护/ToolLoop mock）");
    {
        const char* path = std::getenv("SHINE_NOVEL_CHECK_OUT");
        if (path && *path) {
            FILE* f = std::fopen(path, "ab");
            if (f) {
                const char* line = "tools:ok\n";
                std::fwrite(line, 1, std::strlen(line), f);
                std::fclose(f);
            }
        }
    }
    return true;
}

} // namespace shine::agent
