#pragma once
// C++26 reflection: struct <-> Redis Hash (header-only).
#include <meta>

#include <charconv>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "db/redis/RedisClient.h"
#include "db/redis/RedisError.h"

namespace shine::db::redis {
namespace hash_detail {

template <class> inline constexpr bool kAlwaysFalse = false;

template <class T>
consteval auto FieldInfos() {
    return std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current()));
}

template <class V>
consteval bool IsSupportedField() {
    using D = std::remove_cvref_t<V>;
    if constexpr (std::is_same_v<D, std::string> || std::is_same_v<D, std::string_view>) {
        return true;
    } else if constexpr (std::is_same_v<D, bool>) {
        return true;
    } else if constexpr (std::is_enum_v<D>) {
        return true;
    } else if constexpr (std::is_arithmetic_v<D>) {
        return true;
    } else {
        return false;
    }
}

template <class V>
[[nodiscard]] std::string FieldToString(const V& v) {
    using D = std::remove_cvref_t<V>;
    if constexpr (std::is_same_v<D, std::string>) {
        return v;
    } else if constexpr (std::is_same_v<D, std::string_view>) {
        return std::string{v};
    } else if constexpr (std::is_same_v<D, bool>) {
        return v ? "1" : "0";
    } else if constexpr (std::is_enum_v<D>) {
        return fmt::format("{}", std::to_underlying(v));
    } else if constexpr (std::is_arithmetic_v<D>) {
        return fmt::format("{}", v);
    } else {
        static_assert(kAlwaysFalse<D>, "RedisHash unsupported field type");
        return {};
    }
}

template <class V>
[[nodiscard]] bool ParseFieldInto(std::string_view text, V& out) {
    using D = std::remove_cvref_t<V>;
    if constexpr (std::is_same_v<D, std::string>) {
        out = std::string{text};
        return true;
    } else if constexpr (std::is_same_v<D, std::string_view>) {
        out = text;
        return true;
    } else if constexpr (std::is_same_v<D, bool>) {
        out = (text == "1" || text == "true" || text == "TRUE");
        return true;
    } else if constexpr (std::is_enum_v<D>) {
        std::underlying_type_t<D> raw{};
        const auto [p, ec] = std::from_chars(text.data(), text.data() + text.size(), raw);
        if (ec != std::errc{}) {
            return false;
        }
        out = static_cast<D>(raw);
        return true;
    } else if constexpr (std::is_arithmetic_v<D>) {
        D raw{};
        const auto [p, ec] = std::from_chars(text.data(), text.data() + text.size(), raw);
        if (ec != std::errc{}) {
            return false;
        }
        out = raw;
        return true;
    } else {
        static_assert(kAlwaysFalse<D>, "RedisHash unsupported field type");
        return false;
    }
}

} // namespace hash_detail

template <class T>
[[nodiscard]] std::expected<void, RedisError> HSet(Client& client, std::string_view key, const T& obj) {
    static_assert(std::is_class_v<T>);
    std::vector<std::pair<std::string_view, std::string>> storage;
    storage.reserve(hash_detail::FieldInfos<T>().size());
    template for (constexpr auto m : hash_detail::FieldInfos<T>()) {
        static_assert(hash_detail::IsSupportedField<decltype(obj.[: m :])>());
        constexpr std::string_view name = std::meta::identifier_of(m);
        storage.emplace_back(name, hash_detail::FieldToString(obj.[: m :]));
    }

    std::vector<std::pair<std::string_view, std::string_view>> fields;
    fields.reserve(storage.size());
    for (auto& [n, v] : storage) {
        fields.emplace_back(n, std::string_view{v});
    }
    return client.HSet(key, fields);
}

template <class T>
[[nodiscard]] std::expected<void, RedisError> HGetAll(Client& client, std::string_view key, T& obj) {
    static_assert(std::is_class_v<T>);
    auto flat = client.HGetAll(key);
    if (!flat) {
        return std::unexpected(flat.error());
    }
    const auto& arr = *flat;

    template for (constexpr auto m : hash_detail::FieldInfos<T>()) {
        static_assert(hash_detail::IsSupportedField<decltype(obj.[: m :])>());
        constexpr std::string_view name = std::meta::identifier_of(m);
        for (std::size_t i = 0; i + 1 < arr.size(); i += 2) {
            if (arr[i] == name) {
                (void)hash_detail::ParseFieldInto(arr[i + 1], obj.[: m :]);
                break;
            }
        }
    }
    return {};
}

} // namespace shine::db::redis
