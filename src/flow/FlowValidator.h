#pragma once
// P07-S9：提交前把 K19–K21 与绑定错误合并为一份可展示结果。
#include "flow/ApiGraphValidator.h"
#include "flow/FlowBinder.h"

#include <string>
#include <vector>

namespace shine::flow {

struct GenerationValidationInput {
    std::string api_json;
    BindingShotContext shot;
    bool object_info_ready = false;
    std::size_t width = 0;
    std::size_t height = 0;
    int length = 0;
    std::size_t reference_count = 0;
    bool references_sheet_ready = true;
    const FlowBinder* binder = nullptr;
};

struct GenerationValidationIssue {
    std::string code;
    std::string severity;
    std::string message;
};

struct GenerationValidationResult {
    bool ok = false;
    bool degraded = false;
    std::vector<GenerationValidationIssue> issues;
    [[nodiscard]] std::string Describe() const;
};

[[nodiscard]] GenerationValidationResult ValidateForSubmit(
    const GenerationValidationInput& input, video::NodeDefLookup lookup);

} // namespace shine::flow
