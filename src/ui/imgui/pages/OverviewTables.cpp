// 总控页左列的**两张表**：全书甘特 · 阶段链 / 账本 · 成本·调用·降级
//
// 为什么合成一个文件：两张表是同一列里**上下相接**的两张卡（ledgerTop 就是
// gantt.max.y + kGap），而它们共享同一批读数（s.chain / s.doneChain / s.entries）。
// 分成两个文件的话，那句「三态都来自真实状态」的注释要劈成两半，
// 而两半各自都会开始假设对方已经说过了另一半。
#include "ui/imgui/pages/Overview.h"

#include "ui/imgui/pages/PageCommon.h"
#include "util/Encoding.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

namespace shine::pages {
namespace overview {

using namespace shine::kit;

Rect DrawGantt(ImDrawList* draw, Rect left, const OverviewRun& s) {
    // ---- 甘特：行 = T 链阶段，列 = 链上 8 等分桶，表头是那一桶的**真实阶段码** ----
    // 旧版是 `(i*3)%8` 的假跨度 + 只有"在/不在跨度"两态。现在三态都来自真实状态：
    // 账本里有 → done(ok)，正在跑 → run(accent)，被规则卡住 → stop(danger)，其余 todo。
    constexpr int kGanttCols = 8;
    // 行数只受**列数**限制（8 等分桶是设计稿定的），不再有「窄窗口先砍行数」：
    // 那个循环拿 `left.height() - kGap - ledgerMinH` 当预算，而 `left` 的下边界
    // 来自 Shell 的 2400 布局区，预算恒大于卡高 ⇒ 循环**永远不触发**，是死代码；
    // 它记的还是「甘特与账本挤在同一列固定高度里」那个已经被拆掉的布局。
    // 现在账本卡按内容定高、内容超出由工作区滚动（真能滚了），行数无需为它让位。
    const int totalStages = static_cast<int>(s.chain.size());
    const int ganttRows = std::min(totalStages, kGanttCols);
    const Rect gantt{left.min.x, left.min.y, left.max.x,
                     left.min.y + 96.0f + 22.0f * static_cast<float>(ganttRows)};
    Rect ganttBody = Card(draw, gantt, "全书甘特 · 阶段链", "grid", false, false);
    const float rowLabelW = 88.0f;
    const float cellW =
        std::max(24.0f, (ganttBody.width() - rowLabelW) / static_cast<float>(kGanttCols));
    for (int c = 0; c < kGanttCols; ++c) {
        const int first = totalStages == 0 ? 0 : c * totalStages / kGanttCols;
        const std::string label = totalStages == 0 ? "—" : s.chain[static_cast<std::size_t>(first)].code;
        draw->AddText(FontBoldAt(10.5f), 10.5f,
                      ImVec2(ganttBody.min.x + rowLabelW + cellW * c + 8.0f, ganttBody.min.y),
                      ColorTextMuted(), label.data(), label.data() + label.size());
    }
    float gy = ganttBody.min.y + 20.0f;
    for (int i = 0; i < ganttRows; ++i) {
        const auto& def = s.chain[static_cast<std::size_t>(i)];
        // 原来写死 `gy + 5.0f`：甘特行高 22，正确中心是 gy + 11，字盒中心落在 10.25，
        // **偏上 0.75px**。改成按行中心算（行高变了也不会再算错）。
        draw->AddText(MonoAt(10.5f), 10.5f,
                      ImVec2(ganttBody.min.x, kit::CenterTextY(MonoAt(10.5f), 10.5f, gy + 11.0f)),
                      ColorTextSecondary(),
                      def.code.data(), def.code.data() + def.code.size());
        const bool done = std::find(s.doneChain.begin(), s.doneChain.end(), def.code) != s.doneChain.end();
        const bool running = s.running && def.id == s.runningStage;
        // 停在这一格 = 它就是当前阶段，而这轮真的带着停止原因结束了
        const bool stopped = !running && !s.stopReason.empty() && def.id == s.runningStage;
        const int column = totalStages == 0 ? 0 : i * kGanttCols / totalStages;
        for (int c = 0; c < kGanttCols; ++c) {
            ImU32 fill = ColorFillMuted();
            if (c == column) {
                if (done) {
                    fill = ColorOf(theme::CurrentDerived().ganttCell[static_cast<int>(theme::Tone::Ok)]);
                } else if (running) {
                    fill = ColorOf(theme::CurrentDerived().accentDim);
                } else if (stopped) {
                    fill = ColorOf(theme::CurrentDerived().ganttCell[static_cast<int>(theme::Tone::Danger)]);
                }
            }
            DrawRoundRect(draw, ImVec2(ganttBody.min.x + rowLabelW + cellW * c + 4.0f, gy),
                          ImVec2(ganttBody.min.x + rowLabelW + cellW * (c + 1) - 4.0f, gy + 22.0f),
                          4.0f, fill);
            if (c == column && (done || running || stopped)) {
                DrawIconCentered(draw, done ? "check" : (stopped ? "x" : "play"),
                                 ImVec2(ganttBody.min.x + rowLabelW + cellW * c + cellW * 0.5f, gy + 11.0f),
                                 11.0f,
                                 done    ? ColorOf(theme::Current().statusOk)
                                 : stopped ? ColorOf(theme::Current().statusDanger)
                                           : ColorAccent());
            }
        }
        gy += 22.0f;
    }
    if (totalStages > ganttRows) {
        DrawTextClipped(draw, FontAt(11.0f), 11.0f, ImVec2(ganttBody.min.x, ganttBody.max.y - 12.0f),
                        ganttBody.width(), ColorTextMuted(),
                        "T 链共 " + std::to_string(totalStages) + " 个阶段，这里显示前 " +
                            std::to_string(ganttRows) + " 个");
    }
    return gantt;
}

Rect DrawLedger(ImDrawList* draw, Rect left, float ledgerTop, const OverviewRun& s) {
    // ---- 账本：input_hash 直接用 Ledger 记下来的值（不再拼 "T1-a91f2c"）----
    //
    // ⚠️ 卡高按**内容**算，不再拉到 `left.max.y`（= Shell 给的 2400 布局区底）。
    //    拉满的后果实测是一张 **1750px 高、只有 6 行**的卡：6 行数据浮在顶上，
    //    下面 1450px 全是空背景，顺带让工作区多出约 1450px 的无效滚动 —— 滚到底
    //    只能看到一片空盒子。**卡高跟着内容走，需要滚动交给工作区**（现在它真能滚了）。
    const int ledgerTotal = static_cast<int>(s.entries.size());
    constexpr int kLedgerMaxRows = 14;
    const int ledgerShown = std::min(ledgerTotal, kLedgerMaxRows);
    // 卡高 = 头 44 + 列头 18 + 行 22×N + 脚注 14 + 汇总行 16 + 卡脚 16
    const float ledgerBodyH =
        s.entries.empty()
            ? 24.0f
            : 18.0f + 22.0f * static_cast<float>(ledgerShown) +
                  (ledgerTotal > ledgerShown ? 14.0f : 0.0f) + 16.0f;
    const float ledgerH = 44.0f + ledgerBodyH + 16.0f;
    const Rect ledger{left.min.x, ledgerTop, left.max.x, ledgerTop + ledgerH};
    Rect ledgerBody = Card(draw, ledger, "账本 · 成本 / 调用 / 降级", "list", false, false);
    if (s.entries.empty()) {
        DrawTextClipped(draw, FontAt(12.5f), 12.5f, ImVec2(ledgerBody.min.x, ledgerBody.min.y),
                        ledgerBody.width(), ColorTextMuted(),
                        s.bound ? "还没有落盘产物：跑完一个阶段后这里会出现真实记录。"
                                : "未绑定工程根。",
                        true);
    } else {
        const float colStage = ledgerBody.min.x;
        const float colHash = colStage + 70.0f;
        const float colOut = colHash + 150.0f;
        const float colDeg = colOut + 280.0f;
        const std::pair<const char*, float> heads[] = {{"阶段", colStage}, {"输入哈希", colHash},
                                                      {"产物", colOut},     {"降级", colDeg}};
        for (const auto& [text, x] : heads) {
            draw->AddText(FontBoldAt(10.5f), 10.5f, ImVec2(x, ledgerBody.min.y), ColorTextMuted(), text,
                          text + std::strlen(text));
        }
        const float rowH = 22.0f;
        // 「显示最后 N 条」是按**行数上限**定的，不再按「可用高度」反推 ——
        // 可用高度依赖那张被撑到 2400 的卡，是循环论证。
        const int shown = ledgerShown;
        const int first = ledgerTotal - shown;
        float ly = ledgerBody.min.y + 18.0f;
        for (int i = first; i < static_cast<int>(s.entries.size()); ++i) {
            const auto& entry = s.entries[static_cast<std::size_t>(i)];
            const std::string code = pipeline::StageCode(entry.stage);
            draw->AddText(MonoAt(11.5f), 11.5f, ImVec2(colStage, ly), ColorTextSecondary(), code.data(),
                          code.data() + code.size());
            DrawTextClipped(draw, MonoAt(11.5f), 11.5f, ImVec2(colHash, ly), 140.0f, ColorTextMuted(),
                            entry.input_hash.empty() ? "—" : entry.input_hash);
            DrawTextClipped(draw, MonoAt(11.5f), 11.5f, ImVec2(colOut, ly),
                            colDeg - colOut - 12.0f, ColorText(),
                            util::PathToUtf8(util::PathFromUtf8(entry.output_path).filename()));
            if (entry.degradation.empty()) {
                draw->AddText(FontAt(11.5f), 11.5f, ImVec2(colDeg, ly), ColorTextMuted(), "—", "—");
            } else {
                const float tagW = TagWidth(entry.degradation, true, false);
                Tag(draw, RectAt(colDeg, ly - 2.0f, tagW, 17.0f), entry.degradation, theme::Tone::Warn,
                    true);
            }
            ly += rowH;
        }
        if (ledgerTotal > shown) {
            // 脚注位置跟着行数走（脚注那 14px 已经算进卡高了），不再用 max.y - 24。
            DrawTextClipped(draw, FontAt(11.0f), 11.0f,
                            ImVec2(ledgerBody.min.x, ly + 2.0f), ledgerBody.width(),
                            ColorTextMuted(),
                            "共 " + std::to_string(ledgerTotal) + " 条，显示最后 " +
                                std::to_string(shown) + " 条");
            ly += 14.0f;
        }
        DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                        ImVec2(ledgerBody.min.x, ly + 2.0f), ledgerBody.width(),
                        ColorTextMuted(),
                        "阶段产物 " + std::to_string(ledgerTotal) +
                            " · LLM 调用 " + std::to_string(s.budget.llm_calls) + " · 高档 " +
                            std::to_string(s.budget.high_quality_calls) + " · 镜头 " +
                            std::to_string(s.budget.shots) + " · 估算成本 " + Money(s.budget.cost));
    }
    return ledger;
}

} // namespace overview
} // namespace shine::pages
