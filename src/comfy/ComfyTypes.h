#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "comfy/ComfyNodeDef.h" // P3.1：节点定义结构化模型（ObjectInfoResult 持有它）

namespace shine::comfy {

enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Error,
};

[[nodiscard]] const char* ConnectionStateLabel(ConnectionState s) noexcept;

enum class TaskState {
    Pending,
    Running,
    Done,
    Failed,
    Cancelled,
};

[[nodiscard]] const char* TaskStateLabel(TaskState s) noexcept;

struct QueueEntry {
    std::string promptId;
    double number = 0.0;
    int queueIndex = -1;
    std::int64_t createdAtMillis = 0;
    TaskState state = TaskState::Pending;
};

struct QueueResult {
    bool ok = false;
    std::string error;
    int queueRemaining = 0;
    std::vector<QueueEntry> running;
    std::vector<QueueEntry> pending;
};

struct PromptSubmitRequest {
    std::string promptJson; // full API-format graph JSON object
    std::string clientId;
    std::string promptId; // optional empty
    bool front = false;
};

struct NodeError {                     // /prompt 400 → node_errors 条目（P3.0 S7）
    std::string nodeId, nodeType;
    std::string inputName, receivedValue;
    std::string message;               // 中文，可直接显示
    std::string hint;                  // 中文可操作建议
};

struct PromptSubmitResult {
    bool ok = false;
    std::string error;
    std::string promptId;
    double number = 0.0;
    std::string rawNodeErrorsJson;
    std::vector<NodeError> nodeErrors; // 节点级中文错误（S7 填充）
};

// —— P3.0：忙碌状态与错误详情（对应 `Doc/RULES-COMFY.md` §12.4 / §12.3）——
enum class BusyState { Idle, Queued, Running, Interrupted, Stalled };

[[nodiscard]] const char* BusyStateLabel(BusyState s) noexcept;

struct ErrorDetail {
    bool valid = false;
    std::string promptId, nodeId, nodeType;
    std::string exceptionType, exceptionMessage;
    std::vector<std::string> traceback;
    std::string hint;                  // 中文可操作建议
    std::string source;                // ws / history / prompt400（日志与证据用）
};

// 中文建议：按异常类型/消息关键词给。三条错误来源（WS / /history / /prompt 400）共用，避免各写一份。
[[nodiscard]] std::string ErrorHintFor(std::string_view exceptionType, std::string_view message);

struct OperationResult {
    bool ok = false;
    std::string error;
};

struct ObjectInfoResult {
    bool ok = false;
    std::string error;
    std::string rawJson;   // 原始响应（既有语义不变）
    int nodeClassCount = 0; // 「类名 → 定义」映射的条目数（既有语义不变）
    // —— P3.1：结构化节点定义（`ParseObjectInfoJson` 填充；字段语义见 comfy/ComfyNodeDef.h）——
    std::vector<NodeTypeDef> nodes;
    // 整体形状：出现 ≥1 个 V2 形状节点即记 V2（日志/诊断用；单个节点以 NodeTypeDef::format 为准）
    NodeDefFormat format = NodeDefFormat::V1;
};

struct HistoryMedia {
    std::string nodeId;
    std::string fileName;
    std::string subfolder;
    std::string type;
    std::string kind; // image / video / audio / other
};

struct HistoryEntry {
    std::string promptId;
    std::string statusText;
    std::string rawJson;
    std::int64_t createdAtMillis = 0;
    std::int64_t completedAtMillis = 0;
    bool failed = false;
    std::vector<HistoryMedia> media;
};

struct HistoryResult {
    bool ok = false;
    std::string error;
    std::vector<HistoryEntry> entries;
};

struct SystemStatsResult {
    bool ok = false;
    std::string error;
    std::string deviceName;
    double vramTotalBytes = 0.0;
    double vramFreeBytes = 0.0;

    [[nodiscard]] double FreeGb() const noexcept { return vramFreeBytes / (1024.0 * 1024.0 * 1024.0); }
    [[nodiscard]] double TotalGb() const noexcept { return vramTotalBytes / (1024.0 * 1024.0 * 1024.0); }
};

struct PromptEvent {
    std::string type; // execution_start / progress / executed / ...
    std::string promptId;
    std::string nodeId;
    int progressValue = 0;
    int progressMax = 0;
    int cachedNodeCount = 0;
    std::vector<std::string> cachedNodeIds;
    std::string imageFileName;
    std::string imageSubfolder;
    std::string imageType;
    bool failed = false;
    std::string errorMessage;
    std::string errorNodeId;
    std::string errorNodeType;

    // —— 官方 execution_error / executing 全字段（S2 解析时填充）——
    std::string exceptionType;               // 如 torch.cuda.OutOfMemoryError
    std::string exceptionMessage;            // 原始异常消息，不做本地化
    std::vector<std::string> traceback;      // 逐行
    std::vector<std::string> executedNodes;  // executed 事件累积的节点 id
    std::string currentNodeType;             // executing 事件的 class_type（如 KSampler）
    std::int64_t timestampMillis = 0;        // 收到该事件的本机时间（用于帧静默计时）
};

// —— P4.5：WS 二进制预览帧 ——
// 官方示例用 `out[8:]` 取图（前 8 字节头），但**头内字段划分官方未给出** → 我们**不解析头**，
// 只用魔数判断格式；`format` 仅供诊断（详见 Doc/RULES-COMFY.md §12.1）。
struct BinaryFrame {
    std::vector<std::byte> payload; // PNG / JPEG 字节
    std::string format;             // "png" / "jpeg" / ""（未知）
    std::int64_t timestampMillis = 0;
};

struct StatusEvent {
    int execInfoQueueRemaining = 0;
    int execInfoQueueRunning = 0; // usually 0/1 in status payload
};

// Normalize "127.0.0.1:8188" / "http://127.0.0.1:8188/" -> "http://127.0.0.1:8188"
[[nodiscard]] std::string NormalizeBaseUrl(std::string_view baseUrl);
[[nodiscard]] std::string BuildApiUrl(std::string_view baseUrl, std::string_view relativePath);
[[nodiscard]] std::string BuildWebSocketUrl(std::string_view baseUrl, std::string_view clientId);
[[nodiscard]] std::string MakeClientId();
[[nodiscard]] std::string MakePromptId();

} // namespace shine::comfy
