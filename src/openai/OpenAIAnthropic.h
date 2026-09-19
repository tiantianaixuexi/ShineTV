#pragma once
// Anthropic Messages 协议（MiMo / MiniMax 兼容端点）
// 文档：docs/compose/plans/novel-agent/API-PROVIDERS.md §4
#include <atomic>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "openai/OpenAIClient.h" // ApiError
#include "openai/OpenAIProvider.h"

namespace shine::openai {

struct AnthropicTool {
    std::string name;
    std::string description;
    std::string inputSchemaJson; // {"type":"object",...}
};

struct AnthropicMessage {
    std::string role; // user | assistant
    std::string contentJson; // 字符串 text 或块数组 JSON
};

struct AnthropicRequest {
    std::string model;
    std::string system;
    std::vector<AnthropicMessage> messages;
    int maxTokens = 4096;
    double temperature = 1.0;
    std::vector<AnthropicTool> tools;
    bool disableThinking = true; // M3
};

struct AnthropicTurn {
    std::string stopReason; // end_turn | tool_use | ...
    std::string text;       // 拼好的 text 块（剥 thinking）
    std::string rawContentJson; // content 数组原样，下一轮 assistant 必须回传
    struct ToolUse {
        std::string id;
        std::string name;
        std::string inputJson;
    };
    std::vector<ToolUse> toolUses;
};

// 解析 POST /v1/messages 响应
[[nodiscard]] std::expected<AnthropicTurn, ApiError>
ParseAnthropicTurn(std::string_view body);

// 同步调用。baseUrl 传 anthropic 兼容根（如 https://api.minimax.cn/anthropic）
// 或 OpenAI 格式根（会自动改写成 /anthropic）。
[[nodiscard]] std::expected<AnthropicTurn, ApiError>
AnthropicComplete(std::string_view baseUrl, std::string_view apiKey,
                  const AnthropicRequest& req,
                  std::chrono::seconds timeout = kDefaultTimeout,
                  const std::atomic<bool>* cancel = nullptr);

// 当前 Provider → Anthropic base（MiMo Token Plan / 按量 / MiniMax）
[[nodiscard]] std::string AnthropicBaseUrlFor(const LlmProfile& p);

// 用当前 Provider 走 Anthropic 协议补一轮
// `model` 空 = profile 默认模型；非空 = 按阶段指定的模型（S16，`09` §2.4）
[[nodiscard]] std::expected<std::string, ApiError>
AnthropicLlmComplete(std::string_view instructions, std::string_view userText,
                     std::chrono::seconds timeout = kDefaultTimeout,
                     const std::atomic<bool>* cancel = nullptr, std::string_view model = {});

[[nodiscard]] bool RunAnthropicSelfCheck();

} // namespace shine::openai
