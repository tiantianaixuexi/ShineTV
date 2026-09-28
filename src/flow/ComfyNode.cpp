#include "flow/ComfyNode.h"

#include "util/Strings.h"

#include <algorithm>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace shine::flow {
namespace {

using comfy::InputDef;
using comfy::WidgetKind;

// 布局常量（**图坐标系**；渲染层按 Zoom 换算屏幕像素）
constexpr float kNodeWidth = 320.0f;  // 节点宽
constexpr float kNodeMinHeight = 96.0f; // 节点高下限（按行数增长）

[[nodiscard]] std::string BoolText(bool v) { return v ? "true" : "false"; }

// —— P3.7：ComfyUI 的插口类型语义（比"两边 AllowedTypes 求交集"宽，见头文件 CanConnect 注释）——
[[nodiscard]] bool IsWildcardType(std::string_view type) {
    return type == "*" || type == "ANY" || util::StartsWith(type, "COMFY_MATCHTYPE") || type == "COMFY_AUTOGROW_V3" ||
           type == "COMFY_DYNAMICCOMBO_V3";
}

// 端口声明类型 → AllowedTypes 列表（并集拆开；通配补一个 "ANY" 作为放行标记）
[[nodiscard]] std::vector<std::string> AllowedTypesFor(std::string_view type) {
    std::vector<std::string> out;
    if (type.empty()) {
        out.emplace_back("ANY");
        return out;
    }
    std::size_t begin = 0;
    bool wildcard = false;
    while (begin <= type.size()) {
        const std::size_t comma = type.find(',', begin);
        const std::size_t end = (comma == std::string_view::npos) ? type.size() : comma;
        std::string part{util::Trim(type.substr(begin, end - begin))};
        if (!part.empty()) {
            wildcard = wildcard || IsWildcardType(part);
            out.push_back(std::move(part));
        }
        if (comma == std::string_view::npos) {
            break;
        }
        begin = comma + 1;
    }
    if (out.empty()) {
        out.emplace_back("ANY");
    } else if (wildcard) {
        out.emplace_back("ANY");
    }
    return out;
}

[[nodiscard]] bool HasWildcard(const std::vector<std::string>& types) {
    // ⚠️ `std::ranges::find(v, "ANY")` 在 ranges 下不接受 `const char*` 隐式转换 → 显式给 string_view
    return std::ranges::find(types, std::string_view{"ANY"}) != types.end();
}

// 两个端口能不能连：任一侧含通配 → 放行；否则按 ComfyUI 的"并集求交"
[[nodiscard]] bool SocketTypesCompatible(const Socket& a, const Socket& b) {
    if (HasWildcard(a.allowedTypes) || HasWildcard(b.allowedTypes)) {
        return true;
    }
    return std::ranges::any_of(a.allowedTypes, [&b](const std::string& t) {
        return std::ranges::find(b.allowedTypes, t) != b.allowedTypes.end();
    });
}

} // namespace

const std::string ShineComfyNode::kEmptyValue{};

ShineComfyNode::ShineComfyNode(std::shared_ptr<const comfy::NodeTypeDef> def) : def_(std::move(def)) {
    if (def_) {
        className_ = def_->className;
        title_ = def_->displayName.empty() ? className_ : def_->displayName;
    }
    BuildSockets();
    LoadDefaults();
    const std::size_t rows = std::max<std::size_t>({inputs_.size(), outputs_.size(), 1});
    size = Vec2{kNodeWidth, kNodeMinHeight + 24.0f * static_cast<float>(rows)};
}

ShineComfyNode::ShineComfyNode(std::string className, std::string title)
    : className_(std::move(className)), title_(std::move(title)) {
    inputs_.push_back(Socket{SocketFlow::Input, "In", {"ANY"}});
    outputs_.push_back(Socket{SocketFlow::Output, "Out", {"ANY"}});
    size = Vec2{kNodeWidth, kNodeMinHeight};
}

void ShineComfyNode::BuildSockets() {
    if (!def_) {
        return;
    }
    // 每个输入一行（含控件输入）——行与端口 1:1（见头文件说明）
    for (const InputDef& in : def_->inputs) {
        inputs_.push_back(Socket{SocketFlow::Input, in.name, AllowedTypesFor(in.type)});
    }
    for (const comfy::OutputDef& out : def_->outputs) {
        outputs_.push_back(Socket{SocketFlow::Output, out.name.empty() ? out.type : out.name, AllowedTypesFor(out.type)});
    }
}

void ShineComfyNode::RestoreSockets(std::vector<Socket> inputs, std::vector<Socket> outputs) {
    inputs_ = std::move(inputs);
    outputs_ = std::move(outputs);
    for (Socket& s : inputs_) {
        s.flow = SocketFlow::Input;
        if (s.allowedTypes.empty()) {
            s.allowedTypes.emplace_back("ANY");
        }
    }
    for (Socket& s : outputs_) {
        s.flow = SocketFlow::Output;
        if (s.allowedTypes.empty()) {
            s.allowedTypes.emplace_back("ANY");
        }
    }
}

