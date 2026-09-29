#pragma once
// shine::kit::Views —— 过程艺术占位画与过程视图（P3.3 / P3.5）
//
// Art / ArtInk 在 webui 里是**程序化 SVG**（UI.jsx:198/223），无外部资源。
// 它们本身就是纯几何绘制，所以 ImDrawList 能 1:1 复刻 —— 不需要任何图片资源。
#pragma once

#include "ui/imgui/kit/Widgets.h"

#include <string>
#include <string_view>
#include <vector>

namespace shine::kit {

// ---- 18. Art（UI.jsx:198）----
// 12 组调色板按 |seed| % 12；viewBox 160×100；竖直渐变天空 + 日轮 r13（+r20 光晕）
// + 3 层山形。cover=true 时按 cover 规则铺满（见 P5.5 的锚点注意事项）。
void Art(ImDrawList* draw, Rect bounds, int seed, bool cover);

// ---- 19. ArtInk（UI.jsx:223）----
// 800×260；4 层水墨山形用 text-primary 透明度 .08/.13/.22/.42，
// 远两层高斯模糊 7（多层半透明叠近似）；小船 + 26×26 印泥印章
void ArtInk(ImDrawList* draw, Rect bounds, int seed, float opacity = 1.0f);

// ---- 20. Steps（UI.jsx:253）----
// 步 12/600 + 20×20 序号圆；当前 = accent 底 + accent-fg 字；
// 完成 = accent 边 + 10px 对勾；连接线 24×1.5
// 返回整条的宽度。
float Steps(ImDrawList* draw, Rect bounds, const std::vector<std::string>& steps, int current);
[[nodiscard]] float StepsWidth(const std::vector<std::string>& steps);

// ---- 21. StageFlow（StageFlow.jsx）----
// 节点 h30 pad 0 11 r-pill 1px 边 + 等宽 .code 10.5/700；5 态 todo/run/done/fail/skip；
// 连接线 18×1.5，done 时 accent 填充
enum class StageState { Todo, Running, Done, Failed, Skipped };

struct StageNode {
    std::string code;   // T1 / I3 / V7
    std::string label;  // 可选
    StageState state = StageState::Todo;
};

float StageFlow(ImDrawList* draw, Rect bounds, const std::vector<StageNode>& nodes);
[[nodiscard]] float StageFlowWidth(const std::vector<StageNode>& nodes);

// ---- 22. StageList（design-spec §5 #22）----
// 行网格 44px 1fr auto 88px gap10 pad 7/10 r6；等宽码 11.5/700 按态着色
void StageList(ImDrawList* draw, Rect bounds, const std::vector<StageNode>& nodes,
               std::string_view artifactColumn = {});

// ---- 23. FlowCanvas（FlowCanvas.jsx）----
// ⚠️ 整个重构最难的单个控件（refactor/phases.md P3.5 风险 R3）。
// 节点 150×76；5 态 todo/run/done/fail/skip；端口在左右两侧 y+38；
// 连线是三次贝塞尔（dx = max(36, |Δx| * 0.55)）；
// 滚轮**以光标为锚**缩放（0.35..2.0）、拖节点、拖背景平移、适应视图、
// 左上浮动工具条（放大/缩小/适应 + 运行中转圈）。
//
// 为什么状态要外部持有：ImGui 的自绘控件不接管生命周期，节点坐标、选中项、
// 视图变换必须由调用方（页面）存着，组件本身无状态可留 —— 跨帧状态放
// ImGui 内部 storage 会在页面切换后错位。
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
