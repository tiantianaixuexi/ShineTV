// shine::pages —— P5.2 小说（novel）
//
// 骨架是 .vw：页头 + 8 个模式页签 + 模式内容区。数据全部来自 novel.db 快照
// （BookData.h），这一页只负责排版与交互。
//
// ⚠️ 这一页的实现从 WorkspaceB.cpp 搬到这里（连同资产页）：真实数据源 novel.db 的
//    读取/快照/worker 全在 BookData.cpp / BookQuery.cpp 里，把实现留在总控那个
//    .cpp 会让「数据在哪」分裂成两处。声明仍在 WorkspacePages.h，Shell 不用改。
//
// 旧版的 "第 3 章 · 雨夜"、固定摘要、固定草稿三段、字数 "2180"、状态 "草稿"、
// 8 个模式里 6 个的假流水线节点，全部改成业务层返回值或诚实空态。
#include "ui/imgui/pages/BookData.h"

#include "ui/imgui/pages/PageCommon.h"

#include "pipeline/StageMachine.h"
#include "ui/imgui/kit/Fmt.h"
#include "ui/imgui/kit/Hash.h"
#include "ui/imgui/kit/Text.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace shine::pages {

using namespace shine::kit;

// ---- 显示缓存（第二十轮波 3）：指纹没变就整帧复用，Draw 每帧零分配 ----
// 正文原样绘制，缓存只依赖当前章的显示字段、实体数、镜头数和库路径。
void NovelPage::RefreshTexts() {
    const BookState& s = Book();
    const BookChapter* chapter = s.chapter();
    std::uint64_t key = HashMix(0, s.assets.size());
    if (chapter != nullptr) {
        key = HashMix(key, static_cast<std::uint64_t>(chapter->ord));
        key = HashMix(key, Fnv1a(chapter->title));
        key = HashMix(key, Fnv1a(chapter->status));
        key = HashMix(key, Fnv1a(chapter->summary));
        key = HashMix(key, static_cast<std::uint64_t>(chapter->words));
        key = HashMix(key, static_cast<std::uint64_t>(chapter->updated));
    }
    key = HashMix(key, static_cast<std::uint64_t>(s.shots.size()));
    key = HashMix(key, Fnv1a(s.dbPath));
    if (key == textKey_) {
        return;
    }
    textKey_ = key;

    // 设定模式的空态行（与章无关，mode 1 在没有选中章时也可能显示）
    assetsEmptyLine_ = kit::Fmt("共 {} 个实体 · 来自 entities 表", s.assets.size());

    // 章「章节」模式的头 / 字数行 / 摘要行 / 状态标签
    if (chapter != nullptr) {
        head_ = kit::Fmt("第 {} 章 · {}", chapter->ord,
                         chapter->title.empty() ? std::string(kDash) : chapter->title);
        wordsLine_ = kit::Fmt("{} 字 · 更新 {}",
                              chapter->words > 0 ? kit::Fmt("{}", chapter->words)
                                                 : std::string("未统计"),
                              TimeText(chapter->updated));
        summaryLine_ =
            chapter->summary.empty() ? std::string() : "本章目标 · " + chapter->summary;
        ChapterTone(chapter->status, statusLabel_, statusTone_);

        // 右栏属性 KV（行序即显示序）
        railKv_[0] = {"章节", kit::Fmt("第 {} 章", chapter->ord)};
        railKv_[1] = {"标题", chapter->title.empty() ? std::string(kDash) : chapter->title};
        railKv_[2] = {"字数", chapter->words > 0 ? kit::Fmt("{}", chapter->words)
                                                 : std::string("未统计")};
        railKv_[3] = {"状态", statusLabel_};
        railKv_[4] = {"更新", TimeText(chapter->updated)};
        railKv_[5] = {"本章镜头", kit::Fmt("{}", s.shots.size())};
        dataSourceLine_ = "数据源：" + s.dbPath;
    }
}

