#include "pipeline/Runner.h"

#include "util/File.h"

#include <algorithm>

namespace shine::pipeline {

void Runner::Configure(std::filesystem::path root, RunMode mode, StageExecutor executor, HashProvider hash) {
    root_ = std::move(root);
    mode_ = mode;
    executor_ = std::move(executor);
    hash_provider_ = std::move(hash);
    current_ = StageId::T1;
}

RunResult Runner::RunNext() {
    RunResult result;
    result.stage = current_;
    const auto index = static_cast<int>(current_);
    if (index >= static_cast<int>(AllStages().size())) {
        // 已跑完：越界不再前进，停在上一个合法阶段（"已完成"由调用方判断）
        current_ = static_cast<StageId>(static_cast<int>(AllStages().size()) - 1);
        result.ok = true;
        return result;
    }
    if (current_ != StageId::T1 && !CanEnter(current_, static_cast<StageId>(index - 1))) {
        result.stopped = true;
        result.reason = TransitionError(static_cast<StageId>(index - 1), current_);
        return result;
    }
    const std::string hash = hash_provider_ ? hash_provider_(current_) : StageCode(current_);
    const std::string key = StageCode(current_);
    if (const auto it = completed_hashes_.find(key); it != completed_hashes_.end() && it->second == hash) {
        ++result.reused;
        current_ = static_cast<StageId>(index + 1);
        result.ok = true;
        return result;
    }
    if (!budget_.ConsumeLlm(static_cast<int>(current_) >= 18, 0.01)) {
        result.stopped = true;
        result.reason = budget_.Reason();
        return result;
    }
    std::string error;
    if (!executor_ || !executor_(current_, hash, error)) {
        result.stopped = true;
        result.reason = error.empty() ? "阶段执行失败" : error;
        return result;
    }
    const auto output = root_ / "work" / (StageCode(current_) + ".json");
    std::error_code ec;
    std::filesystem::create_directories(output.parent_path(), ec);
    (void)util::WriteFileBytes(output, "{\"stage\":\"" + StageCode(current_) + "\",\"input_hash\":\"" + hash + "\"}\n");
    ledger_.Record(current_, hash, output.string());
    completed_hashes_[key] = hash;
    ++result.executed;
    current_ = static_cast<StageId>(index + 1);
    result.ok = true;
    return result;
}

RunResult Runner::RunStage(StageId stage) {
    const StageId previous = current_;
    current_ = stage;
    const auto result = RunNext();
    current_ = previous;
    return result;
}

RunResult Runner::RunAll() {
    RunResult aggregate;
    for (std::size_t i = 0; i < AllStages().size(); ++i) {
        const auto one = RunNext();
        aggregate.executed += one.executed;
        aggregate.reused += one.reused;
        aggregate.stage = one.stage;
        if (!one.ok) {
            aggregate.stopped = true;
            aggregate.reason = one.reason;
            return aggregate;
        }
    }
    // 全部阶段跑完后 current_ 会递增到 AllStages().size()（越界），
    // StageCode() 于是返回 "UNKNOWN"，UI 上就出现「当前阶段 UNKNOWN」。
    // 这里把它收敛回最后一个合法阶段，"已完成"由调用方看 aggregate.ok 判断。
    const auto last = static_cast<int>(AllStages().size()) - 1;
    if (static_cast<int>(current_) > last) {
        current_ = static_cast<StageId>(last);
    }
    aggregate.ok = true;
    return aggregate;
}

bool Runner::SaveCheckpoint(int chapter) const {
    Checkpoint checkpoint{chapter, current_, "", (root_ / "work" / "_manifest.json").string()};
    return checkpoint.Save(root_ / "work" / "checkpoint.json");
}

bool Runner::Resume(int chapter) {
    const auto checkpoint = Checkpoint::Load(root_ / "work" / "checkpoint.json");
    if (checkpoint.chapter < 0) return false;
    current_ = checkpoint.next_stage;
    (void)chapter;
    return true;
}

std::string Runner::Probe() const {
    return "mode=" + std::to_string(static_cast<int>(mode_)) + "; stage=" + StageCode(current_) +
           "; executed_entries=" + std::to_string(ledger_.Entries().size());
}

} // namespace shine::pipeline
