#pragma once
// shine::util::reflect —— C++26 静态反射 × yyjson 的序列化助手（header-only）
//
// 需要编译选项 -freflection（CMakeLists.txt 已加，仅对 C++ 生效）。
// 用法（Doc/RULES-LANG.md §13.6）：
//     const std::string json = util::reflect::ToJsonString(settings);
//     util::reflect::FromJsonString(json, settings);            // 宽容读：键缺失/类型不符取原值
//
// 设计原则：
//   * 只做"字段名 ↔ JSON 键"的机械映射，不做嵌套结构魔法；
//   * **嵌套结构体**（`vector<Shot>` / 成员是结构体）会**递归**写/读成 JSON 对象/数组（P5.1 起支持）；
//     `std::string` 与顺序容器在更早的分支被拦下，不会被误判成对象；
//   * 不支持的类型会在编译期 static_assert 报错（而不是静默丢字段）；
//   * 枚举按**整数**存（稳定、不怕改枚举名）；需要字符串形式就自己额外存一份名字。
#include <meta>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <yyjson.h>

namespace shine::util::reflect {
namespace detail {

template <class> inline constexpr bool kAlwaysFalse = false;

// 反射得到的字段表（consteval-only 值，只能在编译期上下文使用 —— 见 Doc/RULES-LANG.md §13.6）
template <class T>
consteval auto FieldInfos() {
    return std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current()));
}

// 该类型是否按"嵌套对象"处理：自身有可反射成员的类型。
// 注意判定顺序 —— `std::string` / 顺序容器必须在**更早的分支**被拦下，否则会被当成对象。
template <class V>
consteval bool IsReflectableObject() {
    if constexpr (std::is_class_v<V> && !std::is_union_v<V>) {
        return std::meta::nonstatic_data_members_of(^^V, std::meta::access_context::current()).size() > 0;
    } else {
        return false;
    }
}

} // namespace detail

// 前置声明：嵌套对象分支要递归调用（定义在下面）
template <class T>
std::size_t WriteObject(yyjson_mut_doc* doc, yyjson_mut_val* obj, const T& v);
template <class T>
std::size_t ReadObject(yyjson_val* root, T& v);

// 字段数量（编译期）
template <class T>
[[nodiscard]] constexpr std::size_t FieldCount() {
    return detail::FieldInfos<T>().size();
}

// 字段名表：编译期生成、元素是普通 string_view，运行时随便用（桥接模式）
template <class T>
[[nodiscard]] consteval auto FieldNames() {
    constexpr auto infos = detail::FieldInfos<T>();
    std::array<std::string_view, infos.size()> out{};
    for (std::size_t i = 0; i < infos.size(); ++i) {
        out[i] = std::meta::identifier_of(infos[i]);
    }
    return out;
}

// 类型名（日志/错误信息用）
template <class T>
[[nodiscard]] consteval std::string_view TypeName() {
    return std::meta::display_string_of(^^T);
}

// ---------------------------------------------------------------- 写入

// 单值 → yyjson 可变值；不支持的类型编译期报错
template <class V>
yyjson_mut_val* ToValue(yyjson_mut_doc* doc, const V& value) {
    if constexpr (std::is_same_v<V, std::string>) {
        return yyjson_mut_strncpy(doc, value.c_str(), value.size());
    } else if constexpr (std::is_same_v<V, std::string_view>) {
        return yyjson_mut_strncpy(doc, value.data(), value.size());
    } else if constexpr (std::is_same_v<V, bool>) {
        return yyjson_mut_bool(doc, value);
    } else if constexpr (std::is_enum_v<V>) {
        return yyjson_mut_sint(doc, static_cast<std::int64_t>(std::to_underlying(value)));
    } else if constexpr (std::is_integral_v<V>) {
        return yyjson_mut_sint(doc, static_cast<std::int64_t>(value));
    } else if constexpr (std::is_floating_point_v<V>) {
        return yyjson_mut_real(doc, static_cast<double>(value));
    } else if constexpr (requires { value.begin(); value.end(); value.size(); }) {
        // 顺序容器（vector<string> 等）：写成 JSON 数组
        yyjson_mut_val* arr = yyjson_mut_arr(doc);
        for (const auto& item : value) {
            yyjson_mut_arr_add_val(arr, ToValue(doc, item));
        }
        return arr;
    } else if constexpr (detail::IsReflectableObject<V>()) {
        // 嵌套对象（如 VideoProject 里的 vector<Shot>）：递归写成一个 JSON 对象
        yyjson_mut_val* obj = yyjson_mut_obj(doc);
        WriteObject(doc, obj, value);
        return obj;
    } else {
        static_assert(detail::kAlwaysFalse<V>,
                      "util::reflect: 该字段类型未支持，请在 ToValue/FromValue 里加分支");
        return nullptr;
    }
}

