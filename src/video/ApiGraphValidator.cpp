#include "video/ApiGraphValidator.h"

#include "core/Log.h"
#include "util/Strings.h"

#include <yyjson.h>

#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace shine::video {
namespace {

using util::FromInt;

// 动态输入的 io_type 标记（与 ComfyUI `comfy_api/latest/_io.py` 一致）
constexpr std::string_view kAutogrow = "COMFY_AUTOGROW_V3";
constexpr std::string_view kDynamicCombo = "COMFY_DYNAMICCOMBO_V3";

// 把一段路径（`a.b.c`）拆成段
[[nodiscard]] std::vector<std::string_view> SplitPath(std::string_view path) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t dot = path.find('.', start);
        if (dot == std::string_view::npos) {
            out.push_back(path.substr(start));
            break;
        }
        out.push_back(path.substr(start, dot - start));
        start = dot + 1;
    }
    return out;
}

[[nodiscard]] std::string JoinTail(const std::vector<std::string_view>& parts, std::size_t from) {
    std::string out;
    for (std::size_t i = from; i < parts.size(); ++i) {
        if (!out.empty()) {
            out += '.';
        }
        out += std::string(parts[i]);
    }
    return out;
}

// 取节点的某个直接输入的**字面值**（只认字符串/数字；连线返回空）
[[nodiscard]] std::string LiteralOf(yyjson_val* inputs, std::string_view name) {
    yyjson_val* value = yyjson_obj_getn(inputs, name.data(), name.size());
    if (value == nullptr) {
        return {};
    }
    if (yyjson_is_str(value)) {
        return std::string(yyjson_get_str(value), yyjson_get_len(value));
    }
    if (yyjson_is_num(value)) {
        return util::FromDouble(yyjson_get_num(value));
    }
    return {};
}

// 值是否在容器里（字符串视图比较）
template <class Container>
[[nodiscard]] bool Contains(const Container& items, std::string_view value) {
    return std::ranges::any_of(items, [value](const std::string& item) { return item == value; });
}

} // namespace

