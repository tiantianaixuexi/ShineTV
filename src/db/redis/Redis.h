#pragma once
// Redis module entry: mimalloc allocators + connection pool.
#include <expected>
#include <mutex>
#include <string_view>

#include "db/redis/RedisClient.h"
#include "db/redis/RedisError.h"
#include "db/redis/RedisPool.h"

namespace shine::db::redis {

// 预热失败（如连不上服务器）会返回错误，且池保持 not ready —— 调用方必须处理。
// 需要"服务器没起也算成功、连接推迟到首次 Acquire"的语义，把 opt.minConnections 设为 0。
[[nodiscard]] std::expected<void, RedisError> Init(const PoolOptions& opt = {});
void Shutdown();

[[nodiscard]] bool ready() noexcept;
[[nodiscard]] PoolStats poolStats();

[[nodiscard]] Client* DefaultClient() noexcept;
[[nodiscard]] std::mutex& DefaultMutex();

[[nodiscard]] std::expected<Lease, RedisError> Acquire();

} // namespace shine::db::redis
