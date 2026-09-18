#include "openai/OpenAIAnthropic.h"

#include "core/Log.h"
#include "openai/OpenAIHttp.h"
#include "util/Json.h"

#include <fmt/format.h>

#include <yyjson.h>

#include <cstdlib>

namespace shine::openai {
namespace {

[[nodiscard]] std::string EscapeJson(std::string_view s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (const char c : s) {
        if (c == '"' || c == '\\') {
            o += '\\';
            o += c;
        } else if (c == '\n') {
            o += "\\n";
        } else if (c == '\r') {
            o += "\\r";
        } else if (c == '\t') {
            o += "\\t";
        } else {
            o += c;
        }
    }
    return o;
}

[[nodiscard]] ApiError AErr(int st, std::string code, std::string msg) {
    return ApiError{.http_status = st, .code = std::move(code), .message = std::move(msg)};
}

} // namespace

std::string AnthropicBaseUrlFor(const LlmProfile& p) {
    switch (p.provider) {
    case Provider::MiMo: {
        // OpenAI 根 → anthropic 根
        std::string b = p.baseUrl;
        if (b.find("/anthropic") != std::string::npos) return b;
        if (b.find("token-plan") != std::string::npos) {
            // https://token-plan-cn.xiaomimimo.com/v1 → .../anthropic
            const auto pos = b.find("/v1");
            if (pos != std::string::npos) return b.substr(0, pos) + "/anthropic";
            return b + "/anthropic";
        }
        const auto pos = b.find("/v1");
        if (pos != std::string::npos) return b.substr(0, pos) + "/anthropic";
        return "https://api.xiaomimimo.com/anthropic";
    }
    case Provider::MiniMax: {
        std::string b = p.baseUrl;
        if (b.find("/anthropic") != std::string::npos) return b;
        const auto pos = b.find("/v1");
        if (pos != std::string::npos) return b.substr(0, pos) + "/anthropic";
        return "https://api.minimax.cn/anthropic";
    }
    default:
        return p.baseUrl;
    }
}

std::expected<AnthropicTurn, ApiError> ParseAnthropicTurn(std::string_view body) {
    yyjson_doc* doc = yyjson_read(body.data(), body.size(), 0);
    if (!doc) {
        return std::unexpected(AErr(0, "json", "Anthropic 响应不是 JSON"));
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    AnthropicTurn out;
    out.stopReason = util::json::GetStrCopy(root, "stop_reason");
    if (yyjson_val* content = util::json::GetArr(root, "content")) {
        size_t len = 0;
        char* raw = yyjson_val_write(content, 0, &len);
        if (raw) {
            out.rawContentJson.assign(raw, len);
            std::free(raw);
        }
        size_t i = 0, n = 0;
        yyjson_val* block = nullptr;
        yyjson_arr_foreach(content, i, n, block) {
            const auto type = util::json::GetStr(block, "type");
            if (type == "text") {
                out.text += util::json::GetStrCopy(block, "text");
            } else if (type == "tool_use") {
                AnthropicTurn::ToolUse tu;
                tu.id = util::json::GetStrCopy(block, "id");
                tu.name = util::json::GetStrCopy(block, "name");
                if (yyjson_val* input = util::json::Get(block, "input")) {
                    size_t il = 0;
                    char* t = yyjson_val_write(input, 0, &il);
                    if (t) {
                        tu.inputJson.assign(t, il);
                        std::free(t);
                    }
                }
                if (!tu.name.empty()) out.toolUses.push_back(std::move(tu));
            }
            // thinking 块：保留在 rawContentJson，不进 text
        }
    }
    yyjson_doc_free(doc);
    if (out.rawContentJson.empty() && out.text.empty()) {
        return std::unexpected(AErr(0, "json", "Anthropic 响应缺少 content"));
    }
    return out;
}

std::expected<AnthropicTurn, ApiError>
AnthropicComplete(std::string_view baseUrl, std::string_view apiKey, const AnthropicRequest& req,
                  std::chrono::seconds timeout, const std::atomic<bool>* cancel) {
    if (apiKey.empty()) {
        return std::unexpected(AErr(0, "no_key", "未配置 Anthropic API 密钥"));
    }
    if (cancel && cancel->load()) {
        return std::unexpected(AErr(0, "cancelled", "已取消"));
    }
    std::string root{baseUrl};
    if (root.find("/anthropic") == std::string::npos) {
        const auto pos = root.find("/v1");
        if (pos != std::string::npos) root = root.substr(0, pos);
        while (!root.empty() && root.back() == '/') root.pop_back();
        root += "/anthropic";
    }
    while (!root.empty() && root.back() == '/') root.pop_back();
    const std::string url = root + "/v1/messages";

    std::string body = fmt::format(
        R"({{"model":"{}","max_tokens":{},"temperature":{})", EscapeJson(req.model), req.maxTokens,
        req.temperature);
    if (!req.system.empty()) {
        body += fmt::format(R"(,"system":"{}")", EscapeJson(req.system));
    }
    if (req.disableThinking) {
        body += R"(,"thinking":{"type":"disabled"})";
    }
    if (!req.tools.empty()) {
        body += ",\"tools\":[";
        for (std::size_t i = 0; i < req.tools.size(); ++i) {
            if (i) body += ",";
            const auto& t = req.tools[i];
            body += fmt::format(
                R"({{"name":"{}","description":"{}","input_schema":{}}})", EscapeJson(t.name),
                EscapeJson(t.description),
                t.inputSchemaJson.empty() ? "{}" : t.inputSchemaJson);
        }
        body += "]";
    }
    body += ",\"messages\":[";
    for (std::size_t i = 0; i < req.messages.size(); ++i) {
        if (i) body += ",";
        const auto& m = req.messages[i];
        // content 已是 JSON（字符串或数组）
        body += fmt::format(R"({{"role":"{}","content":{}}})", EscapeJson(m.role),
                            m.contentJson.empty() ? "\"\"" : m.contentJson);
    }
    body += "]}";

    log::Info("Anthropic → {} model={}", url, req.model);

    // Anthropic：x-api-key + anthropic-version（不是 Bearer）
    const HttpTransportResult http = PostJsonHeaders(
        url,
        {{"x-api-key", std::string{apiKey}}, {"anthropic-version", "2023-06-01"}},
        body, timeout);
    if (cancel && cancel->load()) {
        return std::unexpected(AErr(0, "cancelled", "已取消"));
    }
    if (http.status < 200 || http.status >= 300) {
        if (http.status == 0 && !http.error.empty()) {
            return std::unexpected(AErr(0, "network", http.error));
        }
        yyjson_doc* ed = yyjson_read(http.body.data(), http.body.size(), 0);
        if (ed) {
            yyjson_val* eroot = yyjson_doc_get_root(ed);
            if (yyjson_val* err = util::json::GetObj(eroot, "error")) {
                auto e = AErr(http.status, util::json::GetStrCopy(err, "type"),
                              util::json::GetStrCopy(err, "message"));
                yyjson_doc_free(ed);
                if (e.message.empty()) e.message = fmt::format("HTTP {}", http.status);
                return std::unexpected(e);
            }
            yyjson_doc_free(ed);
        }
        return std::unexpected(AErr(http.status, "http_" + std::to_string(http.status),
                                    fmt::format("HTTP {}", http.status)));
    }
    return ParseAnthropicTurn(http.body);
}

std::expected<std::string, ApiError>
AnthropicLlmComplete(std::string_view instructions, std::string_view userText,
                     std::chrono::seconds timeout, const std::atomic<bool>* cancel) {
    const LlmProfile p = ResolveActiveProfile();
    AnthropicRequest req;
    req.model = p.model;
    req.system = std::string{instructions};
    req.disableThinking = p.minimaxDisableThinking;
    req.messages.push_back(
        {.role = "user",
         .contentJson = fmt::format(R"([{{"type":"text","text":"{}"}}])", EscapeJson(userText))});
    auto r = AnthropicComplete(AnthropicBaseUrlFor(p), p.apiKey, req, timeout, cancel);
    if (!r) return std::unexpected(r.error());
    return r->text;
}

bool RunAnthropicSelfCheck() {
    const std::string sample = R"({
      "id": "msg_1",
      "type": "message",
      "role": "assistant",
      "stop_reason": "tool_use",
      "content": [
        {"type": "thinking", "thinking": "想一想"},
        {"type": "text", "text": "我需要查天气"},
        {"type": "tool_use", "id": "toolu_1", "name": "get_weather",
         "input": {"location": "SF"}}
      ]
    })";
    auto t = ParseAnthropicTurn(sample);
    if (!t || t->stopReason != "tool_use" || t->toolUses.size() != 1 ||
        t->toolUses[0].name != "get_weather" || t->text.find("天气") == std::string::npos ||
        t->rawContentJson.find("thinking") == std::string::npos) {
        log::Error("Anthropic Parse 自检失败");
        return false;
    }
    LlmProfile mimo;
    mimo.provider = Provider::MiMo;
    mimo.baseUrl = "https://api.xiaomimimo.com/v1";
    if (AnthropicBaseUrlFor(mimo) != "https://api.xiaomimimo.com/anthropic") {
        log::Error("Anthropic URL 自检失败：{}", AnthropicBaseUrlFor(mimo));
        return false;
    }
    mimo.baseUrl = "https://token-plan-cn.xiaomimimo.com/v1";
    if (AnthropicBaseUrlFor(mimo).find("token-plan-cn") == std::string::npos ||
        AnthropicBaseUrlFor(mimo).find("/anthropic") == std::string::npos) {
        log::Error("Token Plan Anthropic URL 自检失败");
        return false;
    }
    LlmProfile mm;
    mm.provider = Provider::MiniMax;
    mm.baseUrl = "https://api.minimax.cn/v1";
    if (AnthropicBaseUrlFor(mm) != "https://api.minimax.cn/anthropic") {
        log::Error("MiniMax Anthropic URL 自检失败");
        return false;
    }
    log::Info("Anthropic 协议自检通过");
    return true;
}

} // namespace shine::openai
