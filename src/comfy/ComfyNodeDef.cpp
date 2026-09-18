#include "comfy/ComfyNodeDef.h"

#include "core/Log.h"
#include "util/Json.h"
#include "util/Strings.h"

#include <yyjson.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// 节点定义 JSON 的解析实现（v2.0 优先 / v1.0 只读兼容）
// 规范：Doc/RULES-COMFY.md §12.6 A（V2 字段）/ §12.6 B（V1 字段）/ §12.6 D（形状自动识别、必须实测）
//
// 宽容原则（§12.7）：未知字段忽略、缺失字段取默认值、类型不符不抛异常；坏 JSON 只 Warn 不崩。

namespace shine::comfy {
namespace {

using util::json::Get;
using util::json::GetArr;
using util::json::GetBool;
using util::json::GetObj;
using util::json::GetStr;
using util::json::HasKey;

[[nodiscard]] std::string Own(std::string_view s) { return std::string{s}; }

// yyjson 的对象键 / 数组项 → key 视图（指向 doc 内存，doc 存活期间有效）
[[nodiscard]] std::string_view KeyView(yyjson_val* key) noexcept {
    const char* s = key ? yyjson_get_str(key) : nullptr;
    return s ? std::string_view{s, yyjson_get_len(key)} : std::string_view{};
}

// JSON 标量 → 文本（COMBO 选项可能是字符串 / 数字 / 布尔；整数值不留小数点）
[[nodiscard]] std::string ScalarToText(yyjson_val* v) {
    if (!v || yyjson_is_null(v)) {
        return {};
    }
    if (yyjson_is_str(v)) {
        const char* s = yyjson_get_str(v);
        return s ? s : "";
    }
    if (yyjson_is_bool(v)) {
        return yyjson_get_bool(v) ? "true" : "false";
    }
    if (yyjson_is_num(v)) {
        const double d = yyjson_get_num(v);
        const auto i = static_cast<std::int64_t>(d);
        if (static_cast<double>(i) == d) {
            return std::to_string(i);
        }
        return util::FromDouble(d);
    }
    return {};
}

// 数组第 i 项的文本；数组缺失 / 越界一律返回空串（调用方不需要再判空）
[[nodiscard]] std::string ArrText(yyjson_val* arr, std::size_t i) {
    if (!arr || !yyjson_is_arr(arr) || i >= yyjson_arr_size(arr)) {
        return {};
    }
    return ScalarToText(yyjson_arr_get(arr, i));
}

[[nodiscard]] std::vector<std::string> ReadTextArray(yyjson_val* arr) {
    std::vector<std::string> out;
    if (!arr || !yyjson_is_arr(arr)) {
        return out;
    }
    const std::size_t n = yyjson_arr_size(arr);
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        std::string s = ScalarToText(yyjson_arr_get(arr, i));
        if (!s.empty()) {
            out.push_back(std::move(s));
        }
    }
    return out;
}

[[nodiscard]] WidgetKind WidgetKindFor(std::string_view upperType) noexcept {
    if (upperType == "INT") return WidgetKind::Int;
    if (upperType == "FLOAT") return WidgetKind::Float;
    if (upperType == "BOOLEAN") return WidgetKind::Bool;
    if (upperType == "STRING") return WidgetKind::Text;
    if (upperType == "COMBO") return WidgetKind::Combo;
    return WidgetKind::None;
}

// 默认值三形态：v2 的 `default`（STRING 另可用 `defaultVal`）、v1 的 `default`
void ReadDefaultValue(yyjson_val* src, InputDef& in) {
    yyjson_val* v = Get(src, "default");
    if (!v || yyjson_is_null(v)) {
        v = Get(src, "defaultVal");
    }
    if (!v || yyjson_is_null(v)) {
        return;
    }
    if (yyjson_is_str(v)) {
        in.hasDefaultString = true;
        in.defaultString = ScalarToText(v);
    } else if (yyjson_is_bool(v)) {
        in.hasDefaultBool = true;
        in.defaultBool = yyjson_get_bool(v);
    } else if (yyjson_is_num(v)) {
        in.hasDefaultNumber = true;
        in.defaultNumber = yyjson_get_num(v);
    }
}

void ReadNumField(yyjson_val* src, const char* key, bool& has, double& out) {
    yyjson_val* v = Get(src, key);
    if (v && yyjson_is_num(v)) {
        has = true;
        out = yyjson_get_num(v);
    }
}

// 输入项公共可选字段（v1 的选项对象与 v2 的输入对象键名一致的部分）
void ReadCommonInputFlags(yyjson_val* src, InputDef& in) {
    in.forceInput = GetBool(src, "forceInput", in.forceInput);
    in.hidden = GetBool(src, "hidden", in.hidden);
    in.advanced = GetBool(src, "advanced", in.advanced);
    in.lazy = GetBool(src, "lazy", in.lazy);
    in.rawLink = GetBool(src, "rawLink", in.rawLink);
    in.defaultInput = GetBool(src, "defaultInput", in.defaultInput);
    if (const std::string_view tip = GetStr(src, "tooltip"); !tip.empty()) {
        in.tooltip = Own(tip);
    }
}

// 动态输入模板（AUTOGROW 的 prefix/max、DYNAMICCOMBO 的选项 key）——
// 校验器靠它算"哪些子键合法"，见 `video/ApiGraphValidator.*`
void ReadDynamicFields(yyjson_val* src, InputDef& in) {
    if (in.type == "COMFY_AUTOGROW_V3") {
        if (yyjson_val* tpl = GetObj(src, "template")) {
            in.dynamicPrefix = Own(GetStr(tpl, "prefix"));
            if (yyjson_val* mx = Get(tpl, "max"); mx && yyjson_is_num(mx)) {
                in.dynamicMax = static_cast<int>(yyjson_get_sint(mx));
            }
        }
    } else if (in.type == "COMFY_DYNAMICCOMBO_V3") {
        if (yyjson_val* opts = GetArr(src, "options")) {
            const std::size_t n = yyjson_arr_size(opts);
            for (std::size_t i = 0; i < n; ++i) {
                yyjson_val* item = yyjson_arr_get(opts, i);
                if (item && yyjson_is_obj(item)) {
                    const std::string_view k = GetStr(item, "key");
                    if (!k.empty()) {
                        in.dynamicOptionKeys.emplace_back(k);
                    }
                }
            }
        }
    }
}

// 类型专属字段（INT / FLOAT / BOOLEAN / STRING / COMBO）—— §12.6 A
void ReadWidgetFields(yyjson_val* src, InputDef& in) {
    ReadDynamicFields(src, in);
    switch (in.widget) {
    case WidgetKind::Int:
    case WidgetKind::Float:
        ReadNumField(src, "min", in.hasMin, in.min);
        ReadNumField(src, "max", in.hasMax, in.max);
        ReadNumField(src, "step", in.hasStep, in.step);
        in.display = Own(GetStr(src, "display"));
        in.controlAfterGenerate = GetBool(src, "control_after_generate", in.controlAfterGenerate);
        ReadNumField(src, "round", in.hasRound, in.round); // round 可为 false（= 未设置）
        break;
    case WidgetKind::Bool:
        in.labelOn = Own(GetStr(src, "label_on"));
        in.labelOff = Own(GetStr(src, "label_off"));
        break;
    case WidgetKind::Text:
        in.multiline = GetBool(src, "multiline", in.multiline);
        in.dynamicPrompts = GetBool(src, "dynamicPrompts", in.dynamicPrompts);
        in.placeholder = Own(GetStr(src, "placeholder"));
        break;
    case WidgetKind::Combo: {
        if (yyjson_val* opts = GetArr(src, "options"); opts) {
            std::vector<std::string> list = ReadTextArray(opts);
            if (!list.empty()) {
                in.options = std::move(list);
            }
        }
        in.controlAfterGenerate = GetBool(src, "control_after_generate", in.controlAfterGenerate);
        in.imageUpload = GetBool(src, "image_upload", in.imageUpload);
        in.videoUpload = GetBool(src, "video_upload", in.videoUpload);
        in.allowBatch = GetBool(src, "allow_batch", in.allowBatch);
        in.imageFolder = Own(GetStr(src, "image_folder"));
        if (yyjson_val* remote = GetObj(src, "remote")) {
            in.remoteRoute = Own(GetStr(remote, "route"));
        }
        break;
    }
    case WidgetKind::None:
        break; // 纯连线输入没有专属控件字段
    }
}

// 顶层公共字段（v1 / v2 同名）
void ReadTopLevel(yyjson_val* def, std::string_view classNameFallback, NodeTypeDef& nd) {
    nd.className = Own(GetStr(def, "name"));
    if (nd.className.empty()) {
        nd.className = Own(classNameFallback); // v1 的类名在映射键上，不在定义里
    }
    nd.displayName = Own(GetStr(def, "display_name"));
    if (nd.displayName.empty()) {
        nd.displayName = nd.className;
    }
    nd.description = Own(GetStr(def, "description"));
    nd.category = Own(GetStr(def, "category"));
    nd.pythonModule = Own(GetStr(def, "python_module"));
    nd.outputNode = GetBool(def, "output_node", nd.outputNode);
    nd.deprecated = GetBool(def, "deprecated", nd.deprecated);
    nd.experimental = GetBool(def, "experimental", nd.experimental);
}

// —— 形状判定（§12.6 D：自动识别，不依赖外部开关）——

// V2 判据：顶层有 `inputs` 对象，且 `outputs[]` 项含 `index` / `is_list`
[[nodiscard]] bool LooksV2(yyjson_val* def) noexcept {
    if (!def || !yyjson_is_obj(def)) {
        return false;
    }
    yyjson_val* inputs = Get(def, "inputs");
    if (!inputs || !yyjson_is_obj(inputs)) {
        return false;
    }
    yyjson_val* outputs = Get(def, "outputs");
    if (!outputs || !yyjson_is_arr(outputs)) {
        return false;
    }
    if (yyjson_arr_size(outputs) == 0) {
        return true; // 无输出节点：`inputs` 是映射已足够判定
    }
    yyjson_val* first = yyjson_arr_get(outputs, 0);
    if (!first || !yyjson_is_obj(first)) {
        return false;
    }
    return HasKey(first, "index") || HasKey(first, "is_list");
}

// —— V2：inputs 是映射（键 = 输入名），每项 { type, name, … } ——

void ParseV2Input(std::string_view keyName, yyjson_val* v, InputDef& in) {
    in.name = Own(keyName);
    if (yyjson_is_obj(v)) {
        if (const std::string_view declared = GetStr(v, "name"); !declared.empty()) {
            in.name = Own(declared); // v2 每项必填 name
        }
        in.type = util::ToUpper(GetStr(v, "type"));
        in.widget = WidgetKindFor(in.type);
        in.isOptional = GetBool(v, "isOptional", in.isOptional);
        ReadCommonInputFlags(v, in);
        ReadDefaultValue(v, in);
        ReadWidgetFields(v, in); // 内含 ReadDynamicFields（v2 的 template/options 也直接挂在输入对象上）
    } else if (yyjson_is_str(v)) {
        // 规范之外的简写（缺字段不崩）：键值直接给类型名
        in.type = util::ToUpper(ScalarToText(v));
        in.widget = WidgetKindFor(in.type);
    }
    in.isWidget = in.widget != WidgetKind::None;
}

[[nodiscard]] NodeTypeDef ParseV2Def(yyjson_val* def, std::string_view classNameFallback) {
    NodeTypeDef nd;
    nd.format = NodeDefFormat::V2;
    ReadTopLevel(def, classNameFallback, nd);

    if (yyjson_val* inputs = GetObj(def, "inputs")) {
        nd.inputs.reserve(yyjson_obj_size(inputs));
        yyjson_obj_iter it;
        yyjson_obj_iter_init(inputs, &it);
        yyjson_val* key = nullptr;
        while ((key = yyjson_obj_iter_next(&it))) {
            // 保持 JSON 出现顺序（v2 没有 input_order）
            InputDef in;
            ParseV2Input(KeyView(key), yyjson_obj_iter_get_val(key), in);
            nd.inputs.push_back(std::move(in));
        }
    }

    if (yyjson_val* outputs = GetArr(def, "outputs")) {
        const std::size_t n = yyjson_arr_size(outputs);
        nd.outputs.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            yyjson_val* o = yyjson_arr_get(outputs, i);
            if (!o || !yyjson_is_obj(o)) {
                continue; // 非对象项跳过（不崩）
            }
            OutputDef od;
            od.index = util::json::GetInt(o, "index", static_cast<int>(i));
            od.name = Own(GetStr(o, "name"));
            od.type = Own(GetStr(o, "type"));
            od.isList = GetBool(o, "is_list", od.isList);
            od.tooltip = Own(GetStr(o, "tooltip"));
            nd.outputs.push_back(std::move(od));
        }
        // 规范允许乱序 → 按 index 升序；stable 保证同 index 时维持原始先后
        std::stable_sort(nd.outputs.begin(), nd.outputs.end(),
                         [](const OutputDef& a, const OutputDef& b) { return a.index < b.index; });
    }
    return nd;
}

