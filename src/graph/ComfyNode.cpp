#include "graph/ComfyNode.h"

#include "core/Log.h"
#include "util/Strings.h"

#include <VisualNodeSystem.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ranges>
#include <string>
#include <vector>

namespace shine::graph {

using VisNodeSys::NodeSocket; // VNS 的插口类型（头文件里只有声明）

namespace {

using comfy::InputDef;
using comfy::WidgetKind;

// 布局常量（**图形单位**，屏幕像素 = 这些都乘 Zoom）
constexpr float kNodeWidth = 320.0f;   // 节点宽
constexpr float kLabelWidth = 118.0f;  // 左侧插口标签预留
constexpr float kRightPad = 14.0f;     // 右内边距

[[nodiscard]] ImColor TitleColorFor(const std::string& category) {
    // 分类 → 固定色（同分类同色，跨会话稳定：只按字符串哈希，不用随机）
    size_t h = std::hash<std::string>{}(category);
    const float hue = static_cast<float>(h % 360u) / 360.0f;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    ImGui::ColorConvertHSVtoRGB(hue, 0.55f, 0.62f, r, g, b);
    const ImColor base(r, g, b);
    return base;
}

[[nodiscard]] std::string BoolText(bool v) { return v ? "true" : "false"; }

// —— P3.7：ComfyUI 的插口类型语义（比 VNS 的"两边 AllowedTypes 求交集"宽）——
//   * 并集：`"FLOAT,INT,BOOLEAN"`（一个插口能接多种类型，如 `ComfyMathExpression.values.a`）
//   * 通配：`*` / `ANY` / `COMFY_MATCHTYPE_*`（如 `ComfySwitchNode.on_true/output`，接什么就是什么）
//   * 动态父插口：`COMFY_AUTOGROW_V3` / `COMFY_DYNAMICCOMBO_V3`（子键类型由模板决定，我们只知道父）
// 这三类若按"集合求交"判，**永远接不上** —— 2026-09-17 导入官方 H3 模板时实测：33 条连线只连上 24 条。
[[nodiscard]] bool IsWildcardType(std::string_view type) {
    return type == "*" || type == "ANY" || util::StartsWith(type, "COMFY_MATCHTYPE") || type == "COMFY_AUTOGROW_V3" ||
           type == "COMFY_DYNAMICCOMBO_V3";
}

// 插口声明类型 → VNS 的 AllowedTypes 列表（并集拆开；通配补一个 "ANY" 作为放行标记）
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

// 两个插口能不能连：任一侧含通配 → 放行；否则按 ComfyUI 的"并集求交"
[[nodiscard]] bool SocketTypesCompatible(const NodeSocket& a, const NodeSocket& b) {
    const std::vector<std::string>& at = a.GetAllowedTypes();
    const std::vector<std::string>& bt = b.GetAllowedTypes();
    if (HasWildcard(at) || HasWildcard(bt)) {
        return true;
    }
    return std::ranges::any_of(at, [&bt](const std::string& t) { return std::ranges::find(bt, t) != bt.end(); });
}

} // namespace

const std::string ShineComfyNode::kEmptyValue{};

ShineComfyNode::ShineComfyNode(std::shared_ptr<const comfy::NodeTypeDef> def) : def_(std::move(def)) {
    if (def_) {
        className_ = def_->className;
    }
    Type = className_;                     // VNS 工厂 key / 存盘 NodeType
    SetName(def_ ? def_->displayName : className_);
    TitleBackgroundColor = TitleColorFor(def_ ? def_->category : std::string{});
    TitleBackgroundColorHovered = ImColor(216, 226, 255);
    SetMaxInputLabelWidth(kLabelWidth - 26.0f);
    SetMaxOutputLabelWidth(96.0f);

    BuildSockets();
    LoadDefaults();

    // 初始尺寸（Draw 里每帧按内容校正；这里给个合理默认，避免首帧过小）
    const float rows = static_cast<float>(std::max<size_t>(def_ ? def_->inputs.size() : 1, 1));
    SetSize(ImVec2(kNodeWidth, NODE_TITLE_HEIGHT + NODE_HEIGHT_PER_SOCKET * rows));
}

ShineComfyNode::ShineComfyNode(const ShineComfyNode& other) : Node(other) {
    def_ = other.def_;
    className_ = other.className_;
    widgets_ = other.widgets_;             // 拷贝构造必须带走控件值（工厂复制节点用）
    TitleBackgroundColor = other.TitleBackgroundColor;
    TitleBackgroundColorHovered = other.TitleBackgroundColorHovered;
}

void ShineComfyNode::BuildSockets() {
    if (!def_) {
        return;
    }
    // 每个输入一行（含控件输入）——行与插口 1:1，控件与插口标签必然对齐（见头文件说明）
    for (const InputDef& in : def_->inputs) {
        auto* sock = new NodeSocket(this, AllowedTypesFor(in.type), in.name, NodeSocket::SocketFlow::Input);
        sock->SetCanBeDeletedByUser(false); // 规格固定：由节点定义决定，不能手动删
        AddSocket(sock);
    }
    for (const comfy::OutputDef& out : def_->outputs) {
        const std::string name = out.name.empty() ? out.type : out.name;
        auto* sock = new NodeSocket(this, AllowedTypesFor(out.type), name, NodeSocket::SocketFlow::Output);
        sock->SetCanBeDeletedByUser(false);
        AddSocket(sock);
    }
}

// 结构规则（自连/同向/重复）与 VNS 基类一致，只把**类型**判定换成 ComfyUI 语义。
bool ShineComfyNode::IsValidAsNewConnection(NodeSocket* ownSocket, NodeSocket* candidateSocket) {
    if (ownSocket == nullptr || candidateSocket == nullptr || ownSocket == candidateSocket) {
        return false;
    }
    if (candidateSocket->GetParent() == this) {
        return false; // 不能连到自己
    }
    if (ownSocket->GetFlowDirection() == candidateSocket->GetFlowDirection()) {
        return false; // 必须一进一出
    }
    for (const NodeSocket* connected : ownSocket->GetConnectedSockets()) {
        if (connected->GetID() == candidateSocket->GetID()) {
            return false; // 已经连过这一对
        }
    }
    return SocketTypesCompatible(*ownSocket, *candidateSocket);
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
    const InputDef* in = def_ ? def_->FindInput(key) : nullptr;
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

void ShineComfyNode::DrawWidget(const InputDef& in, float widgetX, float rowCenterY, float zoom) {
    const float frameH = ImGui::GetFrameHeight();
    const float width = (kNodeWidth - kLabelWidth - kRightPad) * zoom;
    ImGui::PushID(in.name.c_str());
    ImGui::SetCursorScreenPos(ImVec2(widgetX, rowCenterY - frameH * 0.5f));
    ImGui::SetNextItemWidth(width);

    std::string& value = widgets_[in.name]; // 已由 LoadDefaults 初始化
    switch (in.widget) {
    case WidgetKind::Int: {
        int v = util::ToInt(value).value_or(0);
        const int step = (in.hasStep && in.step >= 1.0) ? static_cast<int>(in.step) : 1;
        const int lo = in.hasMin ? static_cast<int>(in.min) : 0;
        const int hi = in.hasMax ? static_cast<int>(in.max) : 0;
        bool changed = false;
        if (in.display == "slider" && in.hasMin && in.hasMax && in.max > in.min) {
            changed = ImGui::SliderInt("##w", &v, lo, hi);
        } else {
            changed = ImGui::DragInt("##w", &v, static_cast<float>(step), lo, hi);
        }
        if (changed) {
            SetWidgetValue(in.name, std::to_string(v));
        }
        break;
    }
    case WidgetKind::Float: {
        float v = static_cast<float>(util::ToDouble(value).value_or(0.0));
        const float step = (in.hasStep && in.step > 0.0) ? static_cast<float>(in.step) : 0.01f;
        const float lo = in.hasMin ? static_cast<float>(in.min) : 0.0f;
        const float hi = in.hasMax ? static_cast<float>(in.max) : 0.0f;
        bool changed = false;
        if (in.display == "knob" || in.display == "slider") {
            changed = ImGui::SliderFloat("##w", &v, lo, in.hasMax ? hi : lo + 1.0f, "%.4g");
        } else {
            changed = ImGui::DragFloat("##w", &v, step, lo, hi, "%.4g");
        }
        if (changed) {
            SetWidgetValue(in.name, util::FromDouble(static_cast<double>(v)));
        }
        break;
    }
    case WidgetKind::Bool: {
        bool b = (value == "true" || value == "1");
        const bool changed = ImGui::Checkbox("##w", &b);
        if (changed) {
            SetWidgetValue(in.name, BoolText(b));
        }
        const std::string& label = b ? in.labelOn : in.labelOff;
        ImGui::SameLine();
        ImGui::TextUnformatted(label.empty() ? in.name.c_str() : label.c_str());
        break;
    }
    case WidgetKind::Text: {
        if (in.multiline) {
            // 高度固定为 1 行（可滚动）：与插口行保持 1:1，避免越行压到下一个控件
            ImGui::InputTextMultiline("##w", &value, ImVec2(width, frameH));
        } else if (!in.placeholder.empty()) {
            ImGui::InputTextWithHint("##w", in.placeholder.c_str(), &value);
        } else {
            ImGui::InputText("##w", &value);
        }
        break;
    }
    case WidgetKind::Combo: {
        int idx = 0;
        for (std::size_t i = 0; i < in.options.size(); ++i) {
            if (in.options[i] == value) {
                idx = static_cast<int>(i);
                break;
            }
        }
        std::vector<const char*> items;
        items.reserve(in.options.size());
        for (const std::string& o : in.options) {
            items.push_back(o.c_str());
        }
        if (!items.empty() && ImGui::Combo("##w", &idx, items.data(), static_cast<int>(items.size()))) {
            SetWidgetValue(in.name, in.options[static_cast<std::size_t>(std::max(0, idx))]);
        }
        break;
    }
    case WidgetKind::None:
        break;
    }
    ImGui::PopID();
}

void ShineComfyNode::Draw() {
    // Node::Draw() 是空实现：节点外框/标题/插口由 VNS 的 NodeArea 渲染，这里只画控件。
    const ImVec2 topLeft = ImGui::GetCursorScreenPos(); // == Node->LeftTop（屏幕坐标）
    const float zoom = (ParentArea != nullptr) ? ParentArea->GetZoomFactor() : 1.0f;
    if (zoom <= 0.0f) {
        return;
    }
    const float titleGraph = (GetTitleBarHeight() > 0.0f) ? GetTitleBarHeight() : NODE_TITLE_HEIGHT;
    const float titleScreen = titleGraph * zoom;
    // 行距取「VNS 插口行距」与「当前 ImGui 控件高度」的较大者，保证控件不重叠
    const float rowPitch = std::max(NODE_HEIGHT_PER_SOCKET * zoom, ImGui::GetFrameHeightWithSpacing());
    const std::size_t inRows = def_ ? def_->inputs.size() : 0;
    const std::size_t outRows = def_ ? def_->outputs.size() : 0;
    const std::size_t rows = std::max<std::size_t>({inRows, outRows, 1});
    // 尺寸：让 VNS 把插口按行均分（Size.y*Zoom - title = rows*rowPitch）
    SetSize(ImVec2(kNodeWidth, (titleScreen + rowPitch * static_cast<float>(rows)) / zoom));

    if (!def_) {
        return;
    }
    bool advancedMarked = false;
    for (std::size_t i = 0; i < def_->inputs.size(); ++i) {
        const InputDef& in = def_->inputs[i];
        const float rowCenter = topLeft.y + titleScreen + rowPitch * (static_cast<float>(i) + 0.5f);
        if (in.widget == WidgetKind::None || in.forceInput) {
            continue; // 纯连线输入（或强制连线）：不画控件
        }
        if (in.advanced && !advancedMarked) {
            advancedMarked = true;
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(topLeft.x + 6.0f * zoom, rowCenter - rowPitch * 0.5f),
                ImVec2(topLeft.x + (kNodeWidth - 6.0f) * zoom, rowCenter - rowPitch * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
            ImGui::SetCursorScreenPos(ImVec2(topLeft.x + (kLabelWidth - 34.0f) * zoom, rowCenter - ImGui::GetTextLineHeight() * 0.5f));
            ImGui::TextDisabled("高级");
        }
        DrawWidget(in, topLeft.x + kLabelWidth * zoom, rowCenter, zoom);
    }
}

Json::Value ShineComfyNode::ToJson() {
    Json::Value result = Node::ToJson();
    Json::Value widgets(Json::objectValue);
    for (const auto& [key, value] : widgets_) {
        widgets[key] = value;
    }
    result["Widgets"] = std::move(widgets);
    return result;
}

bool ShineComfyNode::FromJson(Json::Value json) {
    // 先读控件值（json 随后要交给基类）；类型不符/键不存在一律忽略，**不抛异常**
    if (json.isMember("Widgets") && json["Widgets"].isObject()) {
        for (const std::string& key : json["Widgets"].getMemberNames()) {
            const Json::Value& v = json["Widgets"][key];
            if (v.isString()) {
                SetWidgetValue(key, v.asString());
            } else if (v.isBool()) {
                SetWidgetValue(key, BoolText(v.asBool()));
            } else if (v.isNumeric()) {
                SetWidgetValue(key, util::FromDouble(v.asDouble()));
            }
        }
    }
    return Node::FromJson(std::move(json));
}

} // namespace shine::graph
