#include "db/redis/RedisValue.h"

#include <fmt/format.h>

#include <hiredis.h>

#include <charconv>
#include <cstring>

namespace shine::db::redis {
namespace {

[[nodiscard]] ReplyType MapType(int t) noexcept {
    switch (t) {
    case REDIS_REPLY_NIL: return ReplyType::Nil;
    case REDIS_REPLY_STATUS: return ReplyType::Status;
    case REDIS_REPLY_ERROR: return ReplyType::Error;
    case REDIS_REPLY_INTEGER: return ReplyType::Integer;
    case REDIS_REPLY_DOUBLE: return ReplyType::Double;
    case REDIS_REPLY_STRING: return ReplyType::String;
    case REDIS_REPLY_ARRAY: return ReplyType::Array;
    case REDIS_REPLY_BOOL: return ReplyType::Boolean;
    case REDIS_REPLY_MAP: return ReplyType::Map;
    case REDIS_REPLY_SET: return ReplyType::Set;
    case REDIS_REPLY_PUSH: return ReplyType::Push;
    case REDIS_REPLY_VERB: return ReplyType::Verbatim;
    case REDIS_REPLY_BIGNUM: return ReplyType::BigNumber;
    default: return ReplyType::Unknown;
    }
}

[[nodiscard]] RedisError MakeError(ErrorKind kind, int code, std::string message) {
    return RedisError{.kind = kind, .code = code, .message = std::move(message)};
}

redisReply* CloneReply(const redisReply* src) {
    if (!src) {
        return nullptr;
    }
    auto* out = static_cast<redisReply*>(hi_calloc(1, sizeof(redisReply)));
    if (!out) {
        return nullptr;
    }
    out->type = src->type;
    out->integer = src->integer;
    out->dval = src->dval;
    out->len = src->len;
    out->elements = src->elements;
    std::memcpy(out->vtype, src->vtype, sizeof(out->vtype));
    if (src->str && src->len > 0) {
        out->str = static_cast<char*>(hi_malloc(src->len + 1));
        if (!out->str) {
            freeReplyObject(out);
            return nullptr;
        }
        std::memcpy(out->str, src->str, src->len);
        out->str[src->len] = '\0';
    }
    if (src->element && src->elements > 0) {
        out->element = static_cast<redisReply**>(hi_calloc(src->elements, sizeof(redisReply*)));
        if (!out->element) {
            freeReplyObject(out);
            return nullptr;
        }
        for (size_t i = 0; i < src->elements; ++i) {
            out->element[i] = CloneReply(src->element[i]);
            if (!out->element[i]) {
                out->elements = i;
                freeReplyObject(out);
                return nullptr;
            }
        }
    }
    return out;
}

} // namespace

Value::Value(redisReply* owned) noexcept : reply_(owned) {}

Value::Value(Value&& other) noexcept : reply_(other.reply_) {
    other.reply_ = nullptr;
}

Value& Value::operator=(Value&& other) noexcept {
    if (this != &other) {
        if (reply_) {
            freeReplyObject(reply_);
        }
        reply_ = other.reply_;
        other.reply_ = nullptr;
    }
    return *this;
}

Value::~Value() {
    if (reply_) {
        freeReplyObject(reply_);
        reply_ = nullptr;
    }
}

ReplyType Value::type() const noexcept {
    return reply_ ? MapType(reply_->type) : ReplyType::Nil;
}

bool Value::isNil() const noexcept {
    return !reply_ || reply_->type == REDIS_REPLY_NIL;
}

bool Value::isError() const noexcept {
    return reply_ && reply_->type == REDIS_REPLY_ERROR;
}

bool Value::isOkStatus() const noexcept {
    return reply_ && reply_->type == REDIS_REPLY_STATUS;
}

std::expected<std::int64_t, RedisError> Value::asInt() const {
    if (!reply_) {
        return std::unexpected(MakeError(ErrorKind::Protocol, 0, "null reply"));
    }
    if (reply_->type == REDIS_REPLY_INTEGER) {
        return static_cast<std::int64_t>(reply_->integer);
    }
    if (reply_->type == REDIS_REPLY_STRING || reply_->type == REDIS_REPLY_STATUS) {
        if (!reply_->str) {
            return std::unexpected(MakeError(ErrorKind::Protocol, reply_->type, "empty string reply"));
        }
        std::int64_t v = 0;
        const std::string_view sv{reply_->str, reply_->len};
        const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
        if (ec != std::errc{} || ptr != sv.data() + sv.size()) {
            return std::unexpected(MakeError(ErrorKind::Protocol, 0, fmt::format("not an integer: {}", sv)));
        }
        return v;
    }
    return std::unexpected(
        MakeError(ErrorKind::Protocol, reply_->type, fmt::format("want integer, type={}", reply_->type)));
}

std::expected<double, RedisError> Value::asDouble() const {
    if (!reply_) {
        return std::unexpected(MakeError(ErrorKind::Protocol, 0, "null reply"));
    }
    if (reply_->type == REDIS_REPLY_DOUBLE) {
        return reply_->dval;
    }
    if (reply_->type == REDIS_REPLY_STRING || reply_->type == REDIS_REPLY_STATUS) {
        if (!reply_->str) {
            return std::unexpected(MakeError(ErrorKind::Protocol, reply_->type, "empty string reply"));
        }
        const std::string_view sv{reply_->str, reply_->len};
        double v = 0;
        const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
        if (ec != std::errc{}) {
            return std::unexpected(MakeError(ErrorKind::Protocol, 0, fmt::format("not a double: {}", sv)));
        }
        return v;
    }
    return std::unexpected(MakeError(ErrorKind::Protocol, reply_->type, "want double"));
}

std::expected<bool, RedisError> Value::asBool() const {
    if (!reply_) {
        return std::unexpected(MakeError(ErrorKind::Protocol, 0, "null reply"));
    }
    if (reply_->type == REDIS_REPLY_BOOL || reply_->type == REDIS_REPLY_INTEGER) {
        return reply_->integer != 0;
    }
    return std::unexpected(MakeError(ErrorKind::Protocol, reply_->type, "want bool"));
}

std::expected<std::string, RedisError> Value::asString() const {
    if (!reply_) {
        return std::unexpected(MakeError(ErrorKind::Protocol, 0, "null reply"));
    }
    switch (reply_->type) {
    case REDIS_REPLY_STRING:
    case REDIS_REPLY_STATUS:
    case REDIS_REPLY_ERROR:
    case REDIS_REPLY_VERB:
    case REDIS_REPLY_BIGNUM:
        if (!reply_->str) {
            return std::string{};
        }
        return std::string{reply_->str, reply_->len};
    case REDIS_REPLY_DOUBLE:
        return fmt::format("{}", reply_->dval);
    case REDIS_REPLY_INTEGER:
        return fmt::format("{}", reply_->integer);
    default:
        return std::unexpected(MakeError(ErrorKind::Protocol, reply_->type, "not a string reply"));
    }
}

std::expected<std::string_view, RedisError> Value::asView() const {
    if (!reply_ || !reply_->str) {
        return std::unexpected(MakeError(ErrorKind::Protocol, 0, "no string view"));
    }
    switch (reply_->type) {
    case REDIS_REPLY_STRING:
    case REDIS_REPLY_STATUS:
    case REDIS_REPLY_ERROR:
    case REDIS_REPLY_VERB:
    case REDIS_REPLY_BIGNUM:
        return std::string_view{reply_->str, reply_->len};
    default:
        return std::unexpected(MakeError(ErrorKind::Protocol, reply_->type, "not a viewable reply"));
    }
}

std::expected<std::vector<Value>, RedisError> Value::asArray() const {
    if (!reply_) {
        return std::unexpected(MakeError(ErrorKind::Protocol, 0, "null reply"));
    }
    if (reply_->type != REDIS_REPLY_ARRAY && reply_->type != REDIS_REPLY_SET &&
        reply_->type != REDIS_REPLY_PUSH && reply_->type != REDIS_REPLY_MAP) {
        return std::unexpected(MakeError(ErrorKind::Protocol, reply_->type, "want array reply"));
    }
    std::vector<Value> out;
    out.reserve(reply_->elements);
    for (size_t i = 0; i < reply_->elements; ++i) {
        auto* clone = CloneReply(reply_->element[i]);
        if (!clone) {
            return std::unexpected(MakeError(ErrorKind::Protocol, 0, "clone element failed"));
        }
        out.emplace_back(clone);
    }
    return out;
}

std::expected<std::vector<std::string>, RedisError> Value::asStringArray() const {
    if (!reply_) {
        return std::unexpected(MakeError(ErrorKind::Protocol, 0, "null reply"));
    }
    if (reply_->type != REDIS_REPLY_ARRAY && reply_->type != REDIS_REPLY_SET) {
        return std::unexpected(MakeError(ErrorKind::Protocol, reply_->type, "want string array"));
    }
    std::vector<std::string> out;
    out.reserve(reply_->elements);
    for (size_t i = 0; i < reply_->elements; ++i) {
        const auto* el = reply_->element[i];
        if (!el) {
            continue;
        }
        if (el->type == REDIS_REPLY_STRING || el->type == REDIS_REPLY_STATUS) {
            out.emplace_back(el->str ? std::string{el->str, el->len} : std::string{});
        } else if (el->type == REDIS_REPLY_INTEGER) {
            out.push_back(fmt::format("{}", el->integer));
        } else if (el->type == REDIS_REPLY_NIL) {
            out.emplace_back();
        } else {
            return std::unexpected(MakeError(ErrorKind::Protocol, el->type, "array element not string"));
        }
    }
    return out;
}

std::string Value::debug() const {
    if (!reply_) {
        return "(null)";
    }
    switch (reply_->type) {
    case REDIS_REPLY_NIL: return "nil";
    case REDIS_REPLY_STATUS:
    case REDIS_REPLY_ERROR:
    case REDIS_REPLY_STRING:
    case REDIS_REPLY_VERB:
    case REDIS_REPLY_BIGNUM:
        return fmt::format("\"{}\"",
                           reply_->str ? std::string_view{reply_->str, reply_->len} : std::string_view{});
    case REDIS_REPLY_INTEGER: return fmt::format("{}", reply_->integer);
    case REDIS_REPLY_DOUBLE: return fmt::format("{}", reply_->dval);
    case REDIS_REPLY_BOOL: return reply_->integer ? "true" : "false";
    case REDIS_REPLY_ARRAY:
    case REDIS_REPLY_SET:
    case REDIS_REPLY_PUSH: return fmt::format("array[{}]", reply_->elements);
    case REDIS_REPLY_MAP: return fmt::format("map[{}]", reply_->elements);
    default: return fmt::format("type={}", static_cast<int>(reply_->type));
    }
}

} // namespace shine::db::redis
