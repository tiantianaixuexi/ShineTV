#include "openai/OpenAIClient.h"

#include "core/Log.h"
#include "openai/OpenAIConfig.h"
#include "openai/OpenAIHttp.h"
#include "openai/OpenAIStream.h"
#include "util/File.h" // S38：dump 落盘
#include "util/Json.h"

#include <fmt/format.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <utility>

namespace shine::openai {
namespace {

[[nodiscard]] std::string JsonQuote(std::string_view s) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (!doc) {
        return "\"\"";
    }
    yyjson_mut_val* v = yyjson_mut_strncpy(doc, s.data(), s.size());
    if (!v) {
        yyjson_mut_doc_free(doc);
        return "\"\"";
    }
    size_t len = 0;
    char* text = yyjson_mut_val_write(v, 0, &len);
    yyjson_mut_doc_free(doc);
    if (!text) {
        return "\"\"";
    }
    std::string out{text, len};
    std::free(text);
    return out;
}

// 序列化只读 val（input/tools 原样嵌入 body）
[[nodiscard]] bool AppendValJson(std::string& out, yyjson_val* v) {
    if (!v) {
        return false;
    }
    size_t len = 0;
    char* text = yyjson_val_write(v, 0, &len);
    if (!text) {
        return false;
    }
    out.append(text, len);
    std::free(text);
    return true;
}

[[nodiscard]] ApiError ChineseError(int httpStatus, std::string code, std::string message) {
    ApiError e;
    e.http_status = httpStatus;
    e.code = std::move(code);
    e.message = std::move(message);
    return e;
}

[[nodiscard]] std::string HintForStatus(int status) {
    switch (status) {
    case 401:
        return "API 密钥无效或已过期（请检查设置或 OPENAI_API_KEY）";
    case 403:
        return "无权限访问该模型或接口";
    case 404:
        return "接口或模型不存在（检查 openaiBaseUrl / 模型名）";
    case 429:
        return "请求过于频繁或配额不足，请稍后再试";
    default:
        if (status >= 500) {
            return "OpenAI 服务端错误，请稍后重试";
        }
        if (status >= 400) {
            return "请求被拒绝";
        }
        return "未知错误";
    }
}

} // namespace

std::string BuildCreateBody(const CreateRequest& req, bool stream) {
    if (req.model.empty()) {
        return {};
    }
    std::string body;
    body.reserve(256);
    body += "{\"model\":" + JsonQuote(req.model);
    if (!req.instructions.empty()) {
        body += ",\"instructions\":" + JsonQuote(req.instructions);
    }
    body += ",\"input\":";
    if (req.input) {
        if (!AppendValJson(body, req.input)) {
            return {};
        }
    } else {
        body += "\"\"";
    }
    body += ",\"store\":";
    body += req.store ? "true" : "false";
    if (stream) {
        body += ",\"stream\":true";
    }
    if (req.tools) {
        body += ",\"tools\":";
        if (!AppendValJson(body, req.tools)) {
            return {};
        }
    }
    if (!req.previous_response_id.empty()) {
        body += ",\"previous_response_id\":" + JsonQuote(req.previous_response_id);
    }
    // S38：`max_output_tokens`（Responses 的官方字段；Chat 那条路的 4096 限制不适用这里）
    if (req.maxOutputTokens > 0) {
        body += ",\"max_output_tokens\":" + std::to_string(req.maxOutputTokens);
    }
    body += "}";
    return body;
}

