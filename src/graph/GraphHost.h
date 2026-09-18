#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "comfy/ComfyNodeDef.h"
#include "graph/GraphCompiler.h"
#include "graph/WorkflowIO.h" // ImportReport（P3.7：导入明细对外可见）

namespace VisNodeSys {
class NodeArea;
class Node;
} // namespace VisNodeSys

namespace shine::graph {

struct SelectedNodeInfo {
    std::string id;
    std::string name;
    std::string type;
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;
    int inputs = 0;
    int outputs = 0;
};

bool Init();
void Shutdown();

void DrawCanvas();

bool SaveGraph();
bool LoadGraph();
// P3.6：按**内容**识别格式导入（API JSON / 工作流 v1.0 / 0.4 / 自家 VNS 图）
bool ImportGraphFile(std::string_view path);
// P3.7：同一件事，但吃**文本**（远端拉回来的工作流没有本地文件；`sourceLabel` 只用于日志）。
// 传 `report` 可拿到明细（节点/连线/未注册类型/告警）—— 面板用它提示"先连 ComfyUI 刷新 object_info"。
bool ImportGraphText(std::string_view text, std::string_view sourceLabel, ImportReport* report = nullptr);
std::string GraphPath();

size_t NodeCount();
size_t ConnectionCount();
float Zoom();
size_t SelectedCount();
std::vector<SelectedNodeInfo> SelectedNodes();
void DeleteSelected();

VisNodeSys::NodeArea* Area();

[[nodiscard]] bool SpawnNode(std::string_view type, float x, float y);
// P3.7b：建在当前**视口中心**（节点浏览器用；连点多次时自动错开一点，避免完全重叠）
[[nodiscard]] bool SpawnNodeAtViewCenter(std::string_view type);
// 按类型建节点（未注册返回 nullptr 并 log::Warn）；调用方负责 AddNode
[[nodiscard]] VisNodeSys::Node* CreateNodeByType(std::string_view type);
void CenterView();
void AddGroupCommentAt(float x, float y, std::string_view caption);
void ClearGraph();

// —— P3.3：动态注册与节点目录（节点定义来自 P3.1 的 object_info 解析结果）——
struct CatalogEntry {
    std::string className;
    std::string displayName;
    std::string category;
    bool deprecated = false;
    bool experimental = false;
};

struct CatalogGroup {
    std::string category;
    std::vector<CatalogEntry> entries;
};

// 幂等：已注册的 className 直接跳过（VNS 工厂没有注销接口，重复注册会失败）
void RegisterComfyNodes(const std::vector<comfy::NodeTypeDef>& defs);
[[nodiscard]] std::vector<CatalogGroup> NodeCatalog();
[[nodiscard]] bool IsRegistered(std::string_view className);
[[nodiscard]] std::size_t RegisteredComfyNodeCount();

// —— P3.4：连线枚举（从节点侧反查，不碰 VNS 私有 Connections）——
struct GraphLink {
    std::string fromNodeId;
    std::string toNodeId;
    std::size_t fromSocketIndex = 0; // 上游**输出**槽位
    std::size_t toSocketIndex = 0;   // 下游**输入**槽位
    std::string type;
    std::size_t passedThrough = 0;   // 透传（Reroute / LinkNode）跳过的节点数
};

[[nodiscard]] std::vector<GraphLink> EnumerateLinks();
// 画布遍历顺序的全部节点（= 编译时 id 发号顺序；确定性）
[[nodiscard]] std::vector<VisNodeSys::Node*> CanvasNodes();

// —— P3.5：提交接线 ——
bool RunCurrentGraph();
void InterruptCurrentGraph();
[[nodiscard]] std::string LastPromptId();
[[nodiscard]] const std::vector<CompileError>& LastCompileErrors();

} // namespace shine::graph
