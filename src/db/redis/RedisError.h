#pragma once
#include <string>

namespace shine::db::redis {

enum class ErrorKind {
    None,
    Connect,
    Io,
    Protocol,
    Command,
    Timeout,
    Closed,
};

struct RedisError {
    ErrorKind kind = ErrorKind::None;
    int code = 0;
    std::string message;
};

[[nodiscard]] inline const char* ToChar(ErrorKind k) noexcept {
    switch (k) {
    case ErrorKind::None: return "None";
    case ErrorKind::Connect: return "Connect";
    case ErrorKind::Io: return "Io";
    case ErrorKind::Protocol: return "Protocol";
    case ErrorKind::Command: return "Command";
    case ErrorKind::Timeout: return "Timeout";
    case ErrorKind::Closed: return "Closed";
    }
    return "Unknown";
}

} // namespace shine::db::redis
