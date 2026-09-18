#pragma once
// Async wrappers on stdexec worker pool. Callbacks fire on worker unless caller posts to UI.
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "db/redis/RedisCmd.h"
#include "db/redis/RedisError.h"
#include "db/redis/RedisValue.h"

namespace shine::db::redis {

using ValueCb = std::function<void(std::expected<Value, RedisError>)>;
using VoidCb = std::function<void(std::expected<void, RedisError>)>;
using StringCb = std::function<void(std::expected<std::optional<std::string>, RedisError>)>;

void GetAsync(std::string_view key, StringCb cb);
void SetAsync(std::string_view key, std::string_view value, VoidCb cb);
void DelAsync(std::string_view key, ValueCb cb);
void PingAsync(VoidCb cb);
void CommandAsync(std::vector<std::string> args, ValueCb cb);

template <RedisCommand Cmd>
void ExecuteAsync(Cmd cmd, ValueCb cb) {
    CommandAsync(BuildArgv(cmd), std::move(cb));
}

} // namespace shine::db::redis
