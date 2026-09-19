#include "agent/AgentKit.h"

#include "core/Log.h"
#include "mcp/ToolRegistry.h"
#include "novel/NovelFields.h"
#include "util/Json.h"
#include "util/Time.h"

#include <fmt/format.h>

#include <yyjson.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace shine::agent {
namespace {

void CheckFail(std::string_view msg) {
    log::Error("MultiAgent 自检：{}", msg);
    if (const char* path = std::getenv("SHINE_NOVEL_CHECK_OUT"); path && *path) {
        if (FILE* f = std::fopen(path, "ab")) {
            const std::string line = fmt::format("multiagent:fail {}\n", msg);
            std::fwrite(line.data(), 1, line.size(), f);
            std::fclose(f);
        }
    }
}

using novelcore::DbError;
using novelcore::FieldDefRow;
using novelcore::EntityFieldRow;
using novelcore::NovelFields;
using novelcore::RowId;

[[nodiscard]] std::string EscJson(std::string_view s) {
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
        } else {
            o += c;
        }
    }
    return o;
}

[[nodiscard]] yyjson_doc* OkDoc(std::string_view jsonBody) {
    const std::string j = fmt::format(R"({{"ok":true,"data":{}}})", jsonBody);
    if (yyjson_doc* d = yyjson_read(j.data(), j.size(), 0)) {
        return d;
    }
    // data 非法时退化为空 data，避免返回 null doc
    const std::string fallback = R"({"ok":true,"data":null})";
    return yyjson_read(fallback.data(), fallback.size(), 0);
}

[[nodiscard]] yyjson_doc* ErrDoc(std::string_view code, std::string_view msg) {
    const std::string j =
        fmt::format(R"({{"ok":false,"code":"{}","message":"{}"}})", EscJson(code), EscJson(msg));
    return yyjson_read(j.data(), j.size(), 0);
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

[[nodiscard]] bool ArgBool(yyjson_val* args, std::string_view key, bool def = false) {
    if (!args) return def;
    yyjson_val* v = yyjson_obj_getn(args, key.data(), key.size());
    if (!v) return def;
    return yyjson_is_true(v);
}

// 通用：写 tool schema
[[nodiscard]] yyjson_mut_val* ObjType(yyjson_mut_doc* doc) {
    yyjson_mut_val* o = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_strcpy(doc, o, "type", "object");
    yyjson_mut_val* props = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, o, "properties", props);
    yyjson_mut_val* req = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, o, "required", req);
    return o;
}

void AddStrProp(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* name, const char* desc) {
    yyjson_mut_val* props = yyjson_mut_obj_get(obj, "properties");
    if (!props) return;
    yyjson_mut_val* p = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_strcpy(doc, p, "type", "string");
    yyjson_mut_obj_add_strcpy(doc, p, "description", desc);
    yyjson_mut_obj_add_val(doc, props, name, p);
}

void AddIntProp(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* name, const char* desc) {
    yyjson_mut_val* props = yyjson_mut_obj_get(obj, "properties");
    if (!props) return;
    yyjson_mut_val* p = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_strcpy(doc, p, "type", "integer");
    yyjson_mut_obj_add_strcpy(doc, p, "description", desc);
    yyjson_mut_obj_add_val(doc, props, name, p);
}

void AddBoolProp(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* name, const char* desc) {
    yyjson_mut_val* props = yyjson_mut_obj_get(obj, "properties");
    if (!props) return;
    yyjson_mut_val* p = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_strcpy(doc, p, "type", "boolean");
    yyjson_mut_obj_add_strcpy(doc, p, "description", desc);
    yyjson_mut_obj_add_val(doc, props, name, p);
}

class KitTool : public Tool {
public:
    explicit KitTool(std::shared_ptr<AgentKit> kit) noexcept : kit_(std::move(kit)) {}
    explicit KitTool(std::shared_ptr<novelcore::NovelGraph> graph) noexcept
        : graph_(std::move(graph)) {}
    explicit KitTool(std::shared_ptr<NovelFields> fields,
                     std::shared_ptr<novelcore::NovelGraph> graph)
        : fields_(std::move(fields)), graph_(std::move(graph)) {}

protected:
    std::shared_ptr<AgentKit> kit_;
    std::shared_ptr<NovelFields> fields_;
    std::shared_ptr<novelcore::NovelGraph> graph_;
};

// ── 字段工具 ──────────────────────────────────────────────
class ListFieldDefsTool final : public KitTool {
public:
    using KitTool::KitTool;
    std::string_view Name() const override { return "list_field_defs"; }
    std::string_view Description() const override {
        return "列出动态字段定义（AI 可扩展的设定维度）";
    }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddStrProp(doc, o, "scope", "entity|world|chapter|agent，空=全部");
        AddStrProp(doc, o, "entity_kind", "如 person/location，可空");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        if (!fields_) {
            return ErrDoc("state", "fields 未绑定");
        }
        auto list = fields_->ListFieldDefs(ArgStr(args, "scope"), ArgStr(args, "entity_kind"));
        if (!list) return ErrDoc("db", list.error().message);
        std::string arr = "[";
        for (std::size_t i = 0; i < list->size(); ++i) {
            const auto& d = (*list)[i];
            if (i) arr += ",";
            arr += fmt::format(
                R"({{"id":{},"scope":"{}","entity_kind":"{}","key":"{}","title":"{}","value_type":"{}","is_system":{},"description":"{}"}})",
                d.id, EscJson(d.scope), EscJson(d.entity_kind), EscJson(d.field_key),
                EscJson(d.title), EscJson(d.value_type), d.is_system, EscJson(d.description));
        }
        arr += "]";
        return OkDoc(arr);
    }
};

class UpsertFieldDefTool final : public KitTool {
public:
    using KitTool::KitTool;
    bool IsWrite() const noexcept override { return true; }
    std::string_view Name() const override { return "upsert_field_def"; }
    std::string_view Description() const override {
        return "新增/更新动态字段定义（FieldAgent 扩展小说体系）";
    }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddStrProp(doc, o, "scope", "entity|world|chapter|agent");
        AddStrProp(doc, o, "entity_kind", "scope=entity 时可填 person 等");
        AddStrProp(doc, o, "field_key", "稳定键名");
        AddStrProp(doc, o, "title", "展示名");
        AddStrProp(doc, o, "value_type", "text|number|json|enum");
        AddStrProp(doc, o, "description", "说明");
        AddStrProp(doc, o, "created_by", "agent id");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        if (!fields_) return ErrDoc("state", "fields 未绑定");
        FieldDefRow row;
        row.scope = ArgStr(args, "scope");
        row.entity_kind = ArgStr(args, "entity_kind");
        row.field_key = ArgStr(args, "field_key");
        row.title = ArgStr(args, "title");
        row.value_type = ArgStr(args, "value_type");
        row.description = ArgStr(args, "description");
        row.created_by = ArgStr(args, "created_by");
        if (row.created_by.empty()) row.created_by = "field_builder";
        if (row.field_key.empty()) return ErrDoc("bad_args", "需要 field_key");
        auto id = fields_->UpsertFieldDef(row);
        if (!id) return ErrDoc("db", id.error().message);
        return OkDoc(fmt::format(R"({{"id":{}}})", *id));
    }
};

