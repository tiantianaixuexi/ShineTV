#pragma once
// P07-S10：批量出图的本地队列与降级账（不依赖 Comfy 连接）。
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace shine::flow {

enum class BatchPriority { Asset = 0, SceneImage = 10, ShotVideo = 20 };
enum class BatchState { Pending, Running, Done, Failed, Cancelled, Degraded };

struct BatchJob {
    std::int64_t id = 0;
    std::int64_t shot_id = 0;
    std::string label;
    std::string flow_name;
    BatchPriority priority = BatchPriority::SceneImage;
    BatchState state = BatchState::Pending;
    int progress = 0;
    int artifacts = 0;
    std::string error;
    std::string degradation;
    std::int64_t seq = 0;
};

class BatchRenderQueue {
  public:
    std::int64_t Add(std::int64_t shot_id, std::string label, std::string flow_name,
                     BatchPriority priority = BatchPriority::SceneImage);
    bool StartNext();
    bool Complete(std::int64_t id, int artifacts, std::string degradation = {});
    bool Fail(std::int64_t id, std::string error);
    bool Retry(std::int64_t id);
    void CancelAll();
    void Clear() noexcept;
    [[nodiscard]] std::vector<BatchJob> Snapshot() const;
    [[nodiscard]] std::size_t PendingCount() const;
    [[nodiscard]] std::string Describe() const;
    [[nodiscard]] const std::vector<std::string>& DegradationLedger() const noexcept { return ledger_; }

  private:
    std::vector<BatchJob> jobs_;
    std::vector<std::string> ledger_;
    std::int64_t next_id_ = 1;
    std::int64_t next_seq_ = 1;
};

} // namespace shine::flow
