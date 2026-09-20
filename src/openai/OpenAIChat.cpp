#include "openai/OpenAIClient.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "openai/OpenAIAnthropic.h"
#include "openai/OpenAIConfig.h"
#include "openai/OpenAIHttp.h"
#include "openai/OpenAIProvider.h"
#include "openai/OpenAIStream.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Json.h"

#include <fmt/format.h>

#include <yyjson.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>

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

[[nodiscard]] ApiError MakeErr(int status, std::string code, std::string msg) {
    return ApiError{.http_status = status, .code = std::move(code), .message = std::move(msg)};
}

} // namespace

std::string ExtractChatContent(std::string_view body) {
    if (body.empty()) return {};
    yyjson_doc* doc = yyjson_read(body.data(), body.size(), 0);
    if (!doc) return {};
    yyjson_val* root = yyjson_doc_get_root(doc);
    std::string content;
    if (yyjson_val* choices = util::json::GetArr(root, "choices")) {
        if (yyjson_val* c0 = yyjson_arr_get(choices, 0)) {
            if (yyjson_val* msg = util::json::GetObj(c0, "message")) {
                content = util::json::GetStrCopy(msg, "content");
            }
        }
    }
    yyjson_doc_free(doc);
    // 剥 MiniMax/MiMo 思考块 <think>...</think>
    auto stripThink = [](std::string s) {
        for (;;) {
            const auto a = s.find("<think>");
            if (a == std::string::npos) break;
            const auto b = s.find("</think>", a);
            if (b == std::string::npos) {
                s.erase(a);
                break;
            }
            s.erase(a, b + 8 - a);
        }
        return s;
    };
    content = stripThink(std::move(content));
    // trim
    while (!content.empty() && (content.front() == '\n' || content.front() == ' ')) {
        content.erase(content.begin());
    }
    while (!content.empty() && (content.back() == '\n' || content.back() == ' ')) {
        content.pop_back();
    }
    return content;
}

// S38：**把"到底提交了什么"变得可见** —— 起因：用户问「我能在哪里看到到底提交了什么给 minimax」，
// 而原先日志只有一行 `ChatComplete → URL model=x`，请求体**一个字都看不到**（连它多大都不知道）。
// `SHINE_LLM_DUMP=<目录>` → 把**请求体**与**响应原文**逐个落盘（诊断 LLM 问题的唯一可靠手段）。
// ⚠️ 请求体里**不含 apiKey**（它在 HTTP header 里）—— 所以 dump 出去是安全的；关闭时零开销。
[[nodiscard]] std::string LlmDumpDir() {
    const char* raw = std::getenv("SHINE_LLM_DUMP");
    return (raw == nullptr || *raw == '\0') ? std::string{} : std::string{raw};
}

