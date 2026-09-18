#include "db/redis/RedisAsync.h"

#include <fmt/format.h>

#include <array>
#include <memory>
#include <utility>

#include "core/Async.h"
#include "core/Log.h"
#include "db/redis/Redis.h"

namespace shine::db::redis {
namespace {

void RunCmd(std::string what, std::function<std::expected<Value, RedisError>(Client&)> body, ValueCb cb) {
    async::RunOnWorker([what = std::move(what), body = std::move(body), cb = std::move(cb)]() mutable {
        auto lease = Acquire();
        if (!lease) {
            log::Warn("redis acquire failed for {}: kind={} code={} msg={}", what, ToChar(lease.error().kind),
                      lease.error().code, lease.error().message);
            if (cb) {
                cb(std::unexpected(lease.error()));
            }
            return;
        }
        auto r = body(**lease);
        if (!r) {
            log::Warn("redis {} failed: kind={} code={} msg={}", what, ToChar(r.error().kind), r.error().code,
                      r.error().message);
        }
        if (cb) {
            cb(std::move(r));
        }
    });
}

} // namespace

void GetAsync(std::string_view key, StringCb cb) {
    auto keyCopy = std::make_shared<std::string>(key);
    RunCmd(
        fmt::format("GET {}", *keyCopy),
        [keyCopy](Client& c) -> std::expected<Value, RedisError> {
            const std::array<std::string_view, 2> args{"GET", std::string_view{*keyCopy}};
            return c.Command(args);
        },
        [cb = std::move(cb)](std::expected<Value, RedisError> r) {
            if (!cb) {
                return;
            }
            if (!r) {
                cb(std::unexpected(r.error()));
                return;
            }
            if (r->isNil()) {
                cb(std::nullopt);
                return;
            }
            auto s = r->asString();
            if (!s) {
                cb(std::unexpected(s.error()));
                return;
            }
            cb(std::optional<std::string>{std::move(*s)});
        });
}

void SetAsync(std::string_view key, std::string_view value, VoidCb cb) {
    auto keyCopy = std::make_shared<std::string>(key);
    auto valCopy = std::make_shared<std::string>(value);
    RunCmd(
        fmt::format("SET {}", *keyCopy),
        [keyCopy, valCopy](Client& c) -> std::expected<Value, RedisError> {
            const std::array<std::string_view, 3> args{"SET", std::string_view{*keyCopy}, std::string_view{*valCopy}};
            return c.Command(args);
        },
        [cb = std::move(cb)](std::expected<Value, RedisError> r) {
            if (!cb) {
                return;
            }
            if (!r) {
                cb(std::unexpected(r.error()));
                return;
            }
            cb(std::expected<void, RedisError>{});
        });
}

void DelAsync(std::string_view key, ValueCb cb) {
    auto keyCopy = std::make_shared<std::string>(key);
    RunCmd(
        fmt::format("DEL {}", *keyCopy),
        [keyCopy](Client& c) -> std::expected<Value, RedisError> {
            const std::array<std::string_view, 2> args{"DEL", std::string_view{*keyCopy}};
            return c.Command(args);
        },
        std::move(cb));
}

void PingAsync(VoidCb cb) {
    RunCmd(
        "PING",
        [](Client& c) -> std::expected<Value, RedisError> {
            const std::array<std::string_view, 1> args{"PING"};
            return c.Command(args);
        },
        [cb = std::move(cb)](std::expected<Value, RedisError> r) {
            if (!cb) {
                return;
            }
            if (!r) {
                cb(std::unexpected(r.error()));
                return;
            }
            cb(std::expected<void, RedisError>{});
        });
}

void CommandAsync(std::vector<std::string> args, ValueCb cb) {
    const std::string what = args.empty() ? std::string{"<empty>"} : args[0];
    auto sharedArgs = std::make_shared<std::vector<std::string>>(std::move(args));
    RunCmd(
        what,
        [sharedArgs](Client& c) -> std::expected<Value, RedisError> {
            std::vector<std::string_view> views;
            views.reserve(sharedArgs->size());
            for (const auto& a : *sharedArgs) {
                views.emplace_back(a);
            }
            return c.Command(views);
        },
        std::move(cb));
}

} // namespace shine::db::redis
