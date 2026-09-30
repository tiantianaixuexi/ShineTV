#pragma once
// shine::kit::View_Stage —— 过程视图：步骤条 / 阶段流 / 阶段列表
//
// 从 kit/Views.cpp 拆出。三个都是「把一串有状态的工序画出来」：
//   * Steps     —— 步骤条（20/21/22 号圆 + 连接线，横向一条）
//   * StageFlow —— 阶段流（r-pill 节点 + 18×1.5 连接线，横向一条）
//   * StageList —— 阶段列表（行网格 44px，等宽码 + 状态 Tag）
//
// 放一起的理由不是「都跟阶段有关」，是 StageFlow 与 StageList **共用同一套
// 状态枚举、同一份 5 态配色、同一份状态→文案映射**（StageState / StageNode /
// StageColor / 那句 switch）。拆成两个文件就要么把它提到共享小头文件里
// （一行 switch 不值得一个头），要么让两处的 5 态配色各写一份 —— 那正是
// 「改了 FlowCanvas 的 running 色、忘了改列表里的」这类漂移的来源。
#include "ui/imgui/kit/Widgets.h"

#include <string>
#include <string_view>
#include <vector>

namespace shine::kit {

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

} // namespace shine::kit
