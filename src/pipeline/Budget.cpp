#include "pipeline/Budget.h"

namespace shine::pipeline {

bool Budget::ConsumeLlm(bool high_quality, double estimated_cost) {
    if (Exceeded()) return false;
    ++llm_calls;
    if (high_quality) ++high_quality_calls;
    cost += estimated_cost;
    return !Exceeded();
}

bool Budget::ConsumeShot() {
    if (Exceeded()) return false;
    ++shots;
    return !Exceeded();
}

bool Budget::Exceeded() const {
    return llm_calls >= max_llm_calls || high_quality_calls >= max_high_quality_calls || shots >= max_shots || cost >= max_cost;
}

std::string Budget::Reason() const {
    if (llm_calls >= max_llm_calls) return "LLM 调用预算已耗尽";
    if (high_quality_calls >= max_high_quality_calls) return "高档模型调用预算已耗尽";
    if (shots >= max_shots) return "镜头数量预算已耗尽";
    if (cost >= max_cost) return "估算成本预算已耗尽";
    return {};
}

} // namespace shine::pipeline