// —— V1：input{required,optional,hidden}，单个输入是定长二元数组 [类型标识, 选项对象] ——

enum class V1Group { Required, Optional, Hidden };

void ParseV1Input(std::string_view keyName, yyjson_val* v, V1Group group, InputDef& in) {
    in.name = Own(keyName);
    in.isOptional = group != V1Group::Required; // v1 无 isOptional，由所属分组推断
    in.hidden = group == V1Group::Hidden;

    yyjson_val* ident = nullptr;
    yyjson_val* opts = nullptr;
    if (yyjson_is_arr(v)) {
        ident = yyjson_arr_get(v, 0);
        opts = yyjson_arr_get(v, 1);
    } else if (yyjson_is_str(v)) {
        ident = v; // 宽容：只给类型名的简写
    } else if (yyjson_is_obj(v)) {
        ident = Get(v, "type");
        opts = v; // 宽容：直接给选项对象
    }

    if (ident) {
        if (yyjson_is_arr(ident)) {
            in.options = ReadTextArray(ident); // 旧式 COMBO：选项就在类型位置的枚举数组里
            in.type = "COMBO";
        } else {
            in.type = util::ToUpper(ScalarToText(ident));
        }
    }
    in.widget = WidgetKindFor(in.type);
    if (in.widget == WidgetKind::None && !in.options.empty()) {
        in.widget = WidgetKind::Combo;
    }
    if (opts && yyjson_is_obj(opts)) {
        ReadCommonInputFlags(opts, in);
        ReadDefaultValue(opts, in);
        ReadWidgetFields(opts, in);
    }
    if (group == V1Group::Hidden) {
        in.hidden = true; // 分组是权威（v1 的选项对象一般不写 hidden）
    }
    in.isWidget = in.widget != WidgetKind::None;
}

