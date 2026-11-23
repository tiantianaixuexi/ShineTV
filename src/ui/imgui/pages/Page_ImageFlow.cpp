// shine::pages —— P5.5 出图（imageflow）—— 满幅画布 + FlowCanvas
//
// 节点图的**内容**（副行 / 状态）构造在 FlowGraph.cpp：它同时供出图与出片两页使用，
// 放一份在页面里就是第二份节点定义。这里只负责画布、右侧面板与工具条。
#include "ui/imgui/pages/BookData.h"

#include "ui/imgui/pages/FlowGraph.h"
#include "ui/imgui/pages/PageCommon.h"

#include "comfy/ComfySession.h"
#include "core/Settings.h"
#include "ui/imgui/kit/Fmt.h"
#include "ui/imgui/kit/Hash.h"
#include "ui/imgui/kit/Image.h"  // 结果页签那张成图（真图优先，见那一分支的注释）
#include "ui/imgui/kit/Overlays.h"
#include "ui/imgui/kit/Scroll.h"
#include "ui/imgui/kit/ScrollList.h"
#include "ui/imgui/kit/Text.h"
#include "ui/imgui/pages/MediaViewer.h"  // 点成图 → 查看器（同章各镜的画廊）

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace shine::pages {

using namespace shine::kit;

// ---- 显示缓存（第二十轮波 3）：指纹没变就整帧复用 ----
// 覆盖：镜数（标题/候选行）、选中镜的章节/场景/Canon/Prompt/生成参数字段、
// 场链接标题。「出图状态」随图片异步加载翻转，不进指纹 —— Draw 每帧原地改值。
void ImageFlowPage::RefreshTexts() {
    const BookState& s = Book();
    const pages::BookSideView& bs = pages::BookSide();
    std::uint64_t key = HashMix(0, bs.shots.size());
    key = HashMix(key, static_cast<std::uint64_t>(s.selectedShot));
    const BookChapter* chapter = s.chapter();
    if (chapter != nullptr) {
        key = HashMix(key, static_cast<std::uint64_t>(chapter->ord));
        key = HashMix(key, Fnv1a(chapter->title));
    }
    const BookShot* shot = s.shot();
    if (shot != nullptr) {
        key = HashMix(key, static_cast<std::uint64_t>(shot->sceneOrd));
        key = HashMix(key, Fnv1a(shot->canonStatus));
        key = HashMix(key, Fnv1a(shot->imagePrompt));
        key = HashMix(key, Fnv1a(shot->imageModel));
        key = HashMix(key, static_cast<std::uint64_t>(shot->imageWidth));
        key = HashMix(key, static_cast<std::uint64_t>(shot->imageHeight));
        key = HashMix(key, static_cast<std::uint64_t>(shot->imageSteps));
    }
    for (const BookSceneLink& link : s.sceneLinks) {
        key = HashMix(key, static_cast<std::uint64_t>(link.ord));
        key = HashMix(key, Fnv1a(link.title));
    }
    if (key == textKey_) {
        return;
    }
    textKey_ = key;

    barTitle_ = kit::Fmt("出图流程 · 本章 {} 镜", bs.shots.size());
    candidatesLine_ = kit::Fmt("本章 {} 个镜 · 候选来源 shots 表", bs.shots.size());

    // 结果页签的 KV（行序即显示序；「出图状态」由 Draw 每帧原地刷新）。
    resultKv_.clear();
    std::string chapterText = kDash;
    if (chapter != nullptr) {
        chapterText = kit::Fmt("第 {} 章", chapter->ord);
        if (!chapter->title.empty()) {
            chapterText += " · " + chapter->title;
        }
    }
    std::string sceneText = kDash;
    if (shot != nullptr && shot->sceneOrd > 0) {
        sceneText = kit::Fmt("第 {} 场", shot->sceneOrd);
        for (const BookSceneLink& link : s.sceneLinks) {
            if (link.ord == shot->sceneOrd && !link.title.empty()) {
                sceneText += " · " + link.title;
                break;
            }
        }
    }
    resultKv_.push_back({"所属章节", std::move(chapterText)});
    resultKv_.push_back({"场景", std::move(sceneText)});
    resultKv_.push_back({"出图状态", std::string()});
    resultKv_.push_back({"设定状态",
                         shot != nullptr && !shot->canonStatus.empty() ? shot->canonStatus
                                                                       : std::string(kDash)});
    if (shot != nullptr && !shot->imagePrompt.empty()) {
        // 设计稿第 4 行的标签就是 `Prompt`（英文原文），值这里放**真提示词**
        // —— 设计稿那个值是 prompt 的版本号，库里没有这一列。
        resultKv_.push_back({"Prompt", shot->imagePrompt});
    }
    if (shot != nullptr) {
        // 单位 0 / 空串一律省略该分量，不去凑一个 `0 步`。
        std::string params;
        const auto addParamPart = [&params](const std::string& part) {
            if (part.empty()) {
                return;
            }
            if (!params.empty()) {
                params += " · ";
            }
            params += part;
        };
        addParamPart(shot->imageModel);
        if (shot->imageWidth > 0 && shot->imageHeight > 0) {
            addParamPart(kit::Fmt("{}×{}", shot->imageWidth, shot->imageHeight));
        }
        if (shot->imageSteps > 0) {
            addParamPart(kit::Fmt("{} 步", shot->imageSteps));
        }
        if (!params.empty()) {
            resultKv_.push_back({"生成参数", std::move(params)});
        }
    }
}

void ImageFlowPage::Draw(Rect area, ImDrawList* draw) {
    // 宽窗并排，窄窗将任务面板接在画布下方，页签内容共用同一份实现。
    constexpr float panelW = 348.0f;
    const bool narrow = area.width() < kFlowStackWidth;

    // 画布：宽窗让出右侧 348+16（画布边界已经排除了面板，所以 FlowCanvas 的
    // fitInset 传 0 —— 再传 380 会重复扣一次宽度，把可用宽压到接近 0，缩放退化成
    // 看不清的一团）；窄窗铺满可见主列，高度 = **可视高** − 工具条那 82 ——
    // fit 用的是真看得到的高度。旧窄窗按 500 高 fit 而视口只有 ~270，节点被
    // 居中到首屏以下（「首屏图节点在下边」就是这么来的）。
    const float canvasTop = area.min.y + 82.0f;
    float canvasBottom = area.max.y;
    if (narrow) {
        const float viewportH = pages::WorkspaceViewportHeight();
        canvasBottom =
            canvasTop + std::max(120.0f, (viewportH > 0.0f ? viewportH : area.height()) - 82.0f);
    }
    const float canvasRight = narrow ? area.max.x : area.max.x - panelW - 32.0f;
    const Rect canvas{ImVec2(area.min.x, canvasTop), ImVec2(canvasRight, canvasBottom)};

    // 任务面板：宽窗浮动在右（348 内缩 16，r14）；窄窗纵向接在画布下方（全宽
    // 内缩 16），通过**工作区主滚动**到达 —— 内容高由 SetPageContentHeight 真实上报。
    const float panelHeight = folded_ ? 48.0f
                                      : (narrow ? 560.0f : std::min(560.0f, area.height() - 32.0f));
    const float panelTop = narrow ? (canvas.max.y + 16.0f) : (area.min.y + 16.0f);
    const Rect panel{narrow ? (area.min.x + 16.0f) : (area.max.x - panelW - 16.0f), panelTop,
                     area.max.x - 16.0f, panelTop + panelHeight};
    // 内容高真实上报：窄窗 = 画布 + 面板（+16 底边距）的堆叠高；宽窗 = 布局区高
    // （与旧「页面不报 → Shell 回退 view.height()」的取值相同）。
    pages::SetPageContentHeight(narrow ? (panel.max.y + 16.0f - area.min.y) : area.height());

    // 窄窗内容比布局区高（面板堆在画布下面），点阵背景要铺到真实内容底 ——
    // 只铺到 area.max.y 的话，面板以下会露出 child 底色、断成两种背景。
    const ImVec2 bgMax = narrow ? ImVec2(area.max.x, panel.max.y + 16.0f) : area.max;
    DrawRoundRect(draw, area.min, bgMax, 0.0f, ColorFillMuted());
    // 点阵背景：radial-gradient(circle at 1px 1px, line-normal 1px, transparent 0) 0 0 / 22px 22px
    DrawDotGrid(draw, area.min, bgMax, 22.0f, ColorLineNormal());

    // 数据指纹变了就重建 —— 快照是异步回投的，只判 empty 会把图冻在最空的那一刻。
    if (flowNodes_.empty() || flowDataKey_ != FlowDataKey()) {
        BuildGraph();
    }
    RefreshTexts();
    // fit 触发：① 建图/重建后（ImageFlowNeedsFit）；② 窄窗下画布**尺寸**变了
    //（含宽→窄切换后沿用宽窗 fit、节点被顶出首屏的那一种）。
    // ⚠️ 只比尺寸、不比位置：画布位置含工作区滚动偏移，滚动时每帧都变 —— 拿位置
    //    当判据会把用户拖好的视图每帧冲掉。宽窗**不加**这条：并列布局与
    //    1600×960 取证采样保持「只在建图时 fit」的原行为。
    static ImVec2 lastFitSize{0.0f, 0.0f};
    static bool wasNarrow = narrow;
    const ImVec2 fitSize{canvas.width(), canvas.height()};
    if (ImageFlowNeedsFit() ||
        narrow != wasNarrow || (narrow && (fitSize.x != lastFitSize.x || fitSize.y != lastFitSize.y))) {
        FlowFit(flowNodes_, canvas, 0.0f, flowView_);
        ImageFlowNeedsFit() = false;
        lastFitSize = fitSize;
    }
    wasNarrow = narrow;
    FlowCanvas(draw, canvas, flowNodes_, flowLinks_, flowView_, flowSelected_, 0.0f, narrow);

    // 工具条：宽窗宽 = 画布宽 − 16（右端与画布右界齐）；窄窗铺满（左右各留 16）。
    const float barW = narrow ? (area.width() - 32.0f) : (canvas.width() - 16.0f);
    const Rect bar = RectAt(area.min.x + 16.0f, area.min.y + 16.0f, barW, 52.0f);
    DrawRoundRect(draw, bar.min, bar.max, 10.0f, ColorSurface(), ColorLineNormal(), 1.0f);
    DrawIcon(draw, "image", ImVec2(bar.min.x + 12.0f, bar.center().y - 9.0f), 18.0f, ColorAccent());
    ButtonSpec smallPrimary;
    smallPrimary.variant = ButtonVariant::Primary;
    smallPrimary.size = ButtonSize::Small;
    const char* submit = "批量出图";
    const float submitW = ButtonWidth(ButtonSize::Small, 0.0f,
                                       kit::TextWidth(submit, TextStyle{.size = theme::font::kTiny,
                                                                          .bold = true}));
    TextClipped(draw, ImVec2(bar.min.x + 38.0f, bar.center().y - 8.0f),
                bar.width() - 62.0f - submitW, barTitle_,
                TextStyle{.size = theme::font::kPageTitle, .bold = true, .color = ColorText()});
    // ⚠️ 早先这里 `Button(...)` 的**返回值直接丢弃** —— 画了个 primary 主按钮，点了
    //    什么也不发生。同一页 `panelTab_ == 1` 分支里明明写着「批量提交未接」。
    //    改成点了给 toast 把原因说清楚：面板是**可折叠**的（folded_），禁用了按钮
    //    用户就只能看到「按不动」，而按不动的原因写在可能被折起来的面板里。
    if (Button(draw, RectAt(bar.max.x - submitW - 12.0f, bar.center().y - 12.0f, submitW, 24.0f), submit,
               smallPrimary, "if-submit")) {
        WorkspaceToast("批量提交未接 · 业务层没有替前端提交 V4 出图的接口");
    }

    // 面板矩形（panelHeight / panelTop / panel）在文件开头的布局段算好：
    // 宽窗浮动在右 348px、窄窗纵向接在画布下方。
    DrawShadowed(draw, panel.min, panel.max, 14.0f, GlassColor(), ColorLineNormal(), 1.0f, theme::ShadowTier::Overlay);
    DrawIcon(draw, "link", ImVec2(panel.min.x + 16.0f, panel.min.y + 16.0f), 15.0f, ColorAccent());
    Text(draw, ImVec2(panel.min.x + 40.0f, panel.min.y + 16.0f), "出图任务",
         TextStyle{.size = theme::font::kTitle, .bold = true, .color = ColorText()});
    // ⚠️ 早先这里是 `Tag(..., "运行中", theme::Tone::Accent, ...)` —— 文本与色调**都写死**。
    //    而这一行画在 `folded_` 提前 return **之前**、页签分发**之前**，上下文 30 行内没有
    //    任何队列或 Runner 读数。后果：Comfy 队列是空的、甚至**根本没绑工程**时，
    //    面板照样在断言「现在正在出图」。面板头是这一屏最容易被扫到的位置，
    //    在这里放一个假的进行时状态 = 整屏的第一印象就是假的。
    //
    // 真值源：FlowGraph.cpp 的 Comfy 队列读数（`ComfyRunningForBadge()`，口径同
    // 节点图用的 `ComfyHasRunning()`）。**没在跑就不画这个 Tag** —— 不画假绿，也不用
    // 「空闲」再占一个 tag 槽：面板头右侧是折叠按钮，槽位窄，「空闲」两个字
    // 和一个 20px 的 tag 挤在那儿比空着更抢眼，而且「空闲」在折叠态下会被读成
    // 「折叠起来了所以空闲」，反而引入第二层歧义。空着就是空着。
    if (ComfyRunningForBadge()) {
        const float headTagW = TagWidth("运行中", false, true);
        Tag(draw, RectAt(panel.max.x - 60.0f - headTagW, panel.min.y + 13.0f, headTagW, 20.0f),
            "运行中", theme::Tone::Accent, false, true);
    }
    if (IconButton(draw, RectAt(panel.max.x - 48.0f, panel.min.y + 12.0f, 24.0f, 24.0f), "chevdown",
                   false, false, "if-fold")) {
        folded_ = !folded_;
    }
    if (folded_) {
        return;
    }

    // 页签表恒定：提 static（第二十轮波 3）。值走栈缓冲。
    static const std::string kIfTabValues[4] = {"0", "1", "2", "3"};
    static const std::vector<SegmentOption> tabs{{kIfTabValues[0], "绑定"},
                                                 {kIfTabValues[1], "批量出图"},
                                                 {kIfTabValues[2], "图评审"},
                                                 {kIfTabValues[3], "结果"}};
    char valueBuf[4];
    std::snprintf(valueBuf, sizeof(valueBuf), "%d", panelTab_);
    const std::string_view value = valueBuf;
    const std::string_view picked =
        Segmented(draw,
                  Rect{panel.min.x + 16.0f, panel.min.y + 52.0f, panel.max.x - 16.0f, panel.min.y + 84.0f},
                  tabs, value, "if-tabs");
    if (!picked.empty()) {
        panelTab_ = std::atoi(std::string(picked).c_str());
    }

    const Rect body{panel.min.x + 16.0f, panel.min.y + 94.0f, panel.max.x - 16.0f, panel.max.y - 56.0f};
    if (panelTab_ == 0) {
        // ---- 绑定：Comfy 节点绑定的真状态 ----
        //
        // ⚠️ 早先这里是两行**编出来的**绑定关系：
        //   `{"S012 · 转身", "prompt_v3"} → {"Comfy / ksampler", "workflow.json"}`
        // 镜号、文件名全是设计稿的 mock，而且末尾那句 `缺少 KSampler.seed` 是一个
        // **具体的断言** —— 它声称某个参数字段缺失，而这与打开的是哪本书无关。
        //
        // 真值源：Comfy 会话连没连上（`ComfySession::LastError()`）+ 队列里有没有
        // 任务。连都没连的时候显示什么都能算成「绑定失败」，所以**先说连接状态**，
        // 有连接才列队列里的真实任务；两样都没有就写明为什么没有。
        const comfy::ComfySession& session = comfy::ComfySession::Instance();
        const bool configured = !Settings().comfyBaseUrl.empty();
        const std::vector<comfy::QueueModel::Row> rows = session.Queue().Snapshot();
        if (!configured) {
            Empty(draw, body, "link", "Comfy 未配置",
                  "设置里填 Comfy 地址后这里才有绑定信息。绑定关系来自 Comfy 会话，"
                  "本地编不出来。");
        } else if (!session.LastError().empty()) {
            Empty(draw, body, "link", "Comfy 未连接", session.LastError());
        } else if (rows.empty()) {
            // ⚠️ 「Comfy 已连接，但队列是空的」这句**只有 State()==Connected 才成立**。
            //    早先只判了 `LastError().empty()` —— 而「还没连上 / 正在连接」的
            //    LastError 同样为空，面板照样断言「已连接」：连接状态是编的。
            //    确认 Connected 才用该措辞；否则照实说状态（标签走 ComfyTypes 的
            //    ConnectionStateLabel，与顶栏同一份口径），不冒认连接。
            if (session.State() == comfy::ConnectionState::Connected) {
                Empty(draw, body, "link", "队列里没有任务",
                      "Comfy 已连接，但队列是空的。绑定关系是提交时建立的，"
                      "这里不编 KSampler / workflow 的对应关系。");
            } else {
                const std::string notConfirmed =
                    std::string("Comfy 还没有确认连接（") +
                    comfy::ConnectionStateLabel(session.State()) +
                    "），这里只显示本地队列快照。绑定关系是提交时建立的，"
                    "这里不编 KSampler / workflow 的对应关系。";
                Empty(draw, body, "link", "队列里没有任务", notConfirmed);
            }
        } else {
            // ⚠️ 原来这里是 `if (y + 24.0f > body.max.y) { break; }`：队列有多少行就只画
            //    看得下的那几行，剩下的**无声消失**，界面上看不出还有第 N+1 个任务 ——
            //    列表被截断且不提示 = 界面在骗人（队列一忙、面板一矮就复现）。
            //    R5：收进 kit::ScrollList —— 画全部行的语义不变，外加 clipper
            //    （屏外行不提交）与内容高度自动上报。
            ScrollList bindScroll("img-bind-queue", body, static_cast<int>(rows.size()), 24.0f, 24.0f);
            if (bindScroll) {
                // ⚠️ 内容画在 child 自己的 list 上，裁剪才有效（见 Scroll.h）。
                ImDrawList* bdraw = bindScroll.drawList();
                const Rect inner = bindScroll.content();
                for (int i = bindScroll.firstRow(); i < bindScroll.lastRow(); ++i) {
                    const comfy::QueueModel::Row& row = rows[static_cast<std::size_t>(i)];
                    const float y = bindScroll.RowRect(i).min.y;
                    const std::string label = row.label.empty() ? row.promptId : row.label;
                    kit::TextClipped(bdraw, ImVec2(inner.min.x, y), 170.0f, label,
                                     TextStyle{.size = theme::font::kHint,
                                               .color = ColorTextSecondary()},
                                     true);
                    // R3 收编：裸 AddText → kit::Text。
                    // ⚠️ `"→", "→" + 3` 的长度 3 是 U+2192 的 UTF-8 字节数（3 字节），
                    //    照原样搬（`std::string_view("→", 3)`），不改成 strlen。
                    kit::Text(bdraw, ImVec2(inner.min.x + 180.0f, y),
                              std::string_view("→", 3),
                              TextStyle{.size = theme::font::kHint, .color = ColorTextMuted()});
                    kit::TextClipped(bdraw, ImVec2(inner.min.x + 210.0f, y), inner.width() - 210.0f,
                                     row.state == comfy::TaskState::Running ? "运行中" : "排队中",
                                     TextStyle{.size = theme::font::kHint, .color = ColorText()},
                                     true);
                }
            }
        }
    } else if (panelTab_ == 1) {
        // ---- 批量出图：候选镜来自**本章真实镜表**，提交动作不接线 ----
        //
        // ⚠️ 这个分支以前**根本不存在**：页签有 4 个（绑定/批量出图/图评审/结果），
        //    分发却只有 `if 0 / else if 2 / else` ⇒ 「批量出图」落进 else，看到的是
        //    **「结果」页的尺寸/步数/种子/耗时**。症状是「点了页签没反应 / 串页」，
        //    而那一瞬间的截图里两个页签完全一样，肉眼分辨不出是串页还是没做。
        //
        // 候选镜用 `BookSide().shots`（当前选中章，与侧栏树/故事板同一份），状态取
        // 镜自己的 canon_status。**不编批次号、不编张数** —— 批量提交要走 Comfy
        // 写接口，业务层没有替前端提交 V4 的只读投影，所以这里只列得出候选，提交
        // 一行明说没接，而不是放一个点了什么都不做的按钮。
        const BookSideView& bs = BookSide();
        if (!bs.bound || !bs.error.empty() || bs.shots.empty()) {
            Empty(draw, body, "layers",
                  bs.bound ? "本章还没有镜" : "还没打开工程",
                  bs.bound ? "T1–T17 跑出分镜后这里才有可批量出图的候选。"
                           : "批量出图的候选来自 shots 表，先打开一个跑过初始化链的工程。");
        } else {
            kit::TextClipped(draw, ImVec2(body.min.x, body.min.y), body.width(), candidatesLine_,
                             TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()},
                             true);
            DrawRoundRect(draw, ImVec2(body.min.x, body.min.y + 22.0f),
                          ImVec2(body.max.x, body.min.y + 58.0f), 6.0f,
                          ColorOf(theme::CurrentDerived()
                                      .tagBg[static_cast<std::size_t>(theme::Tone::Warn)]));
            const char* noSubmit = "当前只显示候选。批量出图服务未接入。";
            // R3 收编：裸 AddText → kit::Text。
            kit::TextClipped(draw, ImVec2(body.min.x + 10.0f, body.min.y + 34.0f), body.width() - 20.0f,
                             noSubmit,
                      TextStyle{.size = theme::font::kTiny,
                                .color = ColorOf(theme::Current().statusWarn)});
            // ⚠️ 原来这里是 `if (y + 26.0f > body.max.y) { break; }` —— 一章几十个镜时只画
            //    看得下的一半，候选镜**无声消失**，界面上看不出「本章还有 N 个候选」。
            //    列表被截断且不提示 = 界面在骗人。R5：收进 ScrollList（画全部候选的
            //    语义不变 + clipper + 内容高度自动）。
            //    「本章 N 个镜」说明与「批量提交未接」的警告条留在滚动区**外面**。
            ScrollList candScroll("img-batch-candidates",
                                  Rect{body.min.x, body.min.y + 70.0f, body.max.x, body.max.y},
                                  static_cast<int>(bs.shots.size()), 30.0f, 30.0f);
            if (candScroll) {
                // ⚠️ 内容画在 child 自己的 list 上，裁剪才有效（见 Scroll.h）。
                ImDrawList* cdraw = candScroll.drawList();
                const Rect inner = candScroll.content();
                for (int i = candScroll.firstRow(); i < candScroll.lastRow(); ++i) {
                    const BookShotView& shot = bs.shots[static_cast<std::size_t>(i)];
                    const float y = candScroll.RowRect(i).min.y;
                    const std::string code = ShotCode(shot.ord);
                    // R3 收编：裸 AddText → kit::Text。
                    kit::Text(cdraw, ImVec2(inner.min.x, y), code,
                              TextStyle{.size = theme::font::kHint, .bold = true, .color = ColorAccent()});
                    const std::string action = shot.action.empty() ? std::string(kDash) : shot.action;
                    const std::string canon =
                        shot.canonStatus.empty() ? std::string(kDash) : shot.canonStatus;
                    const float tagW = TagWidth(canon, false, true);
                    kit::TextClipped(cdraw, ImVec2(inner.min.x + 52.0f, y),
                                     inner.width() - tagW - 64.0f, action,
                                     TextStyle{.size = theme::font::kBody, .color = ColorText()});
                    Tag(cdraw, RectAt(inner.max.x - tagW, y - 1.0f, tagW, 20.0f), canon,
                        theme::Tone::Idle, false, true);
                }
            }
        }
    } else if (panelTab_ == 2) {
        // ---- 图评审 ----
        //
        // ⚠️ 这里原来是一个 5 行的假评审表，三处同时是假的：
        //   ① `Checkbox` 的返回值被丢弃 ⇒ 点不动（注册了 InvisibleButton 却没人读）；
        //   ② `on` 传字面量 `i < 3` ⇒ 勾选态**每帧重建**，永远是「前 3 个勾上」；
        //   ③ 五行的 label 都是同一个字符串「构图稳定」⇒ 标签叠印成一坨。
        // 「勾上 3 个、5 个同名、点不动」的三件套在静息态截图上看着还挺像个评审页 ——
        // 所以是取证对照才发现的，不是肉眼。
        //
        // 改成 honest：业务层没有出图评审结果的只读投影（`visual_assets.status` 只到
        // 生产状态，没有 per-check 的评分），所以**不编 5 条评审项**。有真实视觉资产的
        // 实体就列出来（名字 + 生产状态），一条都没有就说明为什么没有。
        const BookSideView& rv = BookSide();
        Art(draw, Rect{body.min.x, body.min.y, body.max.x, body.min.y + 150.0f}, 6, true);
        // ⚠️ 上面那块是 `Art()` 的**程序化占位画**（调色板只按 `seed % 12` 取、形状是固定
        //    几何，与 novel.db / Comfy 任何字段都无关），早先这里**只有画、没有标注**。
        //    `Shell.cpp` 的同一张占位画早就标了「示意插画 · 非本工程出图结果」，本文件
        //    漏了 —— 漏标的那一张会被当成该工程的出图结果读。
        //    标在插画**下方**（与 Shell.cpp 同一位置口径），字号 11.5 / muted。
        kit::TextClipped(draw, ImVec2(body.min.x, body.min.y + 154.0f), body.width(), kArtNotice,
                         TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()}, true);
        // 插画 150 高之后留够落笔空间：Art 的实际下沿比 rect 略高一点，早先 +162 的
        // 间距会让第一行名字压在插画下沿上（截图里能直接看出来）。
        // 现在多了一行 11.5 的标注（154 → 约 170），所以清单起点从 +180 起，标注不会
        // 被清单压住。
        const float listTop = body.min.y + 180.0f;
        // 先把「真的列得出来」的实体收齐：有资产才进清单，否则空态才说得清。
        std::vector<const BookAssetView*> listed;
        for (const BookAssetView& asset : rv.assets) {
            if (asset.hasAsset) {
                listed.push_back(&asset);
            }
        }
        if (listed.empty()) {
            Empty(draw, Rect{body.min.x, listTop, body.max.x, body.max.y}, "target",
                  "没有可评审的出图", rv.bound
                                     ? "这一类实体还没有视觉资产。先在资产工作区出图，再回来评审。"
                                     : "还没打开工程。评审清单来自 entities × visual_assets。");
        } else {
            // ⚠️ 原来这里是 `if (rowY + 22.0f > body.max.y) { break; }` —— 清单比面板高时
            //    多出来的资产**无声消失**，界面上看不出「还有 N 项没列」：列表被截断且不
            //    提示 = 界面在骗人。R5：收进 ScrollList（画全部 + clipper + 内容高度自动；
            //    行距 24，跳过的实体不在中间留空洞的语义由 listed 的下标连续性保证）。
            ScrollList reviewScroll("img-review-assets",
                                    Rect{body.min.x, listTop, body.max.x, body.max.y},
                                    static_cast<int>(listed.size()), 24.0f, 24.0f);
            if (reviewScroll) {
                // ⚠️ 内容画在 child 自己的 list 上，裁剪才有效（见 Scroll.h）。
                ImDrawList* rdraw = reviewScroll.drawList();
                const Rect inner = reviewScroll.content();
                for (int i = reviewScroll.firstRow(); i < reviewScroll.lastRow(); ++i) {
                    const BookAssetView* asset = listed[static_cast<std::size_t>(i)];
                    const float ly = reviewScroll.RowRect(i).min.y;
                    const std::string name = asset->name.empty() ? std::string(kDash) : asset->name;
                    const std::string status =
                        asset->statusLabel.empty() ? std::string(kDash) : asset->statusLabel;
                    kit::TextClipped(rdraw, ImVec2(inner.min.x, ly), inner.width() - 120.0f, name,
                                     TextStyle{.size = theme::font::kHint, .color = ColorText()},
                                     true);
                    const float tagW = TagWidth(status, false, true);
                    Tag(rdraw, RectAt(inner.max.x - tagW, ly - 1.0f, tagW, 20.0f), status,
                        asset->tone, false, true);
                }
            }
            kit::TextClipped(draw, ImVec2(body.min.x, body.max.y - 16.0f), body.width(),
                             "逐项评分（构图/光线/服饰/色彩）没有只读投影，这里只列生产状态。",
                             TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()},
                             true);
        }
    } else {
        // ---- 结果：**当前选中镜**的成图（设计稿 ImageFlow.jsx:133-188 的 ResultPanel）----
        //
        // ⚠️ 这一块的历史（两版错的都记着，别再走回去）：
        //   第一版是 `KeyValues({{"尺寸","1024x576"},{"步数","28"},…})` —— 四个编出来的
        //   生成参数；第二版换成了 `Art()` 占位画 + 「示意插画」标注 + 一张**资产**清单，
        //   理由写的是「业务层没有出图结果的只读投影（visual_assets 只有 status）」。
        //   那个理由**只对 `visual_assets` 成立**：出图结果在**另一张表**
        //   `generated_images` 里（`rel_path` + `source_kind='shot'` + `source_id=shots.id`，
        //   业务层提交单镜出图时就是这么写的，`idx_gimg_source` 索引也在）——
        //   与分镜页 `.tthumb` 是**同一处、同一个错归因**（那边第七轮已订正，这一处漏了）。
        //   页签名就叫「结果」，占位插画摆在这里最容易被读成本工程出出来的那张图。
        //
        // 这一版按设计稿的 ResultPanel 摆：成图（190 高、点开进查看器）+ 四行 KV。
        // 设计稿里的 `Prompt v7（重生成 +1）` 一行**不画** —— 生成参数没有投影到视图层
        // （`generated_images` 的 prompt/steps/model 三列没人读上来），按本仓的纪律
        // 「取不到的组就不出」，不编一个版本号；第 4 行换成 `shots.canon_status`（真字段）。
        // 设计稿那两行 `.dlist`（`S0xx.mp4` / 图评审）同样不画：库里**没有任何视频表**
        // （全仓 grep 不到 `generated_videos` / 剪辑产物），画一行「待出片」等于把一个
        // 永远不可能变成真的占位行钉在界面上；图评审就是左邻那个页签。
        const BookState& book = Book();
        const BookShot* shot = book.shot();
        if (shot == nullptr) {
            Empty(draw, body, "image", book.bound ? "本章还没有镜" : "还没打开工程",
                  book.bound ? "出图结果挂在镜上（generated_images 的 source_kind='shot'），"
                               "本章没有镜就没有结果可显示。"
                             : "结果来自 novel.db 的 shots × generated_images，先打开一个工程。");
        } else {
            const Rect hero{body.min.x, body.min.y, body.max.x, body.min.y + 190.0f};
            // `.art` 自带 `background: var(--fill-muted)` + `--r-sm`（ui.css:1074）——
            // 真图与占位都靠它兜底（`Art` 是纯几何绘制、不铺底）。
            DrawRoundRect(draw, hero.min, hero.max, 6.0f, ColorFillMuted());
            const ImageSlot frame = RequestImage(AssetImagePath(book.root, shot->imageRelPath));
            // 三条分支与分镜页 `.tthumb` **逐条对齐**（别合并）：
            //   ① 有真图 → 画它   ② 登记过但解不出 → `Art` + 「示意」标注   ③ 还没出图 → 诚实空态
            bool placeholder = false;
            if (frame.ready()) {
                DrawImage(draw, hero, frame, 6.0f, ImageFit::Cover);
            } else if (!shot->imageRelPath.empty()) {
                Art(draw, hero, shot->ord, /*cover=*/true);
                placeholder = true;
            } else {
                // 设计稿那一格（ImageFlow.jsx:158-163）：图标 + 「{状态} · 出图后在此显示」。
                static constexpr char kPendingHero[] = "待出图 · 出图后在此显示";
                // R3 收编：量宽与画（原先一个手写 `heroFont` + 一个 `AddText`）从同一份
                // TextStyle 出发，`CenterTextY` → `kit::CenterY`（同为 `cy − size*0.5`）。
                const TextStyle heroStyle{.size = theme::font::kLabel, .color = ColorTextMuted()};
                constexpr float kHeroIcon = 22.0f;
                constexpr float kHeroGap = 6.0f;
                const float heroLineH = 11.5f * 1.6f;
                const float heroTextW = kit::TextWidth(kPendingHero, heroStyle);
                const float heroStackH = kHeroIcon + kHeroGap + heroLineH;
                const float heroTop = hero.center().y - heroStackH * 0.5f;
                DrawIconCentered(draw, "image",
                                 ImVec2(hero.center().x, heroTop + kHeroIcon * 0.5f), kHeroIcon,
                                 ColorTextMuted());
                kit::Text(draw,
                          ImVec2(hero.center().x - heroTextW * 0.5f,
                                 kit::CenterY(heroTop + kHeroIcon + kHeroGap + heroLineH * 0.5f,
                                              heroStyle)),
                          std::string_view(kPendingHero, sizeof(kPendingHero) - 1), heroStyle);
            }
            // 「示意插画 · 非本工程出图结果」只在**画的真是占位画**时标 —— 真图上屏时
            // 照旧画它，等于把真图说成示意插画（成片条那一处同一条纪律）。
            if (placeholder) {
                kit::TextClipped(draw, ImVec2(body.min.x, hero.max.y + 4.0f), body.width(),
                                 kArtNotice,
                                 TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()},
                                 true);
            }
            // 点开大图：设计稿这里是 `cursor:zoom-in` + `setViewer({gallery: 同章各镜})`。
            // 落在哪一页由 `OpenMediaViewer` 的**第二个实参**决定（页序与镜序同序）——
            // 写错这一处的两种写法都编译得过，打开的是别的一页。
            if (HitTest(hero, "if-result-hero").clicked) {
                OpenMediaViewer(ShotViewerRequest(book.selectedShot), book.selectedShot);
            }
            // ---- KV：来自 RefreshTexts 缓存；「出图状态」由**图本身**三分、随异步
            //      加载翻转，所以每帧原地改值（SSO 赋值，不落堆，第二十轮波 3）。
            //      数值全部来自真字段：章序 + 章标题 / 场序 + 场标题（与侧栏树同源）/
            //      出图状态 / 设定状态；Prompt 与生成参数来自 `generated_images` 列。
            const char* shotState = frame.ready()                  ? "已出图"
                                    : shot->imageRelPath.empty()   ? "待出图"
                                                                   : "出图文件不可用";
            if (resultKv_.size() > 2) {
                resultKv_[2].value = shotState;
            }
            const float kvTop = hero.max.y + 16.0f;
            // 每行 20（`KeyValues` 的 rowHeight）+ 2 的行距，行数由数据决定 ⇒ 高度必须
            // 跟着行数走，写死 4 行会把后两行压到按钮底下。
            const float kvHeight = static_cast<float>(resultKv_.size()) * 22.0f;
            KeyValues(draw, Rect{body.min.x, kvTop, body.max.x, kvTop + kvHeight},
                      resultKv_.data(), resultKv_.size());
            // 设计稿那颗按钮（ImageFlow.jsx:186）。点了只发 toast：业务层没有替前端提交
            // **单镜**出图的接口 —— 与工具条上那颗「批量出图」同一条纪律（画一个点了
            // 什么都不发生的主按钮是本仓明令禁止的形态）。
            ButtonSpec rerunSpec;
            rerunSpec.variant = ButtonVariant::Secondary;
            rerunSpec.size = ButtonSize::Small;
            rerunSpec.icon = "refresh";
            const char* rerunLabel = "重跑本镜出图";
            const float rerunW =
                ButtonWidth(ButtonSize::Small, 13.0f,
                            kit::TextWidth(rerunLabel,
                                           TextStyle{.size = theme::font::kTiny, .bold = true}));
            const float rerunY = kvTop + kvHeight + 8.0f;
            if (Button(draw, RectAt(body.min.x, rerunY, rerunW, ButtonHeight(ButtonSize::Small)),
                       rerunLabel, rerunSpec, "if-rerun-shot")) {
                WorkspaceToast(std::string(ShotCode(shot->ord)) +
                               " 重跑本镜出图未接 · 业务层没有替前端提交单镜出图的接口");
            }
        }
    }


}

} // namespace shine::pages
