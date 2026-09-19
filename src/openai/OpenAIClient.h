#pragma once
// OpenAI Responses 同步客户端（契约 S2.2）+ Chat Completions（MiMo/MiniMax）。
// **仅 worker 线程**。
#include "openai/OpenAITypes.h"

#include <atomic>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace shine::openai {

// 从完整响应 JSON 解析 CreateResult（output 数组 type=message 的 output_text）。
// raw 所有权：调用方负责 yyjson_doc_free。
[[nodiscard]] std::expected<CreateResult, ApiError> ParseCreateResponse(std::string_view body);

// 从 HTTP 错误体解析 ApiError（OpenAI `error.message` / `error.code`）。
[[nodiscard]] ApiError ParseErrorBody(int httpStatus, std::string_view body);

class Client {
public:
    // baseUrl / apiKey / defaultModel 为空时从 Settings/环境变量解析
    Client() = default;
    Client(std::string baseUrl, std::string apiKey, std::string model);

    // 同步 POST /v1/responses。解析失败按 spec 重试 1 次。
    [[nodiscard]] std::expected<CreateResult, ApiError>
    Create(const CreateRequest& req, std::chrono::seconds timeout = kDefaultTimeout);

    // SSE 流式 Responses；**阻塞至完成**，回调在本线程触发（调用方用 RunOnWorker）。
    void Stream(const CreateRequest& req, StreamCallback on_event,
                StreamDoneCallback on_done, std::chrono::seconds timeout = kDefaultTimeout);

private:
    std::string baseUrl_;
    std::string apiKey_;
    std::string defaultModel_;

    [[nodiscard]] std::string EffectiveModel(std::string_view model) const;
    [[nodiscard]] std::string EffectiveKey() const;
    [[nodiscard]] std::string EffectiveUrl() const;
};

// 构造请求 body（yyjson_mut 拼 {model,instructions,input,store,tools?,previous_response_id?}）
[[nodiscard]] std::string BuildCreateBody(const CreateRequest& req, bool stream);

// ---- Chat Completions（MiMo / MiniMax / OpenAI 兼容）----
struct ChatMessage {
    std::string role; // system|user|assistant
    std::string content;
};

struct ChatRequest {
    std::string model;
    std::string system;
    std::vector<ChatMessage> messages;
    double temperature = 1.0;
    int maxCompletionTokens = 4096;
    bool stream = false;
    bool disableThinking = false; // MiniMax-M3
};

// 解析 choices[0].message.content（剥 <think>…`）
[[nodiscard]] std::string ExtractChatContent(std::string_view body);

// 构造 /chat/completions body
[[nodiscard]] std::string BuildChatBody(const ChatRequest& req);

// 一次同步 chat 调用。cancel 非空且 *cancel=true 时尽快返回错误。
[[nodiscard]] std::expected<std::string, ApiError>
ChatComplete(std::string_view baseUrl, std::string_view apiKey, const ChatRequest& req,
             std::chrono::seconds timeout = kDefaultTimeout,
             const std::atomic<bool>* cancel = nullptr);

// 按当前 Settings 的 llmProvider 走通（解析 profile → ChatComplete 或 Responses）
// S16（`09` §2.4 模型分层）：`model` 空 = 用 profile 的默认模型；非空 = 按**阶段**指定的模型
// （`ResolveModel("planner"|"writer"|"critic")`）。
[[nodiscard]] std::expected<std::string, ApiError>
LlmComplete(std::string_view instructions, std::string_view userText,
            std::chrono::seconds timeout = kDefaultTimeout,
            const std::atomic<bool>* cancel = nullptr, std::string_view model = {});

// Chat 流式：on_delta 文本增量
using ChatDeltaFn = std::function<void(std::string_view)>;
[[nodiscard]] std::expected<std::string, ApiError>
ChatStream(std::string_view baseUrl, std::string_view apiKey, const ChatRequest& req,
           const ChatDeltaFn& on_delta, std::chrono::seconds timeout = kDefaultTimeout,
           const std::atomic<bool>* cancel = nullptr);

