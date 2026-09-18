#pragma once
// RAII wrapper for redisReply.
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "db/redis/RedisError.h"

struct redisReply;

namespace shine::db::redis {

enum class ReplyType {
    Nil,
    Status,
    Error,
    Integer,
    Double,
    String,
    Array,
    Boolean,
    Map,
    Set,
    Push,
    Verbatim,
    BigNumber,
    Unknown,
};

class Value {
public:
    Value() = default;
    explicit Value(redisReply* owned) noexcept;
    Value(Value&& other) noexcept;
    Value& operator=(Value&& other) noexcept;
    Value(const Value&) = delete;
    Value& operator=(const Value&) = delete;
    ~Value();

    [[nodiscard]] bool valid() const noexcept { return reply_ != nullptr; }
    [[nodiscard]] ReplyType type() const noexcept;
    [[nodiscard]] bool isNil() const noexcept;
    [[nodiscard]] bool isError() const noexcept;
    [[nodiscard]] bool isOkStatus() const noexcept;

    [[nodiscard]] std::expected<std::int64_t, RedisError> asInt() const;
    [[nodiscard]] std::expected<double, RedisError> asDouble() const;
    [[nodiscard]] std::expected<bool, RedisError> asBool() const;
    [[nodiscard]] std::expected<std::string, RedisError> asString() const;
    [[nodiscard]] std::expected<std::string_view, RedisError> asView() const;
    [[nodiscard]] std::expected<std::vector<Value>, RedisError> asArray() const;
    [[nodiscard]] std::expected<std::vector<std::string>, RedisError> asStringArray() const;

    [[nodiscard]] std::string debug() const;

private:
    redisReply* reply_ = nullptr;
};

} // namespace shine::db::redis