class UpsertEntityFieldTool final : public KitTool {
public:
    using KitTool::KitTool;
    bool IsWrite() const noexcept override { return true; }
    std::string_view Name() const override { return "upsert_entity_field"; }
    std::string_view Description() const override {
        return "写入实体动态字段值（支持身份层 layer 与分章 chapter_scope）；entity_id=0 表示世界级";
    }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddIntProp(doc, o, "entity_id", "实体 id；0=世界级字段");
        AddStrProp(doc, o, "field_key", "字段键");
        AddStrProp(doc, o, "value_text", "文本值");
        AddStrProp(doc, o, "value_json", "JSON 值字符串，可空");
        AddIntProp(doc, o, "chapter_scope", "生效起始章，0=全局");
        AddIntProp(doc, o, "chapter_to", "生效结束章，0=至今");
        AddStrProp(doc, o, "layer", "global|public|mask|true|private|自定义");
        AddStrProp(doc, o, "created_by", "agent id");
        AddStrProp(doc, o, "note", "备注");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        if (!fields_) return ErrDoc("state", "fields 未绑定");
        EntityFieldRow row;
        row.entity_id = ArgI64(args, "entity_id");
        row.field_key = ArgStr(args, "field_key");
        row.value_text = ArgStr(args, "value_text");
        row.value_json = ArgStr(args, "value_json");
        row.chapter_scope = ArgI64(args, "chapter_scope");
        row.chapter_to = ArgI64(args, "chapter_to");
        row.layer = ArgStr(args, "layer");
        row.created_by = ArgStr(args, "created_by");
        row.note = ArgStr(args, "note");
        if (row.field_key.empty()) return ErrDoc("bad_args", "需要 field_key");
        auto id = fields_->UpsertEntityField(row);
        if (!id) return ErrDoc("db", id.error().message);
        return OkDoc(fmt::format(R"({{"id":{}}})", *id));
    }
};

class ListEntityFieldsTool final : public KitTool {
public:
    using KitTool::KitTool;
    std::string_view Name() const override { return "list_entity_fields"; }
    std::string_view Description() const override {
        return "读取实体动态字段（按章过滤；可只看某 layer，如只读 mask 或 true）";
    }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddIntProp(doc, o, "entity_id", "实体 id；0=世界级");
        AddIntProp(doc, o, "chapter_id", "章 id，0=全局视图");
        AddStrProp(doc, o, "layer", "可选：global|mask|true|…");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        if (!fields_) return ErrDoc("state", "fields 未绑定");
        const auto eid = ArgI64(args, "entity_id");
        const auto ch = ArgI64(args, "chapter_id");
        auto list = fields_->ListEntityFields(eid, ch, ArgStr(args, "layer"));
        if (!list) return ErrDoc("db", list.error().message);
        std::string arr = "[";
        for (std::size_t i = 0; i < list->size(); ++i) {
            const auto& f = (*list)[i];
            if (i) arr += ",";
            arr += fmt::format(
                R"({{"id":{},"key":"{}","value":"{}","value_json":{},"layer":"{}","chapter_scope":{},"created_by":"{}"}})",
                f.id, EscJson(f.field_key), EscJson(f.value_text),
                f.value_json.empty() ? "null" : f.value_json, EscJson(f.layer), f.chapter_scope,
                EscJson(f.created_by));
        }
        arr += "]";
        return OkDoc(arr);
    }
};

// ── 图谱写工具（人物/物品等共用）────────────────────────
class UpsertEntityTool final : public KitTool {
public:
    using KitTool::KitTool;
    bool IsWrite() const noexcept override { return true; }
    std::string_view Name() const override { return "upsert_entity"; }
    std::string_view Description() const override {
        return "新建/更新实体（kind 可用已有值或新体系值；细节走动态字段）";
    }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddStrProp(doc, o, "kind", "person|item|location|faction|power_system|…或自定义");
        AddStrProp(doc, o, "name", "名称");
        AddStrProp(doc, o, "summary", "摘要");
        AddIntProp(doc, o, "id", "更新时的实体 id");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        if (!graph_) return ErrDoc("state", "graph 未绑定");
        novelcore::EntityRow row;
        row.id = ArgI64(args, "id");
        row.kind = ArgStr(args, "kind");
        row.name = ArgStr(args, "name");
        row.summary = ArgStr(args, "summary");
        if (row.kind.empty() || row.name.empty()) return ErrDoc("bad_args", "需要 kind 与 name");
        auto id = graph_->UpsertEntity(row);
        if (!id) return ErrDoc("db", id.error().message);
        (void)graph_->SetCanon(row.kind, *id, "PROPOSED", "agent 提议");
        return OkDoc(fmt::format(R"({{"id":{}}})", *id));
    }
};

class LinkRelationTool final : public KitTool {
public:
    using KitTool::KitTool;
    bool IsWrite() const noexcept override { return true; }
    std::string_view Name() const override { return "link_relation"; }
    std::string_view Description() const override {
        return "新增关系边；rel_type 可用词表外的新类型（表面关系可用 layer 字段补充）";
    }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddIntProp(doc, o, "from_id", "起点");
        AddIntProp(doc, o, "to_id", "终点");
        AddStrProp(doc, o, "rel_type", "关系类型");
        AddIntProp(doc, o, "strength", "0-100");
        AddStrProp(doc, o, "reason", "原因");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        if (!graph_) return ErrDoc("state", "graph 未绑定");
        novelcore::RelationRow r;
        r.from_id = ArgI64(args, "from_id");
        r.to_id = ArgI64(args, "to_id");
        r.rel_type = ArgStr(args, "rel_type");
        r.strength = static_cast<int>(ArgI64(args, "strength", 50));
        r.reason = ArgStr(args, "reason");
        auto id = graph_->UpsertRelation(r);
        if (!id) return ErrDoc("db", id.error().message);
        (void)graph_->SetCanon("relation", *id, "PROPOSED", "agent 提议");
        return OkDoc(fmt::format(R"({{"id":{}}})", *id));
    }
};

class GetEntityTool final : public KitTool {
public:
    using KitTool::KitTool;
    std::string_view Name() const override { return "get_entity"; }
    std::string_view Description() const override { return "按 id 或 name 查询实体"; }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddIntProp(doc, o, "id", "实体 id");
        AddStrProp(doc, o, "name", "实体名");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        if (!graph_) return ErrDoc("state", "graph 未绑定");
        novelcore::EntityRow e;
        const auto id = ArgI64(args, "id");
        const auto name = ArgStr(args, "name");
        if (id > 0) {
            auto r = graph_->GetEntity(id);
            if (!r) return ErrDoc("not_found", r.error().message);
            e = *r;
        } else if (!name.empty()) {
            auto list = graph_->ListEntities({}, name, 5);
            if (!list || list->empty()) return ErrDoc("not_found", "未找到实体");
            e = list->front();
        } else {
            return ErrDoc("bad_args", "需要 id 或 name");
        }
        return OkDoc(fmt::format(
            R"({{"id":{},"kind":"{}","name":"{}","summary":"{}","status":"{}"}})", e.id, e.kind,
            e.name, e.summary, e.status));
    }
};

class ListEntitiesTool final : public KitTool {
public:
    using KitTool::KitTool;
    std::string_view Name() const override { return "list_entities"; }
    std::string_view Description() const override { return "按 kind/名称过滤列出实体"; }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddStrProp(doc, o, "kind", "实体 kind，可空=全部");
        AddStrProp(doc, o, "filter", "名称子串");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        if (!graph_) return ErrDoc("state", "graph 未绑定");
        auto list = graph_->ListEntities(ArgStr(args, "kind"), ArgStr(args, "filter"), 50);
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

// ── Agent 元工具（agent_meta）────────────────────────────
class ListAgentsTool final : public KitTool {
public:
    explicit ListAgentsTool(std::shared_ptr<AgentKit> kit) : KitTool(std::move(kit)) {}
    std::string_view Name() const override { return "list_agents"; }
    std::string_view Description() const override { return "列出全部 Agent 定义"; }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddBoolProp(doc, o, "only_enabled", "仅启用的");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        if (!kit_) return ErrDoc("state", "kit 未绑定");
        auto list = kit_->ListAgentDefs(ArgBool(args, "only_enabled", false));
        if (!list) return ErrDoc("db", list.error().message);
        std::string arr = "[";
        for (std::size_t i = 0; i < list->size(); ++i) {
            const auto& a = (*list)[i];
            if (i) arr += ",";
            arr += fmt::format(
                R"({{"agent_id":"{}","name":"{}","role_tags":"{}","enabled":{},"version":{},"tools":{}}})",
                a.agent_id, a.name, a.role_tags, a.enabled, a.version,
                a.tools_json.empty() ? "[]" : a.tools_json);
        }
        arr += "]";
        return OkDoc(arr);
    }
};

