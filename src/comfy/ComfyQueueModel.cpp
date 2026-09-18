#include "comfy/ComfyQueueModel.h"

#include "util/Time.h"

#include <algorithm>
#include <ranges>

namespace shine::comfy {
namespace {

QueueModel::Row* FindRow(std::vector<QueueModel::Row>& rows, std::string_view id) {
    const auto it = std::ranges::find_if(rows, [id](const QueueModel::Row& r) { return r.promptId == id; });
    return it == rows.end() ? nullptr : &*it;
}

} // namespace

void QueueModel::ApplyQueueResult(const QueueResult& r) {
    if (!r.ok) {
        return;
    }
    std::lock_guard lock(mutex_);
    queueRemaining_ = r.queueRemaining;
    // Keep progress for still-present ids; rebuild membership from server view.
    std::vector<Row> next;
    next.reserve(r.running.size() + r.pending.size());
    for (const auto& e : r.running) {
        Row row;
        if (const Row* old = FindRow(rows_, e.promptId)) {
            row = *old;
        }
        row.promptId = e.promptId;
        row.state = TaskState::Running;
        row.label = "运行中";
        next.push_back(row);
        activePromptId_ = e.promptId;
    }
    if (r.running.empty()) {
        activePromptId_.clear();
    }
    for (const auto& e : r.pending) {
        Row row;
        if (const Row* old = FindRow(rows_, e.promptId)) {
            row = *old;
        }
        row.promptId = e.promptId;
        row.state = TaskState::Pending;
        row.label = "排队";
        next.push_back(row);
    }
    // 保留服务端列表里已经没有、但本地仍有意义的历史行：
    // 已结束的（Done/Failed/Cancelled）留作记录；**仍在跑/排队的也必须留** —— 那正是"漏收了结果事件"的
    // 情形，S6 会用 /history 给它收尾（静默丢弃会让任务永远消失、用户看不到发生了什么）。
    for (const auto& old : rows_) {
        if (FindRow(next, old.promptId)) {
            continue;
        }
        next.push_back(old);
    }
    if (next.size() > 64) {
        next.erase(next.begin(), next.begin() + static_cast<long>(next.size() - 64));
    }
    rows_ = std::move(next);
}

void QueueModel::ApplyPromptEvent(const PromptEvent& ev, const ErrorDetail* detail) {
    std::lock_guard lock(mutex_);
    if (ev.promptId.empty()) {
        return;
    }
    Row* row = FindRow(rows_, ev.promptId);
    if (!row) {
        Row created;
        created.promptId = ev.promptId;
        rows_.push_back(created);
        row = &rows_.back();
    }
    row->lastEventMs = util::MonotonicMillis();   // S6：/history 兜底的宽限判定依据
    if (ev.type == "execution_start") {
        row->state = TaskState::Running;
        row->label = "开始执行";
        activePromptId_ = ev.promptId;
    } else if (ev.type == "executing") {
        if (ev.nodeId.empty()) {
            // §12.1 完成语义：executing 且 data.node == null → 本次执行结束（出错的话已被 execution_error 标过）
            if (row->state == TaskState::Running || row->state == TaskState::Pending) {
                row->state = TaskState::Done;
                row->progress = 1.f;
                row->label = "完成";
                if (activePromptId_ == ev.promptId) {
                    activePromptId_.clear();
                }
            }
        } else {
            row->state = TaskState::Running;
            row->nodeId = ev.nodeId;
            row->nodeType = ev.currentNodeType;
            if (!ev.currentNodeType.empty()) {
                row->label = "节点 " + ev.nodeId + " " + ev.currentNodeType;
            } else {
                row->label = "节点 " + ev.nodeId;
            }
            activePromptId_ = ev.promptId;
        }
    } else if (ev.type == "progress" || ev.type == "progress_state") {
        row->state = TaskState::Running;
        if (!ev.nodeId.empty()) {
            row->nodeId = ev.nodeId;
        }
        row->progressValue = ev.progressValue;
        row->progressMax = ev.progressMax;
        row->progress = ev.progressMax > 0 ? static_cast<float>(ev.progressValue) / static_cast<float>(ev.progressMax) : 0.f;
        activePromptId_ = ev.promptId;
    } else if (ev.type == "execution_cached") {
        row->label = "缓存跳过 " + std::to_string(ev.cachedNodeCount) + " 节点";
    } else if (ev.type == "executed") {
        row->label = ev.imageFileName.empty() ? "节点完成" : ("产出 " + ev.imageFileName);
    } else if (ev.type == "execution_success") {
        row->state = TaskState::Done;
        row->progress = 1.f;
        row->label = "完成";
        if (activePromptId_ == ev.promptId) {
            activePromptId_.clear();
        }
    } else if (ev.type == "execution_error") {
        row->state = TaskState::Failed;
        row->error = ev.errorMessage;
        row->hint = detail && detail->valid ? detail->hint : ErrorHintFor(ev.exceptionType, ev.exceptionMessage);
        row->traceback = ev.traceback;
        row->nodeType = ev.errorNodeType;
        row->label = row->hint.empty() ? "失败" : ("失败：" + row->hint);
        if (!ev.errorNodeId.empty()) {
            row->nodeId = ev.errorNodeId;
        }
        if (activePromptId_ == ev.promptId) {
            activePromptId_.clear();
        }
    } else if (ev.type == "execution_interrupted") {
        // §12.2：已中断 ≠ 失败（状态是 Cancelled，不是 Failed）
        row->state = TaskState::Cancelled;
        row->label = "已中断";
        if (!ev.errorNodeId.empty()) {
            row->nodeId = ev.errorNodeId;
        }
        if (activePromptId_ == ev.promptId) {
            activePromptId_.clear();
        }
    }
}

void QueueModel::AttachErrorDetail(std::string_view promptId, const ErrorDetail& d) {
    if (promptId.empty() || !d.valid) {
        return;
    }
    std::lock_guard lock(mutex_);
    Row* row = FindRow(rows_, promptId);
    if (!row) {
        Row created;
        created.promptId = std::string{promptId};
        rows_.push_back(created);
        row = &rows_.back();
    }
    // 覆盖式：同一 promptId 只保留一份错误，不追加、不重复报
    row->state = TaskState::Failed;
    row->error = !d.exceptionMessage.empty() ? d.exceptionMessage : d.exceptionType;
    row->hint = d.hint;
    row->traceback = d.traceback;
    row->nodeType = d.nodeType;
    if (!d.nodeId.empty()) {
        row->nodeId = d.nodeId;
    }
    row->label = row->hint.empty() ? "失败" : ("失败：" + row->hint);
}

QueueModel::Counts QueueModel::CountsSnapshot() const {
    std::lock_guard lock(mutex_);
    Counts c;
    for (const auto& r : rows_) {
        if (r.state == TaskState::Pending) {
            ++c.pending;
        } else if (r.state == TaskState::Running) {
            ++c.running;
        } else if (r.state == TaskState::Failed) {
            ++c.failed;
        }
    }
    return c;
}

std::string QueueModel::RunningSignature() const {
    std::lock_guard lock(mutex_);
    std::string sig;
    for (const auto& r : rows_) {
        if (r.state == TaskState::Running) {
            sig += r.promptId;
            sig += ',';
        }
    }
    return sig;
}

void QueueModel::ApplyStatus(const StatusEvent& se) {
    std::lock_guard lock(mutex_);
    queueRemaining_ = se.execInfoQueueRemaining;
}

void QueueModel::Clear() {
    std::lock_guard lock(mutex_);
    rows_.clear();
    queueRemaining_ = 0;
    activePromptId_.clear();
}

bool QueueModel::ActiveViewSnapshot(ActiveView& out) const {
    std::lock_guard lock(mutex_);
    for (const auto& r : rows_) {
        if (r.state != TaskState::Running) {
            continue;
        }
        out.promptId = r.promptId;
        out.nodeId = r.nodeId;
        out.nodeType = r.nodeType;
        out.value = r.progressValue;
        out.max = r.progressMax;
        return true;
    }
    return false;
}

std::vector<std::string> QueueModel::TakeStaleRunningIds(const QueueResult& server) {
    std::lock_guard lock(mutex_);
    std::vector<std::string> out;
    const std::int64_t nowMs = util::MonotonicMillis();
    for (auto& r : rows_) {
        if (r.state != TaskState::Running && r.state != TaskState::Pending) {
            continue;
        }
        if (r.outcomeQueried || r.promptId.empty()) {
            continue;
        }
        // 5s 宽限：刚提交时 /queue 可能还没带上该 prompt（避免误判为"漏收"）
        if (r.lastEventMs == 0 || nowMs - r.lastEventMs < 5000) {
            continue;
        }
        const bool onServer =
            std::ranges::any_of(server.running, [&r](const QueueEntry& e) { return e.promptId == r.promptId; }) ||
            std::ranges::any_of(server.pending, [&r](const QueueEntry& e) { return e.promptId == r.promptId; });
        if (onServer) {
            continue;
        }
        r.outcomeQueried = true;
        out.push_back(r.promptId);
    }
    return out;
}

void QueueModel::MarkDone(std::string_view promptId, std::string_view label) {
    std::lock_guard lock(mutex_);
    Row* row = FindRow(rows_, promptId);
    if (!row) {
        return;
    }
    row->state = TaskState::Done;
    row->progress = 1.f;
    row->label = std::string{label};
    if (activePromptId_ == promptId) {
        activePromptId_.clear();
    }
}

std::vector<QueueModel::Row> QueueModel::Snapshot() const {
    std::lock_guard lock(mutex_);
    return rows_;
}

int QueueModel::QueueRemaining() const {
    std::lock_guard lock(mutex_);
    return queueRemaining_;
}

std::string QueueModel::ActivePromptId() const {
    std::lock_guard lock(mutex_);
    return activePromptId_;
}

} // namespace shine::comfy