GraphCheckResult ValidateApiGraph(std::string_view apiJson, NodeDefLookup lookup) {
    GraphCheckResult result;
    if (apiJson.empty()) {
        result.issues.push_back({std::string{}, std::string{}, std::string{}, "API JSON 为空"});
        return result;
    }
    yyjson_doc* doc = yyjson_read(apiJson.data(), apiJson.size(), 0);
    if (doc == nullptr) {
        result.issues.push_back({std::string{}, std::string{}, std::string{}, "API JSON 解析失败（不是合法 JSON）"});
        return result;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        result.issues.push_back({std::string{}, std::string{}, std::string{}, "API JSON 根不是对象"});
        yyjson_doc_free(doc);
        return result;
    }

    // 节点 id 集合（连线校验用）
    std::set<std::string> ids;
    {
        yyjson_obj_iter iter = yyjson_obj_iter_with(root);
        while (yyjson_val* key = yyjson_obj_iter_next(&iter)) {
            ids.insert(std::string(yyjson_get_str(key), yyjson_get_len(key)));
        }
    }
    result.nodeCount = ids.size();

    if (lookup == nullptr) {
        result.ok = true;
        yyjson_doc_free(doc);
        return result;
    }

    yyjson_obj_iter iter = yyjson_obj_iter_with(root);
    while (yyjson_val* key = yyjson_obj_iter_next(&iter)) {
        const std::string nodeId(yyjson_get_str(key), yyjson_get_len(key));
        yyjson_val* node = yyjson_obj_iter_get_val(key);
        const std::string className = [&] {
            yyjson_val* cls = yyjson_obj_get(node, "class_type");
            return (cls != nullptr && yyjson_is_str(cls)) ? std::string(yyjson_get_str(cls), yyjson_get_len(cls))
                                                          : std::string{};
        }();
        if (className.empty()) {
            result.issues.push_back({nodeId, className, {}, "节点缺少 class_type"});
            continue;
        }
        const comfy::NodeTypeDef* def = lookup(className);
        if (def == nullptr) {
            result.issues.push_back({nodeId, className, {},
                                     "节点类「" + className + "」在当前 ComfyUI 的 /object_info 里不存在"
                                     "（缺自定义节点，或核心版本不支持）"});
            continue;
        }

        yyjson_val* inputs = yyjson_obj_get(node, "inputs");
        if (inputs != nullptr && !yyjson_is_obj(inputs)) {
            result.issues.push_back({nodeId, className, {}, "inputs 不是对象"});
            continue;
        }

        std::set<std::string> provided;
        yyjson_obj_iter inIter = yyjson_obj_iter_with(inputs);
        while (inputs != nullptr) { // 没有 inputs 的节点只走下面的"必填检查"
            yyjson_val* inKey = yyjson_obj_iter_next(&inIter);
            if (inKey == nullptr) {
                break;
            }
            const std::string inputName(yyjson_get_str(inKey), yyjson_get_len(inKey));
            yyjson_val* value = yyjson_obj_iter_get_val(inKey);
            provided.insert(inputName);
            ++result.inputCount;

            const std::vector<std::string_view> parts = SplitPath(inputName);
            const std::string_view head = parts.front();
            const comfy::InputDef* inputDef = def->FindInput(head);
            if (inputDef == nullptr) {
                result.issues.push_back({nodeId, className, inputName,
                                         "节点「" + className + "」没有输入「" + std::string(head) + "」"
                                         "（输入名与该 ComfyUI 版本不一致）"});
                continue;
            }

            // ① 连线：目标节点存在 + 槽位不越界
            if (yyjson_is_arr(value) && yyjson_arr_size(value) == 2) {
                yyjson_val* target = yyjson_arr_get(value, 0);
                yyjson_val* slot = yyjson_arr_get(value, 1);
                if (yyjson_is_str(target) && yyjson_is_num(slot)) {
                    const std::string targetId(yyjson_get_str(target), yyjson_get_len(target));
                    if (ids.find(targetId) == ids.end()) {
                        result.issues.push_back({nodeId, className, inputName,
                                                 "连线指向不存在的节点 id「" + targetId + "」"});
                    }
                    continue;
                }
            }

            // ② 动态输入的子键（`父.子`）
            if (parts.size() > 1) {
                if (inputDef->type == kAutogrow) {
                    // 合法子键 = `template.prefix` + 序号，序号范围 **[0, template.max)**（与 ComfyUI
                    // `_expand_schema_for_dynamic` 的 `names = [f"{prefix}{i}" for i in range(max)]` 一致）
                    const std::string_view child = parts[1];
                    bool matched = false;
                    if (!inputDef->dynamicPrefix.empty() && child.size() > inputDef->dynamicPrefix.size() &&
                        child.compare(0, inputDef->dynamicPrefix.size(), inputDef->dynamicPrefix) == 0) {
                        const std::string_view digits = child.substr(inputDef->dynamicPrefix.size());
                        const bool allDigits =
                            !digits.empty() && std::ranges::all_of(digits, [](char ch) { return ch >= '0' && ch <= '9'; });
                        if (allDigits) {
                            const int index = util::ToInt(digits).value_or(-1);
                            matched = index >= 0 && index < (inputDef->dynamicMax > 0 ? inputDef->dynamicMax : 10);
                        }
                    }
                    if (!matched) {
                        result.issues.push_back(
                            {nodeId, className, inputName,
                             "自动增长输入「" + std::string(head) + "」的子键「" + std::string(child) +
                                 "」不合法：本机允许的是 " + std::string(head) + "." + inputDef->dynamicPrefix + "0 …" +
                                 std::string(head) + "." + inputDef->dynamicPrefix +
                                 FromInt(std::max(0, inputDef->dynamicMax - 1)) + "（**序号从 0 开始**）"});
                    }
                    continue;
                }
                if (inputDef->type == kDynamicCombo) {
                    const std::string selected = LiteralOf(inputs, head);
                    if (selected.empty()) {
                        result.issues.push_back({nodeId, className, inputName,
                                                 "输入「" + std::string(head) + "」的子键「" + inputName +
                                                     "」需要父键先给出选项值"});
                    }
                    // 选中的选项内部还可能嵌套动态输入；本机定义里我们只保留了选项 key，
                    // 深层不再展开（我们自己的生成器不会产出这类子键）→ 只记一条日志。
                    continue;
                }
                result.issues.push_back({nodeId, className, inputName,
                                         "输入「" + inputName + "」带了点号，但「" + std::string(head) +
                                             "」不是动态输入（不能当父键用）"});
                continue;
            }

            // ③ 枚举类输入的值必须在允许列表里（普通 COMBO 看 options；动态组合看选项 key）
            //
            // ⚠️ **文件选择型 COMBO 必须豁免**：`LoadImage.image` 这类输入的 options 只是"当前 input 目录的
            // 快照"，而我们恰恰是**先上传、再用新文件名**去提交 —— 名字当然不在旧快照里（`image_upload: true`
            // 就是为这个场景准备的）。模型文件类 COMBO（unet_name / vae_name / clip_name / lora_name）
            // **不豁免**：值不在列表里说明模型没装，早点报出来比等执行时 500 好。
            const bool filePicker = inputDef->imageUpload || inputDef->videoUpload || !inputDef->imageFolder.empty();
            const std::vector<std::string>& allowed =
                inputDef->widget == comfy::WidgetKind::Combo ? inputDef->options : inputDef->dynamicOptionKeys;
            const bool isEnum = !allowed.empty() && !filePicker &&
                                (inputDef->widget == comfy::WidgetKind::Combo || inputDef->type == kDynamicCombo);
            if (isEnum && yyjson_is_str(value)) {
                const std::string_view v(yyjson_get_str(value), yyjson_get_len(value));
                if (!Contains(allowed, v)) {
                    std::string sample;
                    for (std::size_t i = 0; i < std::min<std::size_t>(allowed.size(), 6); ++i) {
                        if (!sample.empty()) {
                            sample += " / ";
                        }
                        sample += allowed[i];
                    }
                    result.issues.push_back({nodeId, className, inputName,
                                             "「" + className + "." + std::string(head) + "」的值「" + std::string(v) +
                                                 "」不在允许列表里（例如：" + sample + " …）"});
                }
            }
        }

        // ④ 必填输入都得给
        for (const comfy::InputDef& inputDef : def->inputs) {
            if (inputDef.isOptional || inputDef.hidden) {
                continue;
            }
            const bool dynamicParent = inputDef.type == kAutogrow || inputDef.type == kDynamicCombo;
            bool present = provided.find(inputDef.name) != provided.end();
            if (!present && dynamicParent) {
                // 动态父本身可以不出现（可选），但它的子键出现过也算
                for (const std::string& given : provided) {
                    if (given.size() > inputDef.name.size() && given.compare(0, inputDef.name.size(), inputDef.name) == 0 &&
                        given[inputDef.name.size()] == '.') {
                        present = true;
                        break;
                    }
                }
            }
            if (!present && !dynamicParent) {
                result.issues.push_back({nodeId, className, inputDef.name,
                                         "节点「" + className + "」缺少必填输入「" + inputDef.name + "」"});
            }
        }
    }

    yyjson_doc_free(doc);
    result.ok = result.issues.empty();
    return result;
}

} // namespace shine::video
