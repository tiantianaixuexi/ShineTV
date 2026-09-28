#include "flow/BatchRender.h"

#include <algorithm>
#include <utility>

#include <tuple>
namespace shine::flow {

std::int64_t BatchRenderQueue::Add(std::int64_t shot_id, std::string label, std::string flow_name,
                                   BatchPriority priority) {
    BatchJob job;
    job.id = next_id_++;
    job.shot_id = shot_id;
    job.label = std::move(label);
    job.flow_name = std::move(flow_name);
    job.priority = priority;
    job.seq = next_seq_++;
    jobs_.push_back(std::move(job));
    return jobs_.back().id;
}
bool BatchRenderQueue::StartNext() {
    auto it = jobs_.end();
    for (auto candidate = jobs_.begin(); candidate != jobs_.end(); ++candidate) {
        if (candidate->state != BatchState::Pending) continue;
        if (it == jobs_.end() ||
            std::tie(candidate->priority, candidate->seq) < std::tie(it->priority, it->seq)) {
            it = candidate;
        }
    }
    if (it == jobs_.end()) return false;
    it->state = BatchState::Running;
    it->progress = 0;
    return true;
}

bool BatchRenderQueue::Complete(std::int64_t id, int artifacts, std::string degradation) {
    const auto it = std::find_if(jobs_.begin(), jobs_.end(), [id](const BatchJob& j) { return j.id == id; });
    if (it == jobs_.end() || it->state == BatchState::Cancelled) return false;
    const bool has_degradation = !degradation.empty();
    it->state = has_degradation ? BatchState::Degraded : BatchState::Done;
    it->progress = 100;
    it->artifacts = artifacts;
    it->degradation = std::move(degradation);
    if (has_degradation) ledger_.push_back(it->degradation);
    return true;
}

bool BatchRenderQueue::Fail(std::int64_t id, std::string error) {
    const auto it = std::find_if(jobs_.begin(), jobs_.end(), [id](const BatchJob& j) { return j.id == id; });
    if (it == jobs_.end()) return false;
    it->state = BatchState::Failed;
    it->error = std::move(error);
    return true;
}

bool BatchRenderQueue::Retry(std::int64_t id) {
    const auto it = std::find_if(jobs_.begin(), jobs_.end(), [id](const BatchJob& j) { return j.id == id; });
    if (it == jobs_.end()) return false;
    it->state = BatchState::Pending;
    it->progress = 0;
    it->error.clear();
    return true;
}

void BatchRenderQueue::CancelAll() {
    for (auto& job : jobs_) {
        if (job.state == BatchState::Pending) job.state = BatchState::Cancelled;
    }
}

void BatchRenderQueue::Clear() noexcept {
    jobs_.clear();
    ledger_.clear();
    next_id_ = 1;
    next_seq_ = 1;
}

std::vector<BatchJob> BatchRenderQueue::Snapshot() const { return jobs_; }

std::size_t BatchRenderQueue::PendingCount() const {
    return static_cast<std::size_t>(std::count_if(jobs_.begin(), jobs_.end(), [](const BatchJob& j) {
        return j.state == BatchState::Pending;
    }));
}

std::string BatchRenderQueue::Describe() const {
    std::size_t done = 0;
    std::size_t failed = 0;
    std::size_t degraded = 0;
    for (const auto& job : jobs_) {
        done += job.state == BatchState::Done ? 1 : 0;
        failed += job.state == BatchState::Failed ? 1 : 0;
        degraded += job.state == BatchState::Degraded ? 1 : 0;
    }
    return "总数 " + std::to_string(jobs_.size()) + " · 完成 " + std::to_string(done) +
           " · 失败 " + std::to_string(failed) + " · 降级 " + std::to_string(degraded);
}

} // namespace shine::flow
