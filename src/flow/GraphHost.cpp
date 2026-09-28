#include "flow/GraphHost.h"
#include <optional>

#include "comfy/ComfySession.h"
#include "core/Log.h"
#include "flow/ComfyNode.h"
#include "flow/GraphCompiler.h"
#include "flow/WorkflowIO.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Strings.h"

#include <yyjson.h>

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace shine::flow {
namespace {

bool g_inited = false;

// —— 图文档（P01-S2：纯模型，取代旧节点画布库的 NodeArea）——
struct ModelLink {
    std::string fromId;
    std::size_t fromSlot = 0;
    std::string toId;
    std::size_t toSlot = 0;
};

struct ModelGroup {
    Vec2 pos;
    Vec2 size;
    std::string caption;
};

std::vector<std::unique_ptr<ShineComfyNode>> g_nodes;
std::vector<ModelLink> g_links;
std::vector<ModelGroup> g_groups;
std::vector<std::string> g_selection;
std::size_t g_nextNodeId = 1;
float g_zoom = 1.f;
Vec2 g_offset;
Vec2 g_viewCenter;

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

// P2 基础节点：通用「一点进一点出」占位，未连接 ComfyUI 时的示例目录。
struct NodeDef {
    const char* type;
    const char* label;
};

const NodeDef kNodeDefs[] = {
    {"ShineLoadImage", "加载图片"}, {"ShineLoadModel", "加载模型"}, {"ShinePrompt", "CLIP 文本编码"},
    {"ShineSampler", "KSampler"},   {"ShinePreview", "预览图像"},  {"ShineSaveImage", "保存图像"},
};

const NodeDef* FindBasicDef(std::string_view type) {
    const auto it = std::ranges::find_if(kNodeDefs, [&](const NodeDef& d) { return type == d.type; });
    return it == std::end(kNodeDefs) ? nullptr : &*it;
}

// *********************** P3.3 / P3.4 / P3.5 内部状态 ***********************

std::unordered_set<std::string> g_registered;                              // 已注册的 ComfyUI className（幂等）
std::vector<CatalogEntry> g_catalog;                                       // ComfyUI 节点目录（已排序）
std::map<std::string, std::shared_ptr<const comfy::NodeTypeDef>> g_defs;   // 供节点构造
std::size_t g_lastSyncedDefs = 0;                                          // 上次同步的 object_info 数量
std::string g_lastPromptId;
std::vector<CompileError> g_lastCompileErrors;

ShineComfyNode* FindNode(std::string_view id) {
    const auto it = std::ranges::find_if(g_nodes, [&](const auto& n) { return n->id == id; });
    return it == g_nodes.end() ? nullptr : it->get();
}

ShineComfyNode* AddNode(std::unique_ptr<ShineComfyNode> node) {
    if (!node) {
        return nullptr;
    }
    node->id = "n" + std::to_string(g_nextNodeId++);
    ShineComfyNode* raw = node.get();
    g_nodes.push_back(std::move(node));
    return raw;
}

// object_info 到达后自动注册（调用入口处顺手同步；数量变化才算一次同步）
void SyncComfyCatalog() {
    const std::vector<comfy::NodeTypeDef>& defs = comfy::ComfySession::Instance().ObjectInfoNodes();
    if (defs.empty() || defs.size() == g_lastSyncedDefs) {
        return;
    }
    g_lastSyncedDefs = defs.size();
    RegisterComfyNodes(defs);
}

// 载入存档前扫描类型名：未注册类型会被保留但不可编译 → 先给中文告警（P3.3 S6）
// 核心吃**文本**：P3.7 从 ComfyUI 拉回来的工作流没有本地文件，路径版只是包一层读盘。
void WarnUnknownTypesInText(std::string_view text) {
    yyjson_doc* doc = yyjson_read(text.data(), text.size(), 0);
    if (doc == nullptr) {
        return;
    }
    std::vector<std::string> unknown;
    // 通用递归：任何对象里的类型名载体（"NodeType" 旧档 / "type" 模型档与工作流 / "class_type" API）
    static constexpr std::string_view kTypeKeys[] = {"NodeType", "type", "class_type"};
    std::function<void(yyjson_val*)> walk = [&](yyjson_val* val) {
        if (val == nullptr) {
            return;
        }
        if (yyjson_is_obj(val)) {
            for (const std::string_view key : kTypeKeys) {
                yyjson_val* typeVal = yyjson_obj_getn(val, key.data(), key.size());
                const char* alias = yyjson_is_str(typeVal) ? yyjson_get_str(typeVal) : nullptr;
                const std::string type = alias ? alias : "";
                const bool known = type.empty() || g_registered.contains(type) || g_defs.contains(type);
                const bool basic = FindBasicDef(type) != nullptr;
                if (!known && !basic) {
                    unknown.push_back(type);
                }
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
        log::Warn("存档里的节点类型「{}」尚未注册（该节点保留但不可编译）；先连 ComfyUI 刷新 object_info", t);
    }
}

void WarnUnknownTypes(const std::filesystem::path& path) {
    const std::optional<std::string> bytes = util::ReadFileBytes(path);
    if (!bytes.has_value()) {
        return;
    }
    WarnUnknownTypesInText(*bytes);
}

void SeedDemoGraph() {
    if (!g_nodes.empty()) {
        return;
    }
    ShineComfyNode* model = AddNode(std::make_unique<ShineComfyNode>("ShineLoadModel", "加载模型"));
    ShineComfyNode* sampler = AddNode(std::make_unique<ShineComfyNode>("ShineSampler", "KSampler"));
    ShineComfyNode* preview = AddNode(std::make_unique<ShineComfyNode>("ShinePreview", "预览图像"));
    if (model == nullptr || sampler == nullptr || preview == nullptr) {
        return;
    }
    model->pos = Vec2{60.f, 120.f};
    sampler->pos = Vec2{320.f, 120.f};
    preview->pos = Vec2{580.f, 120.f};
    (void)TryConnect(model, 0, sampler, 0);
    (void)TryConnect(sampler, 0, preview, 0);
    AddGroupCommentAt(40.f, 80.f, "演示：模型 → 采样 → 预览");
    log::Info("已播种演示节点图");
}

// —— 自家模型档（graph.json）的读写 —— 格式：{"shine_graph":1, "next_id":N, "view":…, "nodes":[…], "links":[…], "groups":[…]}

void AddVec2(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, Vec2 v) {
    yyjson_mut_val* arr = yyjson_mut_arr(doc);
    (void)yyjson_mut_arr_add_real(doc, arr, static_cast<double>(v.x));
    (void)yyjson_mut_arr_add_real(doc, arr, static_cast<double>(v.y));
    (void)yyjson_mut_obj_add_val(doc, obj, key, arr);
}

void AddSockets(yyjson_mut_doc* doc, yyjson_mut_val* jn, const char* key, const std::vector<Socket>& sockets) {
    yyjson_mut_val* arr = yyjson_mut_arr(doc);
    for (const Socket& s : sockets) {
        yyjson_mut_val* js = yyjson_mut_obj(doc);
        (void)yyjson_mut_obj_add_strcpy(doc, js, "name", s.name.c_str());
        yyjson_mut_val* types = yyjson_mut_arr(doc);
        for (const std::string& t : s.allowedTypes) {
            (void)yyjson_mut_arr_add_strcpy(doc, types, t.c_str());
        }
        (void)yyjson_mut_obj_add_val(doc, js, "types", types);
        (void)yyjson_mut_arr_add_val(arr, js);
    }
    (void)yyjson_mut_obj_add_val(doc, jn, key, arr);
}

std::string SerializeGraph() {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    (void)yyjson_mut_obj_add_int(doc, root, "shine_graph", 1);
    (void)yyjson_mut_obj_add_int(doc, root, "next_id", static_cast<std::int64_t>(g_nextNodeId));

    yyjson_mut_val* view = yyjson_mut_obj(doc);
    (void)yyjson_mut_obj_add_real(doc, view, "zoom", static_cast<double>(g_zoom));
    AddVec2(doc, view, "offset", g_offset);
    (void)yyjson_mut_obj_add_val(doc, root, "view", view);

    yyjson_mut_val* jnodes = yyjson_mut_arr(doc);
    for (const auto& node : g_nodes) {
        yyjson_mut_val* jn = yyjson_mut_obj(doc);
        (void)yyjson_mut_obj_add_strcpy(doc, jn, "id", node->id.c_str());
        (void)yyjson_mut_obj_add_strcpy(doc, jn, "type", node->ClassType().c_str());
        (void)yyjson_mut_obj_add_strcpy(doc, jn, "title", node->Title().c_str());
        AddVec2(doc, jn, "pos", node->pos);
        AddVec2(doc, jn, "size", node->size);
        AddSockets(doc, jn, "inputs", node->Inputs());
        AddSockets(doc, jn, "outputs", node->Outputs());
        yyjson_mut_val* widgets = yyjson_mut_obj(doc);
        // 控件值按键排序写盘（unordered_map 序不确定 → 排序保证同图两次存盘字节一致）
        std::vector<std::string> keys;
        keys.reserve(node->Widgets().size());
        for (const auto& [key, value] : node->Widgets()) {
            keys.push_back(key);
        }
        std::ranges::sort(keys);
        for (const std::string& key : keys) {
            (void)yyjson_mut_obj_add_strcpy(doc, widgets, key.c_str(), node->Widgets().at(key).c_str());
        }
        (void)yyjson_mut_obj_add_val(doc, jn, "widgets", widgets);
        (void)yyjson_mut_arr_add_val(jnodes, jn);
    }
    (void)yyjson_mut_obj_add_val(doc, root, "nodes", jnodes);

    yyjson_mut_val* jlinks = yyjson_mut_arr(doc);
    for (const ModelLink& link : g_links) {
        yyjson_mut_val* jl = yyjson_mut_arr(doc);
        (void)yyjson_mut_arr_add_strcpy(doc, jl, link.fromId.c_str());
        (void)yyjson_mut_arr_add_int(doc, jl, static_cast<std::int64_t>(link.fromSlot));
        (void)yyjson_mut_arr_add_strcpy(doc, jl, link.toId.c_str());
        (void)yyjson_mut_arr_add_int(doc, jl, static_cast<std::int64_t>(link.toSlot));
        (void)yyjson_mut_arr_add_val(jlinks, jl);
    }
    (void)yyjson_mut_obj_add_val(doc, root, "links", jlinks);

    yyjson_mut_val* jgroups = yyjson_mut_arr(doc);
    for (const ModelGroup& g : g_groups) {
        yyjson_mut_val* jg = yyjson_mut_obj(doc);
        AddVec2(doc, jg, "pos", g.pos);
        AddVec2(doc, jg, "size", g.size);
        (void)yyjson_mut_obj_add_strcpy(doc, jg, "caption", g.caption.c_str());
        (void)yyjson_mut_arr_add_val(jgroups, jg);
    }
    (void)yyjson_mut_obj_add_val(doc, root, "groups", jgroups);

    char* text = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, nullptr);
    yyjson_mut_doc_free(doc);
    if (text == nullptr) {
        return {};
    }
    std::string out{text};
    std::free(text);
    return out;
}

[[nodiscard]] Vec2 ReadVec2Val(yyjson_val* v, Vec2 fallback = {}) {
    if (!yyjson_is_arr(v) || yyjson_arr_size(v) < 2) {
        return fallback;
    }
    return Vec2{static_cast<float>(yyjson_get_num(yyjson_arr_get(v, 0))),
                static_cast<float>(yyjson_get_num(yyjson_arr_get(v, 1)))};
}

[[nodiscard]] std::vector<Socket> ReadSockets(yyjson_val* jn, const char* key, SocketFlow flow) {
    std::vector<Socket> out;
    yyjson_val* arr = yyjson_obj_get(jn, key);
    if (!yyjson_is_arr(arr)) {
        return out;
    }
    const std::size_t n = yyjson_arr_size(arr);
    for (std::size_t i = 0; i < n; ++i) {
        yyjson_val* js = yyjson_arr_get(arr, i);
        Socket s;
        s.flow = flow;
        yyjson_val* name = yyjson_obj_get(js, "name");
        s.name = yyjson_is_str(name) ? yyjson_get_str(name) : std::string{};
        yyjson_val* types = yyjson_obj_get(js, "types");
        if (yyjson_is_arr(types)) {
            const std::size_t m = yyjson_arr_size(types);
            for (std::size_t j = 0; j < m; ++j) {
                yyjson_val* t = yyjson_arr_get(types, j);
                if (yyjson_is_str(t)) {
                    s.allowedTypes.emplace_back(yyjson_get_str(t));
                }
            }
        }
        if (s.allowedTypes.empty()) {
            s.allowedTypes.emplace_back("ANY");
        }
        out.push_back(std::move(s));
    }
    return out;
}

// 全量替换图内容（载入存档 / 导入前清场用）
void ResetGraph() {
    g_nodes.clear();
    g_links.clear();
    g_groups.clear();
    g_selection.clear();
}

bool LoadGraphFromText(std::string_view text) {
    yyjson_doc* doc = yyjson_read(text.data(), text.size(), 0);
    if (doc == nullptr) {
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root) || yyjson_obj_get(root, "shine_graph") == nullptr) {
        yyjson_doc_free(doc);
        return false;
    }
    ResetGraph();
    g_nextNodeId = static_cast<std::size_t>(yyjson_get_sint(yyjson_obj_get(root, "next_id")));
    if (g_nextNodeId == 0) {
        g_nextNodeId = 1;
    }
    if (yyjson_val* view = yyjson_obj_get(root, "view"); yyjson_is_obj(view)) {
        g_zoom = static_cast<float>(yyjson_get_num(yyjson_obj_get(view, "zoom")));
        if (g_zoom <= 0.f) {
            g_zoom = 1.f;
        }
        g_offset = ReadVec2Val(yyjson_obj_get(view, "offset"));
    }

    if (yyjson_val* jnodes = yyjson_obj_get(root, "nodes"); yyjson_is_arr(jnodes)) {
        const std::size_t n = yyjson_arr_size(jnodes);
        for (std::size_t i = 0; i < n; ++i) {
            yyjson_val* jn = yyjson_arr_get(jnodes, i);
            yyjson_val* typeVal = yyjson_obj_get(jn, "type");
            const std::string type = yyjson_is_str(typeVal) ? yyjson_get_str(typeVal) : std::string{};
            yyjson_val* titleVal = yyjson_obj_get(jn, "title");
            const std::string title = yyjson_is_str(titleVal) ? yyjson_get_str(titleVal) : std::string{};

            std::unique_ptr<ShineComfyNode> node;
            if (const auto def = g_defs.find(type); def != g_defs.end()) {
                node = std::make_unique<ShineComfyNode>(def->second);
            } else if (const NodeDef* basic = FindBasicDef(type); basic != nullptr) {
                node = std::make_unique<ShineComfyNode>(type, title.empty() ? basic->label : title);
            } else {
                // 未注册类型：**保住数据**（端口按存档重建；连上 ComfyUI 后可编译）
                node = std::make_unique<ShineComfyNode>(type, title.empty() ? type : title);
                node->RestoreSockets(ReadSockets(jn, "inputs", SocketFlow::Input),
                                     ReadSockets(jn, "outputs", SocketFlow::Output));
            }
            if (!title.empty()) {
                node->SetTitle(title);
            }
            yyjson_val* idVal = yyjson_obj_get(jn, "id");
            node->id = yyjson_is_str(idVal) ? yyjson_get_str(idVal) : ("n" + std::to_string(g_nextNodeId));
            node->pos = ReadVec2Val(yyjson_obj_get(jn, "pos"));
            node->size = ReadVec2Val(yyjson_obj_get(jn, "size"), Vec2{320.f, 96.f});
            if (yyjson_val* widgets = yyjson_obj_get(jn, "widgets"); yyjson_is_obj(widgets)) {
                yyjson_obj_iter it;
                yyjson_obj_iter_init(widgets, &it);
                yyjson_val* key = nullptr;
                while ((key = yyjson_obj_iter_next(&it))) {
                    const char* name = yyjson_get_str(key);
                    yyjson_val* val = yyjson_obj_iter_get_val(key);
                    if (name == nullptr) {
                        continue;
                    }
                    if (yyjson_is_str(val)) {
                        node->SetWidgetValue(name, yyjson_get_str(val));
                    } else if (yyjson_is_bool(val)) {
                        node->SetWidgetValue(name, yyjson_get_bool(val) ? "true" : "false");
                    } else if (yyjson_is_num(val)) {
                        node->SetWidgetValue(name, util::FromDouble(yyjson_get_num(val)));
                    }
                }
            }
            g_nodes.push_back(std::move(node));
        }
    }

    if (yyjson_val* jlinks = yyjson_obj_get(root, "links"); yyjson_is_arr(jlinks)) {
        const std::size_t n = yyjson_arr_size(jlinks);
        for (std::size_t i = 0; i < n; ++i) {
            yyjson_val* jl = yyjson_arr_get(jlinks, i);
            if (!yyjson_is_arr(jl) || yyjson_arr_size(jl) < 4) {
                continue;
            }
            yyjson_val* fromVal = yyjson_arr_get(jl, 0);
            yyjson_val* toVal = yyjson_arr_get(jl, 2);
            if (!yyjson_is_str(fromVal) || !yyjson_is_str(toVal)) {
                continue;
            }
            ModelLink link;
            link.fromId = yyjson_get_str(fromVal);
            link.fromSlot = static_cast<std::size_t>(yyjson_get_sint(yyjson_arr_get(jl, 1)));
            link.toId = yyjson_get_str(toVal);
            link.toSlot = static_cast<std::size_t>(yyjson_get_sint(yyjson_arr_get(jl, 3)));
            g_links.push_back(std::move(link));
        }
    }

    if (yyjson_val* jgroups = yyjson_obj_get(root, "groups"); yyjson_is_arr(jgroups)) {
        const std::size_t n = yyjson_arr_size(jgroups);
        for (std::size_t i = 0; i < n; ++i) {
            yyjson_val* jg = yyjson_arr_get(jgroups, i);
            ModelGroup group;
            group.pos = ReadVec2Val(yyjson_obj_get(jg, "pos"));
            group.size = ReadVec2Val(yyjson_obj_get(jg, "size"));
            yyjson_val* cap = yyjson_obj_get(jg, "caption");
            group.caption = yyjson_is_str(cap) ? yyjson_get_str(cap) : std::string{};
            g_groups.push_back(std::move(group));
        }
    }

    yyjson_doc_free(doc);
    return true;
}

} // namespace

std::string GraphPath() { return util::PathToUtf8(GraphFilePath()); }

bool Init() {
    if (g_inited) {
        return true;
    }
    SyncComfyCatalog();

    // 尝试从磁盘恢复；没有则播种演示图。文件读写走 util（宽字符路径安全）。
    const std::filesystem::path graphFile = GraphFilePath();
    if (std::filesystem::exists(graphFile)) {
        const std::optional<std::string> bytes = util::ReadFileBytes(graphFile);
        WarnUnknownTypes(graphFile);
        if (bytes.has_value() && LoadGraphFromText(*bytes)) {
            log::Info("已加载节点图 {}", GraphPath());
        } else {
            log::Warn("节点图加载失败或格式过旧（旧画布档不兼容，可用「导入」转成 API/工作流 JSON），使用演示图");
            ResetGraph();
            SeedDemoGraph();
        }
    } else {
        SeedDemoGraph();
    }

    g_inited = true;
    log::Info("GraphHost 初始化完成（纯图模型）");
    return true;
}

void Shutdown() {
    if (g_inited) {
        SaveGraph();
    }
    ResetGraph();
    g_inited = false;
}

bool SaveGraph() {
    const std::filesystem::path file = GraphFilePath();
    const std::string text = SerializeGraph();
    if (text.empty()) {
        log::Error("节点图序列化失败");
        return false;
    }
    if (util::WriteFileBytes(file, text)) {
        log::Info("节点图已保存 {}", util::PathToUtf8(file));
        return true;
    }
    log::Error("节点图保存失败 {}", util::PathToUtf8(file));
    return false;
}

bool LoadGraph() {
    const std::filesystem::path file = GraphFilePath();
    if (!std::filesystem::exists(file)) {
        log::Warn("不存在 {}", util::PathToUtf8(file));
        return false;
    }
    WarnUnknownTypes(file); // 未注册类型会保留但不可编译 → 先告警（P3.3 S6）
    const std::optional<std::string> bytes = util::ReadFileBytes(file);
    if (!bytes.has_value()) {
        log::Error("节点图加载失败 {}", util::PathToUtf8(file));
        return false;
    }
    if (LoadGraphFromText(*bytes)) {
        log::Info("节点图已加载 {}", util::PathToUtf8(file));
        return true;
    }
    log::Error("节点图加载失败（格式过旧或损坏）{}", util::PathToUtf8(file));
    return false;
}

size_t NodeCount() { return g_nodes.size(); }
size_t ConnectionCount() { return g_links.size(); }
float Zoom() { return g_zoom; }

size_t SelectedCount() { return g_selection.size(); }

namespace {
SelectedNodeInfo InfoOf(const ShineComfyNode& n) {
    SelectedNodeInfo info;
    info.id = n.id;
    info.name = n.Title();
    info.type = n.ClassType();
    info.x = n.pos.x;
    info.y = n.pos.y;
    info.w = n.size.x;
    info.h = n.size.y;
    info.inputs = static_cast<int>(n.Inputs().size());
    info.outputs = static_cast<int>(n.Outputs().size());
    return info;
}
} // namespace

std::vector<SelectedNodeInfo> SelectedNodes() {
    std::vector<SelectedNodeInfo> out;
    for (const std::string& id : g_selection) {
        if (const ShineComfyNode* n = FindNode(id); n != nullptr) {
            out.push_back(InfoOf(*n));
        }
    }
    return out;
}

std::vector<SelectedNodeInfo> AllCanvasNodes() {
    std::vector<SelectedNodeInfo> out;
    out.reserve(g_nodes.size());
    for (const auto& n : g_nodes) {
        out.push_back(InfoOf(*n));
    }
    return out;
}

void SetSelection(std::vector<std::string> ids) { g_selection = std::move(ids); }

void DeleteSelected() {
    if (g_selection.empty()) {
        return;
    }
    // 选中集通常只有个位数，直接线性扫即可，不必为一次 contains 额外建哈希集
    std::erase_if(g_links, [&](const ModelLink& l) {
        return std::ranges::contains(g_selection, l.fromId) || std::ranges::contains(g_selection, l.toId);
    });
    std::erase_if(g_nodes, [&](const auto& n) { return std::ranges::contains(g_selection, n->id); });
    g_selection.clear();
    log::Info("已删除选中节点");
}

ShineComfyNode* CreateNodeByType(std::string_view type) {
    SyncComfyCatalog();
    const std::string typeStr{type};
    std::unique_ptr<ShineComfyNode> node;
    if (const auto def = g_defs.find(typeStr); def != g_defs.end()) {
        node = std::make_unique<ShineComfyNode>(def->second);
    } else if (const NodeDef* basic = FindBasicDef(typeStr); basic != nullptr) {
        node = std::make_unique<ShineComfyNode>(typeStr, basic->label);
    } else {
        log::Warn("未知节点类型 {}", typeStr);
        return nullptr;
    }
    return AddNode(std::move(node));
}

bool SpawnNode(std::string_view type, float x, float y) {
    ShineComfyNode* node = CreateNodeByType(type);
    if (node == nullptr) {
        return false;
    }
    node->pos = Vec2{x, y};
    log::Info("创建节点 {}", type);
    return true;
}

bool SpawnNodeAtViewCenter(std::string_view type) {
    ShineComfyNode* node = CreateNodeByType(type);
    if (node == nullptr) {
        return false;
    }
    // 视口中心（图形坐标）：新节点按其自身尺寸居中；连点多次时阶梯错开，免得叠成一坨
    static int cascade = 0;
    const float step = static_cast<float>(cascade % 8) * 28.0f;
    ++cascade;
    node->pos = Vec2{g_viewCenter.x - node->size.x * 0.5f + step, g_viewCenter.y - node->size.y * 0.5f + step};
    log::Info("创建节点 {}（视口中心）", type);
    return true;
}

void CenterView() {
    // 视图居中 = 视口中心对准整图包围盒中心（渲染层拿 AllCanvasNodes 自行换算）
    if (g_nodes.empty()) {
        g_viewCenter = Vec2{};
        return;
    }
    float minX = g_nodes.front()->pos.x, minY = g_nodes.front()->pos.y;
    float maxX = minX, maxY = minY;
    for (const auto& n : g_nodes) {
        minX = std::min(minX, n->pos.x);
        minY = std::min(minY, n->pos.y);
        maxX = std::max(maxX, n->pos.x + n->size.x);
        maxY = std::max(maxY, n->pos.y + n->size.y);
    }
    g_viewCenter = Vec2{(minX + maxX) * 0.5f, (minY + maxY) * 0.5f};
}

void AddGroupCommentAt(float x, float y, std::string_view caption, float w, float h) {
    g_groups.push_back(ModelGroup{Vec2{x, y}, Vec2{w, h}, std::string{caption}});
}

std::vector<GroupInfo> AllGroups() {
    std::vector<GroupInfo> out;
    out.reserve(g_groups.size());
    for (const ModelGroup& g : g_groups) {
        out.push_back(GroupInfo{g.pos.x, g.pos.y, g.size.x, g.size.y, g.caption});
    }
    return out;
}

Vec2 ViewOffset() { return g_offset; }

void SetView(float zoom, Vec2 offset) {
    g_zoom = zoom > 0.f ? zoom : 1.f;
    g_offset = offset;
}

void ClearGraph() {
    ResetGraph();
    log::Info("已清空节点图");
}

bool ImportGraphText(std::string_view text, std::string_view sourceLabel, ImportReport* report) {
    if (text.empty()) {
        log::Warn("导入内容为空：{}", sourceLabel);
        return false;
    }
    SyncComfyCatalog();
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
    // 兜底：按自家模型档（graph.json）读
    WarnUnknownTypesInText(text);
    const bool loaded = LoadGraphFromText(text);
    if (report != nullptr) {
        report->ok = loaded;
        report->format = WorkflowFormat::Unknown; // 自家模型档（`DetectFormat` 不认这一种）
        report->nodes = loaded ? g_nodes.size() : 0;
        report->links = loaded ? g_links.size() : 0;
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
            ++skipped; // 幂等：已经注册过（重复注册直接跳过）
            continue;
        }
        auto holder = std::make_shared<comfy::NodeTypeDef>(def);
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
    log::Info("注册 ComfyUI 节点：新增 {}，跳过 {}（已注册），目录共 {} 个", added, skipped, g_catalog.size());
}

std::vector<CatalogGroup> NodeCatalog() {
    SyncComfyCatalog();
    std::vector<CatalogGroup> groups;
    if (g_catalog.empty()) {
        // 未连接 ComfyUI：兜底显示内置示例类型（不报错）
        groups.push_back({"内置示例（未连接 ComfyUI，显示内置示例节点）", {}});
        for (const NodeDef& d : kNodeDefs) {
            groups.back().entries.push_back(CatalogEntry{d.type, d.label, groups.back().category, false, false});
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

std::vector<ShineComfyNode*> CanvasNodes() {
    std::vector<ShineComfyNode*> out;
    out.reserve(g_nodes.size());
    for (const auto& n : g_nodes) {
        out.push_back(n.get()); // 图内顺序（= 编译 id 发号顺序，确定性）
    }
    return out;
}

std::vector<GraphLink> EnumerateLinks() {
    std::vector<GraphLink> links;
    links.reserve(g_links.size());
    for (const ModelLink& link : g_links) {
        const ShineComfyNode* from = FindNode(link.fromId);
        const ShineComfyNode* to = FindNode(link.toId);
        if (from == nullptr || to == nullptr) {
            continue;
        }
        GraphLink out;
        out.fromNodeId = link.fromId;
        out.fromSocketIndex = link.fromSlot;
        out.toNodeId = link.toId;
        out.toSocketIndex = link.toSlot;
        if (link.fromSlot < from->Outputs().size()) {
            const std::vector<std::string>& types = from->Outputs()[link.fromSlot].allowedTypes;
            out.type = types.empty() ? std::string{} : types.front();
        }
        out.passedThrough = 0; // 旁路（Reroute 等）导入时已拉直为直接连线
        links.push_back(std::move(out));
    }
    return links;
}

bool TryConnect(ShineComfyNode* from, std::size_t fromOutput, ShineComfyNode* to, std::size_t toInput) {
    if (from == nullptr || to == nullptr || !CanConnect(*from, fromOutput, *to, toInput)) {
        return false;
    }
    // ComfyUI 语义：一个输入只有**一条**连线（重复连/换连由 UI 先删旧线）
    const bool inputTaken = std::ranges::any_of(g_links, [&](const ModelLink& l) {
        return l.toId == to->id && l.toSlot == toInput;
    });
    if (inputTaken) {
        return false;
    }
    const bool duplicate = std::ranges::any_of(g_links, [&](const ModelLink& l) {
        return l.fromId == from->id && l.fromSlot == fromOutput && l.toId == to->id && l.toSlot == toInput;
    });
    if (duplicate) {
        return false;
    }
    g_links.push_back(ModelLink{from->id, fromOutput, to->id, toInput});
    return true;
}

// *********************** P3.5：提交接线 ***********************

bool RunCurrentGraph() {
    SyncComfyCatalog();
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

} // namespace shine::flow
