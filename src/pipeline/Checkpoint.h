#pragma once
#include "pipeline/StageMachine.h"

#include <filesystem>
#include <string>

namespace shine::pipeline {

struct Checkpoint {
    int chapter = 0;
    StageId next_stage = StageId::T1;
    std::string input_state_hash;
    std::string manifest_path;

    bool Save(const std::filesystem::path& path) const;
    static Checkpoint Load(const std::filesystem::path& path);
};

} // namespace shine::pipeline