[[nodiscard]] NodeTypeDef ParseV1Def(yyjson_val* def, std::string_view classNameFallback) {
    NodeTypeDef nd;
    nd.format = NodeDefFormat::V1;
    ReadTopLevel(def, classNameFallback, nd);

    yyjson_val* input = GetObj(def, "input");
    yyjson_val* order = GetObj(def, "input_order");
    constexpr std::array kGroups{
        std::pair{"required", V1Group::Required},
        std::pair{"optional", V1Group::Optional},
        std::pair{"hidden", V1Group::Hidden},
    };

    for (const auto& [groupName, group] : kGroups) {
        yyjson_val* groupMap = GetObj(input, groupName);
        if (!groupMap) {
            continue;
        }
        // 顺序：input_order[组] 优先，其余键按 JSON 出现顺序补在后面（去重）
        std::vector<std::string_view> ordered;
        ordered.reserve(yyjson_obj_size(groupMap));
        if (yyjson_val* arr = GetArr(order, groupName)) {
            const std::size_t n = yyjson_arr_size(arr);
            for (std::size_t i = 0; i < n; ++i) {
                const std::string_view nm = KeyView(yyjson_arr_get(arr, i));
                if (nm.empty() || !HasKey(groupMap, nm)) {
                    continue;
                }
                if (std::ranges::find(ordered, nm) == ordered.end()) {
                    ordered.push_back(nm);
                }
            }
        }
        yyjson_obj_iter it;
        yyjson_obj_iter_init(groupMap, &it);
        yyjson_val* key = nullptr;
        while ((key = yyjson_obj_iter_next(&it))) {
            const std::string_view nm = KeyView(key);
            if (!nm.empty() && std::ranges::find(ordered, nm) == ordered.end()) {
                ordered.push_back(nm);
            }
        }
        for (const std::string_view nm : ordered) {
            InputDef in;
            ParseV1Input(nm, Get(groupMap, nm), group, in);
            nd.inputs.push_back(std::move(in));
        }
    }

    // 输出侧是分散数组；index 用数组下标补齐
    if (yyjson_val* outTypes = GetArr(def, "output")) {
        yyjson_val* outIsList = GetArr(def, "output_is_list");
        yyjson_val* outNames = GetArr(def, "output_name");
        yyjson_val* outTips = GetArr(def, "output_tooltips");
        const std::size_t n = yyjson_arr_size(outTypes);
        nd.outputs.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            OutputDef od;
            od.index = static_cast<int>(i);
            od.type = ScalarToText(yyjson_arr_get(outTypes, i));
            od.name = ArrText(outNames, i);
            od.tooltip = ArrText(outTips, i);
            if (yyjson_val* b = (outIsList && yyjson_is_arr(outIsList) && i < yyjson_arr_size(outIsList))
                                    ? yyjson_arr_get(outIsList, i)
                                    : nullptr;
                b && yyjson_is_bool(b)) {
                od.isList = yyjson_get_bool(b);
            }
            nd.outputs.push_back(std::move(od));
        }
    }
    return nd;
}

