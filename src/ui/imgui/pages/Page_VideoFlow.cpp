// shine::pages —— P5.6 出片（videoflow）—— 满幅画布 + FlowCanvas
//
// 同出图页：节点图内容在 FlowGraph.cpp（与出图页共用同一份构造），这里只画界面。
#include "ui/imgui/pages/BookData.h"

#include "ui/imgui/pages/FlowGraph.h"
#include "ui/imgui/pages/PageCommon.h"

#include "comfy/ComfySession.h"
#include "ui/imgui/kit/Fmt.h"
#include "ui/imgui/kit/Hash.h"
#include "ui/imgui/kit/Image.h"  // RequestImage / DrawImage：成片条那几格的真图
#include "ui/imgui/kit/Scroll.h"
#include "ui/imgui/kit/ScrollList.h"
#include "ui/imgui/kit/Text.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace shine::pages {

using namespace shine::kit;

void VideoFlowPage::Draw(Rect area, ImDrawList* draw) {
    // 宽窗并排，窄窗将任务面板接在画布下方，页签内容共用同一份实现。
    constexpr float panelW = 348.0f;
    const bool narrow = area.width() < kFlowStackWidth;

    // 画布（宽窗让出右侧 348+16 再 fit；窄窗铺满可见主列、高 = 可视高 − 工具条
    // 那 82 —— fit 按真看得到的高度居中，首屏就能看到节点，同出图页）。
    const float canvasTop = area.min.y + 82.0f;
    float canvasBottom = area.max.y - 108.0f;
    if (narrow) {
        const float viewportH = pages::WorkspaceViewportHeight();
        canvasBottom =
            canvasTop + std::max(120.0f, (viewportH > 0.0f ? viewportH : area.height()) - 82.0f);
    }
    const float canvasRight = narrow ? area.max.x : area.max.x - panelW - 32.0f;
    const Rect canvas{ImVec2(area.min.x, canvasTop), ImVec2(canvasRight, canvasBottom)};

    // 胶片条：宽窗钉在布局区底（下沿 area.max.y-16、右端贴画布右界）；窄窗排在
    // 画布与任务面板**之间**。两种模式都是 92 高，格子布局与横滚一字不动 ——
    // 「保持横滚」靠 reel 自己的 setContentWidth（见条尾），与摆放位置无关。
    const float stripTop = narrow ? (canvas.max.y + 16.0f) : (area.max.y - 108.0f);
    const Rect strip{area.min.x + 16.0f, stripTop,
                     narrow ? (area.max.x - 16.0f) : canvas.max.x, stripTop + 92.0f};

    // 任务面板：宽窗浮动在右；窄窗纵向接在胶片条下方，通过**工作区主滚动**到达
    //（内容高由 SetPageContentHeight 真实上报）。
    const float panelHeight = narrow ? 520.0f : std::min(520.0f, area.height() - 140.0f);
    const float panelTop = narrow ? (strip.max.y + 16.0f) : (area.min.y + 16.0f);
    const Rect panel{narrow ? (area.min.x + 16.0f) : (area.max.x - panelW - 16.0f), panelTop,
                     area.max.x - 16.0f, panelTop + panelHeight};
    // 内容高真实上报（同出图页）：窄窗 = 画布 + 胶片条 + 面板的堆叠高 + 16 底边距。
    pages::SetPageContentHeight(narrow ? (panel.max.y + 16.0f - area.min.y) : area.height());

    // 窄窗内容比布局区高（条与面板堆在画布下面），点阵背景铺到真实内容底。
    const ImVec2 bgMax = narrow ? ImVec2(area.max.x, panel.max.y + 16.0f) : area.max;
    DrawRoundRect(draw, area.min, bgMax, 0.0f, ColorFillMuted());
    DrawDotGrid(draw, area.min, bgMax, 22.0f, ColorLineNormal());

    // 同出图页：数据指纹变了就重建。
    if (flowNodes_.empty() || flowDataKey_ != FlowDataKey()) {
        BuildGraph();
    }
    // fit 触发：① 建图/重建后（VideoFlowNeedsFit）；② 窄窗下画布**尺寸**变了。
    // ⚠️ 只比尺寸、不比位置（位置含滚动偏移，滚动时每帧都变）；宽窗保持
    //    「只在建图时 fit」的原行为，1600×960 取证采样不受影响。
    static ImVec2 lastFitSize{0.0f, 0.0f};
    static bool wasNarrow = narrow;
    const ImVec2 fitSize{canvas.width(), canvas.height()};
    if (VideoFlowNeedsFit() ||
        narrow != wasNarrow || (narrow && (fitSize.x != lastFitSize.x || fitSize.y != lastFitSize.y))) {
        FlowFit(flowNodes_, canvas, 0.0f, flowView_);
        VideoFlowNeedsFit() = false;
        lastFitSize = fitSize;
    }
    wasNarrow = narrow;
    FlowCanvas(draw, canvas, flowNodes_, flowLinks_, flowView_, flowSelected_, 0.0f, narrow);

    // 工具条（宽窗宽 = 画布宽 − 16；窄窗铺满、左右各留 16）。
    const float barW = narrow ? (area.width() - 32.0f) : (canvas.width() - 16.0f);
    const Rect bar = RectAt(area.min.x + 16.0f, area.min.y + 16.0f, barW, 52.0f);
    DrawRoundRect(draw, bar.min, bar.max, 10.0f, ColorSurface(), ColorLineNormal(), 1.0f);
    DrawIcon(draw, "film", ImVec2(bar.min.x + 12.0f, bar.center().y - 9.0f), 18.0f, ColorAccent());
    TextClipped(draw, ImVec2(bar.min.x + 38.0f, bar.center().y - 8.0f), bar.width() - 50.0f,
                "出片流程", TextStyle{.size = theme::font::kPageTitle, .bold = true, .color = ColorText()});

    // 面板矩形在文件开头的布局段算好（宽窗在右 / 窄窗在胶片条下方）。
    DrawShadowed(draw, panel.min, panel.max, 14.0f, GlassColor(), ColorLineNormal(), 1.0f, theme::ShadowTier::Overlay);
    DrawIcon(draw, "link", ImVec2(panel.min.x + 16.0f, panel.min.y + 16.0f), 15.0f, ColorAccent());
    Text(draw, ImVec2(panel.min.x + 40.0f, panel.min.y + 16.0f), "出片任务",
         TextStyle{.size = theme::font::kTitle, .bold = true, .color = ColorText()});
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

    // 页签表恒定：提 static（第二十轮波 3）。值走栈缓冲。
    static const std::string kVfTabValues[3] = {"0", "1", "2"};
    static const std::vector<SegmentOption> tabs{{kVfTabValues[0], "首尾帧链"},
                                                 {kVfTabValues[1], "视频任务"},
                                                 {kVfTabValues[2], "成片"}};
    char valueBuf[4];
    std::snprintf(valueBuf, sizeof(valueBuf), "%d", panelTab_);
    const std::string_view value = valueBuf;
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
            // R5：收进 kit::ScrollList —— clipper + 内容高度自动（行距 26）。
            ScrollList chainScroll("vf-frame-chain",
                                   Rect{body.min.x, body.min.y, body.max.x, body.max.y - 24.0f},
                                   static_cast<int>(cv.shots.size() - 1), 26.0f, 26.0f);
            if (chainScroll) {
                // ⚠️ 内容画在 child 自己的 list 上，裁剪才有效（见 Scroll.h）。
                ImDrawList* chdraw = chainScroll.drawList();
                const Rect inner = chainScroll.content();
                for (int i = chainScroll.firstRow(); i < chainScroll.lastRow(); ++i) {
                    const BookShotView& from = cv.shots[static_cast<std::size_t>(i)];
                    const BookShotView& to = cv.shots[static_cast<std::size_t>(i) + 1];
                    const float y = chainScroll.RowRect(i).min.y;
                    const std::string label =
                        ShotCode(from.ord) + " → " + ShotCode(to.ord);
                    // R3 收编：裸 AddText → kit::Text。
                    kit::Text(chdraw, ImVec2(inner.min.x, y), label,
                              TextStyle{.size = theme::font::kHint, .color = ColorTextSecondary()});
                    // 右侧标签显示**后一个镜**自己的设定状态 —— 真字段，不臆断链路通断。
                    const std::string canon =
                        to.canonStatus.empty() ? std::string(kDash) : to.canonStatus;
                    const float tw = TagWidth(canon, false, false);
                    Tag(chdraw, RectAt(inner.max.x - tw, y - 3.0f, tw, 20.0f), canon,
                        theme::Tone::Idle, false, false);
                }
            }
            kit::TextClipped(draw, ImVec2(body.min.x, body.max.y - 16.0f), body.width(),
                             "标签是后一个镜的设定状态；帧链是否断需要连续性报告，"
                             "本页不臆断。",
                             TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()},
                             true);
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
            //    且不提示 = 界面在骗人。R5：收进 ScrollList（画全部行 + clipper + 高度自动）。
            ScrollList queueScroll("vf-video-queue", body, static_cast<int>(rows.size()), 26.0f,
                                   26.0f);
            if (queueScroll) {
                // ⚠️ 内容画在 child 自己的 list 上，裁剪才有效（见 Scroll.h）。
                ImDrawList* qdraw = queueScroll.drawList();
                const Rect inner = queueScroll.content();
                for (int i = queueScroll.firstRow(); i < queueScroll.lastRow(); ++i) {
                    const comfy::QueueModel::Row& row = rows[static_cast<std::size_t>(i)];
                    const bool running = row.state == comfy::TaskState::Running;
                    const bool failed = row.state == comfy::TaskState::Failed;
                    const float y = queueScroll.RowRect(i).min.y;
                    const std::string code = row.label.empty() ? row.promptId : row.label;
                    kit::TextClipped(qdraw, ImVec2(inner.min.x, y), 160.0f, code,
                                     TextStyle{.size = theme::font::kTiny, .mono = true,
                                               .color = failed
                                                           ? ColorOf(theme::Current().statusDanger)
                                                           : ColorTextSecondary()});
                    Progress(qdraw, RectAt(inner.min.x + 170.0f, y + 2.0f, inner.width() - 210.0f, 6.0f),
                             row.progress * 100.0f, running, true);
                }
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
            // KV 显示缓存（第二十轮波 3）：只随 (镜数, 逐镜时长, 资产就绪数) 变。
            std::uint64_t filmKey = HashMix(0, fv.shots.size());
            int totalSec = 0;
            for (const BookShotView& shot : fv.shots) {
                filmKey = HashMix(filmKey, static_cast<std::uint64_t>(shot.durationSec));
                totalSec += shot.durationSec;
            }
            int readyAssets = 0;
            for (const BookAssetView& a : fv.assets) {
                if (a.hasAsset) {
                    ++readyAssets;
                }
            }
            filmKey = HashMix(filmKey, static_cast<std::uint64_t>(readyAssets));
            if (filmKey != filmKvKey_) {
                filmKvKey_ = filmKey;
                filmKv_.clear();
                filmKv_.push_back({"本章镜数", kit::Fmt("{}", fv.shots.size())});
                filmKv_.push_back({"总时长", kit::Fmt("{}s", totalSec)});
                filmKv_.push_back({"视觉资产", kit::Fmt("{} 项", readyAssets)});
            }
            KeyValues(draw, Rect{body.min.x, body.min.y, body.max.x, body.min.y + 96.0f},
                      filmKv_.data(), filmKv_.size());
            kit::TextClipped(draw, ImVec2(body.min.x, body.min.y + 110.0f), body.width(),
                             "分辨率 / 帧率 / 缺失镜头都没有只读投影，这里不编。",
                             TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()},
                             true);
        }
    }

    // 胶片条（矩形 `strip` 在文件开头的布局段：宽窗 left16 / right384 钉布局区底，
    // 窄窗夹在画布与任务面板之间）：格宽 118 + 6 间距。
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
    DrawShadowed(draw, strip.min, strip.max, 14.0f, GlassColor(), ColorLineNormal(), 1.0f, theme::ShadowTier::Card);
    const char* stripTitle = "成片";
    // R3 收编：裸 AddText → kit::Text（11.5 = kLabel）。
    kit::Text(draw, ImVec2(strip.min.x + 14.0f, strip.min.y + 12.0f), stripTitle,
              TextStyle{.size = theme::font::kLabel, .bold = true, .color = ColorTextMuted()});
    // ⚠️ 这里读 `Book()`（而不是 `BookSide()`）是为了拿到 `root` —— 逐镜的成片图是
    //    **相对工程根的路径**（`generated_images.rel_path`），没有 root 拼不出绝对路径。
    //    镜列表两边同源（`RebuildBookSide` 把 `BookState::shots` 一对一投影成
    //    `BookSideView::shots`），所以字段（ord / durationSec / canonStatus / imageRelPath）
    //    读哪一份都是同一行数据 —— 不要在两个结构体之间各读一半字段。
    const BookState& book = Book();
    if (book.shots.empty()) {
        static constexpr char kNoShots[] = "本章没有镜 · 成片条按镜数生成，不补占位格";
        // R3 收编：裸 AddText → kit::Text。
        kit::Text(draw, ImVec2(strip.min.x + 52.0f, strip.center().y - 5.75f),
                  std::string_view(kNoShots, sizeof(kNoShots) - 1),
                  TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()});
    } else {
        // 这一条里有没有格子画的是**程序化占位画** —— 页脚那句「示意插画 · 非本工程出图结果」
        // 只对占位画成立。有真图上屏时照旧画它，等于把真图说成示意插画（同样是错的），
        // 所以页脚改成**按需出现**（全部格子都有真图时就不画）。
        bool anyPlaceholder = false;
        {
        ScrollRegion reel("video-film-strip", Rect{ImVec2(strip.min.x + 52.0f, strip.min.y + 5.0f),
                                                     ImVec2(strip.max.x - 8.0f, strip.min.y + 78.0f)},
                          false, true);
        ImDrawList* reelDraw = reel.drawList();
        const Rect reelContent = reel.content();
        float x = reelContent.min.x;
        constexpr float kCellPitch = 126.0f;
        for (const BookShot& shot : book.shots) {
            // 格高用**绝对值**钉 60（不是 strip.max.y-8）：容器长高是为页脚腾的，
            // 格内布局跟着容器长高会连带把缩略图和那三行真字段一起拉变形。
            const Rect cell{x, reelContent.min.y, x + 118.0f, reelContent.min.y + 60.0f};
            DrawRoundRect(reelDraw, cell.min, cell.max, 8.0f, ColorFillMuted(), ColorLineNormal(), 1.0f);
            // 这一格的缩略图 —— 与分镜页 `.tthumb` **同一条链、同一份数据**
            // （`generated_images` 的 `source_kind='shot'` + `source_id=shots.id`，
            // 由 `BookQuery.cpp` 一次 `IN (...)` 查完并按 id 升序留最新那条）。
            //
            // 三条分支（与 `Page_Storyboard.cpp` 的 `.tthumb` 逐条对齐，别合并）：
            //   ① 有真图且解码成功        → 画它（满幅 cover），**不标「示意」**
            //   ② 登记过这一镜的图但解不出 → 画设计稿那张 `Art` + 「示意」标（fixture 里
            //      `assets/shot/s4_missing.png` 就是这一支）
            //   ③ 压根没登记（还没出图）   → 「待出片」（设计稿 VideoFlow.jsx:157-159 的
            //      non-ready 分支就是这两个字，没有图标）
            // ⚠️ ②③ 必须分开：合成一条就分不出「出过图但文件坏了」与「还没出图」。
            //
            // ⚠️ 早先这里无条件画 `Art(..., shot.ord, true)`，注释里那句「novel.db 里没有
            //    镜头级图像」是**错的**（同一个错归因在分镜页已经订正过一次）：那一对
            //    `source_kind/source_id` 列与 `idx_gimg_source` 索引一直都在库里。
            //    它跟旁边三行真字段并排同字号排着，于是被读成「这一镜的出图」——
            //    而那正是这一段现在真的画的东西（只是以前画的是占位画）。
            const Rect thumb{cell.min.x + 6.0f, cell.min.y + 6.0f, cell.min.x + 62.0f,
                             cell.min.y + 40.0f};
            const ImageSlot frame = RequestImage(AssetImagePath(book.root, shot.imageRelPath));
            if (frame.ready()) {
                DrawImage(reelDraw, thumb, frame, 0.0f, ImageFit::Cover);
            } else if (!shot.imageRelPath.empty()) {
                // 登记过图但解不出来（文件坏了 / 删了）：仍画设计稿那张 `Art`，
                // 并在它**正下方**标「示意」—— 一格只有 118×60，缩略图 56 宽放不下
                // 整句（kArtNotice 约 152px，是 56 的近三倍），所以缩写放格内、
                // 完整那句放条尾页脚（见下）。
                Art(reelDraw, thumb, shot.ord, /*cover=*/true);
                // R3 收编：裸 AddText → kit::Text（10.5 = kSmall）。
                kit::Text(reelDraw, ImVec2(cell.min.x + 6.0f, cell.min.y + 42.0f), kArtNoticeShort,
                          TextStyle{.size = theme::font::kSmall, .color = ColorTextMuted()});
                anyPlaceholder = true;
            } else {
                static constexpr char kPendingCut[] = "待出片";
                // R3 收编：量宽 / 居中 / 画三处从同一份 TextStyle 出发
                // （原先一个手写 `tinyFont` 三处各写一遍 10.5f）。
                const TextStyle cutStyle{.size = theme::font::kSmall, .color = ColorTextMuted()};
                const float tinyW = kit::TextWidth(kPendingCut, cutStyle);
                kit::Text(reelDraw,
                          ImVec2(thumb.center().x - tinyW * 0.5f,
                                 kit::CenterY(thumb.center().y, cutStyle)),
                          std::string_view(kPendingCut, sizeof(kPendingCut) - 1), cutStyle);
            }
            // 镜码 / 时长在栈上拼（每帧 × 每格都走这里，第二十轮波 3）。
            char codeBuf[16];
            const std::string_view code = ShotCodeTo(codeBuf, sizeof(codeBuf), shot.ord);
            // R3 收编：裸 AddText → kit::Text（10.5 = kSmall，等宽档照旧）。
            kit::Text(reelDraw, ImVec2(cell.min.x + 70.0f, cell.min.y + 12.0f), code,
                      TextStyle{.size = theme::font::kSmall, .mono = true, .color = ColorAccent()});
            // 时长是 BookShotView 的真字段（durationSec），不是写死的 24fps。
            char durBuf[16];
            const int durN = std::snprintf(durBuf, sizeof(durBuf), "%ds", shot.durationSec);
            const std::string_view dur(durBuf, durN > 0 ? static_cast<std::size_t>(durN) : 0u);
            kit::Text(reelDraw, ImVec2(cell.min.x + 70.0f, cell.min.y + 28.0f), dur,
                      TextStyle{.size = theme::font::kSmall, .color = ColorTextMuted()});
            // 圆点色调跟着 canonStatus 的空/非空走：设定状态是判据，没有就画中性灰，
            // 不画成「已完成」的绿 —— 绿点在胶片条上会被读成「这一格出好了」。
            const bool hasCanon = !shot.canonStatus.empty();
            StatusDot(reelDraw, ImVec2(cell.min.x + 74.0f, cell.min.y + 44.0f),
                      hasCanon ? theme::Tone::Accent : theme::Tone::Idle, false);
            x += kCellPitch;
        }
        // 页脚：格内那两个字（kArtNoticeShort = 「示意」）在这里还原成**完整那句**
        // （kArtNotice，与 Shell.cpp / 图评审 / 结果三处逐字一致，不另创新文案）。
        // 「这一格是示意」由**那一格自己**标着「示意」来承担；页脚只把缩写展开成整句。
        //
        // ⚠️ 页脚改成**按需出现**（早先无条件画）：真图接上之后，「非本工程出图结果」
        //    这句对画着真图的格子就不成立了 —— 照旧画等于把真图说成示意插画。
        //    条件是「本条里有格子回退到了占位画」，所以本工程真出了图的那条条上
        //    这句话会消失，而回退时它照旧出现（缩写与展开句同时在场）。
        reel.setContentWidth(x - reelContent.min.x);
        }
        if (anyPlaceholder) {
            kit::TextClipped(draw, ImVec2(strip.min.x + 52.0f, strip.max.y - 18.0f),
                             strip.width() - 64.0f, kArtNotice,
                             TextStyle{.size = theme::font::kSmall, .color = ColorTextMuted()},
                             true);
        }
    }
    // 没镜的分支（`book.shots.empty()`）不画页脚：那里没有任何占位画，
    // 「非本工程出图结果」这句话在那一帧没有可指的对象。
}

} // namespace shine::pages