// 把 v 的字段写进 obj；返回写入的字段数
template <class T>
std::size_t WriteObject(yyjson_mut_doc* doc, yyjson_mut_val* obj, const T& v) {
    std::size_t written = 0;
    template for (constexpr auto m : detail::FieldInfos<T>()) {
        constexpr std::string_view key = std::meta::identifier_of(m);
        const auto& value = v.[: m :];
        yyjson_mut_obj_add(obj,
                           yyjson_mut_strncpy(doc, key.data(), key.size()),
                           ToValue(doc, value));
        ++written;
    }
    return written;
}

// 整个对象 → JSON 文本（pretty 便于人工编辑 settings.json / 工程文件）
template <class T>
[[nodiscard]] std::string ToJsonString(const T& v, bool pretty = true) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    WriteObject(doc, root, v);

    const yyjson_write_flag flags = pretty ? YYJSON_WRITE_PRETTY : 0;
    std::size_t len = 0;
    char* text = yyjson_mut_write(doc, flags, &len);
    std::string out = text ? std::string{text, len} : std::string{};
    if (text) {
        free(text);
    }
    yyjson_mut_doc_free(doc);
    return out;
}

// ---------------------------------------------------------------- 读取

// yyjson 值 → 单值；返回**是否真正写入**（类型不符则保持原值并返回 false）
template <class V>
bool FromValue(yyjson_val* jv, V& value) {
    if constexpr (std::is_same_v<V, std::string>) {
        if (yyjson_is_str(jv)) {
            value.assign(yyjson_get_str(jv), yyjson_get_len(jv));
            return true;
        }
        return false;
    } else if constexpr (std::is_same_v<V, bool>) {
        if (yyjson_is_bool(jv)) {
            value = yyjson_get_bool(jv);
            return true;
        }
        return false;
    } else if constexpr (std::is_enum_v<V>) {
        if (yyjson_is_num(jv)) {
            value = static_cast<V>(yyjson_get_sint(jv));
            return true;
        }
        return false;
    } else if constexpr (std::is_integral_v<V>) {
        if (yyjson_is_num(jv)) {
            value = static_cast<V>(yyjson_get_sint(jv));
            return true;
        }
        return false;
    } else if constexpr (std::is_floating_point_v<V>) {
        if (yyjson_is_num(jv)) {
            value = static_cast<V>(yyjson_get_num(jv));
            return true;
        }
        return false;
    } else if constexpr (requires { value.clear(); value.push_back(std::declval<typename V::value_type>()); }) {
        if (!yyjson_is_arr(jv)) {
            return false;
        }
        value.clear();
        yyjson_arr_iter iter = yyjson_arr_iter_with(jv);
        while (yyjson_val* item = yyjson_arr_iter_next(&iter)) {
            typename V::value_type element{};
            FromValue(item, element);
            value.push_back(std::move(element));
        }
        return true;
    } else if constexpr (detail::IsReflectableObject<V>()) {
        // 嵌套对象：是 JSON 对象就递归读（缺键 → 保留原值）
        if (!yyjson_is_obj(jv)) {
            return false;
        }
        ReadObject(jv, value);
        return true;
    } else {
        static_assert(detail::kAlwaysFalse<V>,
                      "util::reflect: 该字段类型未支持，请在 ToValue/FromValue 里加分支");
        return false;
    }
}

// 从 JSON 对象读回 v（宽容：键缺失/类型不符就保留原值）
// 返回**真正生效**的字段数（键存在但类型不符不计入）
template <class T>
std::size_t ReadObject(yyjson_val* root, T& v) {
    std::size_t applied = 0;
    if (!root || !yyjson_is_obj(root)) {
        return 0;
    }
    template for (constexpr auto m : detail::FieldInfos<T>()) {
        constexpr std::string_view key = std::meta::identifier_of(m);
        if (yyjson_val* jv = yyjson_obj_getn(root, key.data(), key.size()); jv && !yyjson_is_null(jv)) {
            if (FromValue(jv, v.[: m :])) {
                ++applied;
            }
        }
    }
    return applied;
}

// JSON 文本 → 对象；返回命中的字段数，0 表示解析失败或无匹配字段
template <class T>
std::size_t FromJsonString(std::string_view text, T& v) {
    yyjson_doc* doc = yyjson_read(text.data(), text.size(), 0);
    if (!doc) {
        return 0;
    }
    const std::size_t hit = ReadObject(yyjson_doc_get_root(doc), v);
    yyjson_doc_free(doc);
    return hit;
}

} // namespace shine::util::reflect
