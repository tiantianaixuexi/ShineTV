#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace shine::pipeline {

enum class StageId : int {
    T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16, T17,
    V0, V1, V2, V3, V4, V5, V6, V7, V8, V9, V10, V11,
};

struct StageDefinition {
    StageId id = StageId::T1;
    std::string code;
    std::string name;
    std::string chain;
    std::string enter_condition;
    std::string exit_condition;
};

[[nodiscard]] const std::vector<StageDefinition>& AllStages();
[[nodiscard]] std::string StageCode(StageId id);
[[nodiscard]] StageId StageFromCode(std::string_view code);
[[nodiscard]] bool CanEnter(StageId id, StageId previous);
[[nodiscard]] bool IsValidTransition(StageId from, StageId to);
[[nodiscard]] std::string TransitionError(StageId from, StageId to);

} // namespace shine::pipeline
