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
    // ⚠️ 必须显式把 key/value 拷进 doc 再接管。
    //    yyjson 的 yyjson_mut_obj_add_str*(doc, obj, key, val) 系列对 **key 只引用不拷贝**
    //    （yyjson.c: `key->uni.str = _key;`），一旦传入局部 std::string 的 c_str()，
    //    函数返回后 key 就悬空 → 序列化时读到垃圾字节 → 报 invalid utf-8 encoding，
    //    整个 inputSchema 静默退化成 {"type":"object"}。
    const char* kp = key.data() != nullptr ? key.data() : "";
    const char* vp = value.data() != nullptr ? value.data() : "";
    yyjson_mut_val* k = yyjson_mut_strncpy(doc, kp, key.size());
    yyjson_mut_val* v = yyjson_mut_strncpy(doc, vp, value.size());
    if (k == nullptr || v == nullptr) return;
    (void)yyjson_mut_obj_add(obj, k, v);
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
    // 关键：yyjson 的写出一律以 doc->root 为准，schema 必须先挂成 root。
    // 之前只调 yyjson_mut_val_write(schema, ...) 而没 set_root —— schema 是游离 val，
    // 写出会失败并静默退化成 {"type":"object"}，导致所有工具的 inputSchema 丢 properties。
    yyjson_mut_doc_set_root(doc, schema);
    size_t len = 0;
    yyjson_write_err werr{};
    char* s = yyjson_mut_write_opts(doc, 0, nullptr, &len, &werr);
    if (s == nullptr) {
        // 带 code/msg：上一次 G22 就是靠它把「结构问题」澄清成「invalid utf-8 encoding」
        log::Warn("mcp schema 序列化失败（code={} msg={}），退化为 {{\"type\":\"object\"}}",
                  static_cast<int>(werr.code), werr.msg != nullptr ? werr.msg : "unknown");
        return R"({"type":"object"})";
    }
    std::string out{s, len};
    std::free(s);
    return out;
}

} // namespace shine::mcp::schema
