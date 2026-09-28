#include "flow/FlowValidator.h"

#include <algorithm>
#include <fmt/format.h>

namespace shine::flow {

std::string GenerationValidationResult::Describe() const {
    if (issues.empty()) return "K19–K21 通过";
    std::string out;
    for (const auto& issue : issues) {
        if (!out.empty()) out += "；";
        out += fmt::format("{} {}: {}", issue.code, issue.severity, issue.message);
    }
    return out;
}

GenerationValidationResult ValidateForSubmit(const GenerationValidationInput& input,
                                            video::NodeDefLookup lookup) {
    GenerationValidationResult result;
    auto add = [&result](std::string code, std::string severity, std::string message) {
        result.issues.push_back({std::move(code), std::move(severity), std::move(message)});
    };
    if (!input.object_info_ready) {
        add("K19", "high", "object_info 尚未就绪，提交已阻止（不是静默跳过）");
    } else {
        const auto graph = video::ValidateApiGraph(input.api_json, lookup);
        if (graph.blocked) add("K19", "high", "无法完成 ComfyUI 图校验");
        for (const auto& issue : graph.issues) {
            add("K19", "high", issue.message);
        }
    }
    if (input.binder != nullptr) {
        for (const auto& issue : input.binder->Validate(input.shot)) {
            add("BIND", issue.blocking ? "high" : "low", issue.message);
        }
    }
    if ((input.width % 64) != 0 || (input.height % 64) != 0) {
        result.degraded = true;
        add("K20", "low", fmt::format("尺寸 {}×{} 将按 64 对齐纠正", input.width, input.height));
    }
    if (input.length > 0 && input.length % 17 != 5) {
        result.degraded = true;
        add("K20", "low", fmt::format("帧数 {} 将按 n%17==5 规则纠正", input.length));
    }
    if (input.reference_count > 9) {
        add("K21", "high", fmt::format("参考图 {} 张，超过上限 9", input.reference_count));
    } else if (!input.references_sheet_ready) {
        add("K21", "high", "参考图存在未达到 SHEET_READY 的资产");
    }
    result.ok = std::none_of(result.issues.begin(), result.issues.end(),
                             [](const auto& issue) { return issue.severity == "high"; });
    return result;
}

} // namespace shine::flow