std::expected<CreateResult, ApiError> ParseCreateResponse(std::string_view body) {
    if (body.empty()) {
        return std::unexpected(ChineseError(0, "json", "响应体为空"));
    }
    yyjson_doc* doc = yyjson_read(body.data(), body.size(), 0);
    if (!doc) {
        return std::unexpected(ChineseError(0, "json", "响应不是合法 JSON"));
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return std::unexpected(ChineseError(0, "json", "响应根节点不是对象"));
    }

    // 错误体：{"error":{"message":...,"code":...}}
    if (yyjson_val* err = util::json::GetObj(root, "error")) {
        const std::string msg = util::json::GetStrCopy(err, "message");
        const std::string code = util::json::GetStrCopy(err, "code");
        yyjson_doc_free(doc);
        return std::unexpected(ChineseError(0, code.empty() ? "api_error" : code,
                                            msg.empty() ? "接口返回错误" : msg));
    }

    CreateResult out;
    out.raw = doc; // 所有权转移给调用方
    out.id = util::json::GetStrCopy(root, "id");
    out.status = util::json::GetStrCopy(root, "status");

    if (yyjson_val* output = util::json::GetArr(root, "output")) {
        size_t idx = 0, max = 0;
        yyjson_val* item = nullptr;
        yyjson_arr_foreach(output, idx, max, item) {
            if (!yyjson_is_obj(item)) {
                continue;
            }
            if (util::json::GetStr(item, "type") != "message") {
                continue;
            }
            yyjson_val* content = util::json::GetArr(item, "content");
            if (!content) {
                continue;
            }
            size_t ci = 0, cm = 0;
            yyjson_val* c = nullptr;
            yyjson_arr_foreach(content, ci, cm, c) {
                if (!yyjson_is_obj(c)) {
                    continue;
                }
                if (util::json::GetStr(c, "type") != "output_text") {
                    continue;
                }
                const std::string_view text = util::json::GetStr(c, "text");
                if (!text.empty()) {
                    if (!out.output_text.empty()) {
                        out.output_text.push_back('\n');
                    }
                    out.output_text.append(text);
                }
            }
        }
    }

    if (out.output_text.empty()) {
        if (const auto t = util::json::GetStr(root, "output_text"); !t.empty()) {
            out.output_text = std::string{t};
        }
    }

    if (out.output_text.empty() && out.status.empty() && out.id.empty()) {
        yyjson_doc_free(doc);
        out.raw = nullptr;
        return std::unexpected(ChineseError(0, "json", "响应缺少 output/id/status"));
    }
    return out;
}

ApiError ParseErrorBody(int httpStatus, std::string_view body) {
    if (!body.empty()) {
        yyjson_doc* doc = yyjson_read(body.data(), body.size(), 0);
        if (doc) {
            yyjson_val* root = yyjson_doc_get_root(doc);
            if (yyjson_val* err = util::json::GetObj(root, "error")) {
                ApiError e;
                e.http_status = httpStatus;
                e.code = util::json::GetStrCopy(err, "code");
                if (e.code.empty()) {
                    e.code = util::json::GetStrCopy(err, "type");
                }
                e.message = util::json::GetStrCopy(err, "message");
                yyjson_doc_free(doc);
                if (e.message.empty()) {
                    e.message = HintForStatus(httpStatus);
                }
                if (httpStatus == 401 || e.code == "invalid_api_key") {
                    e.message = "API 密钥无效：" + e.message;
                }
                return e;
            }
            yyjson_doc_free(doc);
        }
    }
    ApiError e;
    e.http_status = httpStatus;
    e.code = "http_" + std::to_string(httpStatus);
    e.message = HintForStatus(httpStatus);
    return e;
}

Client::Client(std::string baseUrl, std::string apiKey, std::string model)
    : baseUrl_(std::move(baseUrl)), apiKey_(std::move(apiKey)), defaultModel_(std::move(model)) {}

std::string Client::EffectiveModel(std::string_view model) const {
    if (!model.empty()) {
        return std::string{model};
    }
    if (!defaultModel_.empty()) {
        return defaultModel_;
    }
    return ResolveModel("");
}

std::string Client::EffectiveKey() const {
    if (!apiKey_.empty()) {
        return apiKey_;
    }
    return ResolveApiKey();
}

std::string Client::EffectiveUrl() const {
    if (!baseUrl_.empty()) {
        return ResponsesUrl(baseUrl_);
    }
    return ResponsesUrl(Settings().openaiBaseUrl);
}

namespace {
// S38：`SHINE_LLM_DUMP=<dir>` → 请求/响应原文落盘（**Responses 这条路的版本**）。
// `ChatComplete`（Chat Completions 那条）已有一份同款实现；两处分开是因为它们在不同翻译单元，
// 共用就得新建头文件 —— 记账：将来合并到一个 `openai/LlmDump.h`。
void DumpResponsesIo(const char* kind, std::string_view text) {
    const char* raw = std::getenv("SHINE_LLM_DUMP");
    if (raw == nullptr || *raw == '\0') {
        return;
    }
    std::error_code ec;
    const auto dir = std::filesystem::path{raw};
    std::filesystem::create_directories(dir, ec);
    static std::atomic<int> seq{0};
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    (void)util::WriteFileBytes(dir / fmt::format("{}_{}.txt", ms, kind), text);
}
} // namespace

