#include "flow/FlowBinder.h"

#include <algorithm>
#include <utility>

namespace shine::flow {

void FlowBinder::Add(FlowBinding binding) {
    if (binding.id.empty()) binding.id = "binding-" + std::to_string(next_id_++);
    bindings_.push_back(std::move(binding));
}

bool FlowBinder::Remove(std::string_view id) {
    const auto it = std::find_if(bindings_.begin(), bindings_.end(),
                                 [id](const FlowBinding& b) { return b.id == id; });
    if (it == bindings_.end()) return false;
    bindings_.erase(it);
    return true;
}

void FlowBinder::Clear() noexcept { bindings_.clear(); }

std::optional<std::string> FlowBinder::Evaluate(const FlowBinding& binding,
                                                const BindingShotContext& shot) const {
    switch (binding.source) {
    case BindingSource::Constant:
        return binding.expression;
    case BindingSource::ShotPrompt:
        return shot.prompt.empty() ? std::nullopt : std::optional<std::string>{shot.prompt};
    case BindingSource::ShotNegative:
        return shot.negative.empty() ? std::nullopt : std::optional<std::string>{shot.negative};
    case BindingSource::StableSeed:
        return std::to_string(shot.seed);
    case BindingSource::ShotFrameReference:
        return shot.frame_reference.empty() ? std::nullopt
                                             : std::optional<std::string>{shot.frame_reference};
    case BindingSource::AssetSheet:
        return binding.expression.empty() ? std::nullopt
                                          : std::optional<std::string>{binding.expression};
    }
    return std::nullopt;
}

std::vector<BindingIssue> FlowBinder::Validate(const BindingShotContext& shot) const {
    std::vector<BindingIssue> issues;
    issues.reserve(bindings_.size());
    for (const auto& binding : bindings_) {
        if (binding.node_id.empty() || binding.input_name.empty()) {
            issues.push_back({binding.id, binding.node_id, binding.input_name,
                              "绑定缺少节点或输入字段", true});
            continue;
        }
        const auto value = Evaluate(binding, shot);
        if (!value.has_value() && binding.required) {
            issues.push_back({binding.id, binding.node_id, binding.input_name,
                              "当前镜头缺少绑定数据，提交前必须补齐", true});
        }
    }
    return issues;
}

std::string FlowBinder::Preview(std::string_view binding_id, const BindingShotContext& shot) const {
    const auto it = std::find_if(bindings_.begin(), bindings_.end(),
                                 [binding_id](const FlowBinding& b) { return b.id == binding_id; });
    if (it == bindings_.end()) return {};
    const auto value = Evaluate(*it, shot);
    return value.value_or("<缺失>");
}

} // namespace shine::flow
