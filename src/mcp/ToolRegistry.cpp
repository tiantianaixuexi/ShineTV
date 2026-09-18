#include "mcp/ToolRegistry.h"

#include "core/Log.h"

#include <cstdlib>
#include <fmt/format.h>
#include <string>
#include <string_view>
#include <utility>

#include <yyjson.h>

namespace shine::mcp {
namespace {

[[nodiscard]] bool LooksLikeJsonObject(std::string_view text) {
    if (text.empty()) return false;
    yyjson_doc* doc = yyjson_read(text.data(), text.size(), 0);
    if (!doc) return false;
    yyjson_val* root = yyjson_doc_get_root(doc);
    const bool ok = root && yyjson_is_obj(root);
    yyjson_doc_free(doc);
    return ok;
}

void AddToolToArr(yyjson_mut_doc* doc, yyjson_mut_val* arr, const Tool& t) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_strncpy(doc, obj, "name", t.name.data(), t.name.size());
    if (!t.title.empty()) {
        yyjson_mut_obj_add_strncpy(doc, obj, "title", t.title.data(), t.title.size());
    }
    if (!t.description.empty()) {
        yyjson_mut_obj_add_strncpy(doc, obj, "description", t.description.data(),
                                   t.description.size());
    }

    yyjson_val* sroot = nullptr;
    yyjson_doc* sdoc = nullptr;
    if (LooksLikeJsonObject(t.schemaJson)) {
        sdoc = yyjson_read(t.schemaJson.data(), t.schemaJson.size(), 0);
        sroot = sdoc ? yyjson_doc_get_root(sdoc) : nullptr;
    }
    if (sroot && yyjson_is_obj(sroot)) {
        yyjson_mut_val* scopy = yyjson_val_mut_copy(doc, sroot);
        if (scopy) {
            yyjson_mut_obj_add_val(doc, obj, "inputSchema", scopy);
        } else {
            yyjson_mut_val* fb = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_strcpy(doc, fb, "type", "object");
            yyjson_mut_obj_add_val(doc, obj, "inputSchema", fb);
            log::Warn("mcp tools/list：工具 {} 的 inputSchema 拷贝失败，已退化", t.name);
        }
    } else {
        yyjson_mut_val* fb = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strcpy(doc, fb, "type", "object");
        yyjson_mut_obj_add_val(doc, obj, "inputSchema", fb);
        if (!t.schemaJson.empty()) {
            log::Warn("mcp tools/list：工具 {} 的 schemaJson 非 JSON 对象，已退化为 type=object",
                      t.name);
        }
    }
    if (sdoc) yyjson_doc_free(sdoc);
    yyjson_mut_arr_add_val(arr, obj);
}

} // namespace

ToolRegistry& ToolRegistry::Instance() {
    static ToolRegistry inst;
    return inst;
}

void ToolRegistry::EnsureModule(ModuleInfo info) {
    if (info.id.empty()) {
        log::Warn("mcp EnsureModule：模块 id 为空，忽略");
        return;
    }
    for (auto& m : modules_) {
        if (m.id == info.id) {
            m.title = std::move(info.title);
            return;
        }
    }
    modules_.push_back(std::move(info));
}

bool ToolRegistry::UnregisterModule(std::string_view id) {
    if (id.empty()) return false;
    bool removed = false;
    std::erase_if(tools_, [&](const Tool& t) { return t.moduleId == id; });
    const auto before = modules_.size();
    std::erase_if(modules_, [&](const ModuleInfo& m) { return m.id == id; });
    removed = modules_.size() < before;
    return removed;
}

std::vector<ModuleInfo> ToolRegistry::Modules() const { return modules_; }

std::vector<std::string> ToolRegistry::ToolNames(std::string_view moduleId) const {
    std::vector<std::string> out;
    for (const auto& t : tools_) {
        if (moduleId.empty() || t.moduleId == moduleId) out.push_back(t.name);
    }
    return out;
}

void ToolRegistry::Register(Tool tool) {
    if (tool.name.empty()) {
        log::Warn("mcp Register：工具 name 为空，拒绝注册");
        return;
    }
    if (!tool.handler) {
        log::Warn("mcp Register：工具 {} 缺少 handler，拒绝注册", tool.name);
        return;
    }
    if (tool.schemaJson.empty() || !LooksLikeJsonObject(tool.schemaJson)) {
        if (!tool.schemaJson.empty()) {
            log::Warn("mcp Register：工具 {} schemaJson 非对象，将退化为 {{\"type\":\"object\"}}",
                      tool.name);
        }
        tool.schemaJson = R"({"type":"object","properties":{},"required":[]})";
    }

    for (auto& t : tools_) {
        if (t.name == tool.name) {
            log::Info("mcp Register：同名覆盖 tool={} module={}（旧 module={}）", tool.name,
                      tool.moduleId, t.moduleId);
            t = std::move(tool);
            return;
        }
    }
    tools_.push_back(std::move(tool));
}

bool ToolRegistry::Unregister(std::string_view name) {
    if (name.empty()) return false;
    const auto before = tools_.size();
    std::erase_if(tools_, [&](const Tool& t) { return t.name == name; });
    return tools_.size() < before;
}

void ToolRegistry::Clear() noexcept {
    tools_.clear();
    modules_.clear();
}

const Tool* ToolRegistry::Find(std::string_view name) const {
    for (const auto& t : tools_) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

std::string ToolRegistry::BuildToolsListJson() const {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (!doc) return R"({"tools":[]})";
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_val* arr = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "tools", arr);
    for (const auto& t : tools_) {
        AddToolToArr(doc, arr, t);
    }
    size_t len = 0;
    char* s = yyjson_mut_val_write(root, 0, &len);
    std::string out = s ? std::string{s, len} : R"({"tools":[]})";
    if (s) std::free(s);
    yyjson_mut_doc_free(doc);
    return out;
}

CallOutcome ToolRegistry::Call(std::string_view name, yyjson_val* args) {
    const Tool* tool = Find(name);
    if (!tool) {
        return CallOutcome::Fail(CallStatus::NotFound, fmt::format("未知工具: {}", name));
    }
    if (args != nullptr && !yyjson_is_obj(args)) {
        return CallOutcome::Fail(CallStatus::BadArguments, "参数不是合法 JSON 对象");
    }
    // 拷贝 handler 再调用：避免 handler 重入 Register 导致 vector 重分配悬垂
    ToolHandler handler = tool->handler;
    if (!handler) {
        return CallOutcome::Fail(CallStatus::InternalError,
                                 fmt::format("工具 {} 未绑定 handler", tool->name));
    }
    try {
        return handler(args);
    } catch (const std::exception& ex) {
        log::Error("mcp Call 工具 {} 异常：{}", tool->name, ex.what());
        return CallOutcome::Fail(CallStatus::InternalError,
                                 fmt::format("工具执行异常: {}", ex.what()));
    } catch (...) {
        log::Error("mcp Call 工具 {} 未知异常", tool->name);
        return CallOutcome::Fail(CallStatus::InternalError, "工具执行异常: unknown");
    }
}

} // namespace shine::mcp
