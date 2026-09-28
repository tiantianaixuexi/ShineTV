#pragma once
#include "pipeline/Budget.h"
#include "pipeline/StageMachine.h"

#include <filesystem>
#include <string>
#include <vector>

namespace shine::pipeline {

struct LedgerEntry {
    StageId stage = StageId::T1;
    std::string input_hash;
    std::string output_path;
    std::string degradation;
};

class Ledger {
  public:
    void Record(StageId stage, std::string input_hash, std::string output_path,
                std::string degradation = {});
    bool Flush(const std::filesystem::path& root, const Budget& budget) const;
    [[nodiscard]] const std::vector<LedgerEntry>& Entries() const noexcept { return entries_; }

  private:
    std::vector<LedgerEntry> entries_;
};

} // namespace shine::pipeline
