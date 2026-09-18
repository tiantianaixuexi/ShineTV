#include "graph/GraphCompiler.h"

#include "core/Log.h"
#include "graph/ComfyNode.h"
#include "graph/GraphHost.h"
#include "util/Strings.h"

#include <VisualNode.h>
#include <VisualNodeSocket.h>
#include <yyjson.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace shine::graph {
namespace {

using VisNodeSys::Node;

// 控件值按 InputDef 的类型写进 JSON（**不能写字符串化的数字**，ComfyUI 会校验类型）
void AddWidgetValue(yyjson_mut_doc* doc, yyjson_mut_val* inputs, const comfy::InputDef& in, const std::string& raw) {
    using comfy::WidgetKind;
    switch (in.widget) {
    case WidgetKind::Int: {
        const double v = util::ToDouble(raw).value_or(0.0);
        (void)yyjson_mut_obj_add_int(doc, inputs, in.name.c_str(), static_cast<std::int64_t>(v));
        break;
    }
    case WidgetKind::Float: {
        const double v = util::ToDouble(raw).value_or(0.0);
        (void)yyjson_mut_obj_add_real(doc, inputs, in.name.c_str(), v);
        break;
    }
    case WidgetKind::Bool:
        (void)yyjson_mut_obj_add_bool(doc, inputs, in.name.c_str(), raw == "true" || raw == "1");
        break;
    case WidgetKind::Text:
    case WidgetKind::Combo:
        (void)yyjson_mut_obj_add_strcpy(doc, inputs, in.name.c_str(), raw.c_str());
        break;
    case WidgetKind::None:
        break; // 纯连线输入没有控件值
    }
}

} // namespace

CompileResult CompileToApiJson() {
    CompileResult result;
    const std::vector<Node*> nodes = CanvasNodes();
    if (nodes.empty()) {
        result.errors.push_back({std::string{}, std::string{}, "画布为空：请先添加节点再运行"});
        return result;
    }

    // 1) 过滤非 ComfyUI 节点 + 建立**确定性** id（画布遍历顺序 → 1..N）
    std::unordered_map<std::string, std::string> idMap;
    std::vector<std::pair<Node*, ShineComfyNode*>> targets;
    int nextId = 1;
    for (Node* n : nodes) {
        if (n == nullptr) {
            continue;
        }
        auto* cn = dynamic_cast<ShineComfyNode*>(n);
        if (cn == nullptr || !cn->Def()) {
            result.errors.push_back({n->GetID(), n->GetName(),
                                     "不是 ComfyUI 节点（请删除它，或改用 object_info 里的真实节点）"});
            continue;
        }
        idMap[n->GetID()] = std::to_string(nextId++);
        targets.emplace_back(n, cn);
    }
    if (!result.errors.empty()) {
        return result; // 图里混着非 ComfyUI 节点 → 不产出半成品
    }
    if (targets.empty()) {
        result.errors.push_back({std::string{}, std::string{}, "画布上没有可编译的节点"});
        return result;
    }

    // 2) 连线索引：(下游节点, 输入槽位) → (上游 id, 输出槽位)
    struct Endpoint {
        std::string fromId;
        std::size_t fromSlot = 0;
        std::size_t passedThrough = 0;
    };
    std::map<std::pair<std::string, std::size_t>, Endpoint> incoming;
    for (const GraphLink& link : EnumerateLinks()) {
        const auto fromIt = idMap.find(link.fromNodeId);
        if (fromIt == idMap.end()) {
            continue; // 上游不是 ComfyUI 节点（编译失败已在上面报过）
        }
        incoming[{link.toNodeId, link.toSocketIndex}] = Endpoint{fromIt->second, link.fromSocketIndex, link.passedThrough};
    }

    // 3) 组装 `{ "<id>": { "class_type": ..., "inputs": {...} } }`
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    std::size_t compiled = 0;

    for (const auto& [node, cn] : targets) {
        const comfy::NodeTypeDef& def = *cn->Def();
        const std::string vnsId = node->GetID();
        yyjson_mut_val* nodeObj = yyjson_mut_obj(doc);
        (void)yyjson_mut_obj_add_strcpy(doc, nodeObj, "class_type", def.className.c_str());
        yyjson_mut_val* inputs = yyjson_mut_obj(doc);
        bool nodeOk = true;

        for (std::size_t i = 0; i < def.inputs.size(); ++i) {
            const comfy::InputDef& in = def.inputs[i];
            const auto linkIt = incoming.find({vnsId, i});
            if (linkIt != incoming.end()) {
                yyjson_mut_val* ref = yyjson_mut_arr(doc);
                (void)yyjson_mut_arr_add_strcpy(doc, ref, linkIt->second.fromId.c_str());
                (void)yyjson_mut_arr_add_int(doc, ref, static_cast<std::int64_t>(linkIt->second.fromSlot));
                (void)yyjson_mut_obj_add_val(doc, inputs, in.name.c_str(), ref);
                if (linkIt->second.passedThrough > 0) {
                    log::Info("编译：节点「{}」输入「{}」经 {} 个透传节点后接到 id={}", def.displayName, in.name,
                              linkIt->second.passedThrough, linkIt->second.fromId);
                }
                continue;
            }
            if (in.isWidget && !in.forceInput && cn->HasWidgetValue(in.name)) {
                AddWidgetValue(doc, inputs, in, cn->WidgetValue(in.name));
                continue;
            }
            if (in.isOptional) {
                continue; // 可选输入允许缺省（ComfyUI 自己会用默认值）
            }
            result.errors.push_back({vnsId, def.displayName, "必填输入「" + in.name + "」既未连线也没有值"});
            nodeOk = false;
        }

        if (!nodeOk) {
            yyjson_mut_doc_free(doc);
            return result; // 有任何必填缺失就不产出 JSON
        }
        (void)yyjson_mut_obj_add_val(doc, nodeObj, "inputs", inputs);
        (void)yyjson_mut_obj_add_val(doc, root, idMap[vnsId].c_str(), nodeObj);
        ++compiled;
    }

    // 4) 序列化（对象键顺序 = 插入顺序 → 同一张图两次编译字节一致）
    char* text = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, nullptr);
    if (text == nullptr) {
        yyjson_mut_doc_free(doc);
        result.errors.push_back({std::string{}, std::string{}, "API JSON 序列化失败"});
        return result;
    }
    result.apiJson.assign(text);
    std::free(text);
    yyjson_mut_doc_free(doc);
    result.ok = true;
    result.nodeCount = compiled;
    return result;
}

} // namespace shine::graph
