#include "db/redis/RedisClient.h"

#include <fmt/format.h>

#include <hiredis.h>

#include <array>
#include <string>
#include <vector>

namespace shine::db::redis {
namespace {

[[nodiscard]] timeval ToTimeval(std::chrono::milliseconds ms) {
    if (ms.count() < 0) {
        ms = std::chrono::milliseconds{0};
    }
    timeval tv{};
    tv.tv_sec = static_cast<long>(ms.count() / 1000);
    tv.tv_usec = static_cast<long>((ms.count() % 1000) * 1000);
    return tv;
}

[[nodiscard]] RedisError FromContext(const redisContext* ctx, ErrorKind fallback) {
    if (!ctx) {
        return RedisError{.kind = ErrorKind::Connect, .code = 0, .message = "null context"};
    }
    ErrorKind kind = fallback;
    switch (ctx->err) {
    case REDIS_ERR_IO: kind = ErrorKind::Io; break;
    case REDIS_ERR_EOF: kind = ErrorKind::Closed; break;
    case REDIS_ERR_PROTOCOL: kind = ErrorKind::Protocol; break;
    case REDIS_ERR_TIMEOUT: kind = ErrorKind::Timeout; break;
    default: kind = fallback; break;
    }
    return RedisError{.kind = kind, .code = ctx->err, .message = std::string{ctx->errstr}};
}

} // namespace

Client::~Client() {
    CloseNoThrow();
}

Client::Client(Client&& other) noexcept : ctx_(other.ctx_) {
    other.ctx_ = nullptr;
}

Client& Client::operator=(Client&& other) noexcept {
    if (this != &other) {
        CloseNoThrow();
        ctx_ = other.ctx_;
        other.ctx_ = nullptr;
    }
    return *this;
}

void Client::CloseNoThrow() noexcept {
    if (ctx_) {
        redisFree(ctx_);
        ctx_ = nullptr;
    }
}

void Client::Disconnect() noexcept {
    CloseNoThrow();
}

bool Client::connected() const noexcept {
    return ctx_ != nullptr && ctx_->err == 0 && (ctx_->flags & REDIS_CONNECTED) != 0;
}

std::expected<void, RedisError> Client::Connect(const ConnectOptions& opt) {
    CloseNoThrow();

    const auto connectTv = ToTimeval(opt.connectTimeout);
    redisOptions options{};
    REDIS_OPTIONS_SET_TCP(&options, opt.host.c_str(), opt.port);
    options.connect_timeout = &connectTv;

    ctx_ = redisConnectWithOptions(&options);
    if (!ctx_ || ctx_->err) {
        RedisError err = ctx_ ? FromContext(ctx_, ErrorKind::Connect)
                              : RedisError{.kind = ErrorKind::Connect, .code = 0, .message = "redisConnect failed"};
        CloseNoThrow();
        return std::unexpected(std::move(err));
    }

    const auto cmdTv = ToTimeval(opt.commandTimeout);
    (void)redisSetTimeout(ctx_, cmdTv);

    if (!opt.password.empty()) {
        void* raw = redisCommand(ctx_, "AUTH %b", opt.password.data(), opt.password.size());
        if (!raw) {
            RedisError err = FromContext(ctx_, ErrorKind::Connect);
            CloseNoThrow();
            return std::unexpected(std::move(err));
        }
        Value reply{static_cast<redisReply*>(raw)};
        if (reply.isError()) {
            auto msg = reply.asString();
            CloseNoThrow();
            return std::unexpected(RedisError{
                .kind = ErrorKind::Connect, .code = 0, .message = msg ? *msg : std::string{"AUTH failed"}});
        }
    }

    if (opt.db != 0) {
        void* raw = redisCommand(ctx_, "SELECT %d", opt.db);
        if (!raw) {
            RedisError err = FromContext(ctx_, ErrorKind::Connect);
            CloseNoThrow();
            return std::unexpected(std::move(err));
        }
        Value reply{static_cast<redisReply*>(raw)};
        if (reply.isError()) {
            auto msg = reply.asString();
            CloseNoThrow();
            return std::unexpected(RedisError{
                .kind = ErrorKind::Connect, .code = 0, .message = msg ? *msg : std::string{"SELECT failed"}});
        }
    }

    return {};
}

std::expected<Value, RedisError> Client::Command(std::span<const std::string_view> args) {
    if (!ctx_) {
        return std::unexpected(RedisError{.kind = ErrorKind::Closed, .code = 0, .message = "not connected"});
    }
    if (args.empty()) {
        return std::unexpected(RedisError{.kind = ErrorKind::Protocol, .code = 0, .message = "empty command"});
    }

    std::vector<const char*> argv;
    std::vector<size_t> argvlen;
    argv.reserve(args.size());
    argvlen.reserve(args.size());
    for (const auto a : args) {
        argv.push_back(a.data());
        argvlen.push_back(a.size());
    }

    void* raw = redisCommandArgv(ctx_, static_cast<int>(argv.size()), argv.data(), argvlen.data());
    if (!raw) {
        return std::unexpected(FromContext(ctx_, ErrorKind::Io));
    }
    Value reply{static_cast<redisReply*>(raw)};
    if (reply.isError()) {
        auto msg = reply.asString();
        return std::unexpected(
            RedisError{.kind = ErrorKind::Command, .code = 0, .message = msg ? *msg : std::string{"command error"}});
    }
    return reply;
}

std::expected<void, RedisError> Client::Ping() {
    const std::array<std::string_view, 1> args{"PING"};
    auto r = Command(args);
    if (!r) {
        return std::unexpected(r.error());
    }
    return {};
}

std::expected<void, RedisError> Client::Set(std::string_view key, std::string_view value) {
    const std::array<std::string_view, 3> args{"SET", key, value};
    auto r = Command(args);
    if (!r) {
        return std::unexpected(r.error());
    }
    return {};
}

std::expected<std::optional<std::string>, RedisError> Client::Get(std::string_view key) {
    const std::array<std::string_view, 2> args{"GET", key};
    auto r = Command(args);
    if (!r) {
        return std::unexpected(r.error());
    }
    if (r->isNil()) {
        return std::nullopt;
    }
    auto s = r->asString();
    if (!s) {
        return std::unexpected(s.error());
    }
    return std::optional<std::string>{std::move(*s)};
}

std::expected<bool, RedisError> Client::Del(std::string_view key) {
    const std::array<std::string_view, 2> args{"DEL", key};
    auto r = Command(args);
    if (!r) {
        return std::unexpected(r.error());
    }
    auto n = r->asInt();
    if (!n) {
        return std::unexpected(n.error());
    }
    return *n > 0;
}

std::expected<bool, RedisError> Client::Exists(std::string_view key) {
    const std::array<std::string_view, 2> args{"EXISTS", key};
    auto r = Command(args);
    if (!r) {
        return std::unexpected(r.error());
    }
    auto n = r->asInt();
    if (!n) {
        return std::unexpected(n.error());
    }
    return *n > 0;
}

std::expected<std::int64_t, RedisError> Client::Incr(std::string_view key) {
    const std::array<std::string_view, 2> args{"INCR", key};
    auto r = Command(args);
    if (!r) {
        return std::unexpected(r.error());
    }
    return r->asInt();
}

std::expected<void, RedisError> Client::Expire(std::string_view key, std::chrono::seconds ttl) {
    const auto secs = fmt::format("{}", ttl.count());
    const std::array<std::string_view, 3> args{"EXPIRE", key, secs};
    auto r = Command(args);
    if (!r) {
        return std::unexpected(r.error());
    }
    return {};
}

std::expected<void, RedisError>
Client::HSet(std::string_view key, std::span<const std::pair<std::string_view, std::string_view>> fields) {
    if (fields.empty()) {
        return std::unexpected(RedisError{.kind = ErrorKind::Protocol, .code = 0, .message = "HSET empty fields"});
    }
    std::vector<std::string_view> args;
    args.reserve(2 + fields.size() * 2);
    args.push_back("HSET");
    args.push_back(key);
    for (const auto& [f, v] : fields) {
        args.push_back(f);
        args.push_back(v);
    }
    auto r = Command(args);
    if (!r) {
        return std::unexpected(r.error());
    }
    return {};
}

std::expected<std::optional<std::string>, RedisError> Client::HGet(std::string_view key, std::string_view field) {
    const std::array<std::string_view, 3> args{"HGET", key, field};
    auto r = Command(args);
    if (!r) {
        return std::unexpected(r.error());
    }
    if (r->isNil()) {
        return std::nullopt;
    }
    auto s = r->asString();
    if (!s) {
        return std::unexpected(s.error());
    }
    return std::optional<std::string>{std::move(*s)};
}

std::expected<std::vector<std::string>, RedisError> Client::HGetAll(std::string_view key) {
    const std::array<std::string_view, 2> args{"HGETALL", key};
    auto r = Command(args);
    if (!r) {
        return std::unexpected(r.error());
    }
    return r->asStringArray();
}

std::expected<bool, RedisError> Client::HDel(std::string_view key, std::string_view field) {
    const std::array<std::string_view, 3> args{"HDEL", key, field};
    auto r = Command(args);
    if (!r) {
        return std::unexpected(r.error());
    }
    auto n = r->asInt();
    if (!n) {
        return std::unexpected(n.error());
    }
    return *n > 0;
}

} // namespace shine::db::redis
