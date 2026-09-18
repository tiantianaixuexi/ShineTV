#include "graph/GraphHost.h"
#include <optional>

#include "comfy/ComfySession.h"
#include "core/Log.h"
#include "graph/ComfyNode.h"
#include "graph/WorkflowIO.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <GroupComment.h>
#include <VisualNode.h>
#include <VisualNodeFactory.h>
#include <VisualNodeSocket.h>
#include <VisualNodeSystem.h>
#include <imgui.h>
#include <yyjson.h>

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <ranges>
#include <string>
#include <unordered_set>
#include <vector>

namespace shine::graph {
namespace {

using namespace VisNodeSys;

bool g_inited = false;
NodeArea* g_area = nullptr;

std::wstring AppDataDir() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring base = (n > 0 && n < MAX_PATH) ? std::wstring(buf, n) : L".";
    std::wstring dir = base + L"\\ShineTVStudio";
    std::filesystem::create_directories(dir);
    return dir;
}

// graph.json 的**宽字符路径**：打开文件一律用它（不要再从窄字符串拼路径）
std::filesystem::path GraphFilePath() { return std::filesystem::path{AppDataDir()} / L"graph.json"; }

// P2 基础节点：通用「一点进一点出」占位，P3 会换成 object_info 动态类型。
class ShineBasicNode final : public Node {
public:
    ShineBasicNode(std::string name = "ShineNode", std::string type = "ShineNode", ImColor title = ImColor(40, 70, 120)) {
        SetName(std::move(name));
        this->Type = std::move(type);
        TitleBackgroundColor = title;
        TitleBackgroundColorHovered = ImColor(180, 200, 255);
        AddSocket(new NodeSocket(this, "ANY", "In", NodeSocket::SocketFlow::Input));
        AddSocket(new NodeSocket(this, "ANY", "Out", NodeSocket::SocketFlow::Output));
    }

