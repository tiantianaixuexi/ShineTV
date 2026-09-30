// shine::pages —— P5.1 总控（pipeline）· 页的**骨架**
//
// 这一页原先 971 行：状态结构、快照同步、worker 运行驱动、页头、运行状态卡、
// KPI 行、甘特、账本、S1–S12 规则表、右栏四张卡，外加 P5.2/P5.3 的迁移说明，
// 全在一个 .cpp 里，公开入口和七块子区域交错在一起。
//
// 拆分后的形状（**纯结构，零行为变化**）：
//   Overview.h           这一页的私有契约（状态 + 七块子区域的声明）
//   OverviewState.cpp    状态 / 快照同步 / 配置探针 / worker 运行驱动（零绘制）
//   OverviewFlow.cpp     页头操作条 + 运行状态卡
//   OverviewKpi.cpp      KPI 行（卡片本体在 PageKpi）
//   OverviewTables.cpp   左列：全书甘特 + 账本
//   OverviewStopRules.cpp 左列整宽：S1–S12 规则表
//   OverviewRightRail.cpp 右栏：停止规则 / 最近产物 / 运行信息 / 章节×V 阶段
//   WorkspaceA.cpp（本文件）公开入口 + **布局骨架**：谁排在哪、下一块从哪接
//
// 本文件留下的**只有布局**，因为布局是这一页唯一「拆开就没法各自决定」的东西：
// `content` / `left` / `rightCol` 三块矩形互相咬着算，把它们塞进任何一块里
// 都会让那块同时知道另外两块的几何。
#include "ui/imgui/pages/WorkspacePages.h"

#include "ui/imgui/pages/Overview.h"
#include "ui/imgui/pages/PageCommon.h"

#include <algorithm>
#include <cstring>

namespace shine::pages {

using namespace shine::kit;

// 头：图标 + 标题 + 副行 + 右侧控件区。返回内容区起点。
//
// ⚠️ 这是**六页共用**的那一个头（Page_Storyboard.cpp 也调它），所以它留在
//    WorkspacePages.h 的公开声明处 —— 它不是总控页的私有物。
Rect ViewHeader(Rect area, ImDrawList* draw, const char* icon, const char* title,
                const char* subtitle, Rect* rightOut) {
    DrawIcon(draw, icon, ImVec2(area.min.x, area.min.y), 20.0f, ColorAccent());
    draw->AddText(FontBoldAt(18.0f), 18.0f, ImVec2(area.min.x + 28.0f, area.min.y - 2.0f),
                  ColorText(), title, title + std::strlen(title));
    draw->AddText(FontAt(12.5f), 12.5f, ImVec2(area.min.x + 28.0f, area.min.y + 22.0f),
                  ColorTextMuted(), subtitle, subtitle + std::strlen(subtitle));
    if (rightOut != nullptr) {
        *rightOut = Rect{area.max.x - 320.0f, area.min.y, area.max.x, area.min.y + 30.0f};
    }
    return Rect{area.min.x, area.min.y + 48.0f, area.max.x, area.max.y};
}

// ================================================================ P5.1 总控
// 纯数据页：先做它验证数据通路（pipeline::{StageMachine, Budget, StopPolicy, Ledger}）。
// 阶段表直接来自 pipeline::AllStages()，不是前端硬编码的假数据。
void DrawOverview(Rect area, ImDrawList* draw) {
    overview::OverviewRun& s = overview::State();
    overview::SyncSnapshot(s);

    Rect right;
    Rect content = ViewHeader(area, draw, "gauge", "全流程总控台",
                              "一句话到成片 · 阶段机 / 预算 / 账本 / 检查点", &right);
    overview::DrawHeaderActions(draw, right, s);

    // 1) 运行状态卡 + StageFlow
    const Rect flowCard{content.min.x, content.min.y, content.max.x, content.min.y + 122.0f};
    overview::DrawFlowCard(draw, flowCard, s);

    // 2) KPI 行
    const float kpiTop = flowCard.max.y + kGap;
    const int kpiRows = overview::DrawKpiRow(draw, content, kpiTop, s);

    // 3) .grid-3-1（1fr / 300px gap16）
    // ⚠️ gridTop 必须按 KPI 的**实际行数**推，不能当只有一行：auto-fit 排 4 列时
    // 5 个 KPI 会换到第二行，按一行算会让下面的甘特卡盖住第二行卡片（文字直接压在一起）。
    const float gridTop = kpiTop + (96.0f + 14.0f) * static_cast<float>(kpiRows) + kGap;
    const float rightW = 300.0f;
    // 右栏按**可视高度**收口，不跟着 2400 的布局区高跑。
    // ⚠️ 跟着布局区高跑的后果是实测出来的：右栏 child 高 1978px，于是它成了
    //    一个**布局容器**而不是视口 —— 卡片在里面从头排到尾、裁切交给外层，
    //    「右栏自己滚」这件事压根不存在（构造期读到的可视高 1978 > 内容高 654，
    //    期望滚动上限 0）。正文流（左列甘特 + 账本）仍然按布局区高排。
    const float viewportH = pages::WorkspaceViewportHeight();
    const float rightBottom =
        viewportH > 0.0f ? std::min(content.max.y, area.min.y + viewportH) : content.max.y;
    const Rect left{content.min.x, gridTop, content.max.x - rightW - kGap, content.max.y};
    const Rect rightCol{left.max.x + kGap, gridTop, content.max.x, rightBottom};

    // 4) 左列：甘特 → 账本 → S1–S12 规则表，依次接排。
    const Rect gantt = overview::DrawGantt(draw, left, s);
    const Rect ledger = overview::DrawLedger(draw, left, gantt.max.y + kGap, s);
    const Rect s12 = overview::DrawStopRuleTable(draw, content, ledger.max.y + kGap);

    // 5) 右栏四张卡（自带 ScrollRegion）
    const float rightBottomUsed = overview::DrawRightRail(rightCol, s);

    // ---- 自报本页真实内容高度 ----
    //
    // Shell 给的布局区是写死的 2400px（本页在 2400 里排版），但 2400 不是本页的
    // 真实高度。真实高度 = 左右两列**实际画到的最底**，多给一个 kGap 让最后一张卡
    // 离页面底有点余量。Shell 拿它当工作区滚动区高度，滚动范围从此跟着内容走。
    //
    // ⚠️ 取 `max(左, 右)` 而不是右列的 `ry`：左列的账本卡通常比右栏**更深**，
    //    只报右列会让工作区把左列的账本卡底裁掉（外层 child 按上报的高度裁）。
    pages::SetPageContentHeight(std::max(std::max(ledger.max.y, s12.max.y), rightBottomUsed) -
                                area.min.y + kGap);
}

// ================================================================ P5.2 小说 / P5.3 资产
//
// 这两页的实现已**搬到 Page_Novel.cpp / Page_Assets.cpp**：它们要读 novel.db，而开库 / 查询 /
// worker 回投那一整套（BookState）都在 B 的匿名命名空间里。两页的真数据实现见
// Page_Novel.cpp 的 NovelPage::Draw / Page_Assets.cpp 的 AssetsPage::Draw —— 旧版这里的实现每个数字
// 都是写死的（"第 3 章 · 雨夜"、字数 2180、差异 0.2418、12 张假资产卡）。

} // namespace shine::pages
