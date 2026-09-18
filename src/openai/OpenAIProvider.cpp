#include "openai/OpenAIProvider.h"

#include "core/Settings.h"

#include <cstdlib>

namespace shine::openai {

Provider ParseProvider(std::string_view id) noexcept {
    if (id == "mimo") return Provider::MiMo;
    if (id == "minimax") return Provider::MiniMax;
    if (id == "custom") return Provider::Custom;
    return Provider::OpenAi;
}

std::string_view ProviderId(Provider p) noexcept {
    switch (p) {
    case Provider::MiMo: return "mimo";
    case Provider::MiniMax: return "minimax";
    case Provider::Custom: return "custom";
    case Provider::OpenAi:
    default: return "openai";
    }
}

std::string_view ProviderLabel(Provider p) noexcept {
    switch (p) {
    case Provider::MiMo: return "Xiaomi MiMo";
    case Provider::MiniMax: return "MiniMax";
    case Provider::Custom: return "自定义";
    case Provider::OpenAi:
    default: return "OpenAI";
    }
}

Protocol ParseProtocol(std::string_view id) noexcept {
    if (id == "responses") return Protocol::Responses;
    return Protocol::ChatCompletions;
}

std::string_view ProtocolId(Protocol p) noexcept {
    return p == Protocol::Responses ? "responses" : "chat_completions";
}

namespace {

[[nodiscard]] std::string EnvOr(std::string_view envName, const std::string& fallback) {
    if (const char* e = std::getenv(std::string{envName}.c_str()); e && *e) {
        return e;
    }
    return fallback;
}

} // namespace

LlmProfile ResolveProfile(Provider p) {
    const AppSettings& s = Settings();
    LlmProfile out;
    out.provider = p;
    switch (p) {
    case Provider::MiMo:
        out.protocol = Protocol::ChatCompletions;
        // Token Plan：默认中国集群；用户可在设置里改 sgp/ams
        if (s.mimoTokenPlan) {
            out.baseUrl = s.mimoBaseUrl.empty() ||
                                  s.mimoBaseUrl.find("api.xiaomimimo.com") != std::string::npos
                              ? "https://token-plan-cn.xiaomimimo.com/v1"
                              : s.mimoBaseUrl;
        } else {
            out.baseUrl = s.mimoBaseUrl.empty() || s.mimoBaseUrl.find("token-plan") != std::string::npos
                              ? "https://api.xiaomimimo.com/v1"
                              : s.mimoBaseUrl;
        }
        out.apiKey = EnvOr("MIMO_API_KEY", s.mimoApiKey);
        out.model = s.mimoModel.empty() ? "mimo-v2.5-pro" : s.mimoModel;
        break;
    case Provider::MiniMax:
        out.protocol = Protocol::ChatCompletions;
        out.baseUrl = s.minimaxBaseUrl.empty() ? "https://api.minimax.cn/v1" : s.minimaxBaseUrl;
        out.apiKey = EnvOr("MINIMAX_API_KEY", s.minimaxApiKey);
        out.model = s.minimaxModel.empty() ? "MiniMax-M3" : s.minimaxModel;
        out.minimaxDisableThinking = s.minimaxDisableThinking;
        break;
    case Provider::Custom:
        out.protocol = ParseProtocol(s.llmProtocol);
        out.baseUrl = s.openaiBaseUrl;
        out.apiKey = EnvOr("OPENAI_API_KEY", s.openaiApiKey);
        out.model = s.openaiModelDefault;
        break;
    case Provider::OpenAi:
    default:
        // OpenAI 官方可用 Responses；也允许切 chat_completions
        out.protocol = ParseProtocol(s.llmProtocol);
        if (s.llmProtocol.empty()) {
            out.protocol = Protocol::ChatCompletions; // 对第三方更通用
        }
        out.baseUrl = s.openaiBaseUrl.empty() ? "https://api.openai.com/v1" : s.openaiBaseUrl;
        out.apiKey = EnvOr("OPENAI_API_KEY", s.openaiApiKey);
        out.model = s.openaiModelDefault.empty() ? "gpt-4o-mini" : s.openaiModelDefault;
        break;
    }
    return out;
}

LlmProfile ResolveActiveProfile() {
    return ResolveProfile(ParseProvider(Settings().llmProvider));
}

std::string ChatCompletionsUrl(std::string_view baseUrl) {
    std::string base{baseUrl.empty() ? std::string_view{"https://api.openai.com/v1"} : baseUrl};
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (base.empty()) base = "https://api.openai.com/v1";
    if (base.ends_with("/chat/completions")) return base;
    return base + "/chat/completions";
}

} // namespace shine::openai
