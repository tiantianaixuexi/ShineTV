#pragma once
// Sync Redis client. Not thread-safe: one worker or external lock.
#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/redis/RedisError.h"
#include "db/redis/RedisValue.h"

struct redisContext;

namespace shine::db::redis {

struct ConnectOptions {
    std::string host = "127.0.0.1";
    int port = 6379;
    std::chrono::milliseconds connectTimeout{3000};
    std::chrono::milliseconds commandTimeout{5000};
    std::string password; // empty = no AUTH
    int db = 0;
};

class Client {
public:
    Client() = default;
    ~Client();
    Client(Client&& other) noexcept;
    Client& operator=(Client&& other) noexcept;
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    [[nodiscard]] std::expected<void, RedisError> Connect(const ConnectOptions& opt);
    void Disconnect() noexcept;
    [[nodiscard]] bool connected() const noexcept;

    [[nodiscard]] std::expected<Value, RedisError> Command(std::span<const std::string_view> args);

    [[nodiscard]] std::expected<void, RedisError> Ping();
    [[nodiscard]] std::expected<void, RedisError> Set(std::string_view key, std::string_view value);
    [[nodiscard]] std::expected<std::optional<std::string>, RedisError> Get(std::string_view key);
    [[nodiscard]] std::expected<bool, RedisError> Del(std::string_view key);
    [[nodiscard]] std::expected<bool, RedisError> Exists(std::string_view key);

    [[nodiscard]] std::expected<std::int64_t, RedisError> Incr(std::string_view key);
    [[nodiscard]] std::expected<void, RedisError> Expire(std::string_view key, std::chrono::seconds ttl);

    [[nodiscard]] std::expected<void, RedisError>
    HSet(std::string_view key, std::span<const std::pair<std::string_view, std::string_view>> fields);
    [[nodiscard]] std::expected<std::optional<std::string>, RedisError>
    HGet(std::string_view key, std::string_view field);
    [[nodiscard]] std::expected<std::vector<std::string>, RedisError> HGetAll(std::string_view key);
    [[nodiscard]] std::expected<bool, RedisError> HDel(std::string_view key, std::string_view field);

private:
    void CloseNoThrow() noexcept;

    redisContext* ctx_ = nullptr;
};

} // namespace shine::db::redis
