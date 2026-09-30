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

#include <algorithm>
#include <cstring>
#include <string>

namespace shine::pages {

using namespace shine::kit;

void NovelPage::Draw(Rect area, ImDrawList* draw) {
    BookState& s = Book();
    const float inspectorW = 280.0f;
    const Rect center{area.min.x, area.min.y, area.max.x - inspectorW, area.max.y};
    const Rect inspector{center.max.x, area.min.y, area.max.x, area.max.y};

    // 8 个模式标签（min-h 44，横向滚动；选中 = accent + 2px accent 下边框）
    const char* modes[] = {"章节", "设定", "初始化", "流水线", "评审", "模型", "状态", "自动"};
    float tx = area.min.x;
    const float tabY = area.min.y;
    ImFont* tabFont = FontBoldAt(12.5f);
    for (int i = 0; i < 8; ++i) {
        const float w = LabelWidth(tabFont, 12.5f, modes[i]) + 32.0f;
        const Rect tab{tx, tabY, tx + w, tabY + 44.0f};
        if (i == mode_) {
            DrawRoundRect(draw, tab.min, tab.max, 6.0f, ColorFillSelected());
            draw->AddLine(ImVec2(tab.min.x, tab.max.y - 1.0f), ImVec2(tab.max.x, tab.max.y - 1.0f),
                          ColorAccent(), 2.0f);
        }
        // 一次 HitTest 取齐 hovered + clicked。
        //
        // ⚠️ 悬停**背景对选中项也生效**，这不是我一开始写的那样。设计稿
        //    `views.css` 的两条规则是：
        //      .novel-modes .ntab:hover { color: text-primary; background: fill-muted; }
        //      .novel-modes .ntab.on   { color: accent; border-bottom-color: accent; }
        //    两条特异度相同、后者胜出，但 `.on` **没有声明 background**，
        //    所以 hover 的 fill-muted 底**照样作用在选中项上**。早先写成
        //    `hit.hovered && i != mode_`，选中页签悬停时一点反应都没有 ——
        //    CSS 不会因为「已经选中」就免掉 hover 底。悬停探针直接判 `broken` 抓到了它。
        const Hit hit = HitTest(tab, "novel-mode-" + std::to_string(i));
        if (hit.hovered) {
            DrawRoundRect(draw, tab.min, tab.max, 6.0f, ColorFillHover());
        }
        // 原来写死 `tab.min.y + 14.0f`：页签高 44，正确中心是 +22，字盒中心落在
        // 20.25，**偏上 1.75px**。改成按容器中心算。
        draw->AddText(tabFont, 12.5f,
                      ImVec2(tab.min.x + 16.0f, kit::CenterTextY(tabFont, 12.5f, tab.center().y)),
                      i == mode_ ? ColorAccent()
                                 : (hit.hovered ? ColorText() : ColorTextSecondary()),
                      modes[i], modes[i] + std::strlen(modes[i]));
        if (hit.clicked) {
            mode_ = i;
        }
        tx += w + 4.0f;
    }

    const Rect body{center.min.x, tabY + 52.0f, center.max.x, center.max.y};

    // 章选择条：切章会重跑 worker（镜头/阶段/连续性都按章取）。
    if (s.bound && s.error.empty() && !s.chapters.empty()) {
        float cx = body.min.x;
        const float chipH = 24.0f;
        for (int i = 0; i < static_cast<int>(s.chapters.size()) && cx + 70.0f < body.max.x; ++i) {
            const std::string label = "第 " + std::to_string(s.chapters[static_cast<std::size_t>(i)].ord) + " 章";
            const float w = LabelWidth(FontAt(12.0f), 12.0f, label.c_str()) + 22.0f;
            const bool on = (i == s.selectedChapter);
            const Rect chip{cx, body.min.y, cx + w, body.min.y + chipH};
            DrawRoundRect(draw, chip.min, chip.max, 12.0f,
                          on ? ColorOf(theme::CurrentDerived().accentDim) : ColorFillMuted(),
                          on ? ColorAccent() : ColorLineSubtle(), 1.0f);
            // 原来写死 `body.min.y + 5.0f`：chip 高 24，正确中心是 +12，字盒中心落在
            // 11，**偏上 1.0px**。改成按容器中心算（chip 随 cx 走，中心取 chip 自己的）。
            draw->AddText(FontAt(12.0f), 12.0f,
                          ImVec2(cx + 11.0f, kit::CenterTextY(FontAt(12.0f), 12.0f, chip.center().y)),
                          on ? ColorAccent() : ColorTextSecondary(), label.data(), label.data() + label.size());
            if (Clicked(chip, "novel-ch-" + std::to_string(i))) {
                s.selectedChapter = i;
                s.selectedShot = 0;
                RequestBookReload();
            }
            cx += w + 6.0f;
        }
    }

    const Rect modeBody{body.min.x, body.min.y + 34.0f, body.max.x, body.max.y};

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
            std::string statusLabel;
            theme::Tone statusTone = theme::Tone::Idle;
            ChapterTone(chapter->status, statusLabel, statusTone);
            const std::string head = "第 " + std::to_string(chapter->ord) + " 章 · " +
                                     (chapter->title.empty() ? std::string(kDash) : chapter->title);
            draw->AddText(FontBoldAt(20.0f), 20.0f, ImVec2(modeBody.min.x, modeBody.min.y),
                          ColorText(), head.data(), head.data() + head.size());
            const float stW = TagWidth(statusLabel, false, chapter->status == "review");
            Tag(draw, RectAt(modeBody.min.x, modeBody.min.y + 28.0f, stW, 20.0f), statusLabel,
                statusTone, false, chapter->status == "review");
            // 字数：chapters.words 是业务层记的值；为 0 时说"未统计"，不编一个数字。
            const std::string words = chapter->words > 0 ? std::to_string(chapter->words) : "未统计";
            DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                            ImVec2(modeBody.min.x + stW + 10.0f, modeBody.min.y + 32.0f),
                            modeBody.width(), ColorTextMuted(),
                            words + " 字 · 更新 " + TimeText(chapter->updated), true);

