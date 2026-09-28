#pragma once
// P07-S8：把镜头/资产字段绑定到 Comfy 节点输入。
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shine::flow {

enum class BindingSource {
    Constant,
    ShotPrompt,
    ShotNegative,
    StableSeed,
    ShotFrameReference,
    AssetSheet,
};

struct BindingShotContext {
    std::int64_t shot_id = 0;
    std::string prompt;
    std::string negative;
    std::string frame_reference;
    std::int64_t seed = 0;
};

struct FlowBinding {
    std::string id;
    std::string node_id;
    std::string input_name;
    BindingSource source = BindingSource::Constant;
    std::string expression;
    bool required = true;
};

struct BindingIssue {
    std::string binding_id;
    std::string node_id;
    std::string input_name;
    std::string message;
    bool blocking = true;
};

class FlowBinder {
  public:
    void Add(FlowBinding binding);
    bool Remove(std::string_view id);
    void Clear() noexcept;
    [[nodiscard]] const std::vector<FlowBinding>& Bindings() const noexcept { return bindings_; }

    [[nodiscard]] std::optional<std::string> Evaluate(const FlowBinding& binding,
                                                       const BindingShotContext& shot) const;
    [[nodiscard]] std::vector<BindingIssue> Validate(const BindingShotContext& shot) const;
    [[nodiscard]] std::string Preview(std::string_view binding_id, const BindingShotContext& shot) const;

  private:
    std::vector<FlowBinding> bindings_;
    std::int64_t next_id_ = 1;
};

} // namespace shine::flow