std::expected<CreateResult, ApiError> Client::Create(const CreateRequest& req,
                                                     std::chrono::seconds timeout) {
    const std::string key = EffectiveKey();
    if (key.empty()) {
        return std::unexpected(
            ChineseError(0, "no_key", "未配置 OpenAI API 密钥（设置或环境变量 OPENAI_API_KEY）"));
    }

    CreateRequest r = req;
    if (r.model.empty()) {
        r.model = EffectiveModel("");
    }
    const std::string body = BuildCreateBody(r, /*stream=*/false);
    if (body.empty()) {
        return std::unexpected(ChineseError(0, "json", "构造请求体失败（model/input 非法）"));
    }

    const std::string url = EffectiveUrl();
    log::Info("OpenAI Create → {} model={}（密钥 {}）", url, r.model, MaskKey(key));

    auto attempt = [&](int tryNo) -> std::expected<CreateResult, ApiError> {
        DumpResponsesIo("req", body);
        const HttpTransportResult http = PostJson(url, key, body, timeout);
        DumpResponsesIo("resp", fmt::format("status={} error={}\n\n{}", http.status, http.error,
                                            http.body));
        if (http.status == 0 && !http.error.empty() && http.body.empty()) {
            // 传输层失败
            const bool isTimeout = http.error.find("超时") != std::string::npos;
            return std::unexpected(ChineseError(0, isTimeout ? "timeout" : "network", http.error));
        }
        if (http.status < 200 || http.status >= 300) {
            return std::unexpected(ParseErrorBody(http.status, http.body));
        }
        auto parsed = ParseCreateResponse(http.body);
        if (!parsed) {
            if (tryNo == 0) {
                log::Warn("OpenAI Create 解析失败，重试 1 次：{}", parsed.error().message);
            }
            return parsed;
        }
        if (parsed->raw) {
            // raw 留给调用方；这里仅日志摘要（不打印正文全文，避免刷屏）
            log::Info("OpenAI Create 完成 id={} status={} 文本 {} 字",
                      parsed->id, parsed->status, parsed->output_text.size());
        }
        return parsed;
    };

    auto first = attempt(0);
    if (first) {
        return first;
    }
    // 仅 JSON 解析失败重试 1 次（spec）
    if (first.error().code == "json") {
        return attempt(1);
    }
    return first;
}

void Client::Stream(const CreateRequest& req, StreamCallback on_event, StreamDoneCallback on_done,
                    std::chrono::seconds timeout) {
    if (!on_done) {
        return;
    }
    const std::string key = EffectiveKey();
    if (key.empty()) {
        on_done(std::unexpected(
            ChineseError(0, "no_key", "未配置 OpenAI API 密钥（设置或环境变量 OPENAI_API_KEY）")));
        return;
    }

    CreateRequest r = req;
    if (r.model.empty()) {
        r.model = EffectiveModel("");
    }
    const std::string body = BuildCreateBody(r, /*stream=*/true);
    if (body.empty()) {
        on_done(std::unexpected(ChineseError(0, "json", "构造请求体失败（model/input 非法）")));
        return;
    }

    const std::string url = EffectiveUrl();
    log::Info("OpenAI Stream → {} model={}（密钥 {}）", url, r.model, MaskKey(key));

    SseParser parser;
    bool sawErrorEvent = false;
    std::string lastErrorMessage;
    bool completed = false;
    std::size_t deltaBytes = 0;

    const HttpTransportResult http = PostSse(
        url, key, body, timeout,
        [&](std::string_view chunk) -> bool {
            for (const StreamEvent& ev : parser.Feed(chunk)) {
                if (ev.type == StreamEvent::Type::TextDelta) {
                    deltaBytes += ev.text.size();
                }
                if (ev.type == StreamEvent::Type::Error) {
                    sawErrorEvent = true;
                    lastErrorMessage = ev.message;
                }
                if (ev.type == StreamEvent::Type::Completed) {
                    completed = true;
                }
                if (on_event) {
                    on_event(ev);
                }
            }
            return !sawErrorEvent; // 收到失败事件后停止读
        });

    if (http.status == 0 && !http.error.empty() && http.body.empty()) {
        const bool isTimeout = http.error.find("超时") != std::string::npos;
        on_done(std::unexpected(ChineseError(0, isTimeout ? "timeout" : "network", http.error)));
        return;
    }
    if (http.status < 200 || http.status >= 300) {
        on_done(std::unexpected(ParseErrorBody(http.status, http.body)));
        return;
    }
    if (sawErrorEvent) {
        ApiError e;
        e.http_status = http.status;
        e.code = "stream_failed";
        e.message = lastErrorMessage.empty() ? "流式响应失败" : lastErrorMessage;
        on_done(std::unexpected(std::move(e)));
        return;
    }
    if (!completed && deltaBytes == 0) {
        // 完全没有输出且没有 completed —— 断流/空包
        ApiError e;
        e.http_status = http.status;
        e.code = "stream_incomplete";
        e.message = "流式响应中断：未收到任何文本增量";
        on_done(std::unexpected(std::move(e)));
        return;
    }
    log::Info("OpenAI Stream 完成：增量 {} 字 completed={}", deltaBytes, completed);
    on_done({});
}

