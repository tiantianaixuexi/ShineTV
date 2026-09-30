// 总控页的**运行状态卡**与**页头操作条**
//
// 这两块放一起是因为它们是同一族：都在回答「现在能不能跑 / 跑到哪了」，
// 而且共用同一份判据 OverviewPipelineWired()。分成两个文件的话，那条
// 「执行体没接入时按钮为什么按不动」的注释要抄两遍 —— 而这条注释
// 正是本轮之前那个「整页假数据」缺陷的现场记录，抄散了就等于把它删了。
#include "ui/imgui/pages/Overview.h"

#include "ui/imgui/pages/PageCommon.h"
#include "ui/imgui/pages/WorkspacePages.h"

#include <algorithm>
#include <string>

namespace shine::pages {
namespace overview {

using namespace shine::kit;

void DrawHeaderActions(ImDrawList* draw, Rect right, OverviewRun& s) {
    // 头右侧：运行下一阶段(secondary) + 一键全流程(primary)；运行中换成 danger 停止
    // （Overview.jsx:31-35 同款切换）。没绑工程时两个按钮都是 disabled —— 不假装能跑。
    const char* nextLabel = "运行下一阶段";
    const float nextW =
        ButtonWidth(ButtonSize::Medium, 15.0f, LabelWidth(FontBoldAt(13.0f), 13.0f, nextLabel));
    const char* runLabel = s.running ? "停止" : "一键全流程";
    const float runW =
        ButtonWidth(ButtonSize::Medium, 15.0f, LabelWidth(FontBoldAt(13.0f), 13.0f, runLabel));
    ButtonSpec secondary;
    secondary.variant = ButtonVariant::Secondary;
    secondary.icon = "zap";
    secondary.disabled = s.running || !s.bound || !OverviewPipelineWired();
    if (Button(draw, RectAt(right.max.x - runW - nextW - 12.0f, right.min.y, nextW, 30.0f), nextLabel,
               secondary, "ov-next")) {
        StartRun(s, 1);
    }
    ButtonSpec primary;
    primary.variant = s.running ? ButtonVariant::Danger : ButtonVariant::Primary;
    primary.icon = s.running ? "stop" : "play";
    // 「一键全流程」在执行体未接入时也 disabled —— 它不是"点了会失败"，是**真的跑不了**。
    // 早先它可点，点完跑出一整套看起来正常的假数字（见 OverviewState.cpp 的 BindOverviewProject）。
    primary.disabled = !s.running && (!s.bound || !OverviewPipelineWired());
    if (Button(draw, RectAt(right.max.x - runW, right.min.y, runW, 30.0f), runLabel, primary,
               s.running ? "ov-stop" : "ov-all")) {
        if (s.running) {
            s.stopFlag->store(true);  // 阶段粒度：当前阶段收尾后停
        } else {
            StartRun(s, -1);
        }
    }
    // 禁用原因写在按钮**下面**而不是 tooltip：取证截图抓不到 tooltip，
    // 而「为什么按不动」必须在那张图上看得见。
    //
    // ⚠️ AddText 的长度参数是**字节数**。这里跟着字面量走（sizeof），不手数 ——
    //    「阶段执行体未接入 · 两个运行按钮已禁用」手算成 33 会少画两个字且不报错。
    if (!s.running && !OverviewPipelineWired() && s.bound) {
        static constexpr char kDisabledWhy[] = "阶段执行体未接入 · 两个运行按钮已禁用";
        draw->AddText(FontAt(11.5f), 11.5f,
                      ImVec2(right.max.x - runW - nextW - 12.0f, right.min.y + 34.0f),
                      ColorTextMuted(), kDisabledWhy, kDisabledWhy + sizeof(kDisabledWhy) - 1);
    }
}

void DrawFlowCard(ImDrawList* draw, Rect card, const OverviewRun& s) {
    Rect flowBody = Card(draw, card, "运行状态", "zap", false, s.running);
    // Overview.jsx:42-46 的三态文案，判据全部来自 runner_ / 账本，不写死
    std::string status;
    ImU32 statusColor = ColorTextMuted();
    if (s.running) {
        status = "当前阶段 " + pipeline::StageCode(s.runningStage) + " · 运行中";
        statusColor = ColorAccent();
    } else if (s.finished) {
        status = "全流程完成：产物与账本已落盘";
        statusColor = ColorOf(theme::Current().statusOk);
    } else if (s.userStopped) {
        status = "已手动停止 · 停在 " + pipeline::StageCode(s.runningStage);
        statusColor = ColorOf(theme::Current().statusWarn);
    } else if (!s.bound) {
        status = "未绑定工程根 · 打开一本小说后这里才有真实数据";
    } else if (!OverviewPipelineWired()) {
        // ⚠️ 早先这个状态走的是下面的「等待运行 · 从 T1 开始」分支。执行体没接入时
        //    「等待运行」是在**许诺一件做不到的事** —— 用户会一直等下去。
        //    直接把「跑不了」和原因写在这一行，而不是藏进某个脚注。
        status = std::string("未接入阶段执行体 · 本页不产生任何运行数据");
        statusColor = ColorOf(theme::Current().statusWarn);
    } else if (s.entries.empty()) {
        status = "等待运行 · 从 " + (s.chain.empty() ? std::string("T1") : s.chain.front().code) + " 开始";
    } else {
        status = "已停在 " + pipeline::StageCode(s.runningStage) + " · 未跑完";
        if (!s.stopRule.empty()) {
            status += " · " + s.stopRule;
        }
        if (!s.stopReason.empty()) {
            status += "：" + s.stopReason;
        }
    }
    draw->AddText(FontAt(12.5f), 12.5f, ImVec2(flowBody.min.x, flowBody.min.y), statusColor, status.data(),
                  status.data() + status.size());
    const int percent = ChainPercent(s);
    const std::string pct = std::to_string(percent) + "%";
    draw->AddText(FontBoldAt(18.0f), 18.0f, ImVec2(flowBody.max.x - 40.0f, flowBody.min.y - 2.0f),
                  ColorText(), pct.data(), pct.data() + pct.size());
    Progress(draw, Rect{flowBody.min.x, flowBody.min.y + 22.0f, flowBody.max.x, flowBody.min.y + 28.0f},
             static_cast<float>(percent) / 100.0f, s.running, false);

    std::vector<StageNode> nodes;
    nodes.reserve(s.chain.size());
    for (const auto& def : s.chain) {
        const bool landed =
            std::find(s.doneChain.begin(), s.doneChain.end(), def.code) != s.doneChain.end();
        const StageState state =
            landed ? StageState::Done : (s.running && def.id == s.runningStage ? StageState::Running
                                                                              : StageState::Todo);
        nodes.push_back(StageNode{def.code, def.name, state});
    }
    if (!nodes.empty()) {
        StageFlow(draw,
                  Rect{flowBody.min.x, flowBody.max.y - 34.0f, flowBody.min.x + 1400.0f, flowBody.max.y},
                  nodes);
    }
}

} // namespace overview
} // namespace shine::pages
