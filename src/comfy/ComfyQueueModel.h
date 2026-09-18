#pragma once
#include "comfy/ComfyTypes.h"

#include <mutex>
#include <string>
#include <vector>

namespace shine::comfy {

// Thread-safe local view of Comfy queue + running progress for the bottom panel.
class QueueModel {
public:
    void ApplyQueueResult(const QueueResult& r);
    void ApplyPromptEvent(const PromptEvent& ev, const ErrorDetail* detail = nullptr);
    void ApplyStatus(const StatusEvent& se);
    // /history 补漏（S6）：**覆盖式**写入该 promptId 的错误详情，不产生重复错误
    void AttachErrorDetail(std::string_view promptId, const ErrorDetail& d);
    void Clear();

    struct Row {
        std::string promptId;
        std::string label;
        TaskState state = TaskState::Pending;
        float progress = 0.f; // 0..1
        int progressValue = 0;
        int progressMax = 0;
        std::string nodeId;
        std::string nodeType;
        std::string error;                   // 原始错误消息
        std::string hint;                    // 中文可操作建议
        std::vector<std::string> traceback;  // 前几行（UI tooltip 展开）
        std::int64_t lastEventMs = 0;        // 最近一次收到该 prompt 事件的时间（单调 ms）
        bool outcomeQueried = false;         // 是否已用 /history 查过结论（S6，避免重复请求）
    };

    struct Counts {
        int pending = 0;
        int running = 0;
        int failed = 0;
        [[nodiscard]] bool busy() const noexcept { return pending > 0 || running > 0; }
    };

    [[nodiscard]] std::vector<Row> Snapshot() const;
    [[nodiscard]] Counts CountsSnapshot() const;
    // 当前 running 的 promptId 拼串（S4 的"queue_running 内容未变化"判定用）
    [[nodiscard]] std::string RunningSignature() const;

    // 当前执行中那条的轻量视图（状态栏/健康摘要每帧要用，避免 Snapshot 全量拷贝）
    struct ActiveView {
        std::string promptId, nodeId, nodeType;
        int value = 0;
        int max = 0;
    };
    [[nodiscard]] bool ActiveViewSnapshot(ActiveView& out) const;

    // P3.0 S6：取出"本地在跑/排队、但服务端队列里已经没有了"的 promptId（WS 漏收结果的信号），
    // 并把它们标记为"已查询"，避免每个刷新周期重复请求 /history。
    std::vector<std::string> TakeStaleRunningIds(const QueueResult& server);
    // /history 显示成功时的收尾（本地仍在 Running 的行）
    void MarkDone(std::string_view promptId, std::string_view label);

    [[nodiscard]] int QueueRemaining() const;
    [[nodiscard]] std::string ActivePromptId() const;

private:
    mutable std::mutex mutex_;
    std::vector<Row> rows_;
    int queueRemaining_ = 0;
    std::string activePromptId_;
};

} // namespace shine::comfy
