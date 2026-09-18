#pragma once
// 多模型 Provider 解析（OpenAI / Xiaomi MiMo / MiniMax / custom）
// 规则备忘：docs/compose/plans/novel-agent/API-PROVIDERS.md
#include <string>
#include <string_view>

namespace shine::openai {

enum class Provider { OpenAi, MiMo, MiniMax, Custom };
enum class Protocol { ChatCompletions, Responses, Anthropic };

[[nodiscard]] Provider ParseProvider(std::string_view id) noexcept;
[[nodiscard]] std::string_view ProviderId(Provider p) noexcept;
[[nodiscard]] std::string_view ProviderLabel(Provider p) noexcept; // 中文
[[nodiscard]] Protocol ParseProtocol(std::string_view id) noexcept;
[[nodiscard]] std::string_view ProtocolId(Protocol p) noexcept;

struct LlmProfile {
    Provider provider = Provider::OpenAi;
    Protocol protocol = Protocol::ChatCompletions;
    std::string baseUrl;
    std::string apiKey;
    std::string model;
    bool minimaxDisableThinking = true;
};

// 从当前 Settings + 可选环境变量解析生效配置
// 环境变量优先级（按 provider）：
//   openai/custom: OPENAI_API_KEY
//   mimo: MIMO_API_KEY
//   minimax: MINIMAX_API_KEY
[[nodiscard]] LlmProfile ResolveActiveProfile();
[[nodiscard]] LlmProfile ResolveProfile(Provider p);

// Chat Completions 会把 base 末尾补 /chat/completions（若未带）
[[nodiscard]] std::string ChatCompletionsUrl(std::string_view baseUrl);

} // namespace shine::openai