// 「类名 → 定义」映射 → 节点列表
[[nodiscard]] std::vector<NodeTypeDef> ParseDefMap(yyjson_val* root, int& skippedOut) {
    std::vector<NodeTypeDef> nodes;
    skippedOut = 0;
    nodes.reserve(yyjson_obj_size(root));
    yyjson_obj_iter it;
    yyjson_obj_iter_init(root, &it);
    yyjson_val* key = nullptr;
    while ((key = yyjson_obj_iter_next(&it))) {
        yyjson_val* def = yyjson_obj_iter_get_val(key);
        if (!def || !yyjson_is_obj(def)) {
            ++skippedOut;
            continue;
        }
        const std::string_view className = KeyView(key);
        NodeTypeDef nd = LooksV2(def) ? ParseV2Def(def, className) : ParseV1Def(def, className);
        if (nd.className.empty()) {
            ++skippedOut; // 连类名都还原不出来，无法供后续使用
            continue;
        }
        nodes.push_back(std::move(nd));
    }
    return nodes;
}

struct DefCounts {
    int v1 = 0;
    int v2 = 0;
    std::size_t inputs = 0;
};

[[nodiscard]] DefCounts CountDefs(const std::vector<NodeTypeDef>& nodes) {
    DefCounts c;
    for (const NodeTypeDef& nd : nodes) {
        if (nd.format == NodeDefFormat::V2) {
            ++c.v2;
        } else {
            ++c.v1;
        }
        c.inputs += nd.inputs.size();
    }
    return c;
}

} // namespace

