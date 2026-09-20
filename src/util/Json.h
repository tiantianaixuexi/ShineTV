#pragma once
// shine::util::json —— yyjson 读取助手（只读、宽容：类型不符或键缺失一律返回默认值，不抛异常）
//
// 写入侧仍然在各模块自己拼（yyjson_mut_*），这里只收口"读 JSON"这种重复最多的样板。
// 前端/MCP 消费动态字段时：数组/对象必须解析后再展示，禁止把 JSON 原文当普通字符串硬拼。
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <yyjson.h>

namespace shine::util::json {

// —— JSON 字符串序列化（S42：**唯一来源**）——
// 起因：项目里曾有 **7 处**各自实现"把文本变成 JSON 字符串"（4 处手写 + 2 处 yyjson + 1 处内联），
// 手写的那几处**都漏了 `<0x20` 的控制字符**（`AgentKit` 连 `\t` 都漏）—— 只要漏一个，生成的
// JSON 就**非法**。真实跑撞过：工具循环第一步 `yyjson_read` 直接失败（报"input 不是合法 JSON"）。
// 结论：**别再手写**。交给 yyjson，这个类别的 bug 就永远不会有（正确性优先于那点性能）。
//
// ⚠️ 只读侧（`yyjson_val`）用 `yyjson_val_write`；这里补的是**写入侧**（从 `std::string_view` 出发）。
[[nodiscard]] inline std::string JsonQuote(std::string_view text) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (doc == nullptr) {
        return "\"\"";
    }
    yyjson_mut_val* v = yyjson_mut_strncpy(doc, text.data(), text.size());
    if (v == nullptr) { // 非法 UTF-8 → 退化为空串（与 `NovelFields` 的既有策略一致）
        yyjson_mut_doc_free(doc);
        return "\"\"";
    }
    yyjson_mut_doc_set_root(doc, v); // ⚠️ 必须先挂 root，否则 `yyjson_mut_write` 失败
    std::size_t len = 0;
    char* s = yyjson_mut_val_write(v, 0, &len); // flg=0：中文**原样输出**（不转成 \uXXXX）
    std::string out = "\"\"";
    if (s != nullptr) {
        out.assign(s, len);
        std::free(s);
    }
    yyjson_mut_doc_free(doc);
    return out;
}

// **不含**两侧引号的转义（= `JsonQuote` 去掉首尾引号）—— 兼容"调用方自己写引号"的历史用法。
[[nodiscard]] inline std::string JsonEscape(std::string_view text) {
    std::string q = JsonQuote(text);
    if (q.size() >= 2) {
        q.erase(q.size() - 1);
        q.erase(0, 1);
    }
    return q;
}

// —— 宽容提取（S38）：LLM 的输出**常带 markdown 围栏或前后说明文字**（"好的，以下是 JSON："…）。
// 直接 `yyjson_read` 全文会**当场判非法** —— 真实跑就撞上了（MiniMax-M3 在 V6 TIMELINE 上的输出
// 不是纯 JSON，整条阶段链当场断在 V6）。本函数把"可能的 JSON 正文"抠出来：
//   ① 去 markdown 围栏（```json … ``` / ``` … ```）② 取第一个 `{` 到最后一个 `}`。
// ⚠️ **不做 JSON 语义修复**（不补尾逗号、不修中文引号）—— 那是模型的问题，替它修反而掩盖问题。
// ⚠️ 抠不出来时**原样返回**（让 yyjson 去报错），不要假装成功。
[[nodiscard]] inline std::string ExtractJsonObject(std::string_view raw) {
    std::string s{raw};
    if (const auto fence = s.find("```"); fence != std::string::npos) {
        const std::size_t begin = s.find('\n', fence);
        if (begin != std::string::npos) {
            const std::size_t close = s.find("```", begin);
            if (close != std::string::npos) {
                s = s.substr(begin + 1, close - begin - 1);
            }
        }
    }
    const std::size_t b = s.find('{');
    const std::size_t e = s.rfind('}');
    if (b == std::string::npos || e == std::string::npos || e <= b) {
        return s;
    }
    return s.substr(b, e - b + 1);
}

