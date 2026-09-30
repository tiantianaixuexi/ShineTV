#pragma once
// shine::kit::View_FlowCanvas —— 出图/出片两页共用的节点画布
//
// 从 kit/Views.cpp 拆出。规格全部照 FlowCanvas.jsx + views.css:1005-1113。
//
// ⚠️ 整个重构最难的单个控件（refactor/phases.md P3.5 风险 R3）。
// 节点 150×76；5 态 todo/run/done/fail/skip；端口在左右两侧 y+38；
// 连线是三次贝塞尔（dx = max(36, |Δx| * 0.55)）；
// 滚轮**以光标为锚**缩放（0.35..2.0）、拖节点、拖背景平移、适应视图、
// 左上浮动工具条（放大/缩小/适应 + 运行中转圈）。
//
// 单独成文件而不是并进 View_Stage：它与 StageFlow 只是**看起来**相似（都是节点 +
// 连接线），机制完全不同 —— 这里有世界坐标变换、命中测试、跨帧拖拽状态和
// 自己的 5 态枚举（FlowState ≠ StageState），而 View_Stage 那三个都是一维
// 顺序流程、无命中、无坐标变换。放一起会让人以为它们能共用节点基类。
//
// 为什么状态要外部持有：ImGui 的自绘控件不接管生命周期，节点坐标、选中项、
// 视图变换必须由调用方（页面）存着，组件本身无状态可留 —— 跨帧状态放
// ImGui 内部 storage 会在页面切换后错位。
#include "ui/imgui/kit/Widgets.h"

#include <string>
#include <vector>

namespace shine::kit {

enum class FlowState { Todo, Running, Done, Failed, Skipped };

struct FlowNode {
    int id = 0;
    std::string icon;   // 16px 节点头图标
    std::string title;  // 节点标题
    std::string sub;    // 节点副行（等宽）
    FlowState state = FlowState::Todo;
    float x = 0.0f;     // 世界坐标
    float y = 0.0f;
};

struct FlowLink {
    int from = 0;
    int to = 0;
};

// 视图变换（平移 + 缩放）。调用方持有，跨帧保持。
struct FlowView {
    float x = 40.0f;
    float y = 30.0f;
    float z = 1.0f;
};

inline constexpr float kFlowNodeW = 150.0f;
inline constexpr float kFlowNodeH = 76.0f;

// 把节点铺成网格并重置视图（首帧/数据集变化时调一次）。
void FlowLayoutNodes(std::vector<FlowNode>& nodes, FlowView& view);
// 自适应视图：把全部节点缩放居中放进 bounds（FlowCanvas.jsx:36 的 fit()）。
void FlowFit(const std::vector<FlowNode>& nodes, Rect bounds, float fitInset, FlowView& view);

// 画一整张画布。nodes 会被就地改写（拖拽/平移），selectedInOut 读写选中项。
void FlowCanvas(ImDrawList* draw, Rect bounds, std::vector<FlowNode>& nodes,
                const std::vector<FlowLink>& links, FlowView& view, int& selected,
                float fitInset = 0.0f);

} // namespace shine::kit
