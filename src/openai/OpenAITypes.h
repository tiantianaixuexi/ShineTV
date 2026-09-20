#pragma once
// shine::openai —— OpenAI Responses 薄客户端类型（契约见 docs/compose/spec/novel-agent.md S2.2）
// 本头无业务；input/tools 由调用方用 yyjson 构造，Client 不拥有其 doc 生命周期。
#include <chrono>
#include <expected>
#include <functional>
#include <string>
#include <string_view>

#include <yyjson.h>

namespace shine::openai {

struct ApiError {
    int http_status = 0;   // 0 = 传输层失败（超时/连不上）
    std::string code;      // API code，或 timeout / network / json / no_key
    std::string message;   // 中文可展示；**绝不包含密钥**
};

struct CreateRequest {
    std::string model;
    std::string instructions;
    // input / tools：调用方持有的 yyjson_val*（可空）；Client 只读，不 free
    yyjson_val* input = nullptr;
    yyjson_val* tools = nullptr;
    bool store = false;
    std::string previous_response_id;
    // S38：**输出上限**（Responses 的官方字段名是 `max_output_tokens`，见 `responses-create` 文档）。
    // 0 = 不写该字段（用服务端默认）；>0 才写。⚠️ Chat Completions 兼容端实测卡在 4096，
    // 而 Responses 端没有这个限制 —— 这也是"让 MiniMax 走 Responses"的动机之一。
    int maxOutputTokens = 0;
};

struct CreateResult {
    std::string id;
    std::string status;
    std::string output_text; // 拼好的 message 文本
    yyjson_doc* raw = nullptr; // 完整响应文档，**调用方 yyjson_doc_free**
};

struct StreamEvent {
    enum class Type { Created, TextDelta, Completed, Error, Other };
    Type type = Type::Other;
    std::string text;    // TextDelta 时为增量文本
    std::string message; // Error 时为中文说明
};

using StreamCallback = std::function<void(const StreamEvent&)>;
using StreamDoneCallback = std::function<void(std::expected<void, ApiError>)>;

// 默认超时：同步 Create 偏长（长文生成）；SSE 同级
inline constexpr std::chrono::seconds kDefaultTimeout{120};
inline constexpr std::chrono::seconds kDefaultConnectTimeout{15};

} // namespace shine::openai