// 键对应的值节点；不存在返回 nullptr
[[nodiscard]] inline yyjson_val* Get(yyjson_val* obj, std::string_view key) noexcept {
    if (!obj || !yyjson_is_obj(obj)) {
        return nullptr;
    }
    return yyjson_obj_getn(obj, key.data(), key.size());
}

// 字符串值（视图，指向 doc 拥有的内存 —— doc 存活期间有效）
[[nodiscard]] inline std::string_view GetStr(yyjson_val* obj, std::string_view key) noexcept {
    yyjson_val* v = Get(obj, key);
    return (v && yyjson_is_str(v)) ? std::string_view{yyjson_get_str(v), yyjson_get_len(v)} : std::string_view{};
}

// 字符串值（拷贝一份；需要跨 doc 生命周期持有就用这个）
[[nodiscard]] inline std::string GetStrCopy(yyjson_val* obj, std::string_view key) {
    return std::string{GetStr(obj, key)};
}

[[nodiscard]] inline std::int64_t GetI64(yyjson_val* obj, std::string_view key, std::int64_t def = 0) noexcept {
    yyjson_val* v = Get(obj, key);
    return (v && yyjson_is_num(v)) ? yyjson_get_sint(v) : def;
}

[[nodiscard]] inline int GetInt(yyjson_val* obj, std::string_view key, int def = 0) noexcept {
    return static_cast<int>(GetI64(obj, key, def));
}

[[nodiscard]] inline double GetF64(yyjson_val* obj, std::string_view key, double def = 0.0) noexcept {
    yyjson_val* v = Get(obj, key);
    return (v && yyjson_is_num(v)) ? yyjson_get_num(v) : def;
}

[[nodiscard]] inline bool GetBool(yyjson_val* obj, std::string_view key, bool def = false) noexcept {
    yyjson_val* v = Get(obj, key);
    return (v && yyjson_is_bool(v)) ? yyjson_get_bool(v) : def;
}

[[nodiscard]] inline yyjson_val* GetObj(yyjson_val* obj, std::string_view key) noexcept {
    yyjson_val* v = Get(obj, key);
    return (v && yyjson_is_obj(v)) ? v : nullptr;
}

[[nodiscard]] inline yyjson_val* GetArr(yyjson_val* obj, std::string_view key) noexcept {
    yyjson_val* v = Get(obj, key);
    return (v && yyjson_is_arr(v)) ? v : nullptr;
}

[[nodiscard]] inline bool HasKey(yyjson_val* obj, std::string_view key) noexcept {
    return Get(obj, key) != nullptr;
}

// ── 数组 / 动态字段解析（前端与 Agent 读回共用）────────────────

enum class ValueKind { Null, Bool, Number, String, Array, Object, Invalid };

struct OwnedDoc {
    yyjson_doc* doc = nullptr;
    OwnedDoc() = default;
    explicit OwnedDoc(yyjson_doc* d) noexcept : doc(d) {}
    ~OwnedDoc() {
        if (doc) yyjson_doc_free(doc);
    }
    OwnedDoc(const OwnedDoc&) = delete;
    OwnedDoc& operator=(const OwnedDoc&) = delete;
    OwnedDoc(OwnedDoc&& o) noexcept : doc(o.doc) { o.doc = nullptr; }
    OwnedDoc& operator=(OwnedDoc&& o) noexcept {
        if (this != &o) {
            if (doc) yyjson_doc_free(doc);
            doc = o.doc;
            o.doc = nullptr;
        }
        return *this;
    }
    [[nodiscard]] yyjson_val* root() const noexcept {
        return doc ? yyjson_doc_get_root(doc) : nullptr;
    }
    [[nodiscard]] explicit operator bool() const noexcept { return doc != nullptr; }
};

