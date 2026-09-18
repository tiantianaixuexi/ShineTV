#pragma once
// shine::mcp::schema —— JSON Schema 小助手（yyjson mut），对齐 UE ShineMCPSchema
#include <span>
#include <string>
#include <string_view>

#include <yyjson.h>

namespace shine::mcp::schema {

// 新建 mut doc（调用方 yyjson_mut_doc_free）
[[nodiscard]] yyjson_mut_doc* NewDoc();

// {"type":"object","properties":{},"required":[...]}
[[nodiscard]] yyjson_mut_val* Object(yyjson_mut_doc* doc,
                                     std::span<const std::string_view> required = {});

void AddString(yyjson_mut_doc* doc, yyjson_mut_val* schema, std::string_view name,
               std::string_view description, bool required = false,
               std::string_view defaultValue = {});
void AddStringArray(yyjson_mut_doc* doc, yyjson_mut_val* schema, std::string_view name,
                    std::string_view description, bool required = false);
void AddNumber(yyjson_mut_doc* doc, yyjson_mut_val* schema, std::string_view name,
               std::string_view description, bool required = false, double defaultValue = 0.0);
void AddInteger(yyjson_mut_doc* doc, yyjson_mut_val* schema, std::string_view name,
                std::string_view description, bool required = false, int defaultValue = 0);
void AddBoolean(yyjson_mut_doc* doc, yyjson_mut_val* schema, std::string_view name,
                std::string_view description, bool required = false, bool defaultValue = false);

// 把 schema 对象序列化为 owned 字符串（给 Tool::schemaJson）
[[nodiscard]] std::string ToJsonString(yyjson_mut_doc* doc, yyjson_mut_val* schema);

} // namespace shine::mcp::schema
