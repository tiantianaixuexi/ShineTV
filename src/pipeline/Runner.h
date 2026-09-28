#pragma once
#include "pipeline/Budget.h"
#include "pipeline/Checkpoint.h"
#include "pipeline/Ledger.h"
#include "pipeline/StageMachine.h"

#include <filesystem>
#include <functional>
#include <map>
#include <string>

namespace shine::pipeline {

enum class RunMode { Manual, Semi, Auto };
struct RunResult {
    bool ok = false;
    bool stopped = false;
    StageId stage = StageId::T1;
    std::string reason;
    int executed = 0;
    int reused = 0;
};
using StageExecutor = std::function<bool(StageId, const std::string&, std::string&)>;
using HashProvider = std::function<std::string(StageId)>;

class Runner {
  public:
    void Configure(std::filesystem::path root, RunMode mode, StageExecutor executor, HashProvider hash);
    RunResult RunNext();
    RunResult RunStage(StageId stage);
    RunResult RunAll();
    bool SaveCheckpoint(int chapter) const;
    bool Resume(int chapter);
    [[nodiscard]] StageId CurrentStage() const noexcept { return current_; }
    [[nodiscard]] const Budget& Usage() const noexcept { return budget_; }
    [[nodiscard]] const Ledger& LedgerLog() const noexcept { return ledger_; }
    [[nodiscard]] std::string Probe() const;

  private:
    std::filesystem::path root_;
    RunMode mode_ = RunMode::Manual;
    StageExecutor executor_;
    HashProvider hash_provider_;
    StageId current_ = StageId::T1;
    std::map<std::string, std::string> completed_hashes_;
    Budget budget_;
    Ledger ledger_;
};

} // namespace shine::pipeline