const char* NodeDefFormatLabel(NodeDefFormat f) noexcept {
    switch (f) {
    case NodeDefFormat::V2: return "v2";
    case NodeDefFormat::V1: return "v1";
    }
    return "?";
}

const char* WidgetKindLabel(WidgetKind k) noexcept {
    switch (k) {
    case WidgetKind::None: return "none";
    case WidgetKind::Int: return "INT";
    case WidgetKind::Float: return "FLOAT";
    case WidgetKind::Bool: return "BOOLEAN";
    case WidgetKind::Text: return "STRING";
    case WidgetKind::Combo: return "COMBO";
    }
    return "?";
}

const InputDef* NodeTypeDef::FindInput(std::string_view name) const noexcept {
    for (const InputDef& in : inputs) {
        if (in.name == name) {
            return &in;
        }
    }
    return nullptr;
}

std::vector<NodeTypeDef> ParseNodeDefs(std::string_view nodeDefsJson) {
    yyjson_doc* doc = yyjson_read(nodeDefsJson.data(), nodeDefsJson.size(), 0);
    if (!doc) {
        log::Warn("object_info JSON 解析失败（{} 字节），返回空节点列表", nodeDefsJson.size());
        return {};
    }
    std::vector<NodeTypeDef> nodes;
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        log::Warn("object_info 根不是对象（期望「类名 → 定义」映射），返回空节点列表");
    } else {
        int skipped = 0;
        nodes = ParseDefMap(root, skipped);
        const DefCounts c = CountDefs(nodes);
        if (skipped > 0) {
            log::Warn("object_info: {} 个节点定义不是对象或缺类名，已跳过", skipped);
            log::Info("object_info: {} classes parsed (v2 {} / v1 {}), {} inputs, {} skipped",
                      nodes.size(), c.v2, c.v1, c.inputs, skipped);
        } else {
            log::Info("object_info: {} classes parsed (v2 {} / v1 {}), {} inputs",
                      nodes.size(), c.v2, c.v1, c.inputs);
        }
    }
    yyjson_doc_free(doc);
    return nodes;
}

std::optional<NodeTypeDef> ParseNodeDef(std::string_view json, std::string_view className) {
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if (!doc) {
        log::Warn("node def JSON 解析失败（{} 字节）", json.size());
        return std::nullopt;
    }
    std::optional<NodeTypeDef> found;
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (root && yyjson_is_obj(root)) {
        const bool bareV2 = LooksV2(root);
        if (bareV2 || (HasKey(root, "input") && HasKey(root, "name"))) {
            // 裸定义（不套「类名 → 定义」映射）：GET /object_info/{class} 的某些版本直接给定义本体
            found = bareV2 ? ParseV2Def(root, className) : ParseV1Def(root, className);
        } else {
            int skipped = 0;
            for (NodeTypeDef& nd : ParseDefMap(root, skipped)) {
                if (className.empty() || nd.className == className) {
                    found = std::move(nd);
                    break;
                }
            }
        }
    }
    yyjson_doc_free(doc);
    if (!found) {
        log::Warn("node def 解析失败或找不到类：{}", className);
    }
    return found;
}

} // namespace shine::comfy
