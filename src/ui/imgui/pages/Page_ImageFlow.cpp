// shine::pages —— P5.5 出图（imageflow）—— 满幅画布 + FlowCanvas
//
// 节点图的**内容**（副行 / 状态）构造在 FlowGraph.cpp：它同时供出图与出片两页使用，
// 放一份在页面里就是第二份节点定义。这里只负责画布、右侧面板与工具条。
#include "ui/imgui/pages/BookData.h"

#include "ui/imgui/pages/FlowGraph.h"
#include "ui/imgui/pages/PageCommon.h"

#include "comfy/ComfySession.h"
#include "core/Settings.h"
#include "ui/imgui/kit/Overlays.h"
#include "ui/imgui/kit/Scroll.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace shine::pages {

using namespace shine::kit;

void ImageFlowPage::Draw(Rect area, ImDrawList* draw) {
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorFillMuted());
    // 点阵背景：radial-gradient(circle at 1px 1px, line-normal 1px, transparent 0) 0 0 / 22px 22px
    DrawDotGrid(draw, area.min, area.max, 22.0f, ColorLineNormal());

    // 浮动面板占右侧 348+16，画布在它左边。画布边界已经排除了面板，
    // 所以 FlowCanvas 的 fitInset 传 0 —— 再传 380 会重复扣一次宽度，
    // 把可用宽压到接近 0，缩放退化成看不清的一团。
    const float panelW = 348.0f;
    const float canvasRight = area.max.x - panelW - 32.0f;
    const Rect canvas{area.min, ImVec2(std::max(canvasRight, area.min.x + 200.0f), area.max.y)};
    // 数据指纹变了就重建 —— 快照是异步回投的，只判 empty 会把图冻在最空的那一刻。
    if (flowNodes_.empty() || flowDataKey_ != FlowDataKey()) {
        BuildGraph();
    }
    if (ImageFlowNeedsFit()) {
        FlowFit(flowNodes_, canvas, 0.0f, flowView_);
        ImageFlowNeedsFit() = false;
    }
    FlowCanvas(draw, canvas, flowNodes_, flowLinks_, flowView_, flowSelected_, 0.0f);

    // 浮动工具条（左上，玻璃 → --glass 实色，r10，pad 8/12）
    const float barW = 520.0f;
    const Rect bar{area.min.x + 16.0f, area.min.y + 16.0f, area.min.x + 16.0f + barW,
                   area.min.y + 56.0f};
    DrawShadowed(draw, bar.min, bar.max, 10.0f, GlassColor(), ColorLineNormal(), 1.0f, theme::ShadowTier::Card);
    DrawIcon(draw, "image", ImVec2(bar.min.x + 12.0f, bar.center().y - 7.0f), 14.0f, ColorAccent());
    // ⚠️ 早先是 `const char* barTitle = "出图流程 · 分镜图_v3"` —— 「分镜图_v3」是
    //    **设计稿的 mock 字符串**，照抄等于把一个不存在的资产名写死在界面上
    //    （与「实体 · 林晚」同一类错误，见 DrawBreadcrumbs 的注释）。
    //    换成结构性标签：真有的东西是「本章有几个镜」。
    {
        const pages::BookSideView& bs = pages::BookSide();
        const std::string barTitle = "出图流程 · 本章 " + std::to_string(bs.shots.size()) + " 镜";
        draw->AddText(FontBoldAt(12.5f), 12.5f, ImVec2(bar.min.x + 32.0f, bar.center().y - 6.25f),
                      ColorText(), barTitle.data(), barTitle.data() + barTitle.size());
    }
    draw->AddLine(ImVec2(bar.min.x + 196.0f, bar.min.y + 8.0f),
                  ImVec2(bar.min.x + 196.0f, bar.max.y - 8.0f), ColorLineNormal(), 1.0f);
    ButtonSpec smallPrimary;
    smallPrimary.variant = ButtonVariant::Primary;
    smallPrimary.size = ButtonSize::Small;
    const char* submit = "批量出图";
    const float submitW = ButtonWidth(ButtonSize::Small, 0.0f, LabelWidth(FontBoldAt(12.0f), 12.0f, submit));
    // ⚠️ 早先这里 `Button(...)` 的**返回值直接丢弃** —— 画了个 primary 主按钮，点了
    //    什么也不发生。同一页 `panelTab_ == 1` 分支里明明写着「批量提交未接」。
    //    改成点了给 toast 把原因说清楚：面板是**可折叠**的（folded_），禁用了按钮
    //    用户就只能看到「按不动」，而按不动的原因写在可能被折起来的面板里。
    if (Button(draw, RectAt(bar.max.x - submitW - 12.0f, bar.center().y - 12.0f, submitW, 24.0f), submit,
               smallPrimary, "if-submit")) {
        WorkspaceToast("批量提交未接 · 业务层没有替前端提交 V4 出图的接口");
    }

    // 浮动面板（右 348px，内缩 16，r14）
    const float panelHeight = folded_ ? 48.0f : std::min(560.0f, area.height() - 32.0f);
    const Rect panel{area.max.x - panelW - 16.0f, area.min.y + 16.0f, area.max.x - 16.0f,
                     area.min.y + 16.0f + panelHeight};
    DrawShadowed(draw, panel.min, panel.max, 14.0f, GlassColor(), ColorLineNormal(), 1.0f, theme::ShadowTier::Overlay);
    DrawIcon(draw, "link", ImVec2(panel.min.x + 16.0f, panel.min.y + 16.0f), 15.0f, ColorAccent());
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

    const std::vector<SegmentOption> tabs{
        {"0", "绑定"}, {"1", "批量出图"}, {"2", "图评审"}, {"3", "结果"}};
    const std::string value = std::to_string(panelTab_);
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
            Empty(draw, body, "link", "队列里没有任务",
                  "Comfy 已连接，但队列是空的。绑定关系是提交时建立的，"
                  "这里不编 KSampler / workflow 的对应关系。");
        } else {
            // ⚠️ 原来这里是 `if (y + 24.0f > body.max.y) { break; }`：队列有多少行就只画
            //    看得下的那几行，剩下的**无声消失**，界面上看不出还有第 N+1 个任务 ——
            //    列表被截断且不提示 = 界面在骗人（队列一忙、面板一矮就复现）。
            //    改成画**全部**行；面板自己就是视口，超出由 ScrollRegion 滚。
            ScrollRegion bindScroll("img-bind-queue", body);
            if (bindScroll) {
                // ⚠️ 内容画在 child 自己的 list 上，裁剪才有效（见 Scroll.h）。
                ImDrawList* bdraw = bindScroll.drawList();
                const Rect inner = bindScroll.content();
                float y = inner.min.y;
                for (const comfy::QueueModel::Row& row : rows) {
                    const std::string label = row.label.empty() ? row.promptId : row.label;
                    DrawTextClipped(bdraw, FontAt(12.5f), 12.5f, ImVec2(inner.min.x, y), 170.0f,
                                    ColorTextSecondary(), label, true);
                    bdraw->AddText(FontAt(12.5f), 12.5f, ImVec2(inner.min.x + 180.0f, y),
                                   ColorTextMuted(), "→", "→" + 3);
                    DrawTextClipped(bdraw, FontAt(12.5f), 12.5f, ImVec2(inner.min.x + 210.0f, y),
                                    inner.width() - 210.0f, ColorText(),
                                    row.state == comfy::TaskState::Running ? "运行中" : "排队中",
                                    true);
                    y += 24.0f;
                }
                // 不调这一行等于没修：不报内容高，ImGui 侧 ContentSize 恒为 0 ⇒ 滚不动。
                bindScroll.setContentHeight(y - inner.min.y);
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
            DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(body.min.x, body.min.y),
                            body.width(), ColorTextMuted(),
                            ("本章 " + std::to_string(bs.shots.size()) + " 个镜 · 候选来源 shots 表").c_str(),
                            true);
            DrawRoundRect(draw, ImVec2(body.min.x, body.min.y + 22.0f),
                          ImVec2(body.max.x, body.min.y + 58.0f), 6.0f,
                          ColorOf(theme::CurrentDerived()
                                      .tagBg[static_cast<std::size_t>(theme::Tone::Warn)]));
            const char* noSubmit = "批量提交未接：业务层没有替前端提交 V4 出图的接口，这一页只列候选。";
            draw->AddText(FontAt(12.0f), 12.0f, ImVec2(body.min.x + 10.0f, body.min.y + 34.0f),
                          ColorOf(theme::Current().statusWarn), noSubmit,
                          noSubmit + std::strlen(noSubmit));
            // ⚠️ 原来这里是 `if (y + 26.0f > body.max.y) { break; }` —— 一章几十个镜时只画
            //    看得下的一半，候选镜**无声消失**，界面上看不出「本章还有 N 个候选」。
            //    列表被截断且不提示 = 界面在骗人。改成画**全部**候选。
            //    「本章 N 个镜」说明与「批量提交未接」的警告条留在滚动区**外面**。
            ScrollRegion candScroll("img-batch-candidates",
                                    Rect{body.min.x, body.min.y + 70.0f, body.max.x, body.max.y});
            if (candScroll) {
                // ⚠️ 内容画在 child 自己的 list 上，裁剪才有效（见 Scroll.h）。
                ImDrawList* cdraw = candScroll.drawList();
                const Rect inner = candScroll.content();
                float y = inner.min.y;
                for (const BookShotView& shot : bs.shots) {
                    const std::string code = ShotCode(shot.ord);
                    cdraw->AddText(FontBoldAt(12.5f), 12.5f, ImVec2(inner.min.x, y), ColorAccent(),
                                   code.data(), code.data() + code.size());
                    const std::string action = shot.action.empty() ? std::string(kDash) : shot.action;
                    DrawTextClipped(cdraw, FontAt(12.5f), 12.5f, ImVec2(inner.min.x + 52.0f, y),
                                    inner.width() - 180.0f, ColorText(), action, true);
                    const std::string canon =
                        shot.canonStatus.empty() ? std::string(kDash) : shot.canonStatus;
                    const float tagW = TagWidth(canon, false, true);
                    Tag(cdraw, RectAt(inner.max.x - tagW, y - 1.0f, tagW, 20.0f), canon,
                        theme::Tone::Idle, false, true);
                    y += 26.0f;
                }
                // 不调这一行等于没修：不报内容高，ImGui 侧 ContentSize 恒为 0 ⇒ 滚不动。
                candScroll.setContentHeight(y - inner.min.y);
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
        DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(body.min.x, body.min.y + 154.0f),
                        body.width(), ColorTextMuted(), kArtNotice, true);
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
            //    提示 = 界面在骗人。改成画**全部**，行本体自己滚；插画与页脚留在区外。
            ScrollRegion reviewScroll("img-review-assets",
                                      Rect{body.min.x, listTop, body.max.x, body.max.y});
            if (reviewScroll) {
                // ⚠️ 内容画在 child 自己的 list 上，裁剪才有效（见 Scroll.h）。
                ImDrawList* rdraw = reviewScroll.drawList();
                const Rect inner = reviewScroll.content();
                // y 依次累加（不再用全量下标 × 24）：跳过的实体不会在中间留空洞。
                float ly = inner.min.y;
                for (const BookAssetView* asset : listed) {
                    const std::string name = asset->name.empty() ? std::string(kDash) : asset->name;
                    const std::string status =
                        asset->statusLabel.empty() ? std::string(kDash) : asset->statusLabel;
                    DrawTextClipped(rdraw, FontAt(12.5f), 12.5f, ImVec2(inner.min.x, ly),
                                    inner.width() - 120.0f, ColorText(), name, true);
                    const float tagW = TagWidth(status, false, true);
                    Tag(rdraw, RectAt(inner.max.x - tagW, ly - 1.0f, tagW, 20.0f), status,
                        asset->tone, false, true);
                    ly += 24.0f;
                }
                // 不调这一行等于没修：不报内容高，ImGui 侧 ContentSize 恒为 0 ⇒ 滚不动。
                reviewScroll.setContentHeight(ly - inner.min.y);
            }
            DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(body.min.x, body.max.y - 16.0f),
                            body.width(), ColorTextMuted(),
                            "逐项评分（构图/光线/服饰/色彩）没有只读投影，这里只列生产状态。", true);
        }
    } else {
        // ---- 结果 ----
        //
        // ⚠️ 早先这里是
        //   `KeyValues({{"尺寸","1024x576"},{"步数","28"},{"种子","1289471"},{"耗时","12.4s"}})`
        //    四个**编出来的**生成参数。业务层没有出图结果的只读投影 —— visual_assets
        //    只有 status（生产状态），没有尺寸/步数/种子/耗时这些字段。所以这四个数字
        //    没有任何一个是真的，却长得极像一次真实的出图结果。
        //
        // 改成列 BookAssetView 里**真有的**字段：名字 / 生产状态 / 层数完成度 / 是否降级。
        // 一条视觉资产都没有时说清楚为什么没有，不拿 Art 占位图 + 假参数撑场面。
        const BookSideView& rv = BookSide();
        Art(draw, Rect{body.min.x, body.min.y, body.max.x, body.min.y + 150.0f}, 6, true);
        // ⚠️ 同样是 `Art()` 的程序化占位画，seed 还是**写死的 6**（每帧同一张）——
        //    早先这里只画不标。`Shell.cpp` 的同一张占位画早就标注了，这处漏了。
        //    这一页最容易骗人：页签名叫「结果」，读者会把占位插画当成出图结果本身。
        DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(body.min.x, body.min.y + 154.0f),
                        body.width(), ColorTextMuted(), kArtNotice, true);
        // 标注占了 154 → 约 170，清单从 +180 起，两者不重叠。
        const float listTop = body.min.y + 180.0f;
        std::vector<const BookAssetView*> listed;
        for (const BookAssetView& asset : rv.assets) {
            if (asset.hasAsset) {
                listed.push_back(&asset);
            }
        }
        if (listed.empty()) {
            Empty(draw, Rect{body.min.x, listTop, body.max.x, body.max.y}, "check",
                  "这一类实体还没有出图结果",
                  rv.bound ? "visual_assets 里还没有任何已产出的图像。业务层也没有出图结果的"
                             "只读投影，所以这里不列参数。"
                           : "还没打开工程。");
        } else {
            // ⚠️ 原来这里是 `if (rowY + 22.0f > body.max.y) { break; }` —— 出图一多，
            //    后面的结果行**无声消失**，界面上看不出「还有 N 张没列」：列表被截断且不
            //    提示 = 界面在骗人。改成画**全部**，行本体自己滚；插画留在滚动区外。
            ScrollRegion resultScroll("img-result-assets",
                                      Rect{body.min.x, listTop, body.max.x, body.max.y});
            if (resultScroll) {
                // ⚠️ 内容画在 child 自己的 list 上，裁剪才有效（见 Scroll.h）。
                ImDrawList* gdraw = resultScroll.drawList();
                const Rect inner = resultScroll.content();
                float ly = inner.min.y;
                for (const BookAssetView* asset : listed) {
                    const std::string name = asset->name.empty() ? std::string(kDash) : asset->name;
                    const std::string status =
                        asset->statusLabel.empty() ? std::string(kDash) : asset->statusLabel;
                    DrawTextClipped(gdraw, FontAt(12.5f), 12.5f, ImVec2(inner.min.x, ly),
                                    inner.width() - 200.0f, ColorText(), name, true);
                    // 层数完成度是真的（layers / layersDone 来自 visual_assets 的分层表）。
                    const std::string layers =
                        asset->layers > 0 ? ("分层 " + std::to_string(asset->layersDone) + "/" +
                                             std::to_string(asset->layers))
                                          : std::string(kDash);
                    DrawTextClipped(gdraw, FontAt(11.5f), 11.5f, ImVec2(inner.max.x - 190.0f, ly + 3.0f),
                                    90.0f, ColorTextMuted(), layers, true);
                    const std::string flag = asset->degraded ? "已降级" : "完整";
                    const float fw = TagWidth(flag, false, true);
                    Tag(gdraw, RectAt(inner.max.x - fw, ly - 1.0f, fw, 20.0f), flag,
                        asset->degraded ? theme::Tone::Warn : theme::Tone::Ok, false, true);
                    ly += 24.0f;
                }
                // 不调这一行等于没修：不报内容高，ImGui 侧 ContentSize 恒为 0 ⇒ 滚不动。
                resultScroll.setContentHeight(ly - inner.min.y);
            }
        }
    }

    // 画布工具（左下竖排）
    const float toolX = area.min.x + 16.0f;
    float toolY = area.max.y - 16.0f - 3.0f * 32.0f;
    for (const char* icon : {"plus", "minus", "grid"}) {
        const kit::Rect tool = RectAt(toolX, toolY, 28.0f, 28.0f);
        DrawRoundRect(draw, tool.min, tool.max, 6.0f, ColorFillMuted(), ColorLineNormal(), 1.0f);
        DrawIconCentered(draw, icon, ImVec2(toolX + 14.0f, toolY + 14.0f), 14.0f, ColorTextSecondary());
        toolY += 32.0f;
    }
}

} // namespace shine::pages