[[nodiscard]] inline OwnedDoc ParseDoc(std::string_view json) {
    if (json.empty()) {
        return OwnedDoc{};
    }
    return OwnedDoc{yyjson_read(json.data(), json.size(), 0)};
}

[[nodiscard]] inline ValueKind Classify(std::string_view json) {
    if (json.empty()) return ValueKind::Null;
    auto d = ParseDoc(json);
    if (!d) return ValueKind::Invalid;
    yyjson_val* r = d.root();
    if (yyjson_is_null(r)) return ValueKind::Null;
    if (yyjson_is_bool(r)) return ValueKind::Bool;
    if (yyjson_is_num(r)) return ValueKind::Number;
    if (yyjson_is_str(r)) return ValueKind::String;
    if (yyjson_is_arr(r)) return ValueKind::Array;
    if (yyjson_is_obj(r)) return ValueKind::Object;
    return ValueKind::Invalid;
}

[[nodiscard]] inline std::string_view KindLabel(ValueKind k) noexcept {
    switch (k) {
    case ValueKind::Null: return "null";
    case ValueKind::Bool: return "bool";
    case ValueKind::Number: return "number";
    case ValueKind::String: return "string";
    case ValueKind::Array: return "array";
    case ValueKind::Object: return "object";
    case ValueKind::Invalid: return "invalid";
    }
    return "?";
}

// JSON 数组 → 字符串列表。空串 / null / 非数组 / 解析失败 → 空 vector（不抛）。
// 元素不是字符串时，数字/bool 转文本，对象/数组压成紧凑 JSON。
[[nodiscard]] inline std::vector<std::string> ParseStringArray(std::string_view json) {
    std::vector<std::string> out;
    auto d = ParseDoc(json);
    if (!d) return out;
    yyjson_val* r = d.root();
    if (!yyjson_is_arr(r)) return out;
    size_t i = 0, n = 0;
    yyjson_val* item = nullptr;
    yyjson_arr_foreach(r, i, n, item) {
        if (!item || yyjson_is_null(item)) {
            out.emplace_back();
            continue;
        }
        if (yyjson_is_str(item)) {
            out.emplace_back(yyjson_get_str(item), yyjson_get_len(item));
            continue;
        }
        size_t len = 0;
        char* t = yyjson_val_write(item, 0, &len);
        if (t) {
            out.emplace_back(t, len);
            std::free(t);
        } else {
            out.emplace_back();
        }
    }
    return out;
}

// 逗号连接数组（前端一行展示用）
[[nodiscard]] inline std::string JoinArray(std::string_view json, std::string_view sep = ", ") {
    const auto items = ParseStringArray(json);
    std::string o;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) o += sep;
        o += items[i];
    }
    return o;
}

// 有界预览：数组/object 显示 n 项 + …；string 截断
[[nodiscard]] inline std::string ArrayBrief(std::string_view json, std::size_t maxItems = 6,
                                            std::size_t maxChars = 120) {
    const auto kind = Classify(json);
    if (kind == ValueKind::Invalid) {
        std::string s = "(无效 JSON) ";
        s += std::string{json}.substr(0, 40);
        return s;
    }
    if (kind == ValueKind::Array) {
        const auto items = ParseStringArray(json);
        if (items.empty()) return "[]";
        std::string o = "[";
        const std::size_t show = items.size() < maxItems ? items.size() : maxItems;
        for (std::size_t i = 0; i < show; ++i) {
            if (i) o += ", ";
            o += items[i];
        }
        if (items.size() > show) {
            o += ", …+";
            o += std::to_string(items.size() - show);
        }
        o += "]";
        if (o.size() > maxChars) {
            o.resize(maxChars);
            o += "…";
        }
        return o;
    }
    if (kind == ValueKind::Object) {
        std::string s{json};
        if (s.size() > maxChars) {
            s.resize(maxChars);
            s += "…";
        }
        return s;
    }
    if (kind == ValueKind::String) {
        auto d = ParseDoc(json);
        std::string s = d && yyjson_is_str(d.root()) ? std::string{yyjson_get_str(d.root())}
                                                     : std::string{json};
        if (s.size() > maxChars) {
            s.resize(maxChars);
            s += "…";
        }
        return s;
    }
    return std::string{json};
}