bool RunParseSelfCheck() {
    // 手工样例（形状对齐 Responses API message.output_text）
    constexpr std::string_view kSample = R"({
      "id": "resp_selfcheck",
      "object": "response",
      "status": "completed",
      "output": [
        {
          "type": "message",
          "role": "assistant",
          "content": [
            {"type": "output_text", "text": "雪原上只剩下风声。"}
          ]
        }
      ]
    })";
    auto ok = ParseCreateResponse(kSample);
    if (!ok) {
        log::Error("OpenAI 解析自检失败：{}", ok.error().message);
        return false;
    }
    if (ok->output_text != "雪原上只剩下风声。" || ok->id != "resp_selfcheck") {
        log::Error("OpenAI 解析自检失败：文本/ID 不符 id={} text={}", ok->id, ok->output_text);
        if (ok->raw) {
            yyjson_doc_free(ok->raw);
        }
        return false;
    }
    if (ok->raw) {
        yyjson_doc_free(ok->raw);
    }

    // 错误体样例
    auto err = ParseErrorBody(401, R"({"error":{"message":"Incorrect API key","code":"invalid_api_key"}})");
    if (err.http_status != 401 || err.code != "invalid_api_key" || err.message.find("密钥") == std::string::npos) {
        log::Error("OpenAI 错误解析自检失败：status={} code={} msg={}", err.http_status, err.code, err.message);
        return false;
    }
    log::Info("OpenAI 解析自检通过（output_text + error.body）");
    return true;
}

