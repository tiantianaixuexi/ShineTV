#include "mcp/Schema.h"

#include "core/Log.h"

#include <cstdlib>
#include <span>
#include <string>
#include <string_view>

namespace shine::mcp::schema {
namespace {

void AddStrn(yyjson_mut_doc* doc, yyjson_mut_val* obj, std::string_view key,
             std::string_view value) {
    if (!doc || !obj || key.empty()) return;
    // yyjson key 必须 NUL 结尾；value 用 strncpy 拷贝进 doc
    const std::string k{key};
    if (value.empty()) {
        yyjson_mut_obj_add_strcpy(doc, obj, k.c_str(), "");
        return;
    }
    yyjson_mut_obj_add_strncpy(doc, obj, k.c_str(), value.data(), value.size());
}

void EnsureContainer(yyjson_mut_doc* doc, yyjson_mut_val* schema) {
    if (!doc || !schema || !yyjson_mut_is_obj(schema)) return;
    if (!yyjson_mut_obj_get(schema, "properties")) {
        yyjson_mut_val* props = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_val(doc, schema, "properties", props);
    }
    if (!yyjson_mut_obj_get(schema, "required")) {
        yyjson_mut_val* req = yyjson_mut_arr(doc);
        yyjson_mut_obj_add_val(doc, schema, "required", req);
    }
}

void AttachProp(yyjson_mut_doc* doc, yyjson_mut_val* schema, std::string_view name,
                yyjson_mut_val* propVal, bool required) {
    if (!doc || !schema || !propVal) return;
    EnsureContainer(doc, schema);
    const std::string key{name};
    yyjson_mut_val* props = yyjson_mut_obj_get(schema, "properties");
    if (props && yyjson_mut_is_obj(props)) {
        yyjson_mut_val* k = yyjson_mut_strncpy(doc, key.c_str(), key.size());
        yyjson_mut_obj_add(props, k, propVal);
    }
    if (required) {
        yyjson_mut_val* req = yyjson_mut_obj_get(schema, "required");
        if (req && yyjson_mut_is_arr(req)) {
            yyjson_mut_arr_add_strcpy(doc, req, key.c_str());
        }
    }
}

[[nodiscard]] yyjson_mut_val* MakeProp(yyjson_mut_doc* doc, std::string_view description,
                                       std::string_view type) {
    yyjson_mut_val* p = yyjson_mut_obj(doc);
    AddStrn(doc, p, "type", type);
    AddStrn(doc, p, "description", description);
    return p;
}

} // namespace

yyjson_mut_doc* NewDoc() { return yyjson_mut_doc_new(nullptr); }

yyjson_mut_val* Object(yyjson_mut_doc* doc, std::span<const std::string_view> required) {
    if (!doc) return nullptr;
    yyjson_mut_val* schema = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_strcpy(doc, schema, "type", "object");
    yyjson_mut_val* props = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, schema, "properties", props);
    yyjson_mut_val* req = yyjson_mut_arr(doc);
    for (std::string_view r : required) {
        const std::string rs{r};
        yyjson_mut_arr_add_strcpy(doc, req, rs.c_str());
    }
    yyjson_mut_obj_add_val(doc, schema, "required", req);
    return schema;
}

void AddString(yyjson_mut_doc* doc, yyjson_mut_val* schema, std::string_view name,
               std::string_view description, bool required, std::string_view defaultValue) {
    if (!doc || !schema) return;
    yyjson_mut_val* p = MakeProp(doc, description, "string");
    if (!defaultValue.empty()) AddStrn(doc, p, "default", defaultValue);
    AttachProp(doc, schema, name, p, required);
}

void AddStringArray(yyjson_mut_doc* doc, yyjson_mut_val* schema, std::string_view name,
                    std::string_view description, bool required) {
    if (!doc || !schema) return;
    yyjson_mut_val* p = MakeProp(doc, description, "array");
    yyjson_mut_val* items = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_strcpy(doc, items, "type", "string");
    yyjson_mut_obj_add_val(doc, p, "items", items);
    AttachProp(doc, schema, name, p, required);
}

void AddNumber(yyjson_mut_doc* doc, yyjson_mut_val* schema, std::string_view name,
               std::string_view description, bool required, double defaultValue) {
    if (!doc || !schema) return;
    yyjson_mut_val* p = MakeProp(doc, description, "number");
    yyjson_mut_obj_add_real(doc, p, "default", defaultValue);
    AttachProp(doc, schema, name, p, required);
}

void AddInteger(yyjson_mut_doc* doc, yyjson_mut_val* schema, std::string_view name,
                std::string_view description, bool required, int defaultValue) {
    if (!doc || !schema) return;
    yyjson_mut_val* p = MakeProp(doc, description, "integer");
    yyjson_mut_obj_add_int(doc, p, "default", defaultValue);
    AttachProp(doc, schema, name, p, required);
}

void AddBoolean(yyjson_mut_doc* doc, yyjson_mut_val* schema, std::string_view name,
                std::string_view description, bool required, bool defaultValue) {
    if (!doc || !schema) return;
    yyjson_mut_val* p = MakeProp(doc, description, "boolean");
    yyjson_mut_obj_add_bool(doc, p, "default", defaultValue);
    AttachProp(doc, schema, name, p, required);
}

std::string ToJsonString(yyjson_mut_doc* doc, yyjson_mut_val* schema) {
    if (!doc || !schema) return R"({"type":"object"})";
    size_t len = 0;
    char* s = yyjson_mut_val_write(schema, 0, &len);
    if (!s) {
        log::Warn("mcp schema 序列化失败，退化为 {{\"type\":\"object\"}}");
        return R"({"type":"object"})";
    }
    std::string out{s, len};
    std::free(s);
    return out;
}

} // namespace shine::mcp::schema