class GetAgentTool final : public KitTool {
public:
    explicit GetAgentTool(std::shared_ptr<AgentKit> kit) : KitTool(std::move(kit)) {}
    std::string_view Name() const override { return "get_agent"; }
    std::string_view Description() const override { return "读取单个 Agent 定义（含 system_prompt）"; }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddStrProp(doc, o, "agent_id", "如 character / field_builder");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        if (!kit_) return ErrDoc("state", "kit 未绑定");
        auto a = kit_->GetAgentDef(ArgStr(args, "agent_id"));
        if (!a) return ErrDoc("not_found", a.error().message);
        return OkDoc(fmt::format(
            R"({{"agent_id":"{}","name":"{}","role_tags":"{}","system_prompt":"{}","tools":{},"output_hint":"{}","enabled":{},"version":{}}})",
            a->agent_id, a->name, a->role_tags, EscJson(a->system_prompt),
            a->tools_json.empty() ? "[]" : a->tools_json, a->output_hint, a->enabled,
            a->version));
    }
};

class UpsertAgentTool final : public KitTool {
public:
    explicit UpsertAgentTool(std::shared_ptr<AgentKit> kit) : KitTool(std::move(kit)) {}
    bool IsWrite() const noexcept override { return true; }
    std::string_view Name() const override { return "upsert_agent"; }
    std::string_view Description() const override {
        return "更新 Agent 的 Agent：修改 prompt/工具集/启用，或新建自定义 Agent";
    }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddStrProp(doc, o, "agent_id", "稳定 id");
        AddStrProp(doc, o, "name", "中文名");
        AddStrProp(doc, o, "role_tags", "逗号分隔角色标签");
        AddStrProp(doc, o, "system_prompt", "系统提示");
        AddStrProp(doc, o, "tools_json", "工具名 JSON 数组");
        AddStrProp(doc, o, "output_hint", "输出结构说明");
        AddBoolProp(doc, o, "enabled", "是否启用");
        AddIntProp(doc, o, "is_builtin", "1=内置");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        if (!kit_) return ErrDoc("state", "kit 未绑定");
        AgentDefRow row;
        row.agent_id = ArgStr(args, "agent_id");
        row.name = ArgStr(args, "name");
        row.role_tags = ArgStr(args, "role_tags");
        row.system_prompt = ArgStr(args, "system_prompt");
        row.tools_json = ArgStr(args, "tools_json");
        row.output_hint = ArgStr(args, "output_hint");
        row.enabled = ArgBool(args, "enabled", true) ? 1 : 0;
        const auto builtin = ArgI64(args, "is_builtin", -1);
        row.is_builtin = builtin < 0 ? 0 : static_cast<int>(builtin);
        if (row.agent_id.empty()) return ErrDoc("bad_args", "需要 agent_id");
        // 已存在则合并：缺省字段保留旧值
        if (auto old = kit_->GetAgentDef(row.agent_id); old) {
            if (row.name.empty()) row.name = old->name;
            if (row.role_tags.empty()) row.role_tags = old->role_tags;
            if (row.system_prompt.empty()) row.system_prompt = old->system_prompt;
            if (row.tools_json.empty()) row.tools_json = old->tools_json;
            if (row.output_hint.empty()) row.output_hint = old->output_hint;
            if (builtin < 0) row.is_builtin = old->is_builtin;
            row.version = old->version + 1;
        }
        auto id = kit_->UpsertAgentDef(row);
        if (!id) return ErrDoc("db", id.error().message);
        return OkDoc(fmt::format(R"({{"id":{},"agent_id":"{}","version":{}}})", *id, row.agent_id,
                                 row.version));
    }
};

class RouteTaskTool final : public KitTool {
public:
    explicit RouteTaskTool(std::shared_ptr<AgentKit> kit) : KitTool(std::move(kit)) {}
    std::string_view Name() const override { return "route_task"; }
    std::string_view Description() const override {
        return "根据任务文本路由到最合适的 Agent";
    }
    yyjson_mut_val* Schema(yyjson_mut_doc* doc) const override {
        auto* o = ObjType(doc);
        AddStrProp(doc, o, "task", "任务描述");
        return o;
    }
    std::expected<yyjson_doc*, ToolError> Execute(yyjson_val* args) override {
        if (!kit_) return ErrDoc("state", "kit 未绑定");
        auto id = kit_->Route(ArgStr(args, "task"));
        if (!id) return ErrDoc("db", id.error().message);
        return OkDoc(fmt::format(R"({{"agent_id":"{}"}})", *id));
    }
};

} // namespace

// ── AgentKit ─────────────────────────────────────────────

AgentKit::AgentKit(db::sqlite::Database& db, bool allowWrite)
    : db_(&db), allowWrite_(allowWrite) {
    graph_ = std::make_shared<novelcore::NovelGraph>(db);
}

novelcore::NovelGraph& AgentKit::Graph() const {
    if (!graph_) {
        graph_ = std::make_shared<novelcore::NovelGraph>(*db_);
    }
    return *graph_;
}

