// shine::pages —— P5.6 出片（videoflow）—— 满幅画布 + FlowCanvas
//
// 同出图页：节点图内容在 FlowGraph.cpp（与出图页共用同一份构造），这里只画界面。
#include "ui/imgui/pages/BookData.h"

#include "ui/imgui/pages/FlowGraph.h"
#include "ui/imgui/pages/PageCommon.h"

#include "comfy/ComfySession.h"
#include "ui/imgui/kit/Scroll.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace shine::pages {

using namespace shine::kit;

void VideoFlowPage::Draw(Rect area, ImDrawList* draw) {
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorFillMuted());
    DrawDotGrid(draw, area.min, area.max, 22.0f, ColorLineNormal());

    // 与出图页同构：右侧浮动面板 348+16，画布让出这块再 fit
    const float panelW = 348.0f;
    const float canvasRight = area.max.x - panelW - 32.0f;
    const Rect canvas{area.min, ImVec2(std::max(canvasRight, area.min.x + 200.0f), area.max.y)};
    // 同出图页：数据指纹变了就重建。
    if (flowNodes_.empty() || flowDataKey_ != FlowDataKey()) {
        BuildGraph();
    }
    if (VideoFlowNeedsFit()) {
        FlowFit(flowNodes_, canvas, 0.0f, flowView_);
        VideoFlowNeedsFit() = false;
    }
    FlowCanvas(draw, canvas, flowNodes_, flowLinks_, flowView_, flowSelected_, 0.0f);

    const Rect bar{area.min.x + 16.0f, area.min.y + 16.0f, area.min.x + 460.0f, area.min.y + 56.0f};
    DrawShadowed(draw, bar.min, bar.max, 10.0f, GlassColor(), ColorLineNormal(), 1.0f, theme::ShadowTier::Card);
    DrawIcon(draw, "film", ImVec2(bar.min.x + 12.0f, bar.center().y - 7.0f), 14.0f, ColorAccent());
    const char* barTitle = "出片流程 · H3 视频";
    draw->AddText(FontBoldAt(12.5f), 12.5f, ImVec2(bar.min.x + 32.0f, bar.center().y - 6.25f),
                  ColorText(), barTitle, barTitle + std::strlen(barTitle));

    const Rect panel{area.max.x - panelW - 16.0f, area.min.y + 16.0f, area.max.x - 16.0f,
                     area.min.y + 16.0f + std::min(520.0f, area.height() - 140.0f)};
    DrawShadowed(draw, panel.min, panel.max, 14.0f, GlassColor(), ColorLineNormal(), 1.0f, theme::ShadowTier::Overlay);
    // ⚠️ 与出图页同一个毛病：早先是 `Tag(..., "出片中", theme::Tone::Accent, ...)`，
    //    文本与色调写死，绘制点在页签分发之前、上下文 30 行内没有任何读数。
    //    队列空着、甚至没绑工程时，面板仍在说「出片中」。这里同样只画**真在跑**的情形。
    //
    // 判据用同一个 Comfy 队列读数（`ComfyRunningForBadge()`）：出片页的「视频任务」
    // 页签本来就已经列的是 `comfy::ComfySession::Queue()` 的真实快照，所以面板头和
    // 页签内容**指向同一份数据** —— 头说「没在跑」而页签列着任务行，那才是真的矛盾。
    // 代价：这个头不区分「跑的是出图任务还是出片任务」（队列没给任务分类），
    // 所以它只声称「Comfy 在跑」，不声称「在出片」—— 宁可少说，不可说错。
    if (ComfyRunningForBadge()) {
        const float headTagW = TagWidth("出片中", false, true);
        Tag(draw, RectAt(panel.max.x - 16.0f - headTagW, panel.min.y + 13.0f, headTagW, 20.0f),
            "出片中", theme::Tone::Accent, false, true);
    }

    const std::vector<SegmentOption> tabs{{"0", "首尾帧链"}, {"1", "视频任务"}, {"2", "成片"}};
    const std::string value = std::to_string(panelTab_);
    const std::string_view picked =
        Segmented(draw,
                  Rect{panel.min.x + 16.0f, panel.min.y + 48.0f, panel.max.x - 16.0f, panel.min.y + 80.0f},
                  tabs, value, "vf-tabs");
    if (!picked.empty()) {
        panelTab_ = std::atoi(std::string(picked).c_str());
    }
    const Rect body{panel.min.x + 16.0f, panel.min.y + 90.0f, panel.max.x - 16.0f, panel.max.y - 16.0f};
    if (panelTab_ == 0) {
        // ---- 首尾帧链 ----
        //
        // ⚠️ 早先这里循环 3 次造 `"S0" + (10+i) + " -> S0" + (11+i)`，连接状态写死
        //    `i == 1 ? "断链" : "已连接"` —— 镜号是编的，断链也是编的（与本工程无关）。
        //    真值源：BookSide().shots 里**当前选中章相邻的两个镜**。
        //    「断链」这个结论需要判定帧链连通性，业务层没有这个只读投影，所以不编 ——
        //    改列每个镜自己的 canonStatus（真字段），那才是真知道的东西。
        const BookSideView& cv = BookSide();
        if (cv.shots.size() < 2) {
            Empty(draw, body, "link",
                  cv.bound ? "本章还不足两个镜" : "还没打开工程",
                  cv.bound ? "首尾帧链是**相邻两镜**之间的关系，至少要两个镜才画得出来。"
                           : "镜列表来自 novel.db 的 shots 表。");
        } else {
            // ⚠️ 原来这里是 `if (y + 26.0f > body.max.y) { break; }` —— 一章的镜一多，
            //    后面的帧链对**无声消失**，界面上看不出「本章还有 N 对相邻镜没列」：
            //    列表被截断且不提示 = 界面在骗人。改成画**全部**对。
            //    底部那句说明留在滚动区**外面**（区高留出 24，不让它压住页脚）。
            ScrollRegion chainScroll("vf-frame-chain",
                                     Rect{body.min.x, body.min.y, body.max.x, body.max.y - 24.0f});
            if (chainScroll) {
                // ⚠️ 内容画在 child 自己的 list 上，裁剪才有效（见 Scroll.h）。
                ImDrawList* chdraw = chainScroll.drawList();
                const Rect inner = chainScroll.content();
                const std::size_t pairs = cv.shots.size() - 1;
                float y = inner.min.y;
                for (std::size_t i = 0; i < pairs; ++i) {
                    const BookShotView& from = cv.shots[i];
                    const BookShotView& to = cv.shots[i + 1];
                    const std::string label =
                        ShotCode(from.ord) + " → " + ShotCode(to.ord);
                    chdraw->AddText(FontAt(12.5f), 12.5f, ImVec2(inner.min.x, y),
                                    ColorTextSecondary(), label.data(), label.data() + label.size());
                    // 右侧标签显示**后一个镜**自己的设定状态 —— 真字段，不臆断链路通断。
                    const std::string canon =
                        to.canonStatus.empty() ? std::string(kDash) : to.canonStatus;
                    const float tw = TagWidth(canon, false, false);
                    Tag(chdraw, RectAt(inner.max.x - tw, y - 3.0f, tw, 20.0f), canon,
                        theme::Tone::Idle, false, false);
                    y += 26.0f;
                }
                // 不调这一行等于没修：不报内容高，ImGui 侧 ContentSize 恒为 0 ⇒ 滚不动。
                chainScroll.setContentHeight(y - inner.min.y);
            }
            DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(body.min.x, body.max.y - 16.0f),
                            body.width(), ColorTextMuted(),
                            "标签是后一个镜的设定状态；帧链是否断需要连续性报告，"
                            "本页不臆断。", true);
        }
    } else if (panelTab_ == 1) {
        // ---- 视频任务：走 Comfy 队列的**真实快照** ----
        //
        // ⚠️ 这里原来画 4 条 `Progress(20/40/60/80, running = i == 0)` —— 写死的算术级数，
        //    与真实运行态毫无关系。界面上永远显示「第一条在跑、其余到 80%」，而底栏
        //    任务队列早就改用 `QueueModel::Row::progress` 的真值了，同一个队列两个口径。
        // 「看起来很像在跑」是这类假数据最难认的地方。
        //
        // 真值源与底栏任务队列**同一份**（`comfy::ComfySession` 的 `QueueModel`），
        // 不在这里另接一条。队列空就是空，不补 4 条占位。
        const std::vector<comfy::QueueModel::Row> rows =
            comfy::ComfySession::Instance().Queue().Snapshot();
        // ⚠️ 这里不能 `return`：胶片条画在页签分支**之外**，早退会把那一条也一起跳过。
        if (rows.empty()) {
            Empty(draw, body, "film", "队列里没有视频任务",
                  "视频任务来自 ComfyUI 队列。取真实数据源：comfy::ComfySession::Queue()。");
        } else {
            // ⚠️ 原来这里是 `if (y + 26.0f > body.max.y) { break; }` —— 队列里的任务
            //    一多，后面的**无声消失**，界面上看不出还有第 N+1 个在排队：列表被截断
            //    且不提示 = 界面在骗人。改成画**全部**行。
            ScrollRegion queueScroll("vf-video-queue", body);
            if (queueScroll) {
                // ⚠️ 内容画在 child 自己的 list 上，裁剪才有效（见 Scroll.h）。
                ImDrawList* qdraw = queueScroll.drawList();
                const Rect inner = queueScroll.content();
                float y = inner.min.y;
                for (const comfy::QueueModel::Row& row : rows) {
                    const bool running = row.state == comfy::TaskState::Running;
                    const bool failed = row.state == comfy::TaskState::Failed;
                    const std::string code = row.label.empty() ? row.promptId : row.label;
                    DrawTextClipped(qdraw, MonoAt(12.0f), 12.0f, ImVec2(inner.min.x, y), 160.0f,
                                    failed ? ColorOf(theme::Current().statusDanger)
                                           : ColorTextSecondary(),
                                    code, true);
                    Progress(qdraw, RectAt(inner.min.x + 170.0f, y + 2.0f, inner.width() - 210.0f, 6.0f),
                             row.progress * 100.0f, running, true);
                    y += 26.0f;
                }
                // 不调这一行等于没修：不报内容高，ImGui 侧 ContentSize 恒为 0 ⇒ 滚不动。
                queueScroll.setContentHeight(y - inner.min.y);
            }
        }
    } else {
        // ---- 成片 ----
        //
        // ⚠️ 早先这里是 `KeyValues({{"分辨率","1920x1080"},{"帧率","24fps"},
        //    {"帧数","112"},{"缺失镜头","S013"}})` —— 四个**编出来的**成片参数，
        //    连「缺失镜头 S013」这种具体断言都是假的。
        // 业务层没有成片产物的只读投影（没有 video 文件的扫描接口接到这一层），
        // 所以这里只说清楚「真知道什么」：本章有几个镜、每个镜多长。
        const BookSideView& fv = BookSide();
        if (fv.shots.empty()) {
            Empty(draw, body, "film", fv.bound ? "本章没有镜" : "还没打开工程",
                  fv.bound ? "成片要由镜生成，业务层还没有把成片产物的只读投影接到这一层。"
                           : "镜列表来自 novel.db 的 shots 表。");
        } else {
            std::vector<std::pair<std::string, std::string>> kv;
            kv.emplace_back("本章镜数", std::to_string(fv.shots.size()));
            int totalSec = 0;
            for (const BookShotView& shot : fv.shots) {
                totalSec += shot.durationSec;
            }
            kv.emplace_back("总时长", std::to_string(totalSec) + "s");
            kv.emplace_back("视觉资产", std::to_string(static_cast<int>([&] {
                int n = 0;
                for (const BookAssetView& a : fv.assets) {
                    if (a.hasAsset) {
                        ++n;
                    }
                }
                return n;
            }())) + " 项");
            KeyValues(draw, Rect{body.min.x, body.min.y, body.max.x, body.min.y + 96.0f}, kv);
            DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(body.min.x, body.min.y + 110.0f),
                            body.width(), ColorTextMuted(),
                            "分辨率 / 帧率 / 缺失镜头都没有只读投影，这里不编。", true);
        }
    }

    // 底部胶片条：left16 / right384，格宽 118 + 6 间距。
    //
    // ⚠️ 早先这里无条件循环 6 次，`const bool ready = i < 3;` —— 前 3 格画缩略图 +
    // 编出来的镜号 `S010..S012` + 编出来的 `24fps` + 一个绿点，后 3 格写「待出片」。
    // 整个条与本工程毫无关系。而且 1711 行的注释还写着「队列空就是空，不补 6 条占位」
    // —— 面板那边已经改成真值源了，胶片条这边还是补 6 条。
    //
    // 真值源：BookSide().shots（当前选中章的镜）。格数跟着镜数走，镜号 / 时长 / 设定
    // 状态都是真字段。**没有镜就整条不出**（保留容器与标题，写明为什么空），
    // 而不是拿 6 个编出来的格子撑场面。
    //
    // 高度从 76 提到 92（底边仍在 area.max.y-16，顶边上移）：多出来的 16px 是给
    // 页脚那句「示意插画 · 非本工程出图结果」的。格子高度用**绝对值**钉死在 60
    // （strip.min.y+8 → +68），所以格内所有文字的相对位置一字未动，改的只是容器
    // 与新增的那行页脚。
    // 为什么不省这 16px 硬塞：条里除了格子的 60px，只剩标题那 14px，左侧 38px 的
    // 窄槽放不下那句（约 152px）—— 硬塞就得截断成「示意插画…」，反而读不出
    // 「非出图结果」这半句，而那半句正是这句存在的全部理由。
    // 面板在右、最上可达 area.min.y+16+min(520, h-140)，上移 16 不会与它相交。
    const Rect strip{area.min.x + 16.0f, area.max.y - 108.0f, area.max.x - 384.0f,
                     area.max.y - 16.0f};
    DrawShadowed(draw, strip.min, strip.max, 14.0f, GlassColor(), ColorLineNormal(), 1.0f, theme::ShadowTier::Card);
    const char* stripTitle = "成片";
    draw->AddText(FontBoldAt(11.5f), 11.5f, ImVec2(strip.min.x + 14.0f, strip.min.y + 12.0f),
                  ColorTextMuted(), stripTitle, stripTitle + std::strlen(stripTitle));
    const BookSideView& sv = BookSide();
    if (sv.shots.empty()) {
        static constexpr char kNoShots[] = "本章没有镜 · 成片条按镜数生成，不补占位格";
        draw->AddText(FontAt(11.5f), 11.5f,
                      ImVec2(strip.min.x + 52.0f, strip.center().y - 5.75f), ColorTextMuted(),
                      kNoShots, kNoShots + sizeof(kNoShots) - 1);
    } else {
        float x = strip.min.x + 52.0f;
        for (const BookShotView& shot : sv.shots) {
            if (x + 118.0f > strip.max.x - 8.0f) {
                break;  // 放不下就不画，不压缩也不重叠
            }
            // 格高用**绝对值**钉 60（不是 strip.max.y-8）：容器长高是为页脚腾的，
            // 格内布局跟着容器长高会连带把缩略图和那三行真字段一起拉变形。
            const Rect cell{x, strip.min.y + 8.0f, x + 118.0f, strip.min.y + 68.0f};
            DrawRoundRect(draw, cell.min, cell.max, 8.0f, ColorFillMuted(), ColorLineNormal(), 1.0f);
            // ⚠️ 这块缩略图是 `Art(..., shot.ord, true)` —— **纯程序化绘制**，seed 只影响
            //    取色与形状，与这一镜的真实画面无关（`novel.db` 里没有镜头级图像，
            //    业务层也没有镜头缩略图的只读投影）。早先它跟旁边那三行真字段
            //    （镜号 / durationSec / 设定状态）**并排同字号**排在一起，视觉上是等价信息，
            //    于是缩略图被读成「这一镜的出图」。相邻的真数据反而给它做了背书 ——
            //    这是这处比另外两处更隐蔽的原因。
            //
            // 一格只有 118×60，缩略图 56 宽放不下整句（kArtNotice 是 13 个汉字 + 一个
            // 间隔号，10.5px 下约 152px 宽，是 56 的近三倍），所以分两处写：
            //   * 每格缩略图**下方**标「示意」两字（kArtNoticeShort，kArtNotice 的缩写），
            //     为此把缩略图高度从 48 收到 34 让出标注行 —— 标注贴在**它说的那张图
            //     正下方**，这是三处里唯一能做到这一点的（另两处图宽 316，标注在图下沿
            //     之下、文字左对齐，读起来是同一组）；
            //   * 整条胶片条的页脚再写一遍**完整那句**（kArtNotice）。
            // 两处都要：只写页脚的话，格内的图仍然是「看起来像出图」，而胶片条一屏能
            // 并排七八格，读者多半不会逐格往下看到页脚。
            Art(draw, Rect{cell.min.x + 6.0f, cell.min.y + 6.0f, cell.min.x + 62.0f,
                           cell.min.y + 40.0f}, shot.ord, true);
            draw->AddText(FontAt(10.5f), 10.5f, ImVec2(cell.min.x + 6.0f, cell.min.y + 42.0f),
                          ColorTextMuted(), kArtNoticeShort,
                          kArtNoticeShort + std::strlen(kArtNoticeShort));
            const std::string code = ShotCode(shot.ord);
            draw->AddText(MonoAt(10.5f), 10.5f, ImVec2(cell.min.x + 70.0f, cell.min.y + 12.0f),
                          ColorAccent(), code.data(), code.data() + code.size());
            // 时长是 BookShotView 的真字段（durationSec），不是写死的 24fps。
            const std::string dur = std::to_string(shot.durationSec) + "s";
            draw->AddText(FontAt(10.5f), 10.5f, ImVec2(cell.min.x + 70.0f, cell.min.y + 28.0f),
                          ColorTextMuted(), dur.data(), dur.data() + dur.size());
            // 圆点色调跟着 canonStatus 的空/非空走：设定状态是判据，没有就画中性灰，
            // 不画成「已完成」的绿 —— 绿点在胶片条上会被读成「这一格出好了」。
            const bool hasCanon = !shot.canonStatus.empty();
            StatusDot(draw, ImVec2(cell.min.x + 74.0f, cell.min.y + 44.0f),
                      hasCanon ? theme::Tone::Accent : theme::Tone::Idle, false);
            x += 124.0f;
        }
    }
    // 页脚：格内那两个字（kArtNoticeShort = 「示意」）在这里还原成**完整那句**
    // （kArtNotice，与 Shell.cpp / 图评审 / 结果三处逐字一致，不另创新文案）。
    // 「每格都是示意」这个意思由**每格都各标了一个「示意」**来承担，不靠页脚措辞 ——
    // 页脚只负责把缩写展开成整句：读者在格内看到「示意」想知道指什么，往下就能看到。
    // 画在 if/else **之外**：没镜的那条分支没有占位画，这句仍然成立（说明这批格子
    // 将来长什么样），也让这句不随镜数忽隐忽现。
    DrawTextClipped(draw, FontAt(10.5f), 10.5f, ImVec2(strip.min.x + 52.0f, strip.max.y - 18.0f),
                    strip.width() - 64.0f, ColorTextMuted(), kArtNotice, true);
}

} // namespace shine::pages
