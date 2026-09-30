// 总控页的 **KPI 行**
//
// 五张卡逐像素同构，所以卡片本体住在 PageKpi（可复用组件）。这里只负责
// 「这一行怎么排」：列数、卡宽、每张卡摆哪、以及**每张卡显示什么**。
//
// ⚠️ 返回值是**实际行数**而不是卡底：调用方据此推 gridTop。早先这里按一行算，
//    auto-fit 排 4 列时 5 个 KPI 会换到第二行，下面的甘特卡就盖住第二行卡片
//    （文字直接压在一起）—— 返回行数是唯一能让调用点算对的形状。
#include "ui/imgui/pages/Overview.h"

#include "ui/imgui/pages/PageKpi.h"
// OverviewPipelineWired() 的声明在 WorkspacePages.h（公开入口），不在 Overview.h
// （后者只放这一页内部共享的东西）。KPI 行要判「执行体接没接」才能决定显示
// 「未接入」还是真数字，所以这里确实要公开声明。
#include "ui/imgui/pages/WorkspacePages.h"

#include <algorithm>
#include <string>

namespace shine::pages {
namespace overview {

using namespace shine::kit;

int DrawKpiRow(ImDrawList* draw, Rect content, float kpiTop, const OverviewRun& s) {
    // KPI 行：repeat(auto-fit, minmax(210px,1fr)) gap 14
    const int kpiCols = AutoGridCols(content.width(), 210.0f, 14.0f);
    const float kpiW =
        (content.width() - 14.0f * static_cast<float>(kpiCols - 1)) / static_cast<float>(kpiCols);
    // 五张卡的数全部来自 Budget / 账本。脚注写的是**口径**（上限、档位、条数），
    // 不是 mock 里那种没有业务字段可对应的"较上轮 +12"。
    //
    // ⚠️ 执行体未接入时，账本/预算恒为全 0。于是这五行会显示「0/17 · LLM 0 次 ·
    //    估算成本 ¥0.00 · 镜头 0 个 · 阶段产物 0 条」—— 看着像**测出来的零**，
    //    其实是**从来没测**。这两个 0 在界面上必须长得不一样，否则就是谎报。
    //    统一改成「未接入」，口径信息（上限/档位）保留。
    const bool wired = OverviewPipelineWired();
    const int totalStages = static_cast<int>(s.chain.size());
    const bool overBudget = s.budget.Exceeded();
    const std::string kNoData = "未接入";
    struct Kpi {
        std::string label;
        std::string value;
        std::string unit;
        std::string footnote;
        theme::Tone tone;
    };
    const Kpi kpis[] = {
        {"阶段进度",
         wired ? std::to_string(s.doneChain.size()) + "/" + std::to_string(totalStages) : kNoData,
         "",
         !wired ? std::string("执行体未接入 · 进度无从产生")
                : (s.doneChain.empty()
                       ? std::string("账本里还没有阶段产物")
                       : ("已落盘 " + s.doneChain.front() +
                          (s.doneChain.size() > 1 ? " – " + s.doneChain.back() : std::string()))),
         theme::Tone::Accent},
        {"LLM 调用", wired ? std::to_string(s.budget.llm_calls) : kNoData, "次",
         "高档 " + std::to_string(s.budget.high_quality_calls) + " · 上限 " +
             std::to_string(s.budget.max_llm_calls),
         theme::Tone::Info},
        {"估算成本", wired ? Money(s.budget.cost) : kNoData, "",
         "上限 " + Money(s.budget.max_cost), theme::Tone::Ok},
        {"镜头", wired ? std::to_string(s.budget.shots) : kNoData, "个",
         "上限 " + std::to_string(s.budget.max_shots), theme::Tone::Warn},
        {"阶段产物", wired ? std::to_string(static_cast<int>(s.entries.size())) : kNoData, "条",
         overBudget ? ("预算已停：" + (s.budget.Reason().empty() ? std::string("超限") : s.budget.Reason()))
                    : std::string("work/ 下每阶段一文件"),
         theme::Tone::Danger},
    };
    for (std::size_t i = 0; i < std::size(kpis); ++i) {
        const int column = static_cast<int>(i) % kpiCols;
        const int row = static_cast<int>(i) / kpiCols;
        KpiCard(draw,
                RectAt(content.min.x + (kpiW + 14.0f) * static_cast<float>(column),
                       kpiTop + (96.0f + 14.0f) * static_cast<float>(row), kpiW, 96.0f),
                kpis[i].label, kpis[i].value, kpis[i].unit, kpis[i].footnote, kpis[i].tone);
    }
    return (static_cast<int>(std::size(kpis)) + kpiCols - 1) / std::max(1, kpiCols);
}

} // namespace overview
} // namespace shine::pages