bool RunOfflineSelfCheck() {
    bool ok = true;
    std::string detail;

    // 1) 解析
    if (!RunParseSelfCheck()) {
        ok = false;
        detail += "parse:fail;";
    } else {
        detail += "parse:ok;";
    }

    // 2) body 构造
    {
        CreateRequest req;
        req.model = "gpt-4o-mini";
        req.instructions = "写一句雪原";
        const char* input = "\"写一句雪原\"";
        yyjson_doc* indoc = yyjson_read(input, std::strlen(input), 0);
        req.input = indoc ? yyjson_doc_get_root(indoc) : nullptr;
        const std::string body = BuildCreateBody(req, false);
        if (body.find("\"model\"") == std::string::npos || body.find("\"input\"") == std::string::npos) {
            log::Error("OpenAI body 自检失败：{}", body);
            ok = false;
            detail += "body:fail;";
        } else {
            log::Info("OpenAI body 自检通过（{} 字节）", body.size());
            detail += "body:ok;";
        }
        if (indoc) {
            yyjson_doc_free(indoc);
        }
    }

    // 3) SSE 解析器（样例用 ASCII，避免源文件编码干扰；中文路径由 Create 正文另测）
    {
        SseParser parser;
        const std::string sse =
            "event: response.created\n"
            "data: {\"type\":\"response.created\",\"response\":{\"id\":\"resp_1\"}}\n"
            "\n"
            "event: response.output_text.delta\n"
            "data: {\"type\":\"response.output_text.delta\",\"delta\":\"Snow\"}\n"
            "\n"
            "event: response.output_text.delta\n"
            "data: {\"type\":\"response.output_text.delta\",\"delta\":\"field\"}\n"
            "\n"
            "event: response.completed\n"
            "data: {\"type\":\"response.completed\"}\n"
            "\n";
        auto evs = parser.Feed(sse);
        std::size_t deltas = 0;
        bool sawCompleted = false;
        bool sawCreated = false;
        std::string types;
        for (const auto& e : evs) {
            types += std::to_string(static_cast<int>(e.type)) + "(" + e.message + "/" + e.text + "),";
            if (e.type == StreamEvent::Type::TextDelta) deltas += e.text.size();
            if (e.type == StreamEvent::Type::Created) sawCreated = true;
            if (e.type == StreamEvent::Type::Completed) sawCompleted = true;
        }
        if (!sawCreated || !sawCompleted || deltas != 9 || evs.size() != 4) { // Snow+field=9
            log::Error("OpenAI SSE 自检失败：n={} created={} completed={} delta字节={} types={}",
                       evs.size(), sawCreated, sawCompleted, deltas, types);
            ok = false;
            detail += fmt::format("sse:fail(n={},c={},d={},b={},t={});", evs.size(), sawCreated,
                                  sawCompleted, deltas, types);
        } else {
            log::Info("OpenAI SSE 自检通过（整段 4 事件 + TextDelta）");
            detail += "sse:ok;";
        }

        SseParser p2;
        auto a = p2.Feed(std::string_view{sse}.substr(0, sse.size() / 2));
        auto b = p2.Feed(std::string_view{sse}.substr(sse.size() / 2));
        if (a.size() + b.size() < 3) {
            log::Error("OpenAI SSE 跨 chunk 自检失败：{}+{}", a.size(), b.size());
            ok = false;
            detail += "sse_split:fail;";
        } else {
            detail += "sse_split:ok;";
        }
    }

    // 4) 无密钥：Create 不得发出网络请求
    {
        const std::string savedKey = Settings().openaiApiKey;
        Settings().openaiApiKey.clear();
        if (std::getenv("OPENAI_API_KEY") != nullptr) {
            log::Warn("OpenAI 无密钥自检跳过（环境变量 OPENAI_API_KEY 已设置）");
            detail += "nokey:skip;";
        } else {
            Client c;
            CreateRequest req;
            req.model = "gpt-4o-mini";
            const auto r = c.Create(req, std::chrono::seconds{5});
            if (r || r.error().code != "no_key") {
                log::Error("OpenAI 无密钥自检失败：期望 no_key，得到 {}", r ? "成功" : r.error().code);
                ok = false;
                detail += "nokey:fail;";
            } else if (r.error().message.find("OPENAI_API_KEY") == std::string::npos) {
                log::Error("OpenAI 无密钥自检失败：文案缺少提示 {}", r.error().message);
                ok = false;
                detail += "nokey:msg;";
            } else {
                log::Info("OpenAI 无密钥路径自检通过：{}", r.error().message);
                detail += "nokey:ok;";
            }
        }
        Settings().openaiApiKey = savedKey;
    }

    // 5) 错误体三态可区分（HTTP / JSON）
    {
        const auto e401 = ParseErrorBody(401, R"({"error":{"message":"Incorrect API key provided","code":"invalid_api_key"}})");
        const auto e500 = ParseErrorBody(500, "");
        auto parsedBad = ParseCreateResponse("not-json");
        const auto eJson = parsedBad ? ApiError{} : parsedBad.error();
        if (e401.http_status != 401 || e500.http_status != 500 || eJson.code != "json") {
            log::Error("OpenAI 错误三态自检失败：401={} 500={} json={}", e401.http_status, e500.http_status, eJson.code);
            ok = false;
            detail += "err:fail;";
        } else {
            log::Info("OpenAI 错误三态自检通过（HTTP 401 / HTTP 500 / json）");
            detail += "err:ok;";
        }
    }

    log::Info("OpenAI 离线自检：{} （{}）", ok ? "全部通过" : "存在失败项", detail);

    {
        const char* path = std::getenv("SHINE_OPENAI_CHECK_OUT");
        const std::string outPath = path && *path
                                        ? std::string{path}
                                        : (SettingsPath() + ".openai-check.txt");
        std::string report;
        report += ok ? "PASS\n" : "FAIL\n";
        report += detail;
        report += "\n";
        FILE* f = std::fopen(outPath.c_str(), "wb");
        if (f) {
            std::fwrite(report.data(), 1, report.size(), f);
            std::fclose(f);
            log::Info("OpenAI 自检报告：{}", outPath);
        }
    }
    return ok;
}

} // namespace shine::openai