    ShineBasicNode(const ShineBasicNode& other) : Node(other) {}
};

struct NodeDef {
    const char* type;
    const char* label;
    ImColor color;
};

const NodeDef kNodeDefs[] = {
    {"ShineLoadImage", "加载图片", ImColor(40, 70, 120)},
    {"ShineLoadModel", "加载模型", ImColor(80, 50, 140)},
    {"ShinePrompt", "CLIP 文本编码", ImColor(50, 90, 60)},
    {"ShineSampler", "KSampler", ImColor(130, 70, 40)},
    {"ShinePreview", "预览图像", ImColor(30, 100, 80)},
    {"ShineSaveImage", "保存图像", ImColor(30, 90, 70)},
};

Node* MakeNode(const char* type) {
    return NODE_FACTORY.CreateNode(type ? type : "");
}

void RegisterShineNodes() {
    for (const auto& def : kNodeDefs) {
        const std::string type = def.type;
        const std::string label = def.label;
        const ImColor color = def.color;
        NODE_FACTORY.RegisterNodeType(
            type,
            [label, type, color]() -> Node* { return new ShineBasicNode(label, type, color); },
            [](const Node& src) -> Node* {
                const auto& s = static_cast<const ShineBasicNode&>(src);
                auto* n = new ShineBasicNode(s);
                return n;
            });
    }
    log::Info("已注册 {} 个 Shine 节点类型", static_cast<int>(std::size(kNodeDefs)));
}

void SeedDemoGraph(NodeArea* area) {
    if (!area || area->GetNodeCount() > 0) {
        return;
    }
    Node* model = MakeNode("ShineLoadModel");
    Node* sampler = MakeNode("ShineSampler");
    Node* preview = MakeNode("ShinePreview");
    if (!model || !sampler || !preview) {
        return;
    }
    model->SetPosition(ImVec2(60, 120));
    sampler->SetPosition(ImVec2(320, 120));
    preview->SetPosition(ImVec2(580, 120));
    area->AddNode(model);
    area->AddNode(sampler);
    area->AddNode(preview);
    area->TryToConnect(model, size_t(0), sampler, size_t(0));
    area->TryToConnect(sampler, size_t(0), preview, size_t(0));

    auto* comment = new GroupComment();
    comment->SetCaption("演示：模型 → 采样 → 预览");
    comment->SetPosition(ImVec2(40, 80));
    comment->SetSize(ImVec2(760, 220));
    area->AddGroupComment(comment);

    log::Info("已播种演示节点图");
}

void DrawMainMenu(NodeArea* area) {
    if (ImGui::BeginMenu("添加节点")) {
        for (const auto& def : kNodeDefs) {
            if (ImGui::MenuItem(def.label)) {
                Node* n = MakeNode(def.type);
                if (n) {
                    const ImVec2 mouse = area->GetContextMenuOpenState().MousePositionRecorded;
                    n->SetPosition(mouse);
                    area->AddNode(n);
                }
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("添加注释框")) {
        const ImVec2 mouse = area->GetContextMenuOpenState().MousePositionRecorded;
        AddGroupCommentAt(mouse.x, mouse.y, "注释");
    }
    ImGui::Separator();
    if (ImGui::MenuItem("居中视图")) {
        area->CenterViewOnAllElements();
    }
    if (ImGui::MenuItem("清空图")) {
        area->Clear();
        log::Info("已清空节点图");
    }
    ImGui::Separator();
    if (ImGui::MenuItem("保存图")) {
        SaveGraph();
    }
    if (ImGui::MenuItem("加载图")) {
        LoadGraph();
    }
}

// *********************** P3.3 / P3.4 / P3.5 内部状态 ***********************

std::unordered_set<std::string> g_registered;                  // 已注册的 ComfyUI className（幂等）
std::vector<CatalogEntry> g_catalog;                           // ComfyUI 节点目录（已排序）
std::map<std::string, std::shared_ptr<const comfy::NodeTypeDef>> g_defs; // 供节点构造
std::size_t g_lastSyncedDefs = 0;                              // 上次同步的 object_info 数量
std::string g_lastPromptId;
std::vector<CompileError> g_lastCompileErrors;

// 透传节点（旁路）：连线穿过它们时要继续向上游找真实节点
bool IsPassThrough(const Node* node) {
    if (node == nullptr) {
        return false;
    }
    static constexpr std::string_view kTypes[] = {"LinkNode", "SocketMirrorNode", "SubAreaInputNode",
                                                 "SubAreaOutputNode"};
    return std::ranges::find(kTypes, node->GetType()) != std::end(kTypes);
}

void RegisterSocketColors() {
    struct TypeColor {
        const char* type;
        ImU32 color;
    };
    static constexpr TypeColor kColors[] = {
        {"MODEL", IM_COL32(120, 190, 255, 255)},       {"CLIP", IM_COL32(255, 220, 120, 255)},
        {"VAE", IM_COL32(255, 140, 190, 255)},         {"LATENT", IM_COL32(190, 140, 255, 255)},
        {"IMAGE", IM_COL32(140, 230, 160, 255)},       {"MASK", IM_COL32(230, 230, 140, 255)},
        {"CONDITIONING", IM_COL32(255, 170, 120, 255)}, {"CONTROL_NET", IM_COL32(160, 255, 230, 255)},
        {"ANY", IM_COL32(200, 200, 200, 255)},
    };
    for (const TypeColor& c : kColors) {
        NODE_SYSTEM.AssociateSocketTypeToColor(c.type, ImColor(c.color));
    }
}

// object_info 到达后自动注册（每帧开销可忽略；数量变化才算一次同步）
void SyncComfyCatalog() {
    const std::vector<comfy::NodeTypeDef>& defs = comfy::ComfySession::Instance().ObjectInfoNodes();
    if (defs.empty() || defs.size() == g_lastSyncedDefs) {
        return;
    }
    g_lastSyncedDefs = defs.size();
    RegisterComfyNodes(defs);
}

// 载入旧存档前扫描 "NodeType"：未注册类型会被 VNS 跳过 → 先给中文告警（P3.3 S6）
// 核心吃**文本**：P3.7 从 ComfyUI 拉回来的工作流没有本地文件，路径版只是包一层读盘。
void WarnUnknownTypesInText(std::string_view text) {
    yyjson_doc* doc = yyjson_read(text.data(), text.size(), 0);
    if (doc == nullptr) {
        return;
    }
    std::vector<std::string> unknown;
    // 通用递归：任何对象里的 "NodeType" 字符串都算（不依赖 VNS 的存档结构）
    std::function<void(yyjson_val*)> walk = [&](yyjson_val* val) {
        if (val == nullptr) {
            return;
        }
        if (yyjson_is_obj(val)) {
            yyjson_val* typeVal = yyjson_obj_get(val, "NodeType");
            const char* alias = yyjson_is_str(typeVal) ? yyjson_get_str(typeVal) : nullptr;
            const std::string type = alias ? alias : "";
            const bool known = type.empty() || g_registered.contains(type) || g_defs.contains(type);
            const bool fallback = std::ranges::find_if(kNodeDefs, [&](const NodeDef& d) { return type == d.type; }) !=
                                  std::end(kNodeDefs);
            static constexpr std::string_view kVnsTypes[] = {"LinkNode", "SocketMirrorNode", "SubAreaNode",
                                                            "SubAreaInputNode", "SubAreaOutputNode"};
            const bool vns = std::ranges::find(kVnsTypes, type) != std::end(kVnsTypes);
            if (!known && !fallback && !vns) {
                unknown.push_back(type);
            }
            yyjson_obj_iter it;
            yyjson_obj_iter_init(val, &it);
            yyjson_val* key = nullptr;
            while ((key = yyjson_obj_iter_next(&it))) {
                walk(yyjson_obj_iter_get_val(key));
            }
        } else if (yyjson_is_arr(val)) {
            const std::size_t n = yyjson_arr_size(val);
            for (std::size_t i = 0; i < n; ++i) {
                walk(yyjson_arr_get(val, i));
            }
        }
    };
    walk(yyjson_doc_get_root(doc));
    yyjson_doc_free(doc);
    for (const std::string& t : unknown) {
        log::Warn("存档里的节点类型「{}」尚未注册（该节点会被跳过）；先连 ComfyUI 刷新 object_info", t);
    }
}

void WarnUnknownTypes(const std::filesystem::path& path) {
    const std::optional<std::string> bytes = util::ReadFileBytes(path);
    if (!bytes.has_value()) {
        return;
    }
    WarnUnknownTypesInText(*bytes);
}

} // namespace

std::string GraphPath() { return util::PathToUtf8(GraphFilePath()); }

bool Init() {
    if (g_inited) {
        return true;
    }
    // 必须在 ImGui Context 创建之后调用（Fonts 已加载）
    NODE_SYSTEM.Initialize(false);
    RegisterShineNodes();
    RegisterSocketColors();

    g_area = NODE_SYSTEM.CreateNodeArea();
    g_area->SetName("主图");
    g_area->SetIsFillingWindow(true);
    g_area->SetMainContextMenuFunction([area = g_area]() { DrawMainMenu(area); });

    // 尝试从磁盘恢复；没有则播种演示图。
    // 文件读写走 util（宽字符路径安全），再交给 VNS 的 LoadFromJson ——
    // VNS 自带的 LoadFromFile / SaveToFile 用**窄字符串**打开文件，中文路径会失败。
    const std::filesystem::path graphFile = GraphFilePath();
    if (std::filesystem::exists(graphFile)) {
        const std::optional<std::string> bytes = util::ReadFileBytes(graphFile);
        if (bytes.has_value() && g_area->LoadFromJson(*bytes)) {
            log::Info("已加载节点图 {}", GraphPath());
        } else {
            log::Warn("节点图加载失败，使用演示图");
            SeedDemoGraph(g_area);
        }
    } else {
        SeedDemoGraph(g_area);
    }

    g_inited = true;
    log::Info("GraphHost 初始化完成（VNS）");
    return true;
}

void Shutdown() {
    if (g_area) {
        SaveGraph();
        NODE_SYSTEM.DeleteNodeArea(g_area);
        g_area = nullptr;
    }
    g_inited = false;
}

void DrawCanvas() {
    if (!g_area) {
        ImGui::TextDisabled("GraphHost 未初始化");
        return;
    }
    SyncComfyCatalog(); // object_info 到达后自动注册节点类型（幂等）
    // 占满「图」窗口内容区；VNS 内部 BeginChild
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 32 || avail.y < 32) {
        return;
    }
    g_area->SetPosition(ImVec2(0, 0));
    g_area->Update();
}

bool SaveGraph() {
    if (!g_area) {
        return false;
    }
    const std::filesystem::path file = GraphFilePath();
    if (util::WriteFileBytes(file, g_area->ToJson())) {
        log::Info("节点图已保存 {}", util::PathToUtf8(file));
        return true;
    }
    log::Error("节点图保存失败 {}", util::PathToUtf8(file));
    return false;
}

bool LoadGraph() {
    if (!g_area) {
        return false;
    }
    const std::filesystem::path file = GraphFilePath();
    if (!std::filesystem::exists(file)) {
        log::Warn("不存在 {}", util::PathToUtf8(file));
        return false;
    }
    WarnUnknownTypes(file); // 未注册类型会被 VNS 跳过 → 先告警（P3.3 S6）
    const std::optional<std::string> bytes = util::ReadFileBytes(file);
    if (!bytes.has_value()) {
        log::Error("节点图加载失败 {}", util::PathToUtf8(file));
        return false;
    }
    g_area->Clear();
    if (g_area->LoadFromJson(*bytes)) {
        log::Info("节点图已加载 {}", util::PathToUtf8(file));
        return true;
    }
    log::Error("节点图加载失败 {}", util::PathToUtf8(file));
    return false;
}

size_t NodeCount() { return g_area ? g_area->GetNodeCount() : 0; }
size_t ConnectionCount() { return g_area ? g_area->GetConnectionCount() : 0; }
NodeArea* Area() { return g_area; }

float Zoom() { return g_area ? g_area->GetZoomFactor() : 1.f; }

size_t SelectedCount() {
    return g_area ? g_area->GetSelected().size() : 0;
}

std::vector<SelectedNodeInfo> SelectedNodes() {
    std::vector<SelectedNodeInfo> out;
    if (!g_area) {
        return out;
    }
    for (Node* n : g_area->GetSelected()) {
        if (!n) continue;
        SelectedNodeInfo info;
        info.id = n->GetID();
        info.name = n->GetName();
        info.type = n->GetType();
        const ImVec2 p = n->GetPosition();
        const ImVec2 s = n->GetSize();
        info.x = p.x;
        info.y = p.y;
        info.w = s.x;
        info.h = s.y;
        info.inputs = static_cast<int>(n->GetInputSocketCount());
        info.outputs = static_cast<int>(n->GetOutputSocketCount());
        out.push_back(std::move(info));
    }
    return out;
}

void DeleteSelected() {
    if (!g_area) return;
    const auto selected = g_area->GetSelected();
    for (Node* n : selected) {
        if (n) {
            g_area->Delete(n);
        }
    }
    log::Info("已删除选中节点");
}

Node* CreateNodeByType(std::string_view type) {
    // VNS 工厂的 key 是 std::string（Doc/RULES-LANG.md §13.5 例外 4）：边界处显式转换一次
    Node* n = NODE_FACTORY.CreateNode(std::string{type});
    if (!n) {
        log::Warn("未知节点类型 {}", type);
    }
    return n;
}

bool SpawnNode(std::string_view type, float x, float y) {
    const std::string typeStr{type};
    Node* n = MakeNode(typeStr.c_str());
    if (!n || !g_area) {
        return false;
    }
    n->SetPosition(ImVec2(x, y));
    g_area->AddNode(n);
    log::Info("创建节点 {}", typeStr);
    return true;
}

bool SpawnNodeAtViewCenter(std::string_view type) {
    if (g_area == nullptr) {
        return false;
    }
    const std::string typeStr{type};
    Node* n = MakeNode(typeStr.c_str());
    if (n == nullptr) {
        log::Warn("未知节点类型 {}", typeStr);
        return false;
    }
    // 视口中心（图形坐标）：新节点按其自身尺寸居中；连点多次时阶梯错开，免得叠成一坨
    static int cascade = 0;
    const ImVec2 center = g_area->GetRenderedViewCenter();
    const ImVec2 size = n->GetSize();
    const float step = static_cast<float>(cascade % 8) * 28.0f;
    ++cascade;
    n->SetPosition(ImVec2(center.x - size.x * 0.5f + step, center.y - size.y * 0.5f + step));
    g_area->AddNode(n);
    log::Info("创建节点 {}（视口中心）", typeStr);
    return true;
}

void CenterView() {
    if (g_area) {
        g_area->CenterViewOnAllElements();
    }
}

void AddGroupCommentAt(float x, float y, std::string_view caption) {
    if (!g_area) {
        return;
    }
    auto* c = new GroupComment();
    c->SetCaption(std::string{caption});
    c->SetPosition(ImVec2(x, y));
    c->SetSize(ImVec2(280, 160));
    g_area->AddGroupComment(c);
}

void ClearGraph() {
    if (!g_area) {
        return;
    }
    g_area->Clear();
    log::Info("已清空节点图");
}

bool ImportGraphText(std::string_view text, std::string_view sourceLabel, ImportReport* report) {
    if (text.empty()) {
        log::Warn("导入内容为空：{}", sourceLabel);
        return false;
    }
    // 按内容判定，不看扩展名
    const WorkflowFormat format = DetectFormat(text);
    log::Info("导入 {}：格式识别为 {}", sourceLabel, WorkflowFormatLabel(format));
    switch (format) {
    case WorkflowFormat::ApiFormat: {
        ClearGraph();
        const ImportReport detailed = ImportApiJson(text);
        if (report != nullptr) {
            *report = detailed;
        }
        for (const std::string& w : detailed.warnings) {
            log::Warn("导入告警：{}", w);
        }
        return detailed.ok;
    }
    case WorkflowFormat::WorkflowV1:
    case WorkflowFormat::WorkflowV0_4: {
        ClearGraph();
        const ImportReport detailed = ImportWorkflowJson(text);
        if (report != nullptr) {
            *report = detailed;
        }
        for (const std::string& w : detailed.warnings) {
            log::Warn("导入告警：{}", w);
        }
        return detailed.ok;
    }
    case WorkflowFormat::Unknown:
        break;
    }
    // 兜底：按自家 VNS 图（graph.json）读
    WarnUnknownTypesInText(text);
    if (g_area == nullptr) {
        return false;
    }
    g_area->Clear();
    const bool loaded = g_area->LoadFromJson(std::string{text}); // VNS API 只收 std::string（§13.5 例外）
    if (report != nullptr) {
        report->ok = loaded;
        report->format = WorkflowFormat::Unknown; // 自家 VNS 图（`DetectFormat` 不认这一种）
        report->nodes = loaded ? g_area->GetNodeCount() : 0;
        report->links = loaded ? g_area->GetConnectionCount() : 0;
    }
    if (loaded) {
        log::Info("节点图已加载 {}", sourceLabel);
        return true;
    }
    log::Error("节点图加载失败 {}", sourceLabel);
    return false;
}

bool ImportGraphFile(std::string_view path) {
    const std::optional<std::string> bytes = util::ReadFileBytes(util::PathFromUtf8(path));
    if (!bytes.has_value()) {
        log::Error("打开失败：{}", path);
        return false;
    }
    return ImportGraphText(*bytes, path);
}

// *********************** P3.3：动态注册与节点目录 ***********************

void RegisterComfyNodes(const std::vector<comfy::NodeTypeDef>& defs) {
    int added = 0;
    int skipped = 0;
    for (const comfy::NodeTypeDef& def : defs) {
        if (def.className.empty()) {
            continue;
        }
        if (g_registered.contains(def.className)) {
            ++skipped; // 幂等：已经注册过（VNS 没有注销接口，重复注册必失败）
            continue;
        }
        auto holder = std::make_shared<comfy::NodeTypeDef>(def);
        const bool registered = NODE_FACTORY.RegisterNodeType(
            def.className,
            [holder]() -> Node* { return new ShineComfyNode(holder); },
            [](const Node& src) -> Node* { return new ShineComfyNode(static_cast<const ShineComfyNode&>(src)); });
        if (!registered) {
            ++skipped;
            continue;
        }
        g_registered.insert(def.className);
        g_defs.emplace(def.className, holder);
        CatalogEntry entry;
        entry.className = def.className;
        entry.displayName = def.displayName.empty() ? def.className : def.displayName;
        entry.category = def.category.empty() ? std::string{"未分类"} : def.category;
        entry.deprecated = def.deprecated;
        entry.experimental = def.experimental;
        g_catalog.push_back(std::move(entry));
        ++added;
    }
    std::ranges::sort(g_catalog, [](const CatalogEntry& a, const CatalogEntry& b) {
        if (a.category != b.category) {
            return a.category < b.category;
        }
        return a.displayName < b.displayName;
    });
    log::Info("注册 ComfyUI 节点：新增 {}，跳过 {}（已注册/失败），目录共 {} 个", added, skipped, g_catalog.size());
}

std::vector<CatalogGroup> NodeCatalog() {
    std::vector<CatalogGroup> groups;
    if (g_catalog.empty()) {
        // 未连接 ComfyUI：兜底显示内置示例类型（不报错）
        groups.push_back({"内置示例（未连接 ComfyUI，显示内置示例节点）", {}});
        for (const NodeDef& d : kNodeDefs) {
            groups.back().entries.push_back(
                CatalogEntry{d.type, d.label, groups.back().category, false, false});
        }
        return groups;
    }
    for (const CatalogEntry& e : g_catalog) {
        if (groups.empty() || groups.back().category != e.category) {
            groups.push_back({e.category, {}});
        }
        groups.back().entries.push_back(e);
    }
    return groups;
}

bool IsRegistered(std::string_view className) {
    return g_registered.contains(std::string{className});
}

std::size_t RegisteredComfyNodeCount() { return g_registered.size(); }

// *********************** P3.4：连线枚举 ***********************

std::vector<VisNodeSys::Node*> CanvasNodes() {
    std::vector<Node*> out;
    if (!g_area) {
        return out;
    }
    g_area->RunOnEachNode([&out](Node* n) {
        if (n != nullptr) {
            out.push_back(n); // 画布遍历顺序（= 编译 id 发号顺序，确定性）
        }
    });
    return out;
}

std::vector<GraphLink> EnumerateLinks() {
    std::vector<GraphLink> links;
    if (!g_area) {
        return links;
    }
    g_area->RunOnEachNode([&links](Node* node) {
        if (node == nullptr) {
            return;
        }
        for (std::size_t i = 0; i < node->GetInputSocketCount(); ++i) {
            NodeSocket* inSocket = node->GetSocketByIndex(i, NodeSocket::SocketFlow::Input);
            if (inSocket == nullptr) {
                continue;
            }
            for (NodeSocket* outSocket : inSocket->GetConnectedSockets()) {
                if (outSocket == nullptr) {
                    continue;
                }
                Node* upstream = outSocket->GetParent();
                if (upstream == nullptr) {
                    continue;
                }
                auto outputIndexOf = [](const Node* n, const NodeSocket* s) -> std::size_t {
                    for (std::size_t j = 0; j < n->GetOutputSocketCount(); ++j) {
                        if (n->GetSocketByIndex(j, NodeSocket::SocketFlow::Output) == s) {
                            return j;
                        }
                    }
                    return SIZE_MAX;
                };
                std::size_t outIdx = outputIndexOf(upstream, outSocket);
                if (outIdx == SIZE_MAX) {
                    continue;
                }
                GraphLink link;
                link.toNodeId = node->GetID();
                link.toSocketIndex = i;
                link.fromNodeId = upstream->GetID();
                link.fromSocketIndex = outIdx;
                link.type = outSocket->GetAllowedTypes().empty() ? std::string{} : outSocket->GetAllowedTypes().front();

                // 透传（旁路）节点：继续向上游找真实节点，最多 16 跳防环
                std::size_t hops = 0;
                while (IsPassThrough(upstream) && hops < 16) {
                    NodeSocket* upIn = upstream->GetSocketByIndex(0, NodeSocket::SocketFlow::Input);
                    if (upIn == nullptr || upIn->GetConnectedSockets().empty()) {
                        break;
                    }
                    NodeSocket* prevSocket = upIn->GetConnectedSockets().front();
                    Node* prevNode = (prevSocket != nullptr) ? prevSocket->GetParent() : nullptr;
                    if (prevNode == nullptr) {
                        break;
                    }
                    const std::size_t prevIdx = outputIndexOf(prevNode, prevSocket);
                    if (prevIdx == SIZE_MAX) {
                        break;
                    }
                    link.fromNodeId = prevNode->GetID();
                    link.fromSocketIndex = prevIdx;
                    upstream = prevNode;
                    ++hops;
                }
                link.passedThrough = hops;
                links.push_back(std::move(link));
            }
        }
    });
    return links;
}

// *********************** P3.5：提交接线 ***********************

bool RunCurrentGraph() {
    g_lastCompileErrors.clear();
    const CompileResult compiled = CompileToApiJson();
    if (!compiled.ok) {
        g_lastCompileErrors = compiled.errors;
        for (const CompileError& e : compiled.errors) {
            log::Warn("图编译失败：{}{}", e.nodeName.empty() ? std::string{} : ("节点「" + e.nodeName + "」"), e.message);
        }
        return false;
    }
    auto& session = comfy::ComfySession::Instance();
    if (session.State() != comfy::ConnectionState::Connected) {
        g_lastCompileErrors.push_back({std::string{}, std::string{}, "未连接 ComfyUI：请先连接并刷新 object_info"});
        log::Warn("未连接 ComfyUI，未提交（已编译 {} 个节点）", compiled.nodeCount);
        return false;
    }
    log::Info("提交当前图：{} 个节点 / {} 字节 API JSON", compiled.nodeCount, compiled.apiJson.size());
    session.SubmitPromptJson(compiled.apiJson, [](comfy::PromptSubmitResult res) {
        if (res.ok) {
            g_lastPromptId = res.promptId;
            log::Info("已提交 prompt={}（number={}）", res.promptId, res.number);
            return;
        }
        log::Error("提交被拒绝：{}", res.error.empty() ? std::string{"未知原因"} : res.error);
        if (!res.error.empty()) {
            g_lastCompileErrors.push_back({std::string{}, std::string{}, res.error});
        }
        for (const comfy::NodeError& ne : res.nodeErrors) {
            const std::string text = "节点『" + ne.nodeType + "』(id " + ne.nodeId + ") 输入「" + ne.inputName + "」：" +
                                     ne.message + (ne.hint.empty() ? std::string{} : ("（" + ne.hint + "）"));
            g_lastCompileErrors.push_back({ne.nodeId, ne.nodeType, text});
            log::Warn("submit rejected: node {} {} {} received={} {}", ne.nodeId, ne.nodeType, ne.inputName,
                      ne.receivedValue, ne.message);
        }
    });
    return true;
}

void InterruptCurrentGraph() {
    comfy::ComfySession::Instance().RequestInterruptCurrent();
    log::Info("已请求中断当前任务");
}

std::string LastPromptId() { return g_lastPromptId; }

const std::vector<CompileError>& LastCompileErrors() { return g_lastCompileErrors; }

} // namespace shine::graph
