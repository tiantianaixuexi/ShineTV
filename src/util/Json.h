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

#include "core/Log.h" // S66：修复必须**可见**（告警就发在唯一入口，见 `RepairLlmJson`）

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

// —— S66：LLM 输出的 **JSON 语法修复**（唯一来源）——
// 为什么必须有它（**不是 json 库的问题**，先把结论写清）：yyjson 按 RFC 8259 严格解析，
// 而"字符串内部出现裸 `"`"**本身就是语法错误**，且对解析器**有歧义**（它无法区分"闭合引号"与
// "字符串内的引号"）⇒ **任何** JSON 库都不可能有这种"宽容 flag"。只能在**进解析器之前**修。
// 修三类**确定非法**的东西（因此对**合法** JSON 是**恒等**变换，不会改变语义）：
//   ① 字符串内部的**裸半角引号** —— 真跑实证（第 7 章）：`"summary":"…含半个坐标与日期"7""`
//      ⇒ `yyjson_read` 当场判非法 ⇒ **整章状态不回写**（`status` 停在 review）。
//   ② 字符串内部的**裸控制字符**（换行 / 回车 / 制表）—— JSON 要求转义。
//   ③ **尾逗号**（`[1,2,]` / `{"a":1,}`）。
// ⚠️ 判据是**启发式**（裸引号靠"后面跟 `,` `}` `]` `:` 才算闭合"）⇒ 必须**把修复条数报出来**
//    （偏离可见）；且**修复后仍解析失败时照实报错**，绝不假装成功。
// ⚠️ 不处理的（会在下游照实报非法，可见）：全角引号当**分隔符**用、键名带裸引号、
//    字符串里嵌 `": "` 这种"看起来像闭合"的形态（启发式会判错 ⇒ 仍解析失败 ⇒ 重试）。
[[nodiscard]] inline std::string RepairLlmJson(std::string_view text, int* outFixes = nullptr) {
    std::string out;
    out.reserve(text.size() + 32);
    int fixes = 0;
    bool inStr = false;
    // 该位置的 `"` 是否**闭合**引号：向后看第一个非空白字符是不是 `,` `}` `]` `:`（或已到末尾）
    const auto isCloser = [text](std::size_t i) {
        for (std::size_t j = i + 1; j < text.size(); ++j) {
            const char c = text[j];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                continue;
            }
            return c == ',' || c == '}' || c == ']' || c == ':';
        }
        return true; // 末尾 ⇒ 交给解析器判（不在这里下结论）
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (!inStr) {
            // ③ 尾逗号：`,` 后面（跳过空白）是 `}` / `]` ⇒ 直接丢掉这个逗号
            if (c == ',') {
                bool trailing = false;
                for (std::size_t j = i + 1; j < text.size(); ++j) {
                    const char d = text[j];
                    if (d == ' ' || d == '\t' || d == '\n' || d == '\r') {
                        continue;
                    }
                    trailing = (d == '}' || d == ']');
                    break;
                }
                if (trailing) {
                    ++fixes;
                    continue;
                }
            }
            if (c == '"') {
                inStr = true;
            }
            out += c;
            continue;
        }
        if (c == '\\') { // 转义序列原样带过（含 `\"` `\\` `\n` …）
            out += c;
            if (i + 1 < text.size()) {
                out += text[i + 1];
                ++i;
            }
            continue;
        }
        if (c == '"') {
            if (isCloser(i)) {
                inStr = false;
                out += c;
            } else {
                out += "\\\""; // ① 裸引号 ⇒ 转义（仍在字符串内）
                ++fixes;
            }
            continue;
        }
        if (c == '\n' || c == '\r' || c == '\t') { // ② 裸控制字符
            out += (c == '\n') ? "\\n" : (c == '\r' ? "\\r" : "\\t");
            ++fixes;
            continue;
        }
        out += c;
    }
    if (outFixes != nullptr) {
        *outFixes = fixes;
    }
    if (fixes > 0) {
        // 偏离必须可见（S66）：**不是** json 库的问题 —— 是模型吐了非法 JSON，我们只是兜底。
        // 频繁出现 ⇒ 该改提示词，而不是把这里当常态。
        log::Warn("RepairLlmJson：修正了 {} 处非法 JSON 写法（裸引号 / 裸控制字符 / 尾逗号）—— "
                  "模型输出不合规，这只是兜底",
                  fixes);
    }
    return out;
}

// —— 宽容提取（S38）：LLM 的输出**常带 markdown 围栏或前后说明文字**（"好的，以下是 JSON："…）。
// 直接 `yyjson_read` 全文会**当场判非法** —— 真实跑就撞上了（MiniMax-M3 在 V6 TIMELINE 上的输出
// 不是纯 JSON，整条阶段链当场断在 V6）。本函数把"可能的 JSON 正文"抠出来：
//   ① 去 markdown 围栏（```json … ``` / ``` … ```）② 取第一个 `{` 到最后一个 `}`
//   ③（S66）过一遍 `RepairLlmJson` 修"确定非法"的裸引号/裸控制字符/尾逗号。
// ⚠️ `outQuoteFixes` 非空时会写回**修复条数** —— 调用方应据此告警（**偏离必须可见**）。
// ⚠️ 抠不出来时**原样返回**（让 yyjson 去报错），不要假装成功。
[[nodiscard]] inline std::string ExtractJsonObject(std::string_view raw, int* outQuoteFixes = nullptr) {
    std::string s{raw};
    if (const auto fence = s.find("```"); fence != std::string::npos) {
        const std::size_t begin = s.find('\n', fence);
        if (begin != std::string::npos) {
            const std::size_t close = s.find("```", begin);
            if (close != std::string::npos) {
                s = s.substr(begin + 1, close - begin - 1);
            } else {
                // S52：**围栏只开不闭**（模型被 `max_output_tokens` 截断时很常见）——
                // 原先这里整段跳过、把 ```json 那一行也留在 `s` 里。截掉它，让下面的
                // "第一个 `{` .. 最后一个 `}`" 至少有干净的起点（尾部残缺只能靠重试）。
                s = s.substr(begin + 1);
            }
        }
    }
    // S52：起点 = **第一个 `{` 或 `[`（谁在前）**。⚠️ 原先只找 `{` ⇒ 顶层是数组的输出
    //（`[{...}]`）会被整体漏掉（模型偶尔直接回数组，我们确实碰得到）。
    const std::size_t lb = s.find('[');
    const std::size_t cb = s.find('{');
    std::size_t b = cb;
    if (lb != std::string::npos && (cb == std::string::npos || lb < cb)) {
        b = lb;
    }
    if (b == std::string::npos) {
        return RepairLlmJson(s, outQuoteFixes);
    }
    // 终点 = **最后一个 `}` / `]` 中更靠后的那个**。⚠️ 取向说明：偏后只是多带一点尾巴
    //（`yyjson_read` 会明确判非法 ⇒ 由调用方**重试**，代价可控）；**偏前才是灾难** ——
    // 那会把一份合法 JSON 截断成非法，而且看起来像"模型输出有问题"。
    std::size_t end = s.rfind('}');
    if (const std::size_t la = s.rfind(']');
        la != std::string::npos && (end == std::string::npos || la > end)) {
        end = la;
    }
    const std::string body = (end == std::string::npos || end <= b) ? s : s.substr(b, end - b + 1);
    return RepairLlmJson(body, outQuoteFixes);
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
