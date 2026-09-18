#include "db/Db.h"

#include "core/Log.h"
#include "db/redis/Redis.h"

namespace shine::db {

std::expected<void, redis::RedisError> Init(const DbConfig& cfg) {
    if (!cfg.enableRedis) {
        log::Info("db::Init skip redis");
        return {};
    }
    return redis::Init(cfg.redis);
}

void Shutdown() {
    redis::Shutdown();
}

bool redisReady() noexcept {
    return redis::ready();
}

redis::PoolStats redisStats() {
    return redis::poolStats();
}

std::expected<redis::Lease, redis::RedisError> AcquireRedis() {
    return redis::Acquire();
}

} // namespace shine::db
