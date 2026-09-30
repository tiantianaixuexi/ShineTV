// 总控页**右栏**：流水线停止规则 / 最近产物 / 运行信息 / 章节 × V 阶段
//
// 四张卡在同一个 ScrollRegion 里，且**共用一条纵向游标 ry**（每张卡画完把
// ry 推到自己的卡底 + kGap）。这条游标是这一族与左列最大的结构差异：左列是
// 「按内容算高、依次排下去」，右栏是「按内容算高、依次排下去、溢出交给滚动」。
// 游标一旦断推，最后一张卡就「不在内容里」—— 滚到底也拍不到。
//
// ⚠️ 整块必须画在 **child 自己的** draw list 上（rightScroll.drawList()）。
//    外壳传进来的是**外层**（工作区 child）的 list，而 BeginChild 的裁剪矩形
//    只写进 child 自己那条 list 的 CmdBuffer —— 画到外层 list 上，右栏照样
//    Begin/End，但裁剪对那些 draw call **完全无效**。症状不是「卡片消失」，
//    而是右栏滚上去之后盖住 KPI 行和页头按钮（实测：停止条件卡浮到 y=93，
//    压在「镜头 / 未接入 ↑」和页头「运行」按钮上面），一眼看去像布局算错。
#include "ui/imgui/pages/Overview.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "ui/imgui/kit/Scroll.h"
#include "ui/imgui/pages/PageCommon.h"
#include "util/Encoding.h"
#include "util/Shell.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace shine::pages {
namespace overview {

using namespace shine::kit;

float DrawRightRail(Rect rightCol, const OverviewRun& s) {
    // 「章节 × V 阶段」矩阵（数据层 `BookSideView::chapterVStages` 已接好）之所以
    // 曾经放不下，也是同一个原因：右栏在那个窗口高度下塞不下第四张卡。
    //
    // ⚠️ 上面那段注释的结论已经过期：矩阵现已渲染在本文件末尾。它当时记的
    //    「放不下」建立在一个**从未生效**的机制上（右栏的 ScrollRegion 当时根本
    //    不能滚），所以「放不下」是必然的而不是设计约束。留在这里提醒别再拿它当结论。
    float rightBottomUsed = rightCol.min.y;
    ScrollRegion rightScroll("ov-right", rightCol);
    if (rightScroll) {
        ImDrawList* cdraw = rightScroll.drawList();
        const Rect rc = rightScroll.content();
        const float cardW = rc.width();
        float ry = rc.min.y;
        const pipeline::StopDecision decision = EvaluateStopPolicy(s.budget);
        const bool hasReason = decision.stop && !decision.reason.empty();
        const std::string verdict =
            decision.stop ? decision.rule : std::string("未触发任何停止条件");

        // ---- 卡 1：流水线停止规则（预算 + 前置条件，**不用 S 编号**）----
        //
        // 与左栏那张「停止条件 · S1–S12」（OverviewStopRules.cpp）**不是同一套编号**：
        // `pipeline::StopPolicy` 判 5 组（预算 / LLM 前置 / Comfy 前置 / 交叉复核 /
        // 场景门禁），与 `09` §2.2 的 S1–S12 是两回事。旧实现两边都用 S 开头，于是
        // 「S1」在这张卡里指 LLM 调用预算、在左栏那张卡里指机器校验连续失败 ——
        // 同一个代号两个意思。
        const float budgetH = 44.0f + 20.0f + (hasReason ? 30.0f : 0.0f) + 4.0f * 20.0f + 28.0f;
        const Rect budget{rc.min.x, ry, rc.max.x, ry + budgetH};
        Rect budgetBody = Card(cdraw, budget, "流水线停止规则", "list", false, false);
        ry = budget.max.y + kGap;
        float by = budgetBody.min.y;
        StatusDot(cdraw, ImVec2(budgetBody.min.x + 4.0f, by + 6.0f),
                  decision.stop ? theme::Tone::Warn : theme::Tone::Ok, false);
        cdraw->AddText(FontBoldAt(12.5f), 12.5f, ImVec2(budgetBody.min.x + 16.0f, by),
                       decision.stop ? ColorOf(theme::Current().statusWarn)
                                     : ColorOf(theme::Current().statusOk),
                       verdict.data(), verdict.data() + verdict.size());
        by += 20.0f;
        if (hasReason) {
            DrawTextClipped(cdraw, FontAt(11.5f), 11.5f, ImVec2(budgetBody.min.x, by),
                            budgetBody.width(), ColorTextMuted(), decision.reason, true);
            by += 30.0f;
        }
        // 四个上限就是 Budget 的四个字段（`src/pipeline/Budget.h`），顺序与旧
        // StopReportView 一致。刻意不编 S 编号 —— 上面那张卡已经把 S1–S12 占给真规则了。
        const std::vector<std::pair<std::string, std::string>> thresholds = {
            {"LLM 调用", std::to_string(s.budget.llm_calls) + " / " +
                             std::to_string(s.budget.max_llm_calls)},
            {"高档调用", std::to_string(s.budget.high_quality_calls) + " / " +
                              std::to_string(s.budget.max_high_quality_calls)},
            {"镜头", std::to_string(s.budget.shots) + " / " + std::to_string(s.budget.max_shots)},
            {"成本", Money(s.budget.cost) + " / " + Money(s.budget.max_cost)},
        };
        const bool overBudget = s.budget.Exceeded();
        for (const auto& [code, name] : thresholds) {
            StatusDot(cdraw, ImVec2(budgetBody.min.x + 4.0f, by + 6.0f),
                      overBudget ? theme::Tone::Warn : theme::Tone::Idle, false);
            DrawTextClipped(cdraw, FontAt(12.5f), 12.5f, ImVec2(budgetBody.min.x + 16.0f, by),
                            budgetBody.width() - 16.0f, ColorTextSecondary(), code);
            DrawTextClipped(cdraw, FontAt(12.0f), 12.0f, ImVec2(budgetBody.min.x + 90.0f, by),
                            budgetBody.width() - 90.0f, ColorText(), name);
            by += 20.0f;
        }
        DrawTextClipped(
            cdraw, FontAt(11.0f), 11.0f, ImVec2(budgetBody.min.x, by + 4.0f), budgetBody.width(),
            ColorTextMuted(),
            std::string("前置 · LLM ") + (LlmConfigured() ? "已配置" : "未配置") + " · ComfyUI " +
                (Settings().comfyBaseUrl.empty() ? "未配置" : "已配置") + " · 交叉复核 " +
                (CrossReviewConfigured() ? "已配置" : "未配置") + " · Scene 未查询 novel.db",
            true);

        // 最近产物（webui Overview.jsx:132-157 整块）
        //
        // ⚠️ 画**全部**条目，不再按「可用高度」砍行 —— 砍行的逻辑本身依赖一个
        //    算错了的空间（见本文件开头那段重叠分析），而且砍掉之后用户完全看不出
        //    「还有更多」。现在内容超出由 ScrollRegion 滚。
        const int artRows = static_cast<int>(s.entries.size());
        const float artH =
            44.0f + 16.0f + (artRows > 0 ? 32.0f * static_cast<float>(artRows) + 4.0f : 24.0f);
        const Rect art{rc.min.x, ry, rc.max.x, ry + artH};
        Rect artBody = Card(cdraw, art, "最近产物", "folder", false, false);
        ry = art.max.y + kGap;
        ButtonSpec viewSpec;
        viewSpec.variant = ButtonVariant::Ghost;
        viewSpec.size = ButtonSize::Small;
        viewSpec.disabled = s.entries.empty();
        if (Button(cdraw, RectAt(art.max.x - 60.0f, art.min.y + 10.0f, 44.0f, 24.0f), "查看", viewSpec,
                   "ov-view")) {
            const std::string err = util::ShellOpen(util::PathFromUtf8(s.entries.back().output_path));
            if (!err.empty()) {
                log::Error("总控：打开产物失败 {}", err);
            }
        }
        if (artRows == 0) {
            DrawTextClipped(cdraw, FontAt(12.0f), 12.0f, ImVec2(artBody.min.x, artBody.min.y),
                            artBody.width(), ColorTextMuted(),
                            s.bound ? "还没有落盘产物。" : "未绑定工程根。", true);
        } else {
            float ay = artBody.min.y;
            for (std::size_t i = 0; i < s.entries.size(); ++i) {
                const auto& entry = s.entries[i];
                const std::filesystem::path path = util::PathFromUtf8(entry.output_path);
                // 产物行是**双行卡**（文件名 + 阶段/降级说明）→ kit::ListCard，
                // 不是 ListRow。硬塞进 ListRow 会逼调用点自己排第二行的 Y ——
                // 那正是这 5 处各写各的根源。
                //
                // ⚠️ hit 由 ListCard **一次性**取齐并返回。原代码注释记的就是这个坑：
                // 先 Hovered(idA) 再 Clicked(idB) 会在同一矩形上叠两个 InvisibleButton，
                // ImGui 只让先注册的拿到 HoveredId，第二个永远 clicked=false ——
                // 那一行点不开，而编译、日志、截图全绿。
                std::string sub = pipeline::StageCode(entry.stage);
                if (!entry.degradation.empty()) {
                    sub += " · 降级 " + entry.degradation;
                }
                kit::ListCardSpec spec;
                spec.id = "ov-art-" + std::to_string(i);
                spec.title = util::PathToUtf8(path.filename());
                spec.description = sub;
                spec.iconSize = 0.0f;   // 无图标
                spec.paddingX = 6.0f;
                spec.titleSize = 12.0f;
                spec.descSize = 11.0f;
                spec.textInset = 0.0f;
                // 右侧留给 chevron：文字可用宽因此收窄，不必调用点自己算。
                spec.textGap = 3.0f;
                const Rect row{artBody.min.x - 4.0f, ay, artBody.max.x, ay + 32.0f};
                if (kit::ListCard(cdraw, row, spec).clicked) {
                    const std::string err = util::ShellOpen(path);
                    if (!err.empty()) {
                        log::Error("总控：打开产物失败 {}", err);
                    }
                }
                // chevron：ListCard 不画它（设计里这一族是 hover 卡 + 选中勾）。
                // 这一行原本有 chevron，保留它作为「可点开」的提示。
                DrawIcon(cdraw, "chevron",
                         ImVec2(row.max.x - 16.0f, row.center().y - 6.0f), 12.0f, ColorTextMuted());
                ay += 36.0f;
            }
        }

        // 运行信息：Overview.jsx:160-167 的四行（当前项目 / 数据目录 / 检查点 / 预算余量）
        const float infoH = 44.0f + 4.0f * 20.0f + 16.0f;
        const Rect info{rc.min.x, ry, rc.max.x, ry + infoH};
        Rect infoBody = Card(cdraw, info, "运行信息", "info", false, false);
        ry = info.max.y + kGap;
        char chapterText[16];
        std::snprintf(chapterText, sizeof(chapterText), "ch%03d", s.chapter < 0 ? 0 : s.chapter);
        KeyValues(cdraw, infoBody,
                  {{"当前项目", s.root.empty() ? std::string("未打开项目")
                                              : util::PathToUtf8(s.root.filename())},
                   {"数据目录", s.root.empty() ? std::string("—") : util::PathToUtf8(s.root)},
                   {"检查点", s.chapter < 0 ? std::string("无") : std::string(chapterText)},
                   {"预算余量", Money(s.budget.max_cost - s.budget.cost) + " / " +
                                    Money(s.budget.max_cost)}});

        // ---- 章节 × V 阶段矩阵：行 = 章，列 = V1–V7 ----
        //
        // ⚠️ 这个维度**在数据层一直就有**：表 `stage_artifacts` 带 `chapter_id` + `stage`
        //    + `scene_ord/shot_ord` + `created`，只读接口 `ListStageArtifacts(chapterId,
        //    stage)` 也在（`src/novel/NovelVisual.h:242-243`）。早先把「章节 × 阶段」
        //    整个记成缺口（「章节维度在数据层根本不存在」）是**只看了 T 链**得出的结论
        //    —— T 链的 `LedgerEntry` / `work/T1.json` 确实不带章节，但**V 链带**。
        //    所以左边那张 T 链甘特没有章节轴是事实，这里这张有。
        //
        // 上一轮这张卡渲染过又删掉了：当时右栏是「各自 max(下限, 剩余)」的重叠布局，
        // 塞不进去。改成 ScrollRegion + 内容真实高度之后**容量不再是硬约束**，
        // 高度按行数直接算，超出由滚动承载。
        const pages::BookSideView& book = pages::BookSide();
        if (book.bound && !book.chapters.empty()) {
            const int rows = static_cast<int>(book.chapters.size());
            // 高度按内容**逐段**算，不写死一个数：
            //   Card 带标题时 body.min.y = min.y+44、body.max.y = max.y-16
            //   （Widgets.cpp:416），所以卡高 = 44(头) + bodyH + 16(底 pad)；
            //   bodyH = 16(列头留白) + 22*行数 + 6(行后间隙) + 16(脚注)。
            //
            // ⚠️ 早先这里写的是 `44 + 16 + 22*rows + 16` —— 脚注没有自己的高度，
            //    于是 `matBody.max.y - 12` 落在**最后一行里面**（行底 = +16+22*rows，
            //    脚注顶 = +22*rows+4），脚注压在最后一行上。**表面上矩阵是对的**，
            //    只有截图放大看才发现最后一行被文字糊住。
            constexpr float kMatBodyTopPad = 16.0f;
            constexpr float kMatRowH = 22.0f;
            constexpr float kMatFootGap = 6.0f;
            constexpr float kMatFootH = 16.0f;
            constexpr float kMatBottomPad = 16.0f;
            const float matBodyH = kMatBodyTopPad + kMatRowH * static_cast<float>(rows) +
                                   kMatFootGap + kMatFootH;
            const float matH = 44.0f + matBodyH + kMatBottomPad;
            const Rect mat{rc.min.x, ry, rc.max.x, ry + matH};
            Rect matBody = Card(cdraw, mat, "章节 × V 阶段", "grid", false, false);
            const float labelW = 40.0f;
            const float cell = (matBody.width() - labelW) / 7.0f;
            for (int v = 0; v < 7; ++v) {
                const std::string head = "V" + std::to_string(v + 1);
                cdraw->AddText(FontBoldAt(9.5f), 9.5f,
                              ImVec2(matBody.min.x + labelW + cell * v + 2.0f, matBody.min.y),
                              ColorTextMuted(), head.data(), head.data() + head.size());
            }
            float my = matBody.min.y + kMatBodyTopPad;
            for (int i = 0; i < rows; ++i) {
                const pages::BookChapterView& ch = book.chapters[static_cast<std::size_t>(i)];
                const std::string ord = "第" + std::to_string(ch.ord);
                cdraw->AddText(MonoAt(9.5f), 9.5f, ImVec2(matBody.min.x, my + 4.0f),
                              ColorTextSecondary(), ord.data(), ord.data() + ord.size());
                for (int v = 0; v < 7; ++v) {
                    // 下标与 chapters 对齐；长度不足（快照还没回来）时按「没跑过」处理。
                    const int count =
                        i < static_cast<int>(book.chapterVStages.size())
                            ? book.chapterVStages[static_cast<std::size_t>(i)]
                                  [static_cast<std::size_t>(v)]
                            : 0;
                    const Rect cell0{matBody.min.x + labelW + cell * v + 1.5f, my,
                                     matBody.min.x + labelW + cell * (v + 1) - 1.5f, my + 17.0f};
                    DrawRoundRect(cdraw, cell0.min, cell0.max, 3.0f,
                                  count > 0 ? ColorOf(theme::CurrentDerived()
                                                          .ganttCell[static_cast<int>(theme::Tone::Ok)])
                                            : ColorFillMuted());
                    if (count > 0) {
                        // 居中用实测宽度，不写死字符数偏移 —— 数字会从 1 涨到两位数，
                        // 固定偏移会让两位数整体偏右。
                        const std::string n = std::to_string(count);
                        ImFont* nf = MonoAt(9.0f);
                        const float nw = nf->CalcTextSizeA(9.0f, 1e9f, 0.0f, n.data(),
                                                           n.data() + n.size())
                                             .x;
                        cdraw->AddText(nf, 9.0f,
                                      ImVec2(cell0.min.x + (cell0.max.x - cell0.min.x - nw) * 0.5f,
                                             cell0.min.y + 4.0f),
                                      ColorText(), n.data(), n.data() + n.size());
                    }
                }
                my += kMatRowH;
            }
            // 脚注位置由同一组常量推出来，不是 `max.y - 12` 之类的手写锚点。
            const float footY = my + kMatFootGap; // my 此时已是最后一行的底
            DrawTextClipped(cdraw, FontAt(10.5f), 10.5f,
                            ImVec2(matBody.min.x, footY), matBody.width(), ColorTextMuted(),
                            "格内数字 = 该章在该阶段的产物条数（ListStageArtifacts）。");
            // 推进 ry —— ScrollRegion 的可滚高度靠最后一次上报的内容底算出来，
            // 忘了推就等于这张卡「不在内容里」，滚到底也拍不到（与「有内容但没被拍到」同族）。
            ry = mat.max.y + kGap;
        }
        // 把累加出来的内容高度报给 ScrollRegion —— 没有这一行，右栏就**滚不动**
        // （ScrollMaxY 恒为 0），四张卡里视口以下的三张永远够不着。上面「溢出交给
        // ScrollRegion 滚动」这句话指的就是这一行。
        rightScroll.setContentHeight(ry - rc.min.y);
        rightBottomUsed = ry; // 供调用方自报页面内容高度用（绝对屏幕 y）
    }
    return rightBottomUsed;
}

} // namespace overview
} // namespace shine::pages
