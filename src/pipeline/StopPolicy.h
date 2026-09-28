#pragma once
#include "pipeline/Budget.h"

#include <string>

namespace shine::pipeline {

struct StopDecision {
    bool stop = false;
    std::string rule;
    std::string reason;
};

class StopPolicy {
  public:
    StopDecision Evaluate(const Budget& budget, bool has_comfy, bool has_llm, bool cross_review,
                          bool committed_scenes) const;
};

} // namespace shine::pipeline
