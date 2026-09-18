#pragma once
#include "comfy/ComfyTypes.h"

#include <functional>
#include <string>
#include <string_view>

namespace shine::comfy {

// High-level REST helpers. All *Async methods hop to worker via stdexec,
// then PostToUi so callbacks run on the UI thread.
// 回调用 std::move_only_function：允许持有 move-only 状态，避免为进 std::function 多一次拷贝。

using QueueCb = std::move_only_function<void(QueueResult)>;
using ObjectInfoCb = std::move_only_function<void(ObjectInfoResult)>;
using PromptCb = std::move_only_function<void(PromptSubmitResult)>;
using HistoryCb = std::move_only_function<void(HistoryResult)>;
using OpCb = std::move_only_function<void(OperationResult)>;
using StatsCb = std::move_only_function<void(SystemStatsResult)>;

// P3.0 S6：`GET /history/{prompt_id}` 的结论（成功 / 失败 / 未找到）—— WS 漏收结果事件时用它收尾
struct HistoryOutcome {
    bool found = false;
    bool failed = false;
    ErrorDetail error;   // failed 时尽力填全（status.messages 可能已被清理）
};
using HistoryOutcomeCb = std::move_only_function<void(HistoryOutcome)>;

void FetchQueueAsync(std::string_view baseUrl, QueueCb cb);
void FetchObjectInfoAsync(std::string_view baseUrl, ObjectInfoCb cb);
void FetchHistoryAsync(std::string_view baseUrl, int maxItems, HistoryCb cb);
// P3.0 S6：`GET /history/{prompt_id}` → `status.status_str` + `status.messages` → HistoryOutcome
void FetchHistoryOutcomeAsync(std::string_view baseUrl, std::string_view promptId, HistoryOutcomeCb cb);
void SubmitPromptAsync(std::string_view baseUrl, const PromptSubmitRequest& req, PromptCb cb);
void InterruptAsync(std::string_view baseUrl, OpCb cb);
void FreeVramAsync(std::string_view baseUrl, OpCb cb);
void FetchSystemStatsAsync(std::string_view baseUrl, StatsCb cb);
void PingAsync(std::string_view baseUrl, OpCb cb);

// —— P4.1 S6：`/view` 取图（二进制）——
struct BinaryResult {
    bool ok = false;
    std::string error;
    std::string bytes; // 原始文件字节（PNG/…）
};
using BinaryCb = std::move_only_function<void(BinaryResult)>;

// `/view?filename=…&subfolder=…&type=output`（文件名做百分号转义）
[[nodiscard]] std::string BuildViewUrl(std::string_view baseUrl, std::string_view fileName,
                                       std::string_view subfolder, std::string_view type);
// worker 下载 → PostToUi 回调（MEMORY.md 异步任务规范）
void FetchViewAsync(std::string_view baseUrl, std::string_view fileName, std::string_view subfolder,
                    std::string_view type, BinaryCb cb);

// Synchronous parse helpers (yyjson), used by WS and HTTP paths.
// 注：保留 bool + 出参的"形状"，只现代化参数类型 —— 存量 API 不重构形状，避免连锁改动（Plan/PLAN.md §5.0）。
[[nodiscard]] bool ParseQueueJson(std::string_view json, QueueResult& out);
[[nodiscard]] bool ParseHistoryJson(std::string_view json, int maxItems, HistoryResult& out);
[[nodiscard]] bool ParseObjectInfoJson(std::string_view json, ObjectInfoResult& out);
[[nodiscard]] bool ParseSystemStatsJson(std::string_view json, SystemStatsResult& out);
[[nodiscard]] bool ParsePromptSubmitJson(std::string_view json, PromptSubmitResult& out);
// `/history/{prompt_id}` 的 `status.messages`（`[事件名, 数据]` 二元组数组）→ ErrorDetail
[[nodiscard]] bool ParseHistoryErrorDetail(std::string_view historyJson, std::string_view promptId, ErrorDetail& out);

[[nodiscard]] std::string ClassifyMediaKind(std::string_view fileName);

} // namespace shine::comfy
