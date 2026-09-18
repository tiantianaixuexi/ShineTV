#pragma once
// shine::mcp —— MCP 工具注册地基的类型契约（P7.1 + 分模块注册）
// 只依赖 yyjson / 标准库；业务模块注册工具时只 include 本头 + ToolRegistry.h
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include <yyjson.h>

namespace shine::mcp {

// 协议层 initialize 用；P7.3 消费
inline constexpr std::string_view kServerName = "ShineTVStudio";
inline constexpr std::string_view kServerVersion = "0.1.0";

enum class CallStatus { Ok, NotFound, BadArguments, InternalError };

struct CallOutcome {
    CallStatus status = CallStatus::Ok;
    std::string text; // Ok → 结果文本；否则中文错误说明

    [[nodiscard]] bool ok() const noexcept { return status == CallStatus::Ok; }

    [[nodiscard]] static CallOutcome Ok(std::string text) {
        return CallOutcome{.status = CallStatus::Ok, .text = std::move(text)};
    }
    [[nodiscard]] static CallOutcome Fail(CallStatus st, std::string text) {
        return CallOutcome{.status = st, .text = std::move(text)};
    }
};

// args：tools/call 的 arguments 对象；空参可为 nullptr
using ToolHandler = std::function<CallOutcome(yyjson_val* args)>;

struct ModuleInfo {
    std::string id;    // "comfy" / "graph" / "demo"
    std::string title; // 展示名，可中文
};

struct Tool {
    std::string name;        // 全局唯一；同名 Register 覆盖（幂等）
    std::string title;       // 可空 → list 时省略
    std::string description; // 给模型看
    std::string moduleId;    // 归属模块；空 = 未分组
    std::string schemaJson;  // inputSchema JSON 对象文本（owned）
    ToolHandler handler;     // 必填
};

} // namespace shine::mcp