void DumpLlmText(const char* kind, std::string_view text) {
    const std::string dir = LlmDumpDir();
    if (dir.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    static std::atomic<int> seq{0};
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    const auto path = std::filesystem::path{dir} / fmt::format("{}_{}_{}.txt", ms, kind,
                                                              seq.fetch_add(1));
    if (!util::WriteFileBytes(path, text)) {
        log::Warn("LLM dump 写入失败：{}", util::PathToUtf8(path));
    }
}

std::string BuildChatBody(const ChatRequest& req) {
    if (req.model.empty()) return {};
    std::string body = "{\"model\":\"" + EscapeJson(req.model) + "\",\"messages\":[";
    bool first = true;
    if (!req.system.empty()) {
        body += "{\"role\":\"system\",\"content\":\"" + EscapeJson(req.system) + "\"}";
        first = false;
    }
    for (const auto& m : req.messages) {
        if (!first) body += ",";
        first = false;
        body += "{\"role\":\"" + EscapeJson(m.role) + "\",\"content\":\"" + EscapeJson(m.content) +
                "\"}";
    }
    body += "]";
    body += fmt::format(",\"temperature\":{},\"max_completion_tokens\":{}", req.temperature,
                        req.maxCompletionTokens);
    if (req.stream) {
        body += ",\"stream\":true";
    }
    if (req.disableThinking) {
        // MiniMax-M3
        body += R"(,"thinking":{"type":"disabled"})";
    }
    body += "}";
    return body;
}

std::expected<std::string, ApiError>
ChatComplete(std::string_view baseUrl, std::string_view apiKey, const ChatRequest& req,
             std::chrono::seconds timeout, const std::atomic<bool>* cancel) {
    if (apiKey.empty()) {
        return std::unexpected(MakeErr(
            0, "no_key",
            fmt::format("未配置 {} API 密钥（设置或环境变量）", ProviderLabel(ResolveActiveProfile().provider))));
    }
    if (cancel && cancel->load()) {
        return std::unexpected(MakeErr(0, "cancelled", "已取消"));
    }
    const std::string url = ChatCompletionsUrl(baseUrl);
    const std::string body = BuildChatBody(req);
    if (body.empty()) {
        return std::unexpected(MakeErr(0, "json", "构造 chat body 失败"));
    }
    // S38：**大小分解**（用户问"为什么会提交这么多字符"—— 先得看得见是哪部分撑大的）
    const std::size_t sysChars = req.system.size();
    std::size_t userChars = 0;
    for (const auto& m : req.messages) {
        userChars += m.content.size();
    }
    log::Info("ChatComplete → {} model={} · 请求 {} 字符（system {} / messages {}）", url, req.model,
              body.size(), sysChars, userChars);
    // S38：dump（`SHINE_LLM_DUMP` 开了才有动作）—— 请求体**不含 apiKey**（它在 header），可安全落盘
    DumpLlmText("req", body);
    const HttpTransportResult http = PostJson(url, apiKey, body, timeout);
    DumpLlmText("resp", fmt::format("status={} error={}\n\n{}", http.status, http.error, http.body));
    if (cancel && cancel->load()) {
        return std::unexpected(MakeErr(0, "cancelled", "已取消"));
    }
    if (http.status == 0 && !http.error.empty() && http.body.empty()) {
        const bool isTimeout = http.error.find("超时") != std::string::npos;
        return std::unexpected(MakeErr(0, isTimeout ? "timeout" : "network", http.error));
    }
    if (http.status < 200 || http.status >= 300) {
        return std::unexpected(ParseErrorBody(http.status, http.body));
    }
    std::string content = ExtractChatContent(http.body);
    if (content.empty()) {
        // 有的兼容层直接回纯文本
        content = http.body;
    }
    return content;
}

std::expected<std::string, ApiError>
ChatStream(std::string_view baseUrl, std::string_view apiKey, const ChatRequest& reqIn,
           const ChatDeltaFn& on_delta, std::chrono::seconds timeout,
           const std::atomic<bool>* cancel) {
    if (apiKey.empty()) {
        return std::unexpected(MakeErr(0, "no_key", "未配置 API 密钥"));
    }
    ChatRequest req = reqIn;
    req.stream = true;
    const std::string url = ChatCompletionsUrl(baseUrl);
    const std::string body = BuildChatBody(req);
    SseParser parser; // 通用 data: 行
    std::string acc;
    bool cancelled = false;
    const HttpTransportResult http = PostSse(
        url, apiKey, body, timeout, [&](std::string_view chunk) -> bool {
            if (cancel && cancel->load()) {
                cancelled = true;
                return false;
            }
            // Chat SSE：data: {json}\n\n，无 event 行也可
            std::string buf{chunk};
            for (;;) {
                auto nl = buf.find('\n');
                if (nl == std::string::npos) break;
                std::string_view line{buf.data(), nl};
                buf.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
                if (!line.starts_with("data:")) continue;
                auto payload = line.substr(5);
                if (!payload.empty() && payload.front() == ' ') payload.remove_prefix(1);
                if (payload == "[DONE]") continue;
                // delta.content
                yyjson_doc* doc = yyjson_read(payload.data(), payload.size(), 0);
                if (!doc) continue;
                yyjson_val* root = yyjson_doc_get_root(doc);
                if (yyjson_val* choices = util::json::GetArr(root, "choices")) {
                    if (yyjson_val* c0 = yyjson_arr_get(choices, 0)) {
                        if (yyjson_val* delta = util::json::GetObj(c0, "delta")) {
                            const auto t = util::json::GetStrCopy(delta, "content");
                            if (!t.empty()) {
                                acc += t;
                                if (on_delta) on_delta(t);
                            }
                        }
                    }
                }
                yyjson_doc_free(doc);
            }
            return true;
        });
    if (cancelled || (cancel && cancel->load())) {
        return std::unexpected(MakeErr(0, "cancelled", "已取消"));
    }
    if (http.status < 200 || http.status >= 300) {
        return std::unexpected(ParseErrorBody(http.status, http.body));
    }
    (void)parser;
    return acc;
}

std::expected<std::string, ApiError>
LlmComplete(std::string_view instructions, std::string_view userText, std::chrono::seconds timeout,
            const std::atomic<bool>* cancel, std::string_view model) {
    const LlmProfile p = ResolveActiveProfile();
    // S16（`09` §2.4 模型分层）：空 = profile 默认模型；非空 = 按阶段指定的模型
    const std::string mdl = model.empty() ? p.model : std::string{model};
    // Anthropic Messages（MiMo / MiniMax 兼容端；OpenAI 官方无此协议时走错误提示）
    if (p.protocol == Protocol::Anthropic ||
        Settings().llmProtocol == "anthropic") {
        return AnthropicLlmComplete(instructions, userText, timeout, cancel, mdl);
    }
    if (p.protocol == Protocol::Responses) {
        // S38：**协议由用户显式选**（`llmProtocol=responses`）—— 不再限定 OpenAI。
        // ⚠️ 原先写的是 `&& p.provider == Provider::OpenAi` ⇒ 把 MiniMax 挡在外面：用户在设置里
        //    把协议切到 `responses` 也**走不到**这条路（真 bug）。而 MiniMax 官方**有** Responses
        //    兼容端（`/v1/responses`，响应带顶层 `output_text` 与 `output[].content[].text`）——
        //    那正是 `ExtractOutputText` 认的形状，协议**天然对齐**；且它用 `max_output_tokens`
        //    （不像 Chat 兼容端卡在 4096）。
        Client c(p.baseUrl, p.apiKey, mdl);
        CreateRequest req;
        req.model = mdl;
        req.instructions = std::string{instructions};
        req.maxOutputTokens = 16384; // S38：Responses 的官方字段（Chat 端 4096 的限制不适用）
        yyjson_doc* idoc = yyjson_read(userText.data(), userText.size(), 0);
        if (!idoc) {
            const std::string js = "\"" + EscapeJson(userText) + "\"";
            idoc = yyjson_read(js.data(), js.size(), 0);
        }
        if (!idoc) {
            return std::unexpected(MakeErr(0, "json", "构造 input 失败"));
        }
        req.input = yyjson_doc_get_root(idoc);
        auto r = c.Create(req, timeout);
        yyjson_doc_free(idoc);
        if (!r) {
            return std::unexpected(r.error());
        }
        if (r->raw) {
            size_t len = 0;
            char* t = yyjson_write(r->raw, 0, &len);
            std::string raw;
            if (t) {
                raw.assign(t, len);
                std::free(t);
            }
            yyjson_doc_free(r->raw);
            return raw;
        }
        return r->output_text;
    }

    ChatRequest creq;
    creq.model = mdl;
    creq.system = std::string{instructions};
    creq.messages.push_back({.role = "user", .content = std::string{userText}});
    creq.disableThinking = p.minimaxDisableThinking && p.provider == Provider::MiniMax;
    return ChatComplete(p.baseUrl, p.apiKey, creq, timeout, cancel);
}

bool RunChatSelfCheck() {
    // body 构造
    ChatRequest req;
    req.model = "MiniMax-M3";
    req.system = "你是助手";
    req.messages.push_back({.role = "user", .content = "你好"});
    req.disableThinking = true;
    const std::string body = BuildChatBody(req);
    if (body.find("\"model\":\"MiniMax-M3\"") == std::string::npos ||
        body.find("你好") == std::string::npos || body.find("disabled") == std::string::npos) {
        log::Error("Chat body 自检失败：{}", body);
        return false;
    }
    // content 解析 + 剥 think
    const std::string sample =
        R"({"choices":[{"message":{"role":"assistant","content":"<think>内部推理</think>答案是雪原。"}}]})";
    const auto content = ExtractChatContent(sample);
    if (content.find("雪原") == std::string::npos || content.find("内部推理") != std::string::npos) {
        log::Error("Chat content 自检失败：{}", content);
        return false;
    }
    // Provider 解析
    if (ParseProvider("mimo") != Provider::MiMo || ParseProvider("minimax") != Provider::MiniMax) {
        log::Error("Provider 解析自检失败");
        return false;
    }
    const auto mimo = ResolveProfile(Provider::MiMo);
    if (mimo.baseUrl.find("xiaomimimo") == std::string::npos) {
        log::Error("MiMo base URL 默认值错误：{}", mimo.baseUrl);
        return false;
    }
    const auto mm = ResolveProfile(Provider::MiniMax);
    if (mm.baseUrl.find("minimax") == std::string::npos) {
        log::Error("MiniMax base URL 默认值错误：{}", mm.baseUrl);
        return false;
    }
    log::Info("Chat/Provider 自检通过");
    return true;
}

// ===================== ChatSession =====================

ChatSession::ChatSession(std::string baseUrl, std::string apiKey, std::string model)
    : baseUrl_(std::move(baseUrl)), apiKey_(std::move(apiKey)), model_(std::move(model)) {}

void ChatSession::SetSystem(std::string system) { system_ = std::move(system); }

void ChatSession::SetTools(std::vector<ChatToolDef> tools) { tools_ = std::move(tools); }

void ChatSession::AddUser(std::string_view text) {
    messages_.push_back(
        fmt::format(R"({{"role":"user","content":"{}"}})", EscapeJson(text)));
}

void ChatSession::AppendAssistantRaw(std::string_view rawMessageJson) {
    if (rawMessageJson.empty()) return;
    messages_.emplace_back(rawMessageJson);
}

void ChatSession::AddToolResult(std::string_view toolCallId, std::string_view resultJson) {
    // content 若不是合法 JSON 字符串则包成字符串
    std::string contentField;
    if (!resultJson.empty() && (resultJson.front() == '{' || resultJson.front() == '[' ||
                                resultJson.front() == '"')) {
        contentField = std::string{resultJson};
    } else {
        contentField = "\"" + EscapeJson(resultJson) + "\"";
    }
    messages_.push_back(fmt::format(
        R"({{"role":"tool","tool_call_id":"{}","content":{}}})", EscapeJson(toolCallId),
        contentField));
}

std::string ChatSession::MessagesJson() const {
    std::string arr = "[";
    bool first = true;
    if (!system_.empty()) {
        arr += fmt::format(R"({{"role":"system","content":"{}"}})", EscapeJson(system_));
        first = false;
    }
    for (const auto& m : messages_) {
        if (!first) arr += ",";
        first = false;
        arr += m;
    }
    arr += "]";
    return arr;
}

std::expected<ChatTurnResult, ApiError>
ChatSession::Send(std::chrono::seconds timeout, const std::atomic<bool>* cancel) {
    if (apiKey_.empty()) {
        return std::unexpected(MakeErr(0, "no_key", "未配置 API 密钥"));
    }
    if (cancel && cancel->load()) {
        return std::unexpected(MakeErr(0, "cancelled", "已取消"));
    }
    std::string body = fmt::format(
        R"({{"model":"{}","messages":{},"temperature":1.0,"max_completion_tokens":{})",
        EscapeJson(model_), MessagesJson(), maxTokens_);
    if (disableThinking_) {
        body += R"(,"thinking":{"type":"disabled"})";
    }
    if (jsonObject_) {
        body += R"(,"response_format":{"type":"json_object"})";
    }
    if (!tools_.empty()) {
        body += ",\"tools\":[";
        for (std::size_t i = 0; i < tools_.size(); ++i) {
            if (i) body += ",";
            body += tools_[i].json;
        }
        body += R"(],"tool_choice":"auto")";
    }
    body += "}";

    const std::string url = ChatCompletionsUrl(baseUrl_);
    log::Info("ChatSession.Send → {} msgs={} tools={}", url, messages_.size(), tools_.size());
    const HttpTransportResult http = PostJson(url, apiKey_, body, timeout);
    if (cancel && cancel->load()) {
        return std::unexpected(MakeErr(0, "cancelled", "已取消"));
    }
    if (http.status < 200 || http.status >= 300) {
        if (http.status == 0 && !http.error.empty()) {
            return std::unexpected(MakeErr(0, "network", http.error));
        }
        return std::unexpected(ParseErrorBody(http.status, http.body));
    }
    return ParseChatTurn(http.body);
}

std::expected<ChatTurnResult, ApiError> ParseChatTurn(std::string_view responseBody) {
    yyjson_doc* doc = yyjson_read(responseBody.data(), responseBody.size(), 0);
    if (!doc) {
        return std::unexpected(MakeErr(0, "json", "响应不是 JSON"));
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    ChatTurnResult out;
    if (yyjson_val* choices = util::json::GetArr(root, "choices")) {
        if (yyjson_val* c0 = yyjson_arr_get(choices, 0)) {
            out.finishReason = util::json::GetStrCopy(c0, "finish_reason");
            if (yyjson_val* msg = util::json::GetObj(c0, "message")) {
                // 原样序列化 message（历史回传）
                size_t len = 0;
                char* raw = yyjson_val_write(msg, 0, &len);
                if (raw) {
                    out.rawMessageJson.assign(raw, len);
                    std::free(raw);
                }
                // 展示用 content（剥 think）
                const std::string rawContent = util::json::GetStrCopy(msg, "content");
                out.content = ExtractChatContent(
                    fmt::format(R"({{"choices":[{{"message":{{"content":"{}"}}}}]}})",
                                EscapeJson(rawContent)));
                if (out.content.empty()) {
                    out.content = ExtractChatContent(
                        std::string{R"({"choices":[{"message":{"content":")"} +
                                    EscapeJson(rawContent) + "\"}}]}}");
                }
                // tool_calls
                if (yyjson_val* tcs = util::json::GetArr(msg, "tool_calls")) {
                    size_t i = 0, n = 0;
                    yyjson_val* tc = nullptr;
                    yyjson_arr_foreach(tcs, i, n, tc) {
                        ChatTurnResult::ToolCall call;
                        call.id = util::json::GetStrCopy(tc, "id");
                        if (yyjson_val* fn = util::json::GetObj(tc, "function")) {
                            call.name = util::json::GetStrCopy(fn, "name");
                            call.arguments = util::json::GetStrCopy(fn, "arguments");
                        }
                        if (!call.name.empty()) {
                            out.toolCalls.push_back(std::move(call));
                        }
                    }
                }
            }
        }
    }
    yyjson_doc_free(doc);
    if (out.rawMessageJson.empty()) {
        return std::unexpected(MakeErr(0, "json", "响应缺少 choices[0].message"));
    }
    return out;
}

std::expected<std::string, ApiError>
RunChatToolLoop(std::string_view system, std::string_view userText,
                const std::vector<ChatToolDef>& tools, const ToolExecFn& exec, int maxCalls,
                std::vector<std::string>* callLog, const std::atomic<bool>* cancel) {
    const LlmProfile p = ResolveActiveProfile();
    if (p.apiKey.empty()) {
        return std::unexpected(MakeErr(0, "no_key", "未配置 API 密钥"));
    }
    ChatSession s(p.baseUrl, p.apiKey, p.model);
    s.SetSystem(std::string{system});
    s.SetTools(tools);
    s.SetDisableThinking(p.minimaxDisableThinking && p.provider == Provider::MiniMax);
    s.AddUser(userText);

    int steps = 0;
    int repeat = 0;
    std::string lastKey;
    for (int i = 0; i < maxCalls + 2; ++i) {
        if (cancel && cancel->load()) {
            return std::unexpected(MakeErr(0, "cancelled", "已取消"));
        }
        auto turn = s.Send(kDefaultTimeout, cancel);
        if (!turn) {
            return std::unexpected(turn.error());
        }
        if (turn->toolCalls.empty() || turn->finishReason == "stop") {
            return turn->content;
        }
        // 执行工具
        s.AppendAssistantRaw(turn->rawMessageJson);
        for (const auto& tc : turn->toolCalls) {
            if (++steps > maxCalls) {
                return std::unexpected(MakeErr(0, "max_tools", "工具调用超过上限"));
            }
            const std::string key = tc.name + "|" + tc.arguments;
            if (key == lastKey) {
                if (++repeat >= 3) {
                    return std::unexpected(MakeErr(0, "repeat", "同一工具连续重复，中止"));
                }
            } else {
                repeat = 1;
                lastKey = key;
            }
            if (callLog) {
                callLog->push_back(fmt::format("{}({})", tc.name,
                                               tc.arguments.size() > 40
                                                   ? tc.arguments.substr(0, 40) + "…"
                                                   : tc.arguments));
            }
            std::string result = "{}";
            if (exec) {
                result = exec(tc.name, tc.arguments);
            }
            s.AddToolResult(tc.id, result);
        }
    }
    return std::unexpected(MakeErr(0, "max_tools", "工具循环超出最大迭代"));
}

std::expected<std::string, ApiError> LivePing(int mode) {
    const LlmProfile p = ResolveActiveProfile();
    if (p.apiKey.empty()) {
        return std::unexpected(MakeErr(0, "no_key", "未配置 API 密钥，请先在设置中填写"));
    }
    if (mode == 1) {
        ChatSession s(p.baseUrl, p.apiKey, p.model);
        s.SetSystem("只返回 JSON，不要解释。格式：{\"ok\":true,\"echo\":string}");
        s.SetJsonObject(true);
        s.AddUser("把「雪原」放进 echo 字段");
        auto r = s.Send(std::chrono::seconds{60});
        if (!r) return std::unexpected(r.error());
        return r->content;
    }
    if (mode == 2) {
        ChatToolDef tool;
        tool.json =
            R"({"type":"function","function":{"name":"get_time","description":"获取当前时间","parameters":{"type":"object","properties":{},"required":[]}}})";
        auto r = RunChatToolLoop(
            "你必须调用 get_time 工具后再回答。", "现在几点？", {tool},
            [](std::string_view, std::string_view) {
                return std::string{R"({"time":"2026-02-14T12:00:00Z"})"};
            },
            5);
        if (!r) return std::unexpected(r.error());
        return *r;
    }
    ChatSession s(p.baseUrl, p.apiKey, p.model);
    s.SetDisableThinking(p.minimaxDisableThinking && p.provider == Provider::MiniMax);
    s.AddUser("请用一句中文回答：你是谁？");
    auto r = s.Send(std::chrono::seconds{60});
    if (!r) return std::unexpected(r.error());
    return r->content;
}

bool RunChatSessionSelfCheck() {
    // ParseChatTurn：tool_calls + finish_reason
    const std::string sample = R"({
      "choices": [{
        "finish_reason": "tool_calls",
        "message": {
          "role": "assistant",
          "content": "<think>想一想```",
          "tool_calls": [{
            "id": "call_1",
            "type": "function",
            "function": {"name": "get_entity", "arguments": "{\"id\":12}"}
          }]
        }
      }]
    })";
    auto turn = ParseChatTurn(sample);
    if (!turn || turn->finishReason != "tool_calls" || turn->toolCalls.size() != 1 ||
        turn->toolCalls[0].name != "get_entity" || turn->toolCalls[0].arguments.find("12") == std::string::npos ||
        turn->rawMessageJson.find("tool_calls") == std::string::npos) {
        log::Error("ParseChatTurn 自检失败");
        return false;
    }
    // Session 历史回灌顺序
    ChatSession s("https://api.minimax.cn/v1", "dummy", "MiniMax-M3");
    s.SetSystem("sys");
    s.AddUser("你好");
    s.AppendAssistantRaw(turn->rawMessageJson);
    s.AddToolResult("call_1", R"({"ok":true,"data":{"id":12,"name":"林默"}})");
    const auto msgs = s.MessagesJson();
    if (msgs.find("\"role\":\"tool\"") == std::string::npos ||
        msgs.find("call_1") == std::string::npos ||
        msgs.find("\"role\":\"assistant\"") == std::string::npos) {
        log::Error("ChatSession 历史自检失败：{}", msgs);
        return false;
    }
    // tool 消息应在 assistant 之后
    if (msgs.find("\"role\":\"assistant\"") > msgs.find("\"role\":\"tool\"")) {
        log::Error("ChatSession 历史顺序错误");
        return false;
    }
    log::Info("ChatSession 多轮/工具协议自检通过");
    return true;
}

} // namespace shine::openai
