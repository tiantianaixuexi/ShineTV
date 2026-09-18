#pragma once
// Redis connection pool. Acquire: idle -> create if under max -> wait.
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <expected>
#include <memory>
#include <mutex>
#include <vector>

#include "db/redis/RedisClient.h"
#include "db/redis/RedisError.h"

namespace shine::db::redis {

struct PoolOptions {
    ConnectOptions connect;
    std::size_t maxConnections = 8; // 0 => 1
    std::size_t minConnections = 1;
    std::chrono::milliseconds idleTtl{0};
    std::chrono::milliseconds acquireTimeout{3000};
};

struct PoolStats {
    std::size_t maxConnections = 0;
    std::size_t idle = 0;
    std::size_t leased = 0;
    std::size_t created = 0;
    std::size_t closed = 0;
};

class Lease {
public:
    Lease() = default;
    Lease(Lease&&) noexcept;
    Lease& operator=(Lease&&) noexcept;
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;
    ~Lease();

    [[nodiscard]] explicit operator bool() const noexcept { return client_ != nullptr; }
    [[nodiscard]] Client* operator->() noexcept { return client_; }
    [[nodiscard]] Client& operator*() noexcept { return *client_; }

private:
    friend class Pool;
    Lease(Client* c, void* pool) noexcept;

    Client* client_ = nullptr;
    void* pool_ = nullptr;
};

class Pool {
public:
    Pool() = default;
    ~Pool();
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    [[nodiscard]] std::expected<void, RedisError> Init(const PoolOptions& opt);
    void Shutdown() noexcept;

    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] PoolStats stats() const;

    [[nodiscard]] std::expected<Lease, RedisError> Acquire();
    void Release(Client* client, bool broken) noexcept;

private:
    struct Slot {
        std::unique_ptr<Client> client;
        std::chrono::steady_clock::time_point lastUsed{};
        bool broken = false;
    };

    void CloseUnlocked(Slot& slot) noexcept;
    std::expected<std::unique_ptr<Client>, RedisError> OpenNew();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    PoolOptions opt_{};
    std::vector<Slot> idle_;
    std::size_t leased_ = 0;
    std::size_t created_ = 0;
    std::size_t closed_ = 0;

    // Init 预热失败时留档首个错误：此后 Acquire 原样返回它，
    // 而不是含糊的 "pool not ready"（否则调用方只知道没就绪、不知道连不上）。
    RedisError initError_{};
    bool ready_ = false;
};

} // namespace shine::db::redis
