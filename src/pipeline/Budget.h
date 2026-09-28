#pragma once
#include <cstdint>
#include <string>

namespace shine::pipeline {

struct Budget {
    int max_llm_calls = 1000;
    int max_high_quality_calls = 300;
    int max_shots = 500;
    double max_cost = 100.0;
    int llm_calls = 0;
    int high_quality_calls = 0;
    int shots = 0;
    double cost = 0.0;

    bool ConsumeLlm(bool high_quality, double estimated_cost);
    bool ConsumeShot();
    [[nodiscard]] bool Exceeded() const;
    [[nodiscard]] std::string Reason() const;
};

} // namespace shine::pipeline