std::expected<void, DbError> AgentKit::EnsureSchemaAndSeed() {
    if (auto r = db_->Exec(R"SQL(
CREATE TABLE IF NOT EXISTS agent_defs(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  agent_id TEXT NOT NULL UNIQUE,
  name TEXT NOT NULL DEFAULT '',
  role_tags TEXT NOT NULL DEFAULT '',
  system_prompt TEXT NOT NULL DEFAULT '',
  tools_json TEXT NOT NULL DEFAULT '[]',
  output_hint TEXT NOT NULL DEFAULT '',
  enabled INTEGER NOT NULL DEFAULT 1,
  is_builtin INTEGER NOT NULL DEFAULT 1,
  version INTEGER NOT NULL DEFAULT 1,
  updated INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS field_defs(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  scope TEXT NOT NULL DEFAULT 'entity',
  entity_kind TEXT NOT NULL DEFAULT '',
  field_key TEXT NOT NULL,
  title TEXT NOT NULL DEFAULT '',
  value_type TEXT NOT NULL DEFAULT 'text',
  enum_json TEXT NOT NULL DEFAULT '[]',
  description TEXT NOT NULL DEFAULT '',
  created_by TEXT NOT NULL DEFAULT '',
  is_system INTEGER NOT NULL DEFAULT 0,
  status TEXT NOT NULL DEFAULT 'PROPOSED',
  updated INTEGER NOT NULL DEFAULT 0);
CREATE UNIQUE INDEX IF NOT EXISTS idx_field_defs_key ON field_defs(scope, entity_kind, field_key);
CREATE TABLE IF NOT EXISTS field_aliases(
  alias TEXT PRIMARY KEY,
  canonical_key TEXT NOT NULL DEFAULT '',
  note TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS entity_fields(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  entity_id INTEGER NOT NULL DEFAULT 0,
  field_key TEXT NOT NULL,
  value_text TEXT NOT NULL DEFAULT '',
  value_json TEXT NOT NULL DEFAULT 'null',
  chapter_scope INTEGER NOT NULL DEFAULT 0,
  chapter_to INTEGER NOT NULL DEFAULT 0,
  layer TEXT NOT NULL DEFAULT 'global',
  note TEXT NOT NULL DEFAULT '',
  created_by TEXT NOT NULL DEFAULT '',
  updated INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS idx_ef_entity ON entity_fields(entity_id);
CREATE INDEX IF NOT EXISTS idx_ef_key ON entity_fields(field_key);
CREATE UNIQUE INDEX IF NOT EXISTS idx_ef_uniq
  ON entity_fields(entity_id, field_key, chapter_scope, layer);
)SQL"); !r) {
        return r;
    }
    // v8（S2b）：旧库补 `field_defs.status` —— CREATE TABLE IF NOT EXISTS 不会改已存在的表，
    // 缺了它 UpsertFieldDef 的 INSERT 会整条失败、种子静默丢失（自检就踩到过）。
    // 列已存在则 ALTER 报错，忽略；`is_system=1` 的种子回填为 CANON。
    (void)db_->Exec("ALTER TABLE field_defs ADD COLUMN status TEXT NOT NULL DEFAULT 'PROPOSED'");
    (void)db_->Exec("UPDATE field_defs SET status='CANON' WHERE is_system=1");
    NovelFields fields(*db_);
    NovelFields::SeedBuiltinFieldDefs(fields);

    for (const auto& a : BuiltinAgents()) {
        auto existing = GetAgentDef(a.agent_id);
        if (existing) {
            // 库内已有则不覆盖作者修改的 prompt；仅保证 enabled 行存在
            continue;
        }
        if (auto r = UpsertAgentDef(a); !r) {
            return std::unexpected(r.error());
        }
    }
    return {};
}

std::expected<RowId, DbError> AgentKit::UpsertAgentDef(const AgentDefRow& row) {
    if (row.agent_id.empty()) {
        return std::unexpected(DbError{0, "agent_id 不能为空"});
    }
    const auto now = util::NowMillis() / 1000;
    (void)db_->Exec(fmt::format("DELETE FROM agent_defs WHERE agent_id='{}'", row.agent_id));
    auto st = db_->Prepare(
        "INSERT INTO agent_defs(agent_id,name,role_tags,system_prompt,tools_json,output_hint,"
        "enabled,is_builtin,version,updated) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10)");
    if (!st) {
        return std::unexpected(st.error());
    }
    (void)st->BindText(1, row.agent_id);
    (void)st->BindText(2, row.name);
    (void)st->BindText(3, row.role_tags);
    (void)st->BindText(4, row.system_prompt);
    (void)st->BindText(5, row.tools_json.empty() ? "[]" : row.tools_json);
    (void)st->BindText(6, row.output_hint);
    (void)st->BindInt(7, row.enabled);
    (void)st->BindInt(8, row.is_builtin);
    (void)st->BindInt(9, row.version <= 0 ? 1 : row.version);
    (void)st->BindInt(10, static_cast<std::int64_t>(now));
    if (auto s = st->Step(); !s || *s != db::sqlite::StepResult::Done) {
        return std::unexpected(DbError{0, "insert agent_defs 失败"});
    }
    return db_->LastInsertRowId();
}

std::expected<AgentDefRow, DbError> AgentKit::GetAgentDef(std::string_view agentId) const {
    auto st = db_->Prepare(
        "SELECT id,agent_id,name,role_tags,system_prompt,tools_json,output_hint,enabled,"
        "is_builtin,version,updated FROM agent_defs WHERE agent_id=?1");
    if (!st) {
        return std::unexpected(st.error());
    }
    (void)st->BindText(1, agentId);
    if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
        AgentDefRow r;
        r.id = st->ColumnInt(0);
        r.agent_id = st->ColumnText(1);
        r.name = st->ColumnText(2);
        r.role_tags = st->ColumnText(3);
        r.system_prompt = st->ColumnText(4);
        r.tools_json = st->ColumnText(5);
        r.output_hint = st->ColumnText(6);
        r.enabled = st->ColumnInt(7);
        r.is_builtin = st->ColumnInt(8);
        r.version = st->ColumnInt(9);
        r.updated = st->ColumnInt(10);
        return r;
    }
    return std::unexpected(DbError{0, "agent 不存在"});
}

std::expected<std::vector<AgentDefRow>, DbError> AgentKit::ListAgentDefs(bool onlyEnabled) const {
    std::string sql =
        "SELECT id,agent_id,name,role_tags,system_prompt,tools_json,output_hint,enabled,"
        "is_builtin,version,updated FROM agent_defs";
    if (onlyEnabled) {
        sql += " WHERE enabled=1";
    }
    sql += " ORDER BY agent_id";
    auto st = db_->Prepare(sql);
    if (!st) {
        return std::unexpected(st.error());
    }
    std::vector<AgentDefRow> out;
    while (auto s = st->Step()) {
        if (*s != db::sqlite::StepResult::Row) break;
        AgentDefRow r;
        r.id = st->ColumnInt(0);
        r.agent_id = st->ColumnText(1);
        r.name = st->ColumnText(2);
        r.role_tags = st->ColumnText(3);
        r.system_prompt = st->ColumnText(4);
        r.tools_json = st->ColumnText(5);
        r.output_hint = st->ColumnText(6);
        r.enabled = st->ColumnInt(7);
        r.is_builtin = st->ColumnInt(8);
        r.version = st->ColumnInt(9);
        r.updated = st->ColumnInt(10);
        out.push_back(std::move(r));
    }
    return out;
}

std::expected<void, DbError> AgentKit::SetAgentEnabled(std::string_view agentId, bool enabled) {
    if (auto r = db_->Exec(fmt::format(
            "UPDATE agent_defs SET enabled={},updated={} WHERE agent_id='{}'", enabled ? 1 : 0,
            util::NowMillis() / 1000, agentId));
        !r) {
        return r;
    }
    return {};
}

std::expected<void, DbError> AgentKit::DeleteAgentDef(std::string_view agentId, bool force) {
    auto cur = GetAgentDef(agentId);
    if (!cur) {
        return std::unexpected(cur.error());
    }
    if (cur->is_builtin && !force) {
        return std::unexpected(DbError{0, "内置 Agent 需 force 删除（或改为 enabled=0）"});
    }
    return db_->Exec(fmt::format("DELETE FROM agent_defs WHERE agent_id='{}'", agentId));
}

std::expected<std::string, DbError> AgentKit::Route(std::string_view taskText) const {
    // 库内 routing_hints 字段可增强；默认关键词表
    NovelFields fields(*db_);
    std::string extra;
    if (auto hints = fields.GetEntityFieldByKey(0, "routing_hints", 0, "global"); hints) {
        extra = hints->value_text + " " + hints->value_json;
    }
    const std::string text = std::string{taskText} + " " + extra;

    struct Rule {
        std::string_view agent;
        std::string_view keys;
    };
    constexpr Rule kRules[] = {
        {"agent_meta", "更新agent|改agent|改提示|新建agent|agent的prompt|元agent|提示词|upsert_agent|list_agents"},
        {"field_builder", "添加字段|新字段|扩展字段|自定义字段|新维度|加设定字段|字段"},
        {"character", "创建人物|新建人物|人物设定|角色设定|写人物|身份|卧底|反派角色|人物"},
        {"item", "创建物品|法宝|道具|武器设定|写物品|物品"},
        {"world", "世界观|宇宙|多重宇宙|多元宇宙|世界规则|力量体系|修仙体系|经济体系"},
        {"place", "地点|城市|地图|山脉|势力据点"},
        {"faction", "势力|宗门|国家|组织|阵营"},
        {"event", "事件|因果|时间线|冲突"},
        {"mystery", "伏笔|秘密|谜团|揭秘"},
        {"extract", "抽取|从正文提取|整理实体"},
        {"review", "审校|一致性|检查矛盾|critic"},
        {"memory", "记忆检索|相关设定|回忆设定"},
        {"visual", "视觉|立绘|分镜|出图|prompt层"},
        {"novel_writer", "写小说|写正文|写本章|续写|生成章节|写一章"},
    };

    auto hasKey = [&text](std::string_view keys) {
        // keys 用 | 分隔
        std::size_t start = 0;
        while (start < keys.size()) {
            const auto bar = keys.find('|', start);
            const auto part =
                keys.substr(start, bar == std::string_view::npos ? std::string_view::npos
                                                                 : bar - start);
            if (!part.empty() && text.find(part) != std::string::npos) {
                return true;
            }
            if (bar == std::string_view::npos) break;
            start = bar + 1;
        }
        return false;
    };

    for (const auto& r : kRules) {
        if (hasKey(r.keys)) {
            if (auto def = GetAgentDef(r.agent); def && def->enabled) {
                return std::string{r.agent};
            }
        }
    }
    return std::string{"novel_writer"};
}

std::expected<std::string, DbError> AgentKit::BuildSystemPrompt(std::string_view agentId,
                                                                 RowId chapterId) const {
    auto def = GetAgentDef(agentId);
    std::string base;
    std::string toolsHint = "[]";
    std::string outHint;
    if (def) {
        base = def->system_prompt.empty() ? DefaultPromptFor(agentId) : def->system_prompt;
        toolsHint = def->tools_json.empty() ? "[]" : def->tools_json;
        outHint = def->output_hint;
    } else {
        base = DefaultPromptFor(agentId);
    }

    NovelFields fields(*db_);
    std::string fieldList = "[]";
    if (auto defs = fields.ListFieldDefs({}); defs) {
        std::string arr = "[";
        for (std::size_t i = 0; i < defs->size(); ++i) {
            const auto& d = (*defs)[i];
            if (i) arr += ",";
            arr += fmt::format(R"({{"key":"{}","scope":"{}","kind":"{}","type":"{}","system":{}}})",
                               d.field_key, d.scope, d.entity_kind, d.value_type, d.is_system);
        }
        arr += "]";
        fieldList = arr;
    }

    std::string worldFields = "[]";
    if (auto wf = fields.ListWorldFields(chapterId); wf) {
        std::string arr = "[";
        for (std::size_t i = 0; i < wf->size(); ++i) {
            const auto& f = (*wf)[i];
            if (i) arr += ",";
            arr += fmt::format(R"({{"key":"{}","value":"{}","layer":"{}"}})", f.field_key,
                               f.value_text, f.layer);
        }
        arr += "]";
        worldFields = arr;
    }

    return fmt::format(
        "{}\n\n"
        "## 本项目约束\n"
        "- 小说体系**不写死**：需要新维度时用 upsert_field_def + upsert_entity_field 自己生成并存储。\n"
        "- 人物多面性用 layer 字段（mask=公开伪装，true=真实）；不同章不同世界观用 chapter_scope。\n"
        "- 写库后其它 Agent / MCP 通过 list_entity_fields / list_field_defs 读取理解。\n"
        "- 可用工具（白名单）：{}\n"
        "- 动态字段定义：{}\n"
        "- 当前世界级字段（章 {}）：{}\n"
        "{}",
        base, toolsHint, fieldList, chapterId, worldFields,
        outHint.empty() ? "" : "\n## 期望输出\n" + outHint);
}

void AgentKit::RegisterToolsFor(std::string_view agentId, ToolRegistry& reg) const {
    auto fields = std::make_shared<NovelFields>(*db_);
    auto g = graph_;
    auto self = std::const_pointer_cast<AgentKit>(
        std::shared_ptr<AgentKit>(const_cast<AgentKit*>(this), [](AgentKit*) {}));

    // 默认共享集
    auto addShared = [&] {
        reg.Register(std::make_unique<GetEntityTool>(g));
        reg.Register(std::make_unique<ListEntitiesTool>(g));
        if (allowWrite_) {
            reg.Register(std::make_unique<UpsertEntityTool>(g));
            reg.Register(std::make_unique<LinkRelationTool>(g));
            reg.Register(std::make_unique<ListFieldDefsTool>(fields, g));
            reg.Register(std::make_unique<UpsertEntityFieldTool>(fields, g));
            reg.Register(std::make_unique<ListEntityFieldsTool>(fields, g));
        } else {
            reg.Register(std::make_unique<ListFieldDefsTool>(fields, g));
            reg.Register(std::make_unique<ListEntityFieldsTool>(fields, g));
        }
    };

    // 字段/元 Agent 专属
    const bool isField = agentId == "field_builder";
    const bool isMeta = agentId == "agent_meta";
    const bool isWorld = agentId == "world" || agentId == "novel_writer" || isField;

    if (isMeta) {
        reg.Register(std::make_unique<ListAgentsTool>(self));
        reg.Register(std::make_unique<GetAgentTool>(self));
        if (allowWrite_) {
            reg.Register(std::make_unique<UpsertAgentTool>(self));
        }
        reg.Register(std::make_unique<RouteTaskTool>(self));
        reg.Register(std::make_unique<ListFieldDefsTool>(fields, g));
        return;
    }

    addShared();
    if (isField || isWorld) {
        if (allowWrite_) {
            reg.Register(std::make_unique<UpsertFieldDefTool>(fields, g));
        }
    }
}

namespace {

[[nodiscard]] std::string StripNovelPrefix(std::string_view name) {
    constexpr std::string_view kPrefix = "novel_";
    if (name.starts_with(kPrefix)) {
        return std::string{name.substr(kPrefix.size())};
    }
    return std::string{name};
}

[[nodiscard]] bool WhitelistHas(const std::vector<std::string>& list, std::string_view logic) {
    for (const auto& item : list) {
        if (item == logic || StripNovelPrefix(item) == logic) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string DocToString(yyjson_doc* doc) {
    if (!doc) return "{}";
    size_t len = 0;
    char* t = yyjson_val_write(yyjson_doc_get_root(doc), 0, &len);
    std::string out = t ? std::string{t, len} : "{}";
    if (t) std::free(t);
    yyjson_doc_free(doc);
    return out;
}

} // namespace

std::vector<AgentKit::ResolvedTool> AgentKit::ResolveTools(std::string_view agentId) const {
    std::vector<ResolvedTool> out;
    auto def = GetAgentDef(agentId);
    const std::string toolsJson = def ? def->tools_json : std::string{"[]"};
    const auto whitelist = util::json::ParseStringArray(toolsJson);

    ToolRegistry local;
    RegisterToolsFor(agentId, local);
    const auto localNames = local.Names();
    auto& mcp = mcp::ToolRegistry::Instance();

    for (const auto& logicRaw : whitelist) {
        const std::string logic = StripNovelPrefix(logicRaw);
        ResolvedTool rt;
        rt.name = logic;
        rt.mcpName = "novel_" + logic;
        bool isLocal = false;
        for (const auto& n : localNames) {
            if (n == logic) {
                isLocal = true;
                break;
            }
        }
        const bool isMcp =
            mcp.Find(rt.mcpName) != nullptr || mcp.Find(logic) != nullptr;
        if (isLocal) {
            rt.source = "local";
        } else if (isMcp) {
            rt.source = "mcp";
        } else {
            rt.source = "missing";
        }
        rt.write = logic.find("upsert") != std::string::npos ||
                   logic.starts_with("link_");
        out.push_back(std::move(rt));
    }
    return out;
}

std::expected<std::string, ToolError>
AgentKit::CallToolAsAgent(std::string_view agentId, std::string_view toolName,
                          yyjson_val* args) const {
    auto def = GetAgentDef(agentId);
    if (!def) {
        return std::unexpected(ToolError{"not_found", "agent 不存在"});
    }
    const std::string logic = StripNovelPrefix(toolName);
    const auto whitelist = util::json::ParseStringArray(def->tools_json);
    if (!WhitelistHas(whitelist, logic)) {
        return std::unexpected(ToolError{
            "forbidden",
            fmt::format("Agent {} 白名单不含工具 {}（拒绝全量注入）", agentId, logic)});
    }

    ToolRegistry local;
    RegisterToolsFor(agentId, local);
    bool hasLocal = false;
    for (const auto& n : local.Names()) {
        if (n == logic) {
            hasLocal = true;
            break;
        }
    }
    if (hasLocal) {
        auto r = local.Execute(logic, args);
        if (!r) {
            return std::unexpected(r.error());
        }
        return DocToString(*r);
    }

    auto& mcp = mcp::ToolRegistry::Instance();
    const std::string mcpName = "novel_" + logic;
    if (const mcp::Tool* t = mcp.Find(mcpName); t != nullptr) {
        auto outcome = mcp.Call(mcpName, args);
        if (!outcome.ok()) {
            return std::unexpected(ToolError{"mcp", outcome.text});
        }
        return outcome.text;
    }
    if (const mcp::Tool* t = mcp.Find(logic); t != nullptr) {
        auto outcome = mcp.Call(logic, args);
        if (!outcome.ok()) {
            return std::unexpected(ToolError{"mcp", outcome.text});
        }
        return outcome.text;
    }
    return std::unexpected(ToolError{"not_found",
                                     fmt::format("工具 {} 在本地与 MCP 均不存在", logic)});
}

std::expected<AgentKit::AgentInvokePackage, ToolError>
AgentKit::BuildInvokePackage(std::string_view agentId, std::string_view taskText,
                             novelcore::RowId chapterId) const {
    auto def = GetAgentDef(agentId);
    if (!def) {
        // 未知 agent：先尝试路由
        if (!taskText.empty()) {
            if (auto rid = Route(taskText); rid) {
                return BuildInvokePackage(*rid, taskText, chapterId);
            }
        }
        return std::unexpected(ToolError{"not_found", "agent 不存在且无法路由"});
    }
    if (!def->enabled) {
        return std::unexpected(ToolError{"disabled", fmt::format("Agent {} 已停用", agentId)});
    }

    auto prompt = BuildSystemPrompt(agentId, chapterId);
    if (!prompt) {
        return std::unexpected(ToolError{"db", prompt.error().message});
    }

    const auto resolved = ResolveTools(agentId);
    std::string toolsArr = "[";
    std::string missingArr = "[";
    bool firstT = true;
    bool firstM = true;
    for (const auto& t : resolved) {
        if (t.source == "missing") {
            if (!firstM) missingArr += ",";
            firstM = false;
            missingArr += fmt::format("\"{}\"", t.name);
            continue;
        }
        if (!firstT) toolsArr += ",";
        firstT = false;
        // 对外暴露 MCP 名，便于外部客户端直接 tools/call
        toolsArr += fmt::format(R"({{"name":"{}","source":"{}","write":{}}})", t.mcpName,
                                t.source, t.write);
    }
    toolsArr += "]";
    missingArr += "]";

    AgentInvokePackage pkg;
    pkg.agent_id = std::string{agentId};
    pkg.system_prompt = *prompt;
    pkg.tools_json = toolsArr;
    if (!taskText.empty()) {
        if (auto rid = Route(taskText); rid) {
            pkg.routing_hint = *rid;
        }
    }
    if (!firstM) {
        log::Warn("Agent {} 白名单中无 MCP/本地实现：{}", agentId, missingArr);
    }
    return pkg;
}

std::expected<AgentRunResult, ToolError>
AgentKit::Run(const AgentRunRequest& req, const CreateFn& create, ToolLoopStats* stats) const {
    if (!create) {
        return std::unexpected(ToolError{"no_llm", "未提供 create 回调"});
    }
    auto prompt = BuildSystemPrompt(req.agent_id, req.chapter_id);
    if (!prompt) {
        return std::unexpected(ToolError{"db", prompt.error().message});
    }

    ToolRegistry reg;
    RegisterToolsFor(req.agent_id, reg);

    std::string user = req.user_text;
    if (!req.extra_json.empty() && req.extra_json != "{}") {
        user += "\n\nextra: " + req.extra_json;
    }
    if (req.entity_id > 0) {
        user += fmt::format("\nentity_id: {}", req.entity_id);
    }

    // tools schema 导出
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* tools = reg.ExportOpenAiTools(doc);
    size_t len = 0;
    char* tjson = tools ? yyjson_mut_val_write(tools, 0, &len) : nullptr;
    const std::string toolsJson = tjson ? std::string{tjson, len} : "[]";
    if (tjson) std::free(tjson);
    yyjson_mut_doc_free(doc);

    auto createWrap = [&](std::string_view instructions, std::string_view inputJson,
                          std::string_view toolsJsonIn)
        -> std::expected<std::string, std::string> {
        return create(instructions, inputJson, toolsJsonIn);
    };

    auto out = RunToolLoop(reg, *prompt, user, createWrap, stats);
    if (!out) {
        return std::unexpected(ToolError{"llm", out.error()});
    }

    AgentRunResult res;
    res.agent_id = req.agent_id;
    res.output_text = *out;
    res.tool_steps = stats ? stats->steps : 0;
    if (stats) {
        res.used_tools = stats->callLog;
    }
    return res;
}

std::vector<AgentDefRow> AgentKit::BuiltinAgents() {
    struct Spec {
        std::string_view id;
        std::string_view name;
        std::string_view tags;
        std::string_view tools;
        std::string_view outHint;
    };
    constexpr Spec kSpecs[] = {
        {"novel_writer", "写小说 Agent", "write,chapter",
         R"(["get_entity","list_entities","list_entity_fields","list_field_defs","upsert_entity","upsert_entity_field"])",
         "章节正文或章计划 JSON"},
        {"character", "创建人物 Agent", "character,entity",
         R"(["get_entity","list_entities","upsert_entity","link_relation","list_field_defs","upsert_entity_field","list_entity_fields","upsert_field_def"])",
         R"({"kind":"person","name":"…","fields":[…identity_layers…]})"},
        {"item", "创建物品 Agent", "item,entity",
         R"(["get_entity","list_entities","upsert_entity","upsert_entity_field","list_field_defs","list_entity_fields","upsert_field_def"])",
         "物品实体 + 自定义字段"},
        {"world", "世界观 Agent", "world,universe,rules",
         R"(["get_entity","list_entities","upsert_entity","upsert_entity_field","list_field_defs","list_entity_fields","upsert_field_def"])",
         "宇宙/规则实体 + active_universe / universe_layers 字段"},
        {"place", "地点 Agent", "location",
         R"(["get_entity","list_entities","upsert_entity","upsert_entity_field","list_field_defs","list_entity_fields"])",
         "地点实体与动态地理字段"},
        {"faction", "势力 Agent", "faction",
         R"(["get_entity","list_entities","upsert_entity","link_relation","upsert_entity_field","list_entity_fields","list_field_defs"])",
         "势力实体 + 成员关系（rel_type 可自定义）"},
        {"event", "事件 Agent", "event,causal",
         R"(["get_entity","list_entities","upsert_entity","upsert_entity_field","list_entity_fields"])",
         "事件实体与因果说明"},
        {"mystery", "伏笔秘密 Agent", "mystery,secret,foreshadow",
         R"(["get_entity","list_entities","upsert_entity","upsert_entity_field","list_entity_fields"])",
         "伏笔/秘密 + 知情 layer 字段"},
        {"extract", "抽取 Agent", "extract",
         R"(["list_entities","upsert_entity","upsert_entity_field","list_field_defs","list_entity_fields"])",
         "JSON：new_entities / new_fields"},
        {"review", "审校 Agent", "review,critic",
         R"(["get_entity","list_entities","list_entity_fields","list_field_defs"])",
         R"({"passed":bool,"issues":[…]})"},
        {"memory", "记忆检索 Agent", "memory,retrieve",
         R"(["get_entity","list_entities","list_entity_fields","list_field_defs"])",
         "相关实体与字段摘要"},
        {"visual", "视觉 Agent", "visual,prompt",
         R"(["get_entity","list_entities","upsert_entity_field","list_entity_fields"])",
         "视觉阶段/分层 prompt 字段"},
        {"field_builder", "添加字段 Agent", "field,schema,extend",
         R"(["list_field_defs","upsert_field_def","upsert_entity_field","list_entity_fields","list_entities","upsert_entity"])",
         R"({"new_fields":[{"key":"…","scope":"…"}]})"},
        {"agent_meta", "Agent 更新 Agent", "meta,agent_update",
         R"(["list_agents","get_agent","upsert_agent","route_task","list_field_defs"])",
         R"({"updated_agent":"…","version":N})"},
        {"router", "调度 Agent", "route,dispatch",
         R"(["route_task","list_agents"])",
         R"({"agent_id":"…"})"},
    };

    std::vector<AgentDefRow> out;
    out.reserve(std::size(kSpecs));
    for (const auto& s : kSpecs) {
        AgentDefRow r;
        r.agent_id = std::string{s.id};
        r.name = std::string{s.name};
        r.role_tags = std::string{s.tags};
        r.system_prompt = DefaultPromptFor(s.id);
        r.tools_json = std::string{s.tools};
        r.output_hint = std::string{s.outHint};
        r.enabled = 1;
        r.is_builtin = 1;
        r.version = 1;
        out.push_back(std::move(r));
    }
    return out;
}

std::string AgentKit::DefaultPromptFor(std::string_view agentId) {
    if (agentId == "novel_writer") {
        return
            "你是写小说 Agent。负责章节正文与章级规划。\n"
            "写作前先 list_entity_fields / list_field_defs 理解当前设定（含动态字段与本章宇宙）。\n"
            "不要假设固定世界观类型；需要新信息时让字段工具补全后再写。\n"
            "POV 未知情的 true 层字段不得写穿。只输出正文或约定 JSON，不要闲聊。";
    }
    if (agentId == "character") {
        return
            "你是创建人物 Agent。创建/更新角色时：\n"
            "1) upsert_entity(kind=person) 建主体；\n"
            "2) 用动态字段表达多面性：identity_layers、public_mask、true_faction 等，"
            "必要时 upsert_field_def 新增本书专属维度；\n"
            "3) 表面关系与真实关系可用不同 rel_type + 字段 layer 区分；\n"
            "4) 输出实体 id 与字段清单，供其它 Agent / MCP 读取。";
    }
    if (agentId == "item") {
        return
            "你是创建物品 Agent。物品 kind 可用 item/prop/treasure 或本书自定义 kind。\n"
            "体系相关属性（材料、品级、绑定规则）一律动态字段，不写死枚举。"
            "输出实体 id 与 fields。";
    }
    if (agentId == "world") {
        return
            "你是世界观 Agent。可维护多个 universe 实体与规则。\n"
            "用世界级字段 active_universe / universe_layers / world_rules_active；\n"
            "分章不同世界观时写 entity_id=0 的 universe_of_chapter + chapter_scope。\n"
            "力量/经济/宗教体系用动态字段或实体，不强行塞进固定列。";
    }
    if (agentId == "field_builder") {
        return
            "你是添加字段 Agent。当现有字段不够用时，定义新维度：\n"
            "upsert_field_def(scope/entity_kind/field_key/title/value_type/description) →\n"
            "再 upsert_entity_field 写入样例或真实值。\n"
            "字段键用英文 snake_case；说明写中文。避免与已有 field_key 冲突（先 list_field_defs）。";
    }
    if (agentId == "agent_meta") {
        return
            "你是更新 Agent 的 Agent（元 Agent）。\n"
            "可以 list_agents / get_agent / upsert_agent 修改其它 Agent 的 system_prompt、"
            "tools_json、enabled，或注册新的专用 Agent。\n"
            "修改应服务当前小说阶段（例如某书需要「考古体系专家」就新建 agent）。\n"
            "内置 Agent 可改 prompt；不要删除正在路由到的 Agent。";
    }
    if (agentId == "router") {
        return "你是调度 Agent。用 route_task 根据任务文本选择执行 Agent，输出 agent_id JSON。";
    }
    if (agentId == "review") {
        return
            "你是审校 Agent。对照实体 + 动态字段检查一致性（身份层是否被写穿、"
            "本章宇宙是否正确、力量体系是否违背规则字段）。输出 issues JSON。";
    }
    if (agentId == "extract") {
        return
            "你是抽取 Agent。从正文抽取新实体与新字段建议，调用 upsert_entity / "
            "upsert_field_def / upsert_entity_field 落库（PROPOSED 语义），输出抽取清单 JSON。";
    }
    if (agentId == "place") {
        return "你是地点 Agent。创建地点实体，地理/气候/特殊规则用动态字段。";
    }
    if (agentId == "faction") {
        return
            "你是势力 Agent。创建组织实体；成员用 link_relation 或动态字段 members；"
            "敌盟关系 rel_type 可扩展。";
    }
    if (agentId == "event") {
        return "你是事件 Agent。创建事件实体，因果与时间用动态字段或关系边描述。";
    }
    if (agentId == "mystery") {
        return
            "你是伏笔秘密 Agent。秘密用 layer=true/mask 与 chapter_scope 控制揭示节奏；"
            "知情矩阵可用动态字段 character_knows_*。";
    }
    if (agentId == "memory") {
        return "你是记忆检索 Agent。按任务检索相关实体与动态字段，输出摘要供写作使用。";
    }
    if (agentId == "visual") {
        return
            "你是视觉 Agent。外貌变化/阶段/伪装外观写入动态字段或 visual 相关字段，"
            "不与叙事真相层（true）混淆。";
    }
    return "你是小说专项 Agent。按角色标签完成任务，优先读写动态字段而不是假设固定结构。";
}

bool AgentKit::RunSelfCheck() { return RunMultiAgentSelfCheck(); }

void RegisterAgentSharedTools(ToolRegistry& reg, db::sqlite::Database& db, bool allowWrite) {
    AgentKit kit(db, allowWrite);
    (void)kit.EnsureSchemaAndSeed();
    // 默认注册 novel_writer 的工具集作为「共享可写」面
    kit.RegisterToolsFor("novel_writer", reg);
    // 字段定义写入对共享面开放
    auto fields = std::make_shared<NovelFields>(db);
    auto g = std::make_shared<novelcore::NovelGraph>(db);
    if (allowWrite) {
        reg.Register(std::make_unique<UpsertFieldDefTool>(fields, g));
    }
}

bool RunMultiAgentSelfCheck() {
    auto fail = [](std::string_view msg) {
        CheckFail(msg);
        return false;
    };

    db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) {
        return fail("打开内存库失败");
    }
    // 最小 schema（图谱 + 字段 + agent）
    if (auto r = mem.Exec(R"SQL(
CREATE TABLE IF NOT EXISTS entities(id INTEGER PRIMARY KEY AUTOINCREMENT,kind TEXT,name TEXT,summary TEXT,status TEXT,meta_json TEXT,created_chapter INTEGER,updated INTEGER);
CREATE TABLE IF NOT EXISTS relations(id INTEGER PRIMARY KEY AUTOINCREMENT,from_id INTEGER,to_id INTEGER,rel_type TEXT,strength INTEGER,from_chapter INTEGER,to_chapter INTEGER,reason TEXT,status TEXT);
CREATE TABLE IF NOT EXISTS chapters(id INTEGER PRIMARY KEY AUTOINCREMENT,volume_id INTEGER,ord INTEGER,title TEXT,status TEXT,summary TEXT,body TEXT,pov_entity_id INTEGER,words INTEGER,updated INTEGER);
CREATE TABLE IF NOT EXISTS canon_logs(id INTEGER PRIMARY KEY AUTOINCREMENT,target_kind TEXT,target_id INTEGER,status TEXT,note TEXT,created INTEGER);
CREATE TABLE IF NOT EXISTS audit_logs(id INTEGER PRIMARY KEY AUTOINCREMENT,actor TEXT,action TEXT,target_kind TEXT,target_id INTEGER,detail TEXT,created INTEGER);
)SQL"); !r) {
        return fail(fmt::format("建最小表失败 {}", r.error().message));
    }

    AgentKit kit(mem, true);
    if (auto r = kit.EnsureSchemaAndSeed(); !r) {
        return fail(fmt::format("seed 失败 {}", r.error().message));
    }

    auto agents = kit.ListAgentDefs(false);
    if (!agents || agents->size() < 10) {
        return fail(fmt::format("内置 Agent 数量不足 {}", agents ? agents->size() : 0));
    }

    // 内置 Agent 清单检查
    const char* required[] = {"novel_writer", "character",  "item",         "field_builder",
                              "agent_meta",   "world",      "router",       "review",
                              "extract",      "faction"};
    for (const char* id : required) {
        bool found = false;
        for (const auto& a : *agents) {
            if (a.agent_id == id) {
                found = true;
                break;
            }
        }
        if (!found) {
            return fail(fmt::format("缺少 Agent {}", id));
        }
    }

    // 路由
    if (auto r = kit.Route("帮我创建一个有卧底身份的反派人物"); !r || *r != "character") {
        return fail(fmt::format("人物路由失败 {}", r ? *r : r.error().message));
    }
    if (auto r = kit.Route("添加一个字段表示血脉封印层数"); !r || *r != "field_builder") {
        return fail(fmt::format("字段路由失败 {}", r ? *r : r.error().message));
    }
    if (auto r = kit.Route("更新 writer agent 的提示词"); !r || *r != "agent_meta") {
        return fail(fmt::format("meta 路由失败 got={}", r ? *r : r.error().message));
    }

    // mock：character Agent 写实体 + 双面字段
    novelcore::NovelGraph g(mem);
    novelcore::NovelFields fields(mem);

    auto create = [&](std::string_view instructions, std::string_view /*inputJson*/,
                      std::string_view /*tools*/) -> std::expected<std::string, std::string> {
        if (instructions.find("创建人物") != std::string_view::npos) {
            return std::string{R"({"output_text":"ok","character":"林默"})"};
        }
        return std::string{R"({"output_text":"ok"})"};
    };

    novelcore::EntityRow person;
    person.kind = "person";
    person.name = "林默";
    person.summary = "表面军师";
    auto pid = g.UpsertEntity(person);
    if (!pid) {
        return fail("建人物失败");
    }
    EntityFieldRow mask;
    mask.entity_id = *pid;
    mask.field_key = "public_mask";
    mask.value_text = "反派军师";
    mask.layer = "mask";
    mask.created_by = "character";
    if (!fields.UpsertEntityField(mask)) {
        return fail("写 mask 失败");
    }
    EntityFieldRow truth;
    truth.entity_id = *pid;
    truth.field_key = "true_faction";
    truth.value_text = "北境卧底网";
    truth.layer = "true";
    truth.created_by = "character";
    if (!fields.UpsertEntityField(truth)) {
        return fail("写 true 失败");
    }

    // field_builder 新增维度
    FieldDefRow seal;
    seal.scope = "entity";
    seal.entity_kind = "person";
    seal.field_key = "bloodline_seal";
    seal.title = "血脉封印";
    seal.value_type = "text";
    seal.created_by = "field_builder";
    if (!fields.UpsertFieldDef(seal)) {
        return fail("field_builder 定义失败");
    }

    // agent_meta 更新 prompt
    AgentDefRow upd;
    upd.agent_id = "novel_writer";
    upd.system_prompt = "你是写小说 Agent。已由元 Agent 增强：必须先读 identity_layers。";
    upd.tools_json = R"(["get_entity","list_entity_fields","list_field_defs"])";
    if (auto old = kit.GetAgentDef("novel_writer"); old) {
        upd.name = old->name;
        upd.role_tags = old->role_tags;
        upd.output_hint = old->output_hint;
        upd.enabled = old->enabled;
        upd.is_builtin = old->is_builtin;
        upd.version = old->version + 1;
    } else {
        return fail("GetAgentDef novel_writer 失败");
    }
    if (!kit.UpsertAgentDef(upd)) {
        return fail("agent_meta 更新失败");
    }
    auto after = kit.GetAgentDef("novel_writer");
    if (!after || after->system_prompt.find("元 Agent") == std::string::npos) {
        return fail("prompt 未更新");
    }

    // BuildSystemPrompt 含动态字段说明
    auto prompt = kit.BuildSystemPrompt("character", 0);
    if (!prompt || prompt->find("不写死") == std::string::npos ||
        prompt->find("bloodline_seal") == std::string::npos) {
        return fail(fmt::format("BuildSystemPrompt 未包含动态字段 prompt={}",
                                prompt ? prompt->substr(0, 200) : prompt.error().message));
    }

    // 工具注册 + Execute
    ToolRegistry reg;
    kit.RegisterToolsFor("field_builder", reg);
    if (reg.size() < 5) {
        return fail(fmt::format("field_builder 工具过少 {}", reg.size()));
    }
    {
        const std::string argsJson = fmt::format(
            R"({{"entity_id":{},"field_key":"bloodline_seal","value_text":"第三层","layer":"true","created_by":"field_builder"}})",
            *pid);
        yyjson_doc* args = yyjson_read(argsJson.data(), argsJson.size(), 0);
        if (!args) {
            return fail("构造 args 失败");
        }
        auto r = reg.Execute("upsert_entity_field", yyjson_doc_get_root(args));
        if (!r) {
            yyjson_doc_free(args);
            return fail(fmt::format("upsert_entity_field 工具失败 {}", r.error().message));
        }
        yyjson_doc_free(*r);
        yyjson_doc_free(args);
    }
    {
        const std::string argsJson = R"({"scope":"entity","entity_kind":"person"})";
        yyjson_doc* args = yyjson_read(argsJson.data(), argsJson.size(), 0);
        if (!args) {
            return fail("list_field_defs args 失败");
        }
        auto r = reg.Execute("list_field_defs", yyjson_doc_get_root(args));
        if (!r) {
            yyjson_doc_free(args);
            return fail(fmt::format("list_field_defs 失败 {}", r.error().message));
        }
        if (!*r) {
            yyjson_doc_free(args);
            return fail("list_field_defs 返回空 doc");
        }
        size_t len = 0;
        char* t = yyjson_val_write(yyjson_doc_get_root(*r), 0, &len);
        const std::string body = t ? std::string{t, len} : "";
        if (t) std::free(t);
        yyjson_doc_free(*r);
        yyjson_doc_free(args);
        if (body.empty()) {
            // 直接读库兜底诊断
            auto defs = fields.ListFieldDefs("entity", "person");
            const auto n = defs ? defs->size() : 0;
            bool hasSeal = false;
            if (defs) {
                for (const auto& d : *defs) {
                    if (d.field_key == "bloodline_seal") hasSeal = true;
                }
            }
            return fail(fmt::format(
                "list_field_defs body 空 defs_n={} hasSeal={} raw_ok={}", n, hasSeal,
                body.empty() ? "empty" : "has"));
        }
        if (body.find("bloodline_seal") == std::string::npos) {
            return fail(fmt::format("list_field_defs 未返回新字段 body={}", body));
        }
    }

    // meta 工具列表
    ToolRegistry metaReg;
    kit.RegisterToolsFor("agent_meta", metaReg);
    {
        const std::string argsJson = R"({"only_enabled":true})";
        yyjson_doc* args = yyjson_read(argsJson.data(), argsJson.size(), 0);
        if (!args) {
            return fail("list_agents args 失败");
        }
        auto r = metaReg.Execute("list_agents", yyjson_doc_get_root(args));
        if (!r) {
            yyjson_doc_free(args);
            return fail(fmt::format("list_agents 失败 {}", r.error().message));
        }
        yyjson_doc_free(*r);
        yyjson_doc_free(args);
    }

    // Run mock（router）
    AgentRunRequest req;
    req.agent_id = "router";
    req.user_text = "route: 写一章正文";
    auto run = kit.Run(req, create, nullptr);
    if (!run || run->output_text.find("ok") == std::string::npos) {
        return fail(fmt::format("Run mock 失败 {}", run ? run->output_text : run.error().message));
    }

    log::Info(
        "MultiAgent 自检通过（{} 个 Agent / 字段扩展 / 路由 / 元 Agent 更新 / 工具读写）",
        agents->size());
    if (const char* path = std::getenv("SHINE_NOVEL_CHECK_OUT"); path && *path) {
        if (FILE* f = std::fopen(path, "ab")) {
            const char* line = "multiagent:ok\n";
            std::fwrite(line, 1, std::strlen(line), f);
            std::fclose(f);
        }
    }
    return true;
}

} // namespace shine::agent