// ---- 多轮会话 + 工具调用（OpenAI Chat Completions 协议）----
// 单条工具定义（OpenAI tools 数组元素 JSON）
struct ChatToolDef {
    std::string json; // {"type":"function","function":{...}}
};

// 解析一轮 Chat 响应
struct ChatTurnResult {
    std::string finishReason; // stop | tool_calls | ...
    std::string content;      // 已剥 <think> 的展示文本（历史回传请用 rawMessageJson）
    std::string rawMessageJson; // assistant message 原样（含 tool_calls / 思考），下一轮必须回传
    struct ToolCall {
        std::string id;
        std::string name;
        std::string arguments; // JSON 字符串
    };
    std::vector<ToolCall> toolCalls;
};

[[nodiscard]] std::expected<ChatTurnResult, ApiError>
ParseChatTurn(std::string_view responseBody);

// 多轮会话：维护 messages 历史，支持 tools 循环
class ChatSession {
public:
    ChatSession() = default;
    ChatSession(std::string baseUrl, std::string apiKey, std::string model);

    void SetSystem(std::string system);
    void SetTools(std::vector<ChatToolDef> tools);
    void SetDisableThinking(bool v) noexcept { disableThinking_ = v; }
    void SetJsonObject(bool v) noexcept { jsonObject_ = v; }
    void SetMaxTokens(int n) noexcept { maxTokens_ = n; }

    // 追加 user 文本
    void AddUser(std::string_view text);

    // 跑一轮（不含工具执行）。调用方负责：若 toolCalls 非空 → Execute → AddToolResult → 再 Send
    [[nodiscard]] std::expected<ChatTurnResult, ApiError>
    Send(std::chrono::seconds timeout = kDefaultTimeout,
         const std::atomic<bool>* cancel = nullptr);

    // 回灌：assistant 原样 + tool 结果
    void AppendAssistantRaw(std::string_view rawMessageJson);
    void AddToolResult(std::string_view toolCallId, std::string_view resultJson);

    // 打包 messages JSON 数组
    [[nodiscard]] std::string MessagesJson() const;
    [[nodiscard]] const std::vector<std::string>& messages() const noexcept { return messages_; }

private:
    std::string baseUrl_;
    std::string apiKey_;
    std::string model_;
    std::string system_;
    std::vector<ChatToolDef> tools_;
    std::vector<std::string> messages_; // 每个元素是一条完整 message JSON
    bool disableThinking_ = false;
    bool jsonObject_ = false;
    int maxTokens_ = 4096;
};

// 工具执行回调：name + argsJson → resultJson
using ToolExecFn =
    std::function<std::string(std::string_view name, std::string_view argsJson)>;

// 完整 Chat 工具循环（当前 Provider）。返回最终 assistant 文本。
// callLog 若非空会记录 name(args摘要)
[[nodiscard]] std::expected<std::string, ApiError>
RunChatToolLoop(std::string_view system, std::string_view userText,
                const std::vector<ChatToolDef>& tools, const ToolExecFn& exec,
                int maxCalls = 20, std::vector<std::string>* callLog = nullptr,
                const std::atomic<bool>* cancel = nullptr);

// 真实联调：发一条极短请求。有 Key 才成功。
// mode: 0=简单文本 1=JSON 结构化 2=带一个假工具的 tool_calls
[[nodiscard]] std::expected<std::string, ApiError> LivePing(int mode = 0);

// P1.4 自检
[[nodiscard]] bool RunParseSelfCheck();
[[nodiscard]] bool RunOfflineSelfCheck();
[[nodiscard]] bool RunChatSelfCheck();
// 多轮/工具协议离线自检
[[nodiscard]] bool RunChatSessionSelfCheck();

} // namespace shine::openai