            // .chap-summary：fill-muted + 3px accent 左边框 + pad 10/14
            float y = modeBody.min.y + 60.0f;
            if (!chapter->summary.empty()) {
                const Rect sum{modeBody.min.x, y, modeBody.min.x + 720.0f, y + 48.0f};
                DrawRoundRect(draw, sum.min, sum.max, 0.0f, ColorFillMuted());
                DrawRoundRect(draw, ImVec2(sum.min.x, sum.min.y), ImVec2(sum.min.x + 3.0f, sum.max.y),
                              1.5f, ColorAccent());
                DrawTextClipped(draw, FontAt(12.5f), 12.5f,
                                ImVec2(sum.min.x + 14.0f, sum.min.y + 10.0f), sum.width() - 28.0f,
                                ColorTextSecondary(), "本章目标 · " + chapter->summary, true);
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
                const float bodyH = DrawTextClipped(draw, FontAt(14.0f), 14.0f, ImVec2(modeBody.min.x, y),
                                                    720.0f, ColorText(), chapter->body, true);
                usedBottom = std::max(usedBottom, y + bodyH);
            }
        }
    } else if (mode_ == 1) {
        // ---- 设定：真实实体（entities 表）----
        if (!s.bound || !s.error.empty() || s.assets.empty()) {
            BookEmpty(modeBody, draw, "masks", "还没有实体。初始化链跑完后这里才有设定。");
        } else {
            static const char* kWorldTitle = "设定集";
            draw->AddText(FontBoldAt(16.0f), 16.0f, ImVec2(modeBody.min.x, modeBody.min.y),
                          ColorText(), kWorldTitle, kWorldTitle + std::strlen(kWorldTitle));
            DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(modeBody.min.x, modeBody.min.y + 22.0f),
                            modeBody.width(), ColorTextMuted(),
                            "共 " + std::to_string(s.assets.size()) + " 个实体 · 来自 entities 表", true);
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
                // 写入口只有 SelectBookAsset 一条（它会顺带 RebuildBookSide，侧栏高亮
                // 跟着走）。另存一份 `on` 局部选中态就是第 N 个「点这边亮那边」。
                const bool on = (i == s.selectedAsset);
                // ⚠️ 一次 HitTest 取齐 hovered + clicked，**不要** Hovered(idA) 再
                //    Clicked(idB)：同一矩形上叠两个 InvisibleButton 时后者永远
                //    clicked=false（本仓踩过），症状是「卡画得出来、怎么点都不动」。
                //    描边规则照抄资产总览网格那张卡（.card:hover 把边框提到 accent-glow），
                //    不另发明一套选中样式。
                const Hit hit = HitTest(card, "novel-asset-" + std::to_string(i));
                DrawShadowed(draw, card.min, card.max, 10.0f, ColorPanel(),
                             on ? ColorAccent()
                                : (hit.hovered ? ColorAccentGlow() : ColorLineSubtle()),
                             1.0f);
                if (hit.clicked) {
                    SelectBookAsset(i);
                }
                usedBottom = std::max(usedBottom, card.max.y);
                const std::string name = a.name.empty() ? std::string(kDash) : a.name;
                DrawTextClipped(draw, FontBoldAt(13.0f), 13.0f,
                                ImVec2(card.min.x + 12.0f, card.min.y + 10.0f), cardW - 24.0f,
                                ColorText(), name, true);
                DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                                ImVec2(card.min.x + 12.0f, card.min.y + 30.0f), cardW - 24.0f,
                                ColorTextMuted(), AssetKindLabel(a.kind), true);
                DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                                ImVec2(card.min.x + 12.0f, card.min.y + 50.0f), cardW - 24.0f,
                                ColorTextMuted(), a.summary, true);
            }
        }
    } else if (mode_ == 3) {
        // ---- 流水线：真实阶段表（pipeline::AllStages 的 text 链）----
        std::vector<StageNode> nodes;
        for (const auto& stage : pipeline::AllStages()) {
            if (stage.chain == "text") {
                nodes.push_back(StageNode{stage.code, stage.name, StageState::Todo});
            }
        }
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
            DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(modeBody.min.x, footY), modeBody.width(),
                            ColorTextMuted(),
                            "这里只列阶段定义；真实运行进度与账本在「总控」页的 Runner 上。", true);
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
    DrawRoundRect(draw, inspector.min, inspector.max, 0.0f, ColorSurface());
    draw->AddLine(ImVec2(inspector.min.x + 0.5f, inspector.min.y),
                  ImVec2(inspector.min.x + 0.5f, inspector.max.y), ColorLineSubtle(), 1.0f);
    const BookChapter* chapter = s.chapter();
    if (chapter != nullptr) {
        std::string statusLabel;
        theme::Tone statusTone = theme::Tone::Idle;
        ChapterTone(chapter->status, statusLabel, statusTone);
        KeyValues(draw,
                  Rect{inspector.min.x + 16.0f, tabY + 60.0f, inspector.max.x - 16.0f, tabY + 220.0f},
                  {{"章节", "第 " + std::to_string(chapter->ord) + " 章"},
                   {"标题", chapter->title.empty() ? kDash : chapter->title},
                   {"字数", chapter->words > 0 ? std::to_string(chapter->words) : "未统计"},
                   {"状态", statusLabel},
                   {"更新", TimeText(chapter->updated)},
                   {"本章镜头", std::to_string(s.shots.size())}});
        DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                        ImVec2(inspector.min.x + 16.0f, tabY + 230.0f), inspectorW - 32.0f,
                        ColorTextMuted(), "数据源：" + s.dbPath, true);
    } else {
        DrawTextClipped(draw, FontAt(12.5f), 12.5f,
                        ImVec2(inspector.min.x + 16.0f, tabY + 64.0f), inspectorW - 32.0f,
                        ColorTextMuted(), "未绑定工程", true);
    }
}

} // namespace shine::pages
