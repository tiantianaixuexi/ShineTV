#pragma once
// shine::comfy —— 节点定义结构化模型（NodeTypeDef）
//
// 规范基线（**新代码按新版本写，旧版本只做读取兼容**）：
//   - 目标格式：节点定义 JSON **v2.0**（`ComfyNodeDefV2`）—— 顶层 `inputs` 是**映射**、`outputs` 是数组（每项含 `index`/`is_list`）
//   - 只读兼容：节点定义 JSON **v1.0**（`ComfyNodeDefV1`）—— 顶层 `input{required,optional,hidden}`，单个输入是定长二元数组 `[类型标识, 选项对象]`
// 字段对照与出处：`Doc/RULES-COMFY.md` §12.6 A / B（获取方式与"自动识别形状"的实测要求见 §12.6 D）
//
// 解析入口**自动识别形状**（V2 判据：顶层有 `inputs` 对象，且 `outputs[]` 项含 `index`/`is_list`），不依赖外部开关。
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shine::comfy {

enum class NodeDefFormat { V1, V2 };                            // 解析来源形状（日志/诊断用）
enum class WidgetKind { None, Int, Float, Bool, Text, Combo };  // None = 纯连线输入（MODEL/IMAGE/…）

[[nodiscard]] const char* NodeDefFormatLabel(NodeDefFormat f) noexcept;  // "v1" / "v2"
[[nodiscard]] const char* WidgetKindLabel(WidgetKind k) noexcept;        // "INT" / … / "none"

struct InputDef {
    // 通用（v2 必填 type + name；v1 由 [类型, 选项对象] 与映射键还原）
    std::string name;
    std::string type;                     // INT/FLOAT/BOOLEAN/STRING/COMBO 或 MODEL/IMAGE/…
    std::vector<std::string> options;     // COMBO：v2 的 options；v1 的枚举数组
    WidgetKind widget = WidgetKind::None;
    bool isWidget = false;                // widget != None
    // 公共可选字段（v1 无 isOptional）
    bool isOptional = false;              // v2: isOptional；v1: 由 input.optional/hidden 分组推断
    bool forceInput = false;              // 强制连线，不渲染控件
    bool hidden = false;                  // UI 不显示
    bool advanced = false;                // 归入「高级」
    bool lazy = false, rawLink = false, defaultInput = false;
    std::string tooltip;
    // 默认值三形态（避免 JSON 数字/字符串/布尔混淆）
    bool hasDefaultString = false; std::string defaultString;
    bool hasDefaultNumber = false; double defaultNumber = 0.0;
    bool hasDefaultBool = false;   bool defaultBool = false;
    // INT / FLOAT
    bool hasMin = false, hasMax = false, hasStep = false;
    double min = 0.0, max = 0.0, step = 0.0;
    std::string display;                  // "" | "slider" | "number" | "knob"
    bool controlAfterGenerate = false;    // INT / FLOAT / COMBO
    bool hasRound = false; double round = 0.0;   // FLOAT：round 可为 false（= 未设置）
    // BOOLEAN
    std::string labelOn, labelOff;
    // STRING
    bool multiline = false, dynamicPrompts = false;
    std::string placeholder;
    // COMBO 扩展（本步只记录；远端刷新不做）
    bool imageUpload = false, videoUpload = false, allowBatch = false;
    std::string imageFolder;              // input | output | temp
    std::string remoteRoute;              // v2: COMBO.remote.route
    // —— 动态输入（P5.5 加：`COMFY_AUTOGROW_V3` / `COMFY_DYNAMICCOMBO_V3`）——
    // 目的：让"用错输入名"在**编译期**就被抓住（ComfyUI 的 /prompt 只查必填缺失，
    // 写错的键会被**静默忽略** —— 例如 autogrow 的子键位偏移会让最后一张参考图直接消失）。
    std::string dynamicPrefix;                  // autogrow: `template.prefix`（如 "ref_image_"）
    int dynamicMax = 0;                         // autogrow: `template.max`（合法序号 0..max-1）
    std::vector<std::string> dynamicOptionKeys; // dynamic combo: `options[].key`
    [[nodiscard]] bool isEnum() const noexcept { return widget == WidgetKind::Combo && !options.empty(); }
    // 是否是"需要按规则展开子键"的动态输入
    [[nodiscard]] bool isDynamic() const noexcept {
        return type == "COMFY_AUTOGROW_V3" || type == "COMFY_DYNAMICCOMBO_V3";
    }
};

struct OutputDef {
    int index = 0;                        // v2 必填；v1 用数组下标补齐
    std::string name, type;
    bool isList = false;                  // v2: is_list（v1: output_is_list）
    std::string tooltip;
};

struct NodeTypeDef {
    std::string className;                // v2: name（同时作为 VNS 工厂 type）
    std::string displayName;              // v2: display_name
    std::string description;              // v2: description
    std::string category;                 // v2: category，用于节点面板分组
    std::string pythonModule;             // v2: python_module
    bool outputNode = false;              // v2: output_node
    bool deprecated = false, experimental = false;
    NodeDefFormat format = NodeDefFormat::V2;
    std::vector<InputDef> inputs;         // 顺序：v1 按 input_order；v2 按 JSON 出现顺序
    std::vector<OutputDef> outputs;
    [[nodiscard]] const InputDef* FindInput(std::string_view name) const noexcept;
};

// 自动识别形状并解析（V2 判据：顶层有 inputs 对象 且 outputs[] 项含 index/is_list）
[[nodiscard]] std::vector<NodeTypeDef> ParseNodeDefs(std::string_view nodeDefsJson);
// 单节点：GET /object_info/{class} 或单节点 V2 定义
[[nodiscard]] std::optional<NodeTypeDef> ParseNodeDef(std::string_view json, std::string_view className);

} // namespace shine::comfy
