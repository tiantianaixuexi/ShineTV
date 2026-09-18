#pragma once
// Data layer facade: Init Redis pool; SQLite opened per project file by owner.
#include <expected>

#include "db/redis/Redis.h"
#include "db/redis/RedisError.h"
#include "db/redis/RedisPool.h"
#include "db/sqlite/SqliteDb.h"

namespace shine::db {

struct DbConfig {
    bool enableRedis = true;
    redis::PoolOptions redis{};
};

// Redis 预热失败（如服务器没起）会返回错误，且池保持 not ready —— 调用方必须处理。
// SQLite **不在这里打开**：按工程文件由各 owner 自己 Database::Open（见 novel/NovelProjects.cpp）。
[[nodiscard]] std::expected<void, redis::RedisError> Init(const DbConfig& cfg = {});
void Shutdown();

[[nodiscard]] bool redisReady() noexcept;
[[nodiscard]] redis::PoolStats redisStats();

[[nodiscard]] std::expected<redis::Lease, redis::RedisError> AcquireRedis();

} // namespace shine::db
