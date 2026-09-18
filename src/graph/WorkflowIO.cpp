#include "graph/WorkflowIO.h"

#include "comfy/ComfySession.h"
#include "core/Log.h"
#include "graph/ComfyNode.h"
#include "graph/GraphHost.h"
#include "util/Strings.h"

#include <VisualNode.h>
#include <VisualNodeSocket.h>
#include <VisualNodeSystem.h>
#include <imgui.h>
#include <yyjson.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace shine::graph {
namespace {

using VisNodeSys::Node;
using VisNodeSys::NodeArea;

bool WriteFileText(std::string_view path, std::string_view text) {
    std::ofstream file(std::string{path}, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return file.good();
}

// JSON 标量 → 控件字符串（宽容：对象/数组给空串）
[[nodiscard]] std::string JsonToText(yyjson_val* v) {
    if (v == nullptr || yyjson_is_null(v)) {
        return {};
    }
    if (yyjson_is_str(v)) {
        const char* s = yyjson_get_str(v);
        return s != nullptr ? std::string{s} : std::string{};
    }
    if (yyjson_is_bool(v)) {
        return yyjson_get_bool(v) ? "true" : "false";
    }
    if (yyjson_is_num(v)) {
        const double d = yyjson_get_num(v);
        const auto i = static_cast<std::int64_t>(d);
        return (static_cast<double>(i) == d) ? std::to_string(i) : util::FromDouble(d);
    }
    return {};
}

// `["<id>", slot]` → {id, slot}
[[nodiscard]] std::optional<std::pair<std::string, int>> ReadLinkRef(yyjson_val* v) {
    if (v == nullptr || !yyjson_is_arr(v) || yyjson_arr_size(v) != 2) {
        return std::nullopt;
    }
    yyjson_val* idVal = yyjson_arr_get(v, 0);
    yyjson_val* slotVal = yyjson_arr_get(v, 1);
    if (!yyjson_is_str(idVal) || !yyjson_is_num(slotVal)) {
        return std::nullopt;
    }
    return std::make_pair(std::string{yyjson_get_str(idVal)}, static_cast<int>(yyjson_get_sint(slotVal)));
}

// v1.0 的 pos/size 支持 [x,y] 与 {0:x,1:y} 两种写法
[[nodiscard]] std::optional<ImVec2> ReadVec2(yyjson_val* v) {
    if (v == nullptr || yyjson_is_null(v)) {
        return std::nullopt;
    }
    if (yyjson_is_arr(v) && yyjson_arr_size(v) >= 2) {
        return ImVec2(static_cast<float>(yyjson_get_num(yyjson_arr_get(v, 0))),
                      static_cast<float>(yyjson_get_num(yyjson_arr_get(v, 1))));
    }
    if (yyjson_is_obj(v)) {
        yyjson_val* x = yyjson_obj_get(v, "0");
        yyjson_val* y = yyjson_obj_get(v, "1");
        if (x != nullptr && y != nullptr) {
            return ImVec2(static_cast<float>(yyjson_get_num(x)), static_cast<float>(yyjson_get_num(y)));
        }
    }
    return std::nullopt;
}

// 数组里第 `index` 项当有符号整数读（越界/类型不符 → 0）
[[nodiscard]] std::int64_t ArrSint(yyjson_val* arr, std::size_t index) {
    yyjson_val* v = yyjson_arr_get(arr, index);
    return (v != nullptr && yyjson_is_num(v)) ? yyjson_get_sint(v) : 0;
}

// 导入后左上角留的边距（工作流格式导入会把整图平移到这个位置附近，见 ImportWorkflowJson）
constexpr float kImportMargin = 60.0f;

[[nodiscard]] ImVec2 GridPosition(std::size_t index) {
    const float col = static_cast<float>(index % 4);
    const float row = static_cast<float>(index / 4);
    return ImVec2(60.0f + col * 420.0f, 60.0f + row * 520.0f);
}

// 输入名 → 输入槽位（插口与输入 1:1，见 graph/ComfyNode.h）
[[nodiscard]] std::size_t InputSlotOf(const ShineComfyNode& node, std::string_view name) {
    const auto& def = node.Def();
    if (!def) {
        return 0;
    }
    for (std::size_t i = 0; i < def->inputs.size(); ++i) {
        if (def->inputs[i].name == name) {
            return i;
        }
    }
    return 0;
}

// —— P3.7：按**名字**找槽位（下标的兜底路径）——
// 为什么需要：`/object_info` 给我们的输入/输出顺序，和前端 JSON 里 `nodes[].inputs[]/outputs[]`
// 的数组顺序**不保证一致**（官方模板加载后是 26 节点 / 17 条连线接不上，就是栽在这）。
[[nodiscard]] std::optional<std::size_t> InputSlotByName(const ShineComfyNode& node, std::string_view name) {
    const auto& def = node.Def();
    if (!def || name.empty()) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < def->inputs.size(); ++i) {
        if (def->inputs[i].name == name) {
            return i;
        }
    }
    // JSON 里写的是 `父.子`（autogrow / dynamic combo 展开出来的键），
    // 而我们的定义里**只有一个父插口** → 归到父插口上（P5.5 校验器同一套父子语义）。
    if (const std::size_t dot = name.find('.'); dot != std::string_view::npos) {
        const std::string_view head = name.substr(0, dot);
        for (std::size_t i = 0; i < def->inputs.size(); ++i) {
            if (def->inputs[i].name == head) {
                return i;
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> OutputSlotByName(const ShineComfyNode& node, std::string_view name) {
    const auto& def = node.Def();
    if (!def || name.empty()) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < def->outputs.size(); ++i) {
        if (def->outputs[i].name == name) {
            return i;
        }
    }
    return std::nullopt;
}

// JSON 节点里 `inputs[j]/outputs[j]` 的 `name`（越界/类型不符 → 空串）
[[nodiscard]] std::string JsonSlotName(yyjson_val* jnode, const char* key, std::size_t index) {
    yyjson_val* arr = yyjson_obj_get(jnode, key);
    if (!yyjson_is_arr(arr) || index >= yyjson_arr_size(arr)) {
        return {};
    }
    return JsonToText(yyjson_obj_get(yyjson_arr_get(arr, index), "name"));
}

void AddWidgetJsonValue(yyjson_mut_doc* doc, yyjson_mut_val* arr, const comfy::InputDef& in, const std::string& raw) {
    switch (in.widget) {
    case comfy::WidgetKind::Int:
        (void)yyjson_mut_arr_add_int(doc, arr, static_cast<std::int64_t>(util::ToDouble(raw).value_or(0.0)));
        break;
    case comfy::WidgetKind::Float:
        (void)yyjson_mut_arr_add_real(doc, arr, util::ToDouble(raw).value_or(0.0));
        break;
    case comfy::WidgetKind::Bool:
        (void)yyjson_mut_arr_add_bool(doc, arr, raw == "true" || raw == "1");
        break;
    case comfy::WidgetKind::Text:
    case comfy::WidgetKind::Combo:
        (void)yyjson_mut_arr_add_strcpy(doc, arr, raw.c_str());
        break;
    case comfy::WidgetKind::None:
        break;
    }
}

} // namespace

const char* WorkflowFormatLabel(WorkflowFormat f) noexcept {
    switch (f) {
    case WorkflowFormat::ApiFormat: return "API";
    case WorkflowFormat::WorkflowV1: return "工作流 v1.0";
    case WorkflowFormat::WorkflowV0_4: return "工作流 0.4（旧版）";
    case WorkflowFormat::Unknown: return "未知";
    }
    return "未知";
}

WorkflowFormat DetectFormat(std::string_view jsonText) noexcept {
    yyjson_doc* doc = yyjson_read(jsonText.data(), jsonText.size(), 0);
    if (doc == nullptr) {
        return WorkflowFormat::Unknown;
    }
    WorkflowFormat format = WorkflowFormat::Unknown;
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (yyjson_is_obj(root)) {
        // API 格式：任一成员含 class_type
        yyjson_obj_iter it;
        yyjson_obj_iter_init(root, &it);
        yyjson_val* key = nullptr;
        bool hasClassType = false;
        while ((key = yyjson_obj_iter_next(&it))) {
            yyjson_val* val = yyjson_obj_iter_get_val(key);
            if (yyjson_is_obj(val) && yyjson_obj_get(val, "class_type") != nullptr) {
                hasClassType = true;
                break;
            }
        }
        if (hasClassType) {
            format = WorkflowFormat::ApiFormat;
        } else if (yyjson_obj_get(root, "nodes") != nullptr ||
                   yyjson_obj_get(root, "version") != nullptr) {
            yyjson_val* version = yyjson_obj_get(root, "version");
            format = (version != nullptr && yyjson_is_num(version) && yyjson_get_sint(version) == 1)
                         ? WorkflowFormat::WorkflowV1
                         : WorkflowFormat::WorkflowV0_4;
        }
    }
    yyjson_doc_free(doc);
    return format;
}

bool ExportApiJson(std::string_view filePath) {
    const CompileResult compiled = CompileToApiJson();
    if (!compiled.ok) {
        log::Error("导出 API JSON 失败：图编译未通过（{} 项错误）", compiled.errors.size());
        for (const CompileError& e : compiled.errors) {
            log::Warn("  {}{}", e.nodeName.empty() ? std::string{} : ("节点「" + e.nodeName + "」"), e.message);
        }
        return false;
    }
    if (!WriteFileText(filePath, compiled.apiJson)) {
        log::Error("写入失败：{}", filePath);
        return false;
    }
    log::Info("已导出 API JSON：{}（{} 个节点）", filePath, compiled.nodeCount);
    return true;
}

ImportReport ImportApiJson(std::string_view jsonText) {
    ImportReport report;
    report.format = WorkflowFormat::ApiFormat;
    yyjson_doc* doc = yyjson_read(jsonText.data(), jsonText.size(), 0);
    if (doc == nullptr) {
        report.warnings.emplace_back("JSON 解析失败（文件可能残缺）");
        return report;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    NodeArea* area = Area();
    if (!yyjson_is_obj(root) || area == nullptr) {
        report.warnings.emplace_back("不是 API 格式对象（期望 { \"<id>\": { \"class_type\": … } }）");
        yyjson_doc_free(doc);
        return report;
    }

    std::unordered_map<std::string, Node*> byId;
    struct PendingLink {
        std::string fromId;
        std::size_t fromSlot = 0;
        Node* to = nullptr;
        std::size_t toSlot = 0;
    };
    std::vector<PendingLink> pending;
    std::size_t placed = 0;

    yyjson_obj_iter it;
    yyjson_obj_iter_init(root, &it);
    yyjson_val* key = nullptr;
    while ((key = yyjson_obj_iter_next(&it))) {
        const char* idStr = yyjson_get_str(key);
        yyjson_val* nodeVal = yyjson_obj_iter_get_val(key);
        if (idStr == nullptr || !yyjson_is_obj(nodeVal)) {
            continue;
        }
        const std::string classType = JsonToText(yyjson_obj_get(nodeVal, "class_type"));
        if (classType.empty()) {
            report.warnings.emplace_back("有条目缺少 class_type，已跳过");
            continue;
        }
        if (!IsRegistered(classType)) {
            report.unknownTypes.push_back(classType); // 不静默丢弃：记下来在面板里提示
            continue;
        }
        auto* node = dynamic_cast<ShineComfyNode*>(CreateNodeByType(classType));
        if (node == nullptr) {
            report.unknownTypes.push_back(classType);
            continue;
        }
        node->SetPosition(GridPosition(placed++));
        area->AddNode(node);
        byId[idStr] = node;
        ++report.nodes;

        yyjson_val* inputs = yyjson_obj_get(nodeVal, "inputs");
        if (inputs != nullptr && yyjson_is_obj(inputs)) {
            yyjson_obj_iter iit;
            yyjson_obj_iter_init(inputs, &iit);
            yyjson_val* ik = nullptr;
            while ((ik = yyjson_obj_iter_next(&iit))) {
                const char* inName = yyjson_get_str(ik);
                yyjson_val* val = yyjson_obj_iter_get_val(ik);
                if (inName == nullptr) {
                    continue;
                }
                if (const auto link = ReadLinkRef(val); link.has_value()) {
                    PendingLink pl;
                    pl.fromId = link->first;
                    pl.fromSlot = static_cast<std::size_t>(link->second);
                    pl.to = node;
                    pl.toSlot = InputSlotOf(*node, inName);
                    pending.push_back(std::move(pl));
                } else {
                    node->SetWidgetValue(inName, JsonToText(val));
                }
            }
        }
    }
    yyjson_doc_free(doc);

    for (const PendingLink& link : pending) {
        const auto fromIt = byId.find(link.fromId);
        if (fromIt == byId.end() || link.to == nullptr) {
            report.warnings.emplace_back("连线的上游节点不存在（可能被跳过），已忽略该连线");
            continue;
        }
        if (area->TryToConnect(fromIt->second, link.fromSlot, link.to, link.toSlot)) {
            ++report.links;
        } else {
            report.warnings.emplace_back("有一条连线无法建立（类型不匹配或槽位越界）");
        }
    }
    CenterView();
    report.ok = report.nodes > 0;
    log::Info("导入 API JSON：节点 {}，连线 {}，未知类型 {}，告警 {}", report.nodes, report.links,
              report.unknownTypes.size(), report.warnings.size());
    return report;
}

bool ExportWorkflowV1(std::string_view filePath) {
    NodeArea* area = Area();
    if (area == nullptr) {
        return false;
    }
    const std::vector<Node*> nodes = CanvasNodes();
    std::unordered_map<std::string, int> idMap; // VNS ID → 工作流 id（与图编译器同一发号顺序）
    int nextId = 1;
    for (Node* n : nodes) {
        auto* cn = dynamic_cast<ShineComfyNode*>(n);
        if (cn != nullptr && cn->Def()) {
            idMap[n->GetID()] = nextId++;
        }
    }
    if (idMap.empty()) {
        log::Warn("导出工作流失败：画布上没有 ComfyUI 节点");
        return false;
    }

    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    (void)yyjson_mut_obj_add_int(doc, root, "version", 1);

    yyjson_mut_val* jnodes = yyjson_mut_arr(doc);
    int order = 0;
    for (Node* n : nodes) {
        auto* cn = dynamic_cast<ShineComfyNode*>(n);
        if (cn == nullptr || !cn->Def()) {
            continue;
        }
        const comfy::NodeTypeDef& def = *cn->Def();
        yyjson_mut_val* jn = yyjson_mut_obj(doc);
        (void)yyjson_mut_obj_add_int(doc, jn, "id", idMap[n->GetID()]);
        (void)yyjson_mut_obj_add_strcpy(doc, jn, "type", def.className.c_str());

        const ImVec2 pos = n->GetPosition();
        const ImVec2 size = n->GetSize();
        yyjson_mut_val* jpos = yyjson_mut_arr(doc);
        (void)yyjson_mut_arr_add_real(doc, jpos, static_cast<double>(pos.x));
        (void)yyjson_mut_arr_add_real(doc, jpos, static_cast<double>(pos.y));
        (void)yyjson_mut_obj_add_val(doc, jn, "pos", jpos);

        yyjson_mut_val* jsize = yyjson_mut_arr(doc);
        (void)yyjson_mut_arr_add_real(doc, jsize, static_cast<double>(size.x));
        (void)yyjson_mut_arr_add_real(doc, jsize, static_cast<double>(size.y));
        (void)yyjson_mut_obj_add_val(doc, jn, "size", jsize);

        (void)yyjson_mut_obj_add_val(doc, jn, "flags", yyjson_mut_obj(doc));
        (void)yyjson_mut_obj_add_int(doc, jn, "order", order++);
        (void)yyjson_mut_obj_add_int(doc, jn, "mode", 0);
        yyjson_mut_val* jprops = yyjson_mut_obj(doc);
        (void)yyjson_mut_obj_add_strcpy(doc, jprops, "Node name for S&R", def.displayName.c_str());
        (void)yyjson_mut_obj_add_val(doc, jn, "properties", jprops);

        // widgets_values：按**输入定义顺序**只写控件输入（forceInput 不算）
        yyjson_mut_val* jwidgets = yyjson_mut_arr(doc);
        for (const comfy::InputDef& in : def.inputs) {
            if (in.widget == comfy::WidgetKind::None || in.forceInput) {
                continue;
            }
            AddWidgetJsonValue(doc, jwidgets, in, cn->WidgetValue(in.name));
        }
        (void)yyjson_mut_obj_add_val(doc, jn, "widgets_values", jwidgets);
        (void)yyjson_mut_arr_add_val(jnodes, jn);
    }
    (void)yyjson_mut_obj_add_val(doc, root, "nodes", jnodes);

    // links：`{id, origin_id, origin_slot, target_id, target_slot, type}`
    yyjson_mut_val* jlinks = yyjson_mut_arr(doc);
    int linkId = 1;
    for (const GraphLink& link : EnumerateLinks()) {
        const auto fromIt = idMap.find(link.fromNodeId);
        const auto toIt = idMap.find(link.toNodeId);
        if (fromIt == idMap.end() || toIt == idMap.end()) {
            continue;
        }
        yyjson_mut_val* jl = yyjson_mut_obj(doc);
        (void)yyjson_mut_obj_add_int(doc, jl, "id", linkId++);
        (void)yyjson_mut_obj_add_int(doc, jl, "origin_id", fromIt->second);
        (void)yyjson_mut_obj_add_int(doc, jl, "origin_slot", static_cast<std::int64_t>(link.fromSocketIndex));
        (void)yyjson_mut_obj_add_int(doc, jl, "target_id", toIt->second);
        (void)yyjson_mut_obj_add_int(doc, jl, "target_slot", static_cast<std::int64_t>(link.toSocketIndex));
        (void)yyjson_mut_obj_add_strcpy(doc, jl, "type", link.type.c_str());
        (void)yyjson_mut_arr_add_val(jlinks, jl);
    }
    (void)yyjson_mut_obj_add_val(doc, root, "links", jlinks);

    // groups：VNS 的分组框在 NodeArea 里没有公开枚举接口 → 不导出（导入侧支持读出边界）
    (void)yyjson_mut_obj_add_val(doc, root, "groups", yyjson_mut_arr(doc));

    yyjson_mut_val* jstate = yyjson_mut_obj(doc);
    (void)yyjson_mut_obj_add_int(doc, jstate, "lastNodeId", nextId - 1);
    (void)yyjson_mut_obj_add_int(doc, jstate, "lastLinkId", linkId - 1);
    (void)yyjson_mut_obj_add_int(doc, jstate, "lastGroupid", 0);
    (void)yyjson_mut_obj_add_int(doc, jstate, "lastRerouteId", 0);
    (void)yyjson_mut_obj_add_val(doc, root, "state", jstate);

    yyjson_mut_val* jextra = yyjson_mut_obj(doc);
    yyjson_mut_val* jds = yyjson_mut_obj(doc);
    (void)yyjson_mut_obj_add_real(doc, jds, "scale", static_cast<double>(area->GetZoomFactor()));
    const ImVec2 offset = area->GetRenderOffset();
    yyjson_mut_val* joffset = yyjson_mut_arr(doc);
    (void)yyjson_mut_arr_add_real(doc, joffset, static_cast<double>(offset.x));
    (void)yyjson_mut_arr_add_real(doc, joffset, static_cast<double>(offset.y));
    (void)yyjson_mut_obj_add_val(doc, jds, "offset", joffset);
    (void)yyjson_mut_obj_add_val(doc, jextra, "ds", jds);
    (void)yyjson_mut_obj_add_val(doc, root, "extra", jextra);

    char* text = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, nullptr);
    yyjson_mut_doc_free(doc);
    if (text == nullptr) {
        log::Error("工作流 JSON 序列化失败");
        return false;
    }
    const bool written = WriteFileText(filePath, text);
    std::free(text);
    if (!written) {
        log::Error("写入失败：{}", filePath);
        return false;
    }
    log::Info("已导出工作流 v1.0：{}（{} 个节点 / {} 条连线）", filePath, idMap.size(), linkId - 1);
    return true;
}

ImportReport ImportWorkflowJson(std::string_view jsonText) {
    ImportReport report;
    yyjson_doc* doc = yyjson_read(jsonText.data(), jsonText.size(), 0);
    if (doc == nullptr) {
        report.warnings.emplace_back("JSON 解析失败（文件可能残缺）");
        return report;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    NodeArea* area = Area();
    if (!yyjson_is_obj(root) || area == nullptr) {
        report.warnings.emplace_back("不是工作流 JSON（期望顶层有 nodes）");
        yyjson_doc_free(doc);
        return report;
    }
    const std::int64_t version = yyjson_get_sint(yyjson_obj_get(root, "version"));
    report.format = (version == 1) ? WorkflowFormat::WorkflowV1 : WorkflowFormat::WorkflowV0_4;
    if (version != 1) {
        report.warnings.emplace_back("工作流 version != 1：按旧版 0.4 兼容读取");
    }

    std::unordered_map<std::int64_t, Node*> byWfId;
    std::unordered_map<std::int64_t, yyjson_val*> jsonById; // 连线兜底按名字找槽位要用（P3.7）
    std::size_t placed = 0;

    if (yyjson_val* jnodes = yyjson_obj_get(root, "nodes"); yyjson_is_arr(jnodes)) {
        const std::size_t count = yyjson_arr_size(jnodes);
        for (std::size_t i = 0; i < count; ++i) {
            yyjson_val* jn = yyjson_arr_get(jnodes, i);
            if (!yyjson_is_obj(jn)) {
                continue;
            }
            const std::string type = JsonToText(yyjson_obj_get(jn, "type"));
            if (type.empty()) {
                continue;
            }
            if (!IsRegistered(type)) {
                report.unknownTypes.push_back(type);
                continue;
            }
            auto* node = dynamic_cast<ShineComfyNode*>(CreateNodeByType(type));
            if (node == nullptr) {
                report.unknownTypes.push_back(type);
                continue;
            }
            node->SetPosition(ReadVec2(yyjson_obj_get(jn, "pos")).value_or(GridPosition(placed++)));
            if (const auto size = ReadVec2(yyjson_obj_get(jn, "size")); size.has_value()) {
                node->SetSize(*size);
            }
            area->AddNode(node);
            const std::int64_t wfId = yyjson_get_sint(yyjson_obj_get(jn, "id"));
            byWfId[wfId] = node;
            jsonById[wfId] = jn;
            ++report.nodes;

            // widgets_values 按**输入定义顺序**回填（顺序不一致会错位 → 校验并告警）
            // 定义直接取节点自带的（不依赖 ComfySession 缓存是否已填充）
            if (yyjson_val* jwidgets = yyjson_obj_get(jn, "widgets_values"); yyjson_is_arr(jwidgets)) {
                const comfy::NodeTypeDef* def = node->Def().get();
                std::size_t cursor = 0;
                std::size_t expected = 0;
                if (def != nullptr) {
                    for (const comfy::InputDef& in : def->inputs) {
                        if (in.widget == comfy::WidgetKind::None || in.forceInput) {
                            continue;
                        }
                        if (cursor >= yyjson_arr_size(jwidgets)) {
                            break;
                        }
                        node->SetWidgetValue(in.name, JsonToText(yyjson_arr_get(jwidgets, cursor)));
                        ++cursor;
                        ++expected;
                    }
                }
                if (def != nullptr && expected < yyjson_arr_size(jwidgets)) {
                    report.warnings.emplace_back(
                        fmt::format("节点「{}」的 widgets_values 比输入定义多 {} 项（定义可能已变），多余项已忽略",
                                    type, yyjson_arr_size(jwidgets) - expected));
                }
            }
        }
    }

    if (yyjson_val* jlinks = yyjson_obj_get(root, "links"); yyjson_is_arr(jlinks)) {
        const std::size_t count = yyjson_arr_size(jlinks);
        std::size_t badShape = 0;
        for (std::size_t i = 0; i < count; ++i) {
            yyjson_val* jl = yyjson_arr_get(jlinks, i);
            std::int64_t linkId = 0;
            std::int64_t originId = 0;
            std::int64_t targetId = 0;
            std::int64_t originSlot = 0;
            std::int64_t targetSlot = 0;
            if (yyjson_is_obj(jl)) {
                // v1.0：`{id, origin_id, origin_slot, target_id, target_slot, type}`
                linkId = yyjson_get_sint(yyjson_obj_get(jl, "id"));
                originId = yyjson_get_sint(yyjson_obj_get(jl, "origin_id"));
                targetId = yyjson_get_sint(yyjson_obj_get(jl, "target_id"));
                originSlot = yyjson_get_sint(yyjson_obj_get(jl, "origin_slot"));
                targetSlot = yyjson_get_sint(yyjson_obj_get(jl, "target_slot"));
            } else if (yyjson_is_arr(jl) && yyjson_arr_size(jl) >= 5) {
                // v0.4（**官方模板全是这个**）：`[link_id, origin_id, origin_slot, target_id, target_slot, type]`
                // 2026-09-17 实测：只认对象形式会把 0.4 工作流的连线**全部静默丢掉**（26 节点 / 0 连线）。
                linkId = ArrSint(jl, 0);
                originId = ArrSint(jl, 1);
                originSlot = ArrSint(jl, 2);
                targetId = ArrSint(jl, 3);
                targetSlot = ArrSint(jl, 4);
            } else {
                ++badShape;
                continue;
            }
            const auto fromIt = byWfId.find(originId);
            const auto toIt = byWfId.find(targetId);
            if (fromIt == byWfId.end() || toIt == byWfId.end()) {
                report.warnings.emplace_back("有连线指向被跳过的节点，已忽略");
                continue;
            }
            auto* fromNode = dynamic_cast<ShineComfyNode*>(fromIt->second);
            auto* toNode = dynamic_cast<ShineComfyNode*>(toIt->second);
            const std::size_t outSlot = static_cast<std::size_t>(originSlot);
            const std::size_t inSlot = static_cast<std::size_t>(targetSlot);
            if (fromNode != nullptr && toNode != nullptr && area->TryToConnect(fromNode, outSlot, toNode, inSlot)) {
                ++report.links;
                continue;
            }
            // 下标对不上：`/object_info` 的顺序与前端 JSON 的数组顺序不保证一致 →
            // 用 JSON 里写的**插口名字**重找槽位再试（含 `父.子` 归到父插口）
            std::size_t altOut = outSlot;
            std::size_t altIn = inSlot;
            bool remapped = false;
            if (fromNode != nullptr) {
                if (const auto it = jsonById.find(originId); it != jsonById.end()) {
                    if (const auto slot = OutputSlotByName(*fromNode, JsonSlotName(it->second, "outputs", outSlot));
                        slot.has_value()) {
                        altOut = *slot;
                        remapped = true;
                    }
                }
            }
            if (toNode != nullptr) {
                if (const auto it = jsonById.find(targetId); it != jsonById.end()) {
                    if (const auto slot = InputSlotByName(*toNode, JsonSlotName(it->second, "inputs", inSlot));
                        slot.has_value()) {
                        altIn = *slot;
                        remapped = true;
                    }
                }
            }
            if (remapped && (altOut != outSlot || altIn != inSlot)) {
                if (fromNode != nullptr && toNode != nullptr &&
                    area->TryToConnect(fromNode, altOut, toNode, altIn)) {
                    ++report.links;
                    continue;
                }
            }
            report.warnings.emplace_back(
                fmt::format("连线 #{}（{}[{}] → {}[{}]）无法建立：插口越界或类型不匹配（已按名字重试）", linkId,
                            originId, originSlot, targetId, targetSlot));
        }
        if (badShape > 0) {
            report.warnings.emplace_back(
                fmt::format("有 {} 条连线的格式不认识（既不是对象也不是 ≥5 项数组），已忽略", badShape));
        }
    }

    // 把整图平移到原点附近（相对布局不变）。
    // 为什么必须做：官方模板的坐标常常离原点极远（H3 参考图模板在 y≈4850），
    // 而 VNS 的 `CenterViewOnAllElements()` 里 `SetRenderOffset` 会被 GRID_SIZE 夹住、
    // 目标偏移落在范围外时**直接返回 false**（`VisualNodeAreaRendering.cpp:763-767`）→
    // 表现就是"导入成功、计数正确、画布却一片空白"。平移之后 CenterView 才真的生效。
    ImVec2 shift{0.0f, 0.0f};
    if (!byWfId.empty()) {
        ImVec2 minPos{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        for (const auto& entry : byWfId) {
            const ImVec2 pos = entry.second->GetPosition();
            minPos.x = std::min(minPos.x, pos.x);
            minPos.y = std::min(minPos.y, pos.y);
        }
        shift = ImVec2(kImportMargin - minPos.x, kImportMargin - minPos.y);
        if (shift.x != 0.0f || shift.y != 0.0f) {
            for (const auto& entry : byWfId) {
                const ImVec2 pos = entry.second->GetPosition();
                entry.second->SetPosition(ImVec2(pos.x + shift.x, pos.y + shift.y));
            }
        }
    }

    if (yyjson_val* jreroutes = yyjson_obj_get(root, "reroutes"); yyjson_is_arr(jreroutes)) {
        report.reroutes = yyjson_arr_size(jreroutes);
        if (report.reroutes > 0) {
            log::Info("工作流含 {} 个 reroute（前端连线拐点）：已按直连导入，不重建拐点", report.reroutes);
        }
    }

    if (yyjson_val* jgroups = yyjson_obj_get(root, "groups"); yyjson_is_arr(jgroups)) {
        const std::size_t count = yyjson_arr_size(jgroups);
        for (std::size_t i = 0; i < count; ++i) {
            yyjson_val* jg = yyjson_arr_get(jgroups, i);
            if (!yyjson_is_obj(jg)) {
                continue;
            }
            const std::string title = JsonToText(yyjson_obj_get(jg, "title"));
            if (const auto bounding = ReadVec2(yyjson_obj_get(jg, "bounding")); bounding.has_value()) {
                AddGroupCommentAt(bounding->x + shift.x, bounding->y + shift.y, title.empty() ? "分组" : title);
            }
        }
        if (count > 0) {
            report.warnings.emplace_back("分组框按边界转成注释框（样式不保真）");
        }
    }

    CenterView();
    report.ok = report.nodes > 0;
    log::Info("导入工作流{}：节点 {}，连线 {}，reroute {}，未知类型 {}，告警 {}",
              WorkflowFormatLabel(report.format), report.nodes, report.links, report.reroutes,
              report.unknownTypes.size(), report.warnings.size());
    yyjson_doc_free(doc);
    return report;
}

} // namespace shine::graph