void NovelPage::Draw(Rect area, ImDrawList* draw) {
    BookState& s = Book();
    RefreshTexts();
    const Rect content = ViewHeader(area, draw, "book", "小说工作区",
                                    "切换章节，阅读正文并检查设定");
    const float inspectorW = std::min(256.0f, content.width() * 0.30f);
    const Rect center{content.min, ImVec2(content.max.x - inspectorW - 16.0f, content.max.y)};
    const Rect inspector{center.max.x + 16.0f, center.min.y, content.max.x, content.max.y};

    // 8 个模式标签。走 kit::Tabs + TabsSpec，不再在这里手写第三份页签条 ——
    // 手写那份的尺寸是**逐条错的**，对齐 `views.css:527-554`：
    //   `.novel-modes`  ：gap 2 / padding 0 16 / border-bottom 1px / min-height 44
    //   `.ntab`         ：padding 8px 12px（⇒ 宽 = 字宽 + 24，不是 +32）/ 13px 600 /
    //                     基色 text-**muted**（不是 text-secondary）/ 圆角 6 6 0 0 /
    //                     底部 2px 边框
    //   `.ntab:hover`   ：文字 text-primary + 底 **fill-muted**（原代码画的是 fill-hover，
    //                     它自己的注释写的却是 fill-muted —— 代码与注释互相矛盾，注释对）
    //   `.ntab.on`      ：文字 accent + 下边框 accent，**没有背景**（原代码额外铺了一层
    //                     fill-selected ⇒ 选中页签比设计稿多一个色块）
    // ⚠️ `SegmentOption` 的两个字段都是 `string_view` —— 指的东西必须活过这次调用。
    //    选项表恒定 ⇒ 提函数级 static（第二十轮波 3：Draw 每帧不再重建 8 条）。
    //    值就是 "0".."7"，与 static 的 kModeValues 同生命周期。
    static const std::array<std::string, 8> kModeValues = {
        "0", "1", "2", "3", "4", "5", "6", "7"};
    static const std::vector<SegmentOption> modeOptions = [] {
        std::vector<SegmentOption> opts;
        opts.reserve(8);
        static const char* const kModeNames[8] = {"章节", "设定", "初始化", "流水线",
                                                  "评审", "模型", "状态", "自动"};
        for (int i = 0; i < 8; ++i) {
            opts.push_back({kModeValues[static_cast<std::size_t>(i)], kModeNames[i]});
        }
        return opts;
    }();
    TabsSpec novelTabs;
    novelTabs.fontSize = 13.0f;
    novelTabs.paddingX = 12.0f;
    novelTabs.paddingY = 8.0f;
    novelTabs.gap = 2.0f;
    novelTabs.underlineInset = 0.0f;  // 满宽（它是按钮自己的 border-bottom）
    novelTabs.underlineDy = -1.0f;    // 盒内 2px border
    novelTabs.radius = 6.0f;          // r-sm
    novelTabs.roundTopOnly = true;    // `r-sm r-sm 0 0`
    novelTabs.hoverFill = true;
    novelTabs.selectedFill = true;   // `.on` 没有 background
    // `.novel-modes{min-height:44px}` 且 `align-items:stretch` ⇒ 页签被拉满到 43
    // （44 减去容器自己的 1px 下边框）。
    constexpr float kNovelStripH = 44.0f;
    novelTabs.height = kNovelStripH - 1.0f;
    const float tabStripTop = center.min.y;
    const float tabStripLeft = center.min.x + 16.0f;
    // 当前值走栈缓冲（Tabs 的形参是 view，调用期间活着即可，第二十轮波 3）。
    char modeValueBuf[4];
    std::snprintf(modeValueBuf, sizeof(modeValueBuf), "%d", mode_);
    const std::string_view modeValue = modeValueBuf;
    const std::string_view pickedMode =
        Tabs(draw, RectAt(tabStripLeft, tabStripTop, center.width() - 32.0f, kNovelStripH - 1.0f),
             modeOptions, modeValue, "novel-modes", novelTabs);
    if (!pickedMode.empty()) {
        mode_ = std::atoi(std::string(pickedMode).c_str());
    }
    // 容器下边框：页签的 accent 下划线压在它上面（CSS 里就是这种叠法）。
    draw->AddLine(ImVec2(center.min.x, tabStripTop + kNovelStripH - 0.5f),
                  ImVec2(center.max.x, tabStripTop + kNovelStripH - 0.5f), ColorLineSubtle(), 1.0f);

    // `.novel-body { padding: 16px 18px 24px }`（views.css:561-565）。
    // 章节由侧栏选择，各模式内容直接从页签下的内边距开始。
    const Rect modeBody{center.min.x + 18.0f, tabStripTop + kNovelStripH + 16.0f,
                        center.max.x - 18.0f, center.max.y - 24.0f};

    // 本页**实际画到**的最底（绝对屏幕 y），末尾自报给外壳当滚动区高度。
    // 起点取 modeBody 的顶：空态分支只画一个居中的图标 + 两行字，不该把 2400 的
    // 布局区高原样报回去（那会让工作区多出上千 px 只能滚到空白）。
    float usedBottom = modeBody.min.y;

    if (mode_ == 0) {
        // ---- 章节 ----
        const BookChapter* chapter = s.chapter();
        if (!s.bound || !s.error.empty() || chapter == nullptr) {
            BookEmpty(modeBody, draw, "book", "这本小说还没有章。跑一次 T1–T17 后这里才有正文。");
        } else {
            // 头 / 状态标签 / 字数行来自 RefreshTexts 缓存（第二十轮波 3）。
            const std::string& head = head_;
            TextClipped(draw, ImVec2(modeBody.min.x, modeBody.min.y), modeBody.width(), head,
                 TextStyle{.size = theme::font::kChapter, .bold = true, .color = ColorText()});
            const float stW = TagWidth(statusLabel_, false, chapter->status == "review");
            Tag(draw, RectAt(modeBody.min.x, modeBody.min.y + 28.0f, stW, 20.0f), statusLabel_,
                statusTone_, false, chapter->status == "review");
            // 字数：chapters.words 是业务层记的值；为 0 时说"未统计"，不编一个数字。
            TextClipped(draw, ImVec2(modeBody.min.x + stW + 10.0f, modeBody.min.y + 32.0f),
                        modeBody.width() - stW - 10.0f, wordsLine_,
                        TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()}, true);

            // .chap-summary：fill-muted + 3px accent 左边框 + pad 10/14
            float y = modeBody.min.y + 60.0f;
            if (!chapter->summary.empty()) {
                const Rect sum{modeBody.min.x, y, modeBody.min.x + 720.0f, y + 48.0f};
                DrawRoundRect(draw, sum.min, sum.max, 0.0f, ColorFillMuted());
                DrawRoundRect(draw, ImVec2(sum.min.x, sum.min.y), ImVec2(sum.min.x + 3.0f, sum.max.y),
                              1.5f, ColorAccent());
                TextClipped(draw, ImVec2(sum.min.x + 14.0f, sum.min.y + 10.0f), sum.width() - 28.0f,
                            summaryLine_,
                            TextStyle{.size = theme::font::kHint, .color = ColorTextSecondary()},
                            true);
                y += 60.0f;
            }
            // .draft：14px / 行高 1.9 / max-w 720。正文是 chapters.body 原文。
            // 折行高度由 DrawTextClipped 的返回值给出（它自己按 maxWidth 折行并返回
            // 实际占高），不另算一份行数 —— 两份算法迟早对不上。
            if (chapter->body.empty()) {
                Empty(draw, Rect{modeBody.min.x, y, modeBody.min.x + 720.0f, y + 220.0f}, "text",
                      "本章尚无正文", "T11（正文写作）落库后 chapters.body 才有内容。");
                usedBottom = std::max(usedBottom, y + 220.0f);
            } else {
                const float bodyH =
                    TextClipped(draw, ImVec2(modeBody.min.x, y), 720.0f, chapter->body,
                                TextStyle{.size = theme::font::kTitle, .color = ColorText()}, true);
                usedBottom = std::max(usedBottom, y + bodyH);
            }
        }
    } else if (mode_ == 1) {
        // ---- 设定：真实实体（entities 表）----
        if (!s.bound || !s.error.empty() || s.assets.empty()) {
            BookEmpty(modeBody, draw, "masks", "还没有实体。初始化链跑完后这里才有设定。");
        } else {
            static const char* kWorldTitle = "设定集";
            Text(draw, ImVec2(modeBody.min.x, modeBody.min.y), kWorldTitle,
                 TextStyle{.size = theme::font::kPageTitle, .bold = true, .color = ColorText()});
            TextClipped(draw, ImVec2(modeBody.min.x, modeBody.min.y + 22.0f), modeBody.width(),
                        assetsEmptyLine_,
                        TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()}, true);
            const int columns = AutoGridCols(modeBody.width(), 260.0f, 14.0f);
            const float cardW =
                (modeBody.width() - 14.0f * static_cast<float>(columns - 1)) / static_cast<float>(columns);
            for (int i = 0; i < static_cast<int>(s.assets.size()); ++i) {
                const BookAsset& a = s.assets[static_cast<std::size_t>(i)];
                const int column = i % columns;
                const int row = i / columns;
                // ⚠️ 这里**曾经**是本仓第三处「宽高写进 kit::Rect 四参」的同型 bug：
                //    四参是 (minX,minY,maxX,maxY)，写 (x,y,w,h) 时 max.x = cardW(~250)
                //    < min.x(~296) ⇒ DrawShadowed 整块 return，卡片一张都没画、也点不到。
                //    那个 bug **已修**（下面就是 RectAt），注释留着是因为前两处同型
                //    （资产总览网格 / 底栏页签条）证明它会复发，而当初的
                //    find-rect-wh-misuse.ps1 只认裸 `Rect{...}`、漏掉了本处这种声明式
                //    `Rect name{...}`。写宽高一律 RectAt。
                const Rect card = RectAt(modeBody.min.x + (cardW + 14.0f) * static_cast<float>(column),
                                         modeBody.min.y + 40.0f + 84.0f * static_cast<float>(row),
                                         cardW, 76.0f);
                // 选中态与资产总览网格 / 侧栏树 / 检查器**共用** BookState::selectedAsset：
                // SelectBookAsset 同步侧栏的选中项。
                const bool on = (i == s.selectedAsset);
                // ⚠️ 一次 HitTest 取齐 hovered + clicked，**不要** Hovered(idA) 再
                //    Clicked(idB)：同一矩形上叠两个 InvisibleButton 时后者永远
                //    clicked=false（本仓踩过），症状是「卡画得出来、怎么点都不动」。
                //    描边规则照抄资产总览网格那张卡（.card:hover 把边框提到 accent-glow），
                //    不另发明一套选中样式。
                const Hit hit = HitTest(card, "novel-asset-", i);
                DrawShadowed(draw, card.min, card.max, 10.0f, ColorPanel(),
                             on ? ColorAccent()
                                : (hit.hovered ? ColorAccentGlow() : ColorLineSubtle()),
                             1.0f);
                if (hit.clicked) {
                    SelectBookAsset(i);
                }
                usedBottom = std::max(usedBottom, card.max.y);
                // 名字原样传 view：空名才落 kDash，不再每卡拷一个 string（第二十轮波 3）。
                const std::string_view name = a.name.empty() ? std::string_view(kDash) : a.name;
                TextClipped(draw, ImVec2(card.min.x + 12.0f, card.min.y + 10.0f), cardW - 24.0f,
                            name,
                            TextStyle{.size = theme::font::kBody, .bold = true, .color = ColorText()},
                            true);
                TextClipped(draw, ImVec2(card.min.x + 12.0f, card.min.y + 30.0f), cardW - 24.0f,
                            AssetKindLabel(a.kind),
                            TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()}, true);
                TextClipped(draw, ImVec2(card.min.x + 12.0f, card.min.y + 50.0f), cardW - 24.0f,
                            a.summary,
                            TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()}, true);
            }
        }
    } else if (mode_ == 3) {
        // ---- 流水线：真实阶段表（pipeline::AllStages 的 text 链）----
        // 阶段定义是静态表 ⇒ 提函数级 static，Draw 每帧不再重建 vector（第二十轮波 3）。
        static const std::vector<StageNode> textNodes = [] {
            std::vector<StageNode> v;
            for (const auto& stage : pipeline::AllStages()) {
                if (stage.chain == "text") {
                    v.push_back(StageNode{stage.code, stage.name, StageState::Todo});
                }
            }
            return v;
        }();
        const std::vector<StageNode>& nodes = textNodes;
        if (nodes.empty()) {
            BookEmpty(modeBody, draw, "chip", "没有阶段定义。");
        } else {
            StageFlow(draw, Rect{modeBody.min.x, modeBody.min.y, modeBody.min.x + 1400.0f,
                                 modeBody.min.y + 30.0f},
                      nodes);
            // ⚠️ 阶段**运行态**归总控页的 Runner（它持有 pipeline::Runner 与账本）。
            //    本页不复制一份进度 —— 所以全部 Todo，并在下面说明去哪看真状态。
            //
            //    阶段表是**正文流**：有多长就多长，不再撑满 2400 的布局区。原来写死
            //    `modeBody.max.y - 24`，于是 T1–T17 只有十来行、框却有 2200px 高，
            //    脚注被顶到 y2382 —— 要滚到最底才看得见，而上面全是空白。行高 34
            //    与 StageList 内部一致（Views.cpp 的 rowH），多给 8px 收尾。
            const float listTop = modeBody.min.y + 48.0f;
            const float listH = 34.0f * static_cast<float>(nodes.size()) + 8.0f;
            StageList(draw, Rect{modeBody.min.x, listTop, modeBody.max.x, listTop + listH}, nodes,
                      "artifacts/");
            // 脚注紧跟表底（不再是 `max.y - 18` 的锚点）。
            const float footY = listTop + listH + 8.0f;
            TextClipped(draw, ImVec2(modeBody.min.x, footY), modeBody.width(),
                        "这里只列阶段定义；真实运行进度与账本在「总控」页的 Runner 上。",
                        TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()}, true);
            usedBottom = std::max(usedBottom, footY + 18.0f);
        }
    } else {
        // ---- 其余 5 个模式：业务层还没有对应的**只读投影**接口 ----
        // 如实说明缺什么，而不是拿别的表的数据凑一屏看起来像的东西。
        const char* missing = "";
        switch (mode_) {
        case 2: missing = "初始化链（novel::NovelInit 的门禁与产物表）没有只读查询接口。"; break;
        case 4: missing = "评审记录没有只读查询接口。"; break;
        case 5: missing = "模型角色 / 提示词模板没有只读查询接口。"; break;
        case 6: missing = "角色状态流水（character_status）没有按章的只读投影接口。"; break;
        default: missing = "自动运行策略没有只读查询接口。"; break;
        }
        Empty(draw, modeBody, "sparkles", "尚未接入业务层", missing);
    }

    // ---- 自报本页真实内容高度 ----
    //
    // Shell 给的布局区是写死的 2400px，页面若按它铺内容，工作区就会多出上千 px
    // 只能滚到空白的滚动范围（与总控页同一个问题，见 WorkspaceA.cpp 末尾的同名
    // 调用）。这里报「实际画到的最底 + 一个 kGap 的余量」。
    //
    // ⚠️ 取 `max(实际底, 视口底)`：右栏的面板底与分隔线是按 `inspector.max.y`
    //    （= 布局区底）画满的，只报正文底会在内容短的那几个模式下让右栏**悬空**，
    //    下面露出一条没画的面板底。视口底是下限，不是「撑高」——
    //    2400 那种把空盒子撑出来的做法正是这一行要根治的。
    pages::SetPageContentHeight(std::max(usedBottom, ViewportBottom(area)) - area.min.y + kGap);

    // ---- 右栏：当前章的真实属性 ----
    const Rect chapterCard{inspector.min, ImVec2(inspector.max.x, inspector.min.y + 285.0f)};
    DrawRoundRect(draw, chapterCard.min, chapterCard.max, 10.0f, ColorSurface(), ColorLineNormal(), 1.0f);
    draw->AddLine(ImVec2(inspector.min.x + 0.5f, inspector.min.y),
                  ImVec2(inspector.min.x + 0.5f, chapterCard.max.y), ColorLineSubtle(), 1.0f);
    const BookChapter* chapter = s.chapter();
    // `.novel-right { padding: 14px }`（views.css:521-526），且它的第一行是
    // `<div className="small strong" style={{marginBottom:10}}>属性</div>`
    // （Novel.jsx:457）—— `.small`=12.5px、`.strong`=600（base.css:121-122）。
    // ⚠️ 两块原先都没接：右栏内容从 `area.min.y + 60` 起步（右栏是网格的另一列，
    //    **不随中间的模式页签下移**，它的内容本该从 +14 开始），而且「属性」这个
    //    小标题压根没画。现在按 CSS 从 14 起步 + 补上小标题。
    constexpr float kRailPad = 14.0f;
    const float railX = inspector.min.x + kRailPad;
    const float railW = inspectorW - 2.0f * kRailPad;
    Text(draw, ImVec2(railX, inspector.min.y + kRailPad), "属性",
         TextStyle{.size = theme::font::kHint, .bold = true, .color = ColorText()});
    // 小标题行盒 20（12.5 × 1.6）+ margin-bottom 10 ⇒ 属性表从 +44 开始。
    const float kvTop = inspector.min.y + kRailPad + 12.5f * 1.6f + 10.0f;
    if (chapter != nullptr) {
        // 属性 KV 来自 RefreshTexts 缓存（第二十轮波 3）。
        KeyValues(draw, Rect{railX, kvTop, inspector.max.x - kRailPad, kvTop + 160.0f},
                  railKv_.data(), railKv_.size());
        TextClipped(draw, ImVec2(railX, kvTop + 170.0f), railW, dataSourceLine_,
                    TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()}, true);
    } else {
        TextClipped(draw, ImVec2(railX, kvTop), railW, "未绑定工程",
                    TextStyle{.size = theme::font::kHint, .color = ColorTextMuted()}, true);
    }
}

} // namespace shine::pages
