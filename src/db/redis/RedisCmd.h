#pragma once
// Compile-time Redis commands: Cmd::name + field order -> argv via reflection.
#include <meta>

#include <concepts>
#include <cstddef>
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
#include "db/redis/RedisValue.h"

namespace shine::db::redis {

template <class T>
concept RedisCommand = requires {
    { T::name } -> std::convertible_to<std::string_view>;
};

namespace cmd_detail {

template <class> inline constexpr bool kAlwaysFalse = false;

template <class T>
consteval auto FieldInfos() {
    return std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current()));
}

template <class V>
[[nodiscard]] std::string ToArg(const V& v) {
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
        static_assert(kAlwaysFalse<D>, "RedisCmd unsupported arg type");
        return {};
    }
}

} // namespace cmd_detail

template <RedisCommand Cmd>
[[nodiscard]] std::vector<std::string> BuildArgv(const Cmd& cmd) {
    std::vector<std::string> argv;
    argv.reserve(1 + cmd_detail::FieldInfos<Cmd>().size());
    argv.emplace_back(std::string{Cmd::name});
    template for (constexpr auto m : cmd_detail::FieldInfos<Cmd>()) {
        argv.push_back(cmd_detail::ToArg(cmd.[: m :]));
    }
    return argv;
}

template <RedisCommand Cmd>
[[nodiscard]] consteval std::size_t ArgCount() {
    return cmd_detail::FieldInfos<Cmd>().size();
}

template <RedisCommand Cmd>
[[nodiscard]] std::expected<Value, RedisError> Execute(Client& client, const Cmd& cmd) {
    const auto argv = BuildArgv(cmd);
    std::vector<std::string_view> views;
    views.reserve(argv.size());
    for (const auto& a : argv) {
        views.emplace_back(a);
    }
    return client.Command(views);
}

struct CmdPing {
    static constexpr std::string_view name = "PING";
};

struct CmdGet {
    static constexpr std::string_view name = "GET";
    std::string_view key;
};

struct CmdSet {
    static constexpr std::string_view name = "SET";
    std::string_view key;
    std::string_view value;
};

struct CmdDel {
    static constexpr std::string_view name = "DEL";
    std::string_view key;
};

struct CmdExists {
    static constexpr std::string_view name = "EXISTS";
    std::string_view key;
};

struct CmdIncr {
    static constexpr std::string_view name = "INCR";
    std::string_view key;
};

struct CmdExpire {
    static constexpr std::string_view name = "EXPIRE";
    std::string_view key;
    std::int64_t seconds = 0;
};

struct CmdHSetOne {
    static constexpr std::string_view name = "HSET";
    std::string_view key;
    std::string_view field;
    std::string_view value;
};

struct CmdHGet {
    static constexpr std::string_view name = "HGET";
    std::string_view key;
    std::string_view field;
};

struct CmdHGetAll {
    static constexpr std::string_view name = "HGETALL";
    std::string_view key;
};

static_assert(RedisCommand<CmdGet>);
static_assert(RedisCommand<CmdSet>);
static_assert(ArgCount<CmdGet>() == 1);
static_assert(ArgCount<CmdSet>() == 2);
static_assert(ArgCount<CmdPing>() == 0);

} // namespace shine::db::redis
