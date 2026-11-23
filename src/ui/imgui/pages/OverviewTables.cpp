// 总控页左列的**两张表**：全书甘特 · 阶段链 / 账本 · 成本·调用·降级
//
// 为什么合成一个文件：两张表是同一列里**上下相接**的两张卡（ledgerTop 就是
// gantt.max.y + kGap），而它们共享同一批读数（s.chain / s.doneChain / s.entries）。
// 分成两个文件的话，那句「三态都来自真实状态」的注释要劈成两半，
// 而两半各自都会开始假设对方已经说过了另一半。
#include "ui/imgui/pages/Overview.h"

#include "ui/imgui/kit/Fmt.h"
#include "ui/imgui/kit/Hash.h"
#include "ui/imgui/kit/Layout.h"
#include "ui/imgui/kit/Text.h"
#include "ui/imgui/pages/PageCommon.h"
#include "util/Encoding.h"

#include <algorithm>
#include <array>
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
    const bool showFootnote = totalStages > ganttRows;
    constexpr float kCardHeaderH = 46.6f;  // kit::Card 的标题区
    constexpr float kCardBodyPad = 16.0f;  // kit::Card 的正文内边距
    constexpr float kColumnHeaderH = 20.0f;
    constexpr float kRowH = 22.0f;

    Stack layout(left);
    layout.Advance(kCardHeaderH + kCardBodyPad);
    const Rect columnHeader = layout.Next(kColumnHeaderH);
    std::array<float, kGanttCols> rowTops{};
    for (int i = 0; i < ganttRows; ++i) {
        rowTops[i] = layout.Next(kRowH).min.y;
    }
    const float rowsBottom = layout.Cur();
    float footnoteY = rowsBottom;
    if (showFootnote) {
        layout.Advance(theme::space::kSteps[3]);
        footnoteY = layout.Next(LineBox(theme::font::kMicro)).min.y;
    }
    layout.Advance(kCardBodyPad);
    const Rect gantt{left.min, ImVec2(left.max.x, layout.Cur())};
    const Rect ganttBody = Card(draw, gantt, "全书甘特 · 阶段链", "grid", false, false);

    if (showFootnote) {
        const int footKey = totalStages * 100 + ganttRows;
        static int cachedFootKey = -1;
        static std::string ganttFoot;
        if (cachedFootKey != footKey) {
            cachedFootKey = footKey;
            ganttFoot = kit::Fmt("T 链共 {} 个阶段，这里显示前 {} 个", totalStages, ganttRows);
        }
        TextClipped(draw, ImVec2(ganttBody.min.x, footnoteY), ganttBody.width(),
                    ganttFoot, TextStyle{.size = theme::font::kMicro, .color = ColorTextMuted()});
    }

    // 甘特卡随工作区滚动，列头跟随工作区的裁剪上沿。剩余表身不足一行列头高时，
    // 表头与表行一起退场，脚注继续随卡片滚动。
    const float headerTop = std::max(columnHeader.min.y, draw->GetClipRectMin().y);
    const float headerBottom = headerTop + kColumnHeaderH;
    if (headerBottom > std::min(rowsBottom, draw->GetClipRectMax().y)) {
        return gantt;
    }

    const float rowLabelW = 88.0f;
    const float cellW =
        std::max(24.0f, (ganttBody.width() - rowLabelW) / static_cast<float>(kGanttCols));
    const TextStyle headerStyle{.size = theme::font::kSmall, .bold = true, .color = ColorTextMuted()};
    const float headerTextY = CenterY(headerTop + kColumnHeaderH * 0.5f, headerStyle);
    for (int c = 0; c < kGanttCols; ++c) {
        const int first = c * totalStages / kGanttCols;
        // 列头原样传 view，不再每列拷一个 string（第二十轮波 3）。
        const std::string_view label =
            totalStages == 0
                ? std::string_view("—")
                : std::string_view(s.chain[static_cast<std::size_t>(first)].code);
        Text(draw, ImVec2(ganttBody.min.x + rowLabelW + cellW * c + 8.0f, headerTextY), label, headerStyle);
    }

    draw->PushClipRect(ImVec2(ganttBody.min.x, headerBottom),
                       ImVec2(ganttBody.max.x, rowsBottom), true);
    const TextStyle ganttCodeStyle{.size = theme::font::kSmall, .mono = true,
                                   .color = ColorTextSecondary()};
    for (int i = 0; i < ganttRows; ++i) {
        const auto& def = s.chain[static_cast<std::size_t>(i)];
        const float gy = rowTops[i];
        const float rowCenterY = gy + kRowH * 0.5f;
        Text(draw, ImVec2(ganttBody.min.x, CenterY(rowCenterY, ganttCodeStyle)), def.code,
             ganttCodeStyle);
        const bool done = std::find(s.doneChain.begin(), s.doneChain.end(), def.code) != s.doneChain.end();
        const bool running = s.running && def.id == s.runningStage;
        // 停在这一格 = 它就是当前阶段，而这轮真的带着停止原因结束了
        const bool stopped = !running && !s.stopReason.empty() && def.id == s.runningStage;
        const int column = i * kGanttCols / totalStages;
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
                          ImVec2(ganttBody.min.x + rowLabelW + cellW * (c + 1) - 4.0f, gy + kRowH),
                          4.0f, fill);
            if (c == column && (done || running || stopped)) {
                DrawIconCentered(draw, done ? "check" : (stopped ? "x" : "play"),
                                 ImVec2(ganttBody.min.x + rowLabelW + cellW * c + cellW * 0.5f, rowCenterY),
                                 11.0f,
                                 done    ? ColorOf(theme::Current().statusOk)
                                 : stopped ? ColorOf(theme::Current().statusDanger)
                                           : ColorAccent());
            }
        }
    }
    draw->PopClipRect();
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
        TextClipped(draw, ImVec2(ledgerBody.min.x, ledgerBody.min.y), ledgerBody.width(),
                    s.bound ? "还没有落盘产物：跑完一个阶段后这里会出现真实记录。"
                            : "未绑定工程根。",
                    TextStyle{.size = theme::font::kHint, .color = ColorTextMuted()}, true);
    } else {
        const float colStage = ledgerBody.min.x;
        const float colHash = colStage + 70.0f;
        const float colOut = colHash + 150.0f;
        const float colDeg = colOut + 280.0f;
        const std::pair<const char*, float> heads[] = {{"阶段", colStage}, {"输入哈希", colHash},
                                                      {"产物", colOut},     {"降级", colDeg}};
        for (const auto& [text, x] : heads) {
            Text(draw, ImVec2(x, ledgerBody.min.y), text,
                 TextStyle{.size = theme::font::kSmall, .bold = true, .color = ColorTextMuted()});
        }
        const float rowH = 22.0f;
        // 「显示最后 N 条」是按**行数上限**定的，不再按「可用高度」反推 ——
        // 可用高度依赖那张被撑到 2400 的卡，是循环论证。
        const int shown = ledgerShown;
        const int first = ledgerTotal - shown;
        // 行内阶段码 / 产物文件名缓存（第二十轮波 3）：指纹覆盖 entries 的
        // (stage, output_path)，变了才重建。
        struct LedgerRow {
            std::string code;
            std::string fileName;
        };
        static std::vector<LedgerRow> ledgerRows;
        {
            std::uint64_t lk = HashMix(0, s.entries.size());
            for (const auto& entry : s.entries) {
                lk = HashMix(lk, static_cast<std::uint64_t>(static_cast<int>(entry.stage)));
                lk = HashMix(lk, Fnv1a(entry.output_path));
            }
            static std::uint64_t cachedLedgerKey = 0;
            if (cachedLedgerKey != lk) {
                cachedLedgerKey = lk;
                ledgerRows.clear();
                ledgerRows.reserve(s.entries.size());
                for (const auto& entry : s.entries) {
                    LedgerRow row;
                    row.code = pipeline::StageCode(entry.stage);
                    row.fileName =
                        util::PathToUtf8(util::PathFromUtf8(entry.output_path).filename());
                    ledgerRows.push_back(std::move(row));
                }
            }
        }
        float ly = ledgerBody.min.y + 18.0f;
        for (int i = first; i < static_cast<int>(s.entries.size()); ++i) {
            const auto& entry = s.entries[static_cast<std::size_t>(i)];
            const LedgerRow& row = ledgerRows[static_cast<std::size_t>(i)];
            Text(draw, ImVec2(colStage, ly), row.code,
                 TextStyle{.size = theme::font::kLabel, .mono = true, .color = ColorTextSecondary()});
            TextClipped(draw, ImVec2(colHash, ly), 140.0f,
                        entry.input_hash.empty() ? "—" : entry.input_hash,
                        TextStyle{.size = theme::font::kLabel, .mono = true, .color = ColorTextMuted()});
            TextClipped(draw, ImVec2(colOut, ly), colDeg - colOut - 12.0f, row.fileName,
                        TextStyle{.size = theme::font::kLabel, .mono = true, .color = ColorText()});
            if (entry.degradation.empty()) {
                // ⚠️ 原来写的是 `AddText(..., "—", "—")` —— begin == end，**这一格
                //    什么都没画**（ImGui 的 AddText 对空区间直接 return）。收编成
                //    string_view 时必须显式给长度 0，否则字面量会按 strlen 变成 3 字节
                //    而真的画出那个破折号 —— 那是**像素变化**，不是修复。
                Text(draw, ImVec2(colDeg, ly), std::string_view("—", 0),
                     TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()});
            } else {
                const float tagW = TagWidth(entry.degradation, true, false);
                Tag(draw, RectAt(colDeg, ly - 2.0f, tagW, 17.0f), entry.degradation, theme::Tone::Warn,
                    true);
            }
            ly += rowH;
        }
        if (ledgerTotal > shown) {
            // 页脚只随 (总数, 显示数) 变（第二十轮波 3）。
            const int footKey = ledgerTotal * 100 + shown;
            static int cachedLedgerFootKey = -1;
            static std::string ledgerFoot;
            if (cachedLedgerFootKey != footKey) {
                cachedLedgerFootKey = footKey;
                ledgerFoot = kit::Fmt("共 {} 条，显示最后 {} 条", ledgerTotal, shown);
            }
            // 脚注位置跟着行数走（脚注那 14px 已经算进卡高了），不再用 max.y - 24。
            TextClipped(draw, ImVec2(ledgerBody.min.x, ly + 2.0f), ledgerBody.width(), ledgerFoot,
                        TextStyle{.size = theme::font::kMicro, .color = ColorTextMuted()});
            ly += 14.0f;
        }
        // 汇总行只随预算字段变（第二十轮波 3）。
        {
            std::uint64_t sk = HashMix(0, static_cast<std::uint64_t>(ledgerTotal));
            sk = HashMix(sk, static_cast<std::uint64_t>(s.budget.llm_calls));
            sk = HashMix(sk, static_cast<std::uint64_t>(s.budget.high_quality_calls));
            sk = HashMix(sk, static_cast<std::uint64_t>(s.budget.shots));
            sk = HashMix(sk, static_cast<double>(s.budget.cost));
            static std::uint64_t cachedSummaryKey = 0;
            static std::string ledgerSummary;
            if (cachedSummaryKey != sk) {
                cachedSummaryKey = sk;
                ledgerSummary = kit::Fmt("阶段产物 {} · LLM 调用 {} · 高档 {} · 镜头 {} · 估算成本 {}",
                                         ledgerTotal, s.budget.llm_calls,
                                         s.budget.high_quality_calls, s.budget.shots,
                                         Money(s.budget.cost));
            }
            TextClipped(draw, ImVec2(ledgerBody.min.x, ly + 2.0f), ledgerBody.width(), ledgerSummary,
                        TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()});
        }
    }
    return ledger;
}

} // namespace overview
} // namespace shine::pages