void ShineComfyNode::LoadDefaults() {
    if (!def_) {
        return;
    }
    for (const InputDef& in : def_->inputs) {
        if (in.widget == WidgetKind::None) {
            continue;
        }
        std::string value;
        switch (in.widget) {
        case WidgetKind::Int:
            value = std::to_string(in.hasDefaultNumber ? static_cast<long long>(in.defaultNumber) : 0LL);
            break;
        case WidgetKind::Float:
            value = util::FromDouble(in.hasDefaultNumber ? in.defaultNumber : 0.0);
            break;
        case WidgetKind::Bool:
            value = BoolText(in.hasDefaultBool ? in.defaultBool : false);
            break;
        case WidgetKind::Text:
            value = in.hasDefaultString ? in.defaultString : std::string{};
            break;
        case WidgetKind::Combo:
            if (in.hasDefaultString && !in.defaultString.empty()) {
                value = in.defaultString;
            } else if (!in.options.empty()) {
                value = in.options.front();
            }
            break;
        case WidgetKind::None:
            break;
        }
        widgets_[in.name] = std::move(value);
    }
}

const std::string& ShineComfyNode::WidgetValue(std::string_view inputName) const noexcept {
    const auto it = widgets_.find(std::string{inputName});
    return it == widgets_.end() ? kEmptyValue : it->second;
}

bool ShineComfyNode::HasWidgetValue(std::string_view inputName) const noexcept {
    return widgets_.find(std::string{inputName}) != widgets_.end();
}

void ShineComfyNode::SetWidgetValue(std::string_view inputName, std::string value) {
    const std::string key{inputName};
    if (!def_) {
        widgets_[key] = std::move(value); // 无定义（未注册类型）：原样保住数据，不做归一化
        return;
    }
    const InputDef* in = def_->FindInput(key);
    if (!in && widgets_.find(key) == widgets_.end()) {
        return; // 未知输入名：不写入（避免存档/导入带进垃圾键）
    }
    if (in) {
        // 归一化 + 夹取范围（宽容：解析失败就退回默认值，不抛异常）
        switch (in->widget) {
        case WidgetKind::Int: {
            long long v = 0;
            if (const auto parsed = util::ToDouble(value); parsed.has_value()) {
                v = static_cast<long long>(*parsed);
            }
            if (in->hasMin && static_cast<double>(v) < in->min) v = static_cast<long long>(in->min);
            if (in->hasMax && static_cast<double>(v) > in->max) v = static_cast<long long>(in->max);
            value = std::to_string(v);
            break;
        }
        case WidgetKind::Float: {
            double v = 0.0;
            if (const auto parsed = util::ToDouble(value); parsed.has_value()) {
                v = *parsed;
            }
            if (in->hasMin && v < in->min) v = in->min;
            if (in->hasMax && v > in->max) v = in->max;
            value = util::FromDouble(v);
            break;
        }
        case WidgetKind::Bool:
            value = BoolText(value == "true" || value == "1");
            break;
        case WidgetKind::Combo:
            if (!value.empty() && !in->options.empty() &&
                std::ranges::find(in->options, value) == in->options.end()) {
                value = in->options.front(); // 不在选项里（枚举已变）→ 回落第一个
            }
            break;
        case WidgetKind::Text:
        case WidgetKind::None:
            break;
        }
    }
    widgets_[key] = std::move(value);
}

std::optional<std::size_t> ShineComfyNode::InputSlotByName(std::string_view name) const {
    if (!def_ || name.empty()) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < def_->inputs.size(); ++i) {
        if (def_->inputs[i].name == name) {
            return i;
        }
    }
    // JSON 里写的是 `父.子`（autogrow / dynamic combo 展开出来的键），
    // 而定义里**只有一个父插口** → 归到父插口上。
    if (const std::size_t dot = name.find('.'); dot != std::string_view::npos) {
        const std::string_view head = name.substr(0, dot);
        for (std::size_t i = 0; i < def_->inputs.size(); ++i) {
            if (def_->inputs[i].name == head) {
                return i;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> ShineComfyNode::OutputSlotByName(std::string_view name) const {
    if (!def_ || name.empty()) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < def_->outputs.size(); ++i) {
        if (def_->outputs[i].name == name) {
            return i;
        }
    }
    return std::nullopt;
}

bool CanConnect(const ShineComfyNode& from, std::size_t fromOutput, const ShineComfyNode& to, std::size_t toInput) {
    if (&from == &to) {
        return false; // 不能连到自己
    }
    if (fromOutput >= from.Outputs().size() || toInput >= to.Inputs().size()) {
        return false; // 槽位越界
    }
    return SocketTypesCompatible(from.Outputs()[fromOutput], to.Inputs()[toInput]);
}

} // namespace shine::flow