// 数组 of object：每项抽出 key→值（字符串化），供 UI 表格展示
struct ObjItem {
    std::string raw;
    std::vector<std::pair<std::string, std::string>> fields;
    [[nodiscard]] std::string Get(std::string_view key) const {
        for (const auto& [k, v] : fields) {
            if (k == key) return v;
        }
        return {};
    }
};

[[nodiscard]] inline std::string ValToBrief(yyjson_val* v, std::size_t maxChars = 80) {
    if (!v) return {};
    if (yyjson_is_str(v)) {
        return std::string{yyjson_get_str(v), yyjson_get_len(v)};
    }
    size_t len = 0;
    char* t = yyjson_val_write(v, 0, &len);
    if (!t) return {};
    std::string o{t, len};
    std::free(t);
    if (o.size() > maxChars) {
        o.resize(maxChars);
        o += "…";
    }
    return o;
}

[[nodiscard]] inline std::vector<ObjItem> ParseObjectArray(std::string_view json) {
    std::vector<ObjItem> out;
    auto d = ParseDoc(json);
    if (!d) return out;
    yyjson_val* r = d.root();
    if (!yyjson_is_arr(r)) return out;
    size_t i = 0, n = 0;
    yyjson_val* item = nullptr;
    yyjson_arr_foreach(r, i, n, item) {
        ObjItem oi;
        size_t len = 0;
        if (char* t = yyjson_val_write(item, 0, &len)) {
            oi.raw.assign(t, len);
            std::free(t);
        }
        if (item && yyjson_is_obj(item)) {
            size_t ki = 0, kn = 0;
            yyjson_val* k = nullptr;
            yyjson_val* v = nullptr;
            yyjson_obj_foreach(item, ki, kn, k, v) {
                const char* ks = yyjson_get_str(k);
                oi.fields.emplace_back(ks ? ks : "", ValToBrief(v));
            }
        }
        out.push_back(std::move(oi));
    }
    return out;
}

// 值简述：按类型给出前端可读摘要（数组会展开）
[[nodiscard]] inline std::string ValueBrief(std::string_view json, std::size_t maxChars = 100) {
    const auto kind = Classify(json);
    switch (kind) {
    case ValueKind::Null:
        return "(null)";
    case ValueKind::Invalid:
        return "(无效) " + std::string{json}.substr(0, 32);
    case ValueKind::Array: {
        const auto items = ParseStringArray(json);
        std::string o = "数组(" + std::to_string(items.size()) + "): ";
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i) o += " | ";
            o += items[i];
        }
        if (o.size() > maxChars) {
            o.resize(maxChars);
            o += "…";
        }
        return o;
    }
    case ValueKind::Object: {
        const auto objs = ParseObjectArray("[" + std::string{json} + "]");
        std::string o = "对象: " + std::string{json};
        if (o.size() > maxChars) {
            o.resize(maxChars);
            o += "…";
        }
        (void)objs;
        return o;
    }
    default:
        return ArrayBrief(json, 8, maxChars);
    }
}

// 数组元素数（非数组 → 0）
[[nodiscard]] inline std::size_t ArrayLen(std::string_view json) {
    return ParseStringArray(json).size();
}

// 对象数组里按字段取值列表，例如 identity_layers 的 "layer"
[[nodiscard]] inline std::vector<std::string> ObjArrayField(std::string_view json,
                                                            std::string_view key) {
    std::vector<std::string> out;
    for (const auto& item : ParseObjectArray(json)) {
        out.push_back(item.Get(key));
    }
    return out;
}

} // namespace shine::util::json
