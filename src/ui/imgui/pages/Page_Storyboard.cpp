// shine::pages —— P5.4 分镜（storyboard）
//
// 全部字段来自 novel.db：章/场/镜（NovelGraph::ListChapters/ListScenes +
// NovelVisual::ListShotsByChapter）、V1–V7 阶段产物（ListStageArtifacts）、
// V8 连续性（RunContinuityChecks 的真实三态）。旧版的 "S012 · 转身"、6.0s、
// 6 条 "C1 服装一致"、8 张 S010–S017 缩略图全是写死的，已全部删除。
#include "ui/imgui/pages/BookData.h"

#include "ui/imgui/pages/PageCommon.h"

#include "ui/imgui/kit/Scroll.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace shine::pages {

using namespace shine::kit;

void StoryboardPage::Draw(Rect area, ImDrawList* draw) {
    BookState& s = Book();
    const BookChapter* chapter = s.chapter();
    const BookShot* shot = s.shot();

    // 页头标题/副行：没有选中镜头时说清是哪种空态，不用占位镜头号。
    // 副行恒为"章 + 已落库镜头数 + 库路径"（全是真实计数），标题随选中镜头变。
    std::string title = "分镜";
    std::string subtitle = "Scene → Sequence → Shot";
    if (chapter != nullptr) {
        title = "第 " + std::to_string(chapter->ord) + " 章" +
                (chapter->title.empty() ? std::string() : (" · " + chapter->title));
        subtitle = "已落库镜头 " + std::to_string(s.shots.size()) + " 个 · " + s.dbPath;
    }
    if (shot != nullptr) {
        title = ShotCode(shot->ord) + " · " +
                (shot->action.empty() ? std::string(kDash) : shot->action);
    }
    Rect right;
    Rect content = ViewHeader(area, draw, "clapper", title.c_str(), subtitle.c_str(), &right);

    // 状态标签：取镜头的 canon_status（shots 表的规范值），映射不到就原样显示。
    std::string canonLabel = shot != nullptr && !shot->canonStatus.empty() ? shot->canonStatus : kDash;
    const float tagW = TagWidth(canonLabel, false, true);
    Tag(draw, RectAt(right.max.x - 320.0f, right.min.y, tagW, 20.0f), canonLabel, theme::Tone::Idle,
        false, true);

    ButtonSpec run;
    run.variant = ButtonVariant::Primary;
    const char* runLabel = "重新读取";
    const float runW = ButtonWidth(ButtonSize::Medium, 0.0f, LabelWidth(FontBoldAt(13.0f), 13.0f, runLabel));
    // ⚠️ 业务层目前**没有**暴露"替前端跑 V1–V8"的服务接口（Qt 版靠 NovelDirector +
    // LlmCallFn 注入，ImGui 侧没有注入点）。所以这里只给"重新读取"，不给一个点了
    // 什么都不做的假"运行 V1–V8"。
    run.disabled = !s.bound || s.loading;
    if (Button(draw, RectAt(right.max.x - runW, right.min.y, runW, 30.0f), runLabel, run, "sb-run")) {
        RequestBookReload();
    }

    // 空态闸门：没绑 / 读失败 / 正在读 / 库里没章 —— BookEmpty 内部按这四种分别措辞。
    if (!s.bound || !s.error.empty() || s.chapters.empty()) {
        BookEmpty(content, draw, "clapper", "这本小说还没有章。跑一次 T1–T17 后这里才有分镜。");
        // 空态也要自报：只画一个居中图标 + 两行字，报 2400 会让工作区多出只能滚到
        // 空白的滚动范围。ViewportBottom 已经是绝对 y，不要再加 area.min.y。
        pages::SetPageContentHeight(ViewportBottom(area) - area.min.y + kGap);
        return;
    }

    // V1–V8 状态：逐项来自真实表（V1–V7 看 stage_artifacts 有无产物，V8 看连续性报告）。
    std::vector<StageNode> vs;
    for (int i = 0; i < 8; ++i) {
        vs.push_back(StageNode{"V" + std::to_string(i + 1), "",
                               s.vStage[i] ? StageState::Done : StageState::Todo});
    }
    StageFlow(draw, Rect{content.min.x, content.min.y, content.max.x, content.min.y + 30.0f}, vs);

    // .shots-wrap = minmax(0,1.2fr) / minmax(0,1fr) gap16
    const float detailTop = content.min.y + 46.0f;
    // 时间轴是**常驻可见**元素（横向镜条 + 时长），所以钉**视口**底，不钉布局区底。
    // 原来 `content.max.y - 118` 取的是 2400 布局区的底（≈2282），于是时间轴要滚到
    // 最底才看得着，上面两张 detail / continuity 卡被撑到约 2200px 高却只画十几行。
    //
    // ⚠️ 视口很矮时（< 约 200px）`timelineTop - kGap` 会落到 detailTop 之上，两张卡
    //    的 max.y < min.y 变成反向矩形 —— DrawShadowed / Card 会整块 return（内容
    //    静默消失）。给一个下限，宁可让时间轴压住卡片，也不产出反向矩形。
    const float contentBottom = ViewportBottom(area);
    const float timelineTop =
        std::max(contentBottom - 118.0f, std::min(content.min.y + 120.0f, contentBottom));
    const float detailW = (content.width() - kGap) * 1.2f / 2.2f;
    const float detailBottom = std::max(timelineTop - kGap, detailTop);
    const Rect detail{content.min.x, detailTop, content.min.x + detailW, detailBottom};
    const Rect continuity{detail.max.x + kGap, detailTop, content.max.x, detailBottom};

    // ---- 镜头详情 ----
    Rect detailBody = Card(draw, detail, "镜头详情", "target", false, false);
    if (shot == nullptr) {
        Empty(draw, detailBody, "clapper", "本章尚无镜头",
              "V9（叙事分镜）落库后，shots 表才会有行。");
    } else {
        // 时长：优先 timeline_json.duration_s（秒），退回 duration_note 原文，都没有 → kDash。
        std::string duration = kDash;
        if (shot->durationSec > 0.0) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1fs", shot->durationSec);
            duration = buf;
        } else if (!shot->durationNote.empty()) {
            duration = shot->durationNote;
        }
        auto orDash = [](RowId id) { return id > 0 ? ("#" + std::to_string(id)) : std::string(kDash); };
        KeyValues(draw, detailBody,
                  {{"镜头", "#" + std::to_string(shot->id)},
                   {"场 / 序", "第 " + std::to_string(shot->sceneOrd) + " 场 · 第 " +
                                   std::to_string(shot->ord) + " 镜"},
                   {"动作", shot->action.empty() ? kDash : shot->action},
                   {"表演", shot->expression.empty() ? kDash : shot->expression},
                   {"机位", orDash(shot->cameraId)},
                   {"光线", orDash(shot->lightingId)},
                   {"时长", duration},
                   {"情绪", shot->mood.empty() ? kDash : shot->mood},
                   {"旁白", shot->narration.empty() ? kDash : shot->narration}});
        // Beat 时间轴：直接显示库里的 timeline_json 原文，不编一份示例 JSON。
        const float ty = detailBody.min.y + 190.0f;
        static const char* kBeatLabel = "BEAT 时间轴（shots.timeline_json）";
        draw->AddText(FontBoldAt(11.5f), 11.5f, ImVec2(detailBody.min.x, ty), ColorTextMuted(),
                      kBeatLabel, kBeatLabel + std::strlen(kBeatLabel));
        DrawRoundRect(draw, ImVec2(detailBody.min.x, ty + 16.0f),
                      ImVec2(detailBody.max.x, ty + 104.0f), 6.0f, ColorFillMuted(), ColorLineNormal(),
                      1.0f);
        if (shot->timelineJson.empty() || shot->timelineJson == "{}") {
            DrawTextClipped(draw, FontAt(12.5f), 12.5f,
                            ImVec2(detailBody.min.x + 10.0f, ty + 40.0f), detailBody.width() - 20.0f,
                            ColorTextMuted(), "这一镜没有 timeline_json。", true);
        } else {
            DrawTextClipped(draw, MonoAt(12.5f), 12.5f, ImVec2(detailBody.min.x + 10.0f, ty + 26.0f),
                            detailBody.width() - 20.0f, ColorTextSecondary(), shot->timelineJson, true);
        }
        // 空间 / 转场 / 走位**无专列**（V9 只存盘到 storyboard.json）—— 如实说明，不编值。
        DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                        ImVec2(detailBody.min.x, ty + 112.0f), detailBody.width(),
                        ColorTextMuted(),
                        "空间 / 转场 / 走位没有库字段，只在 work/ch<NNN>/storyboard.json", true);
    }

    // ---- 连续性 C1–C12：真实三态 ----
    Rect continuityBody = Card(draw, continuity, "连续性 C1-C12", "check", false, false);
    if (!s.continuity.ran) {
        Empty(draw, continuityBody, "check", "连续性未校验", "V8 需要该章的镜头数据。");
    } else {
        // 汇总行用真实计数：看了几镜、比对几对、失败几条、数据不足几条。
        const std::string summary = "镜 " + std::to_string(s.continuity.shotsSeen) + " · 比对 " +
                                    std::to_string(s.continuity.pairsChecked) + " 对 · 失败 " +
                                    std::to_string(s.continuity.failed) + " · 数据不足 " +
                                    std::to_string(s.continuity.unverified);
        DrawTextClipped(draw, FontAt(12.5f), 12.5f, continuityBody.min, continuityBody.width(),
                        ColorTextSecondary(), summary, true);
        float cy = continuityBody.min.y + 24.0f;
        if (s.continuity.issues.empty()) {
            // 三态里"没报 issue"不等于"通过"——数据不足时 RunContinuityChecks 记 unverified。
            const theme::Tone tone = s.continuity.unverified > 0 ? theme::Tone::Warn : theme::Tone::Ok;
            const char* label = s.continuity.unverified > 0 ? "无不一致，但有数据不足项" : "无不一致";
            StatusDot(draw, ImVec2(continuityBody.min.x + 4.0f, cy + 6.0f), tone, false);
            draw->AddText(FontAt(12.5f), 12.5f, ImVec2(continuityBody.min.x + 16.0f, cy),
                          ColorTextSecondary(), label, label + std::strlen(label));
            cy += 22.0f;
        }
        // ⚠️ 下面两个循环原来都是 `if (cy + 22.0f > continuityBody.max.y) { break; }`。
        //    卡片高度是按**视口**定的固定槽位，画不下就**静默丢掉**后面的 issue 与 note ——
        //    用户看到的是「就这么多」，界面上没有任何提示说还有第 N+1 条。列表被截断且不
        //    提示 = 界面在骗人（V8 的 issue / notes 都没有条数上限，数据一多必然复现）。
        //
        //    改成：列表本体自己滚（ScrollRegion），画**全部**。卡片标题、汇总行、
        //    「无不一致」结论留在滚动区**外面** —— 滚的只有下面这些行。
        if (!s.continuity.issues.empty() || !s.continuity.notes.empty()) {
            ScrollRegion issueScroll("novel-continuity-issues",
                                     Rect{continuityBody.min.x, cy, continuityBody.max.x,
                                          continuityBody.max.y});
            if (issueScroll) {
                // ⚠️ 内容必须画在 child **自己的** draw list 上：BeginChild 的裁剪矩形
                //    只写进它自己那条 list，画到外层 list 上裁剪**完全无效**（内容会盖住
                //    上层而不是消失）。见 Scroll.h。
                ImDrawList* ldraw = issueScroll.drawList();
                const Rect inner = issueScroll.content();
                float ly = inner.min.y;
                for (const novelcore::ContinuityIssue& issue : s.continuity.issues) {
                    const theme::Tone tone =
                        issue.severity == "high" ? theme::Tone::Danger : theme::Tone::Warn;
                    StatusDot(ldraw, ImVec2(inner.min.x + 4.0f, ly + 6.0f), tone, false);
                    // 镜对换成镜码（`镜 #128→#131：` → `镜 S004 → S005：`）：这条 issue 说的
                    // 就是「这两镜之间」出了什么事，写库主键的话没人对得上号。解析不出来就
                    // 原样显示 detail —— 见 ShotPairToCodes 的注释。
                    const std::string label = issue.code + " " + ShotPairToCodes(issue.detail, s.shots);
                    DrawTextClipped(ldraw, FontAt(12.5f), 12.5f, ImVec2(inner.min.x + 16.0f, ly),
                                    inner.width() - 16.0f, ColorTextSecondary(), label, true);
                    ly += 22.0f;
                }
                // unverified 的原因也照实列（它不是失败，但必须看得见）。
                for (const std::string& note : s.continuity.notes) {
                    DrawTextClipped(ldraw, FontAt(11.5f), 11.5f, ImVec2(inner.min.x, ly),
                                    inner.width(), ColorTextMuted(), note, true);
                    ly += 20.0f;
                }
                // ⚠️ 不调这一行等于没修：自绘内容全程不给 ImGui 提交 item，ContentSize
                //    恒为 0 ⇒ ScrollMaxY 恒为 0 ⇒ 滚轮怎么转都停在原地（本仓已踩过两次）。
                issueScroll.setContentHeight(ly - inner.min.y);
            }
        }
    }

    // ---- 故事板时间线：真实镜头卡 ----
    const Rect timeline{content.min.x, timelineTop, content.max.x, contentBottom};
    DrawShadowed(draw, timeline.min, timeline.max, 10.0f, ColorPanel(), ColorLineSubtle(), 1.0f);
    // 本页最深就是这条时间轴（两张卡的下边界是 timelineTop - kGap，更浅）。
    // 自报它的高度，工作区的滚动范围从此跟着视口走 —— 不再是「滚到 2400 才看得见
    // 时间轴、而那 1400px 里什么都没有」（与总控页同一处治理，注释见那里）。
    pages::SetPageContentHeight(contentBottom - area.min.y + kGap);
    if (s.shots.empty()) {
        DrawTextClipped(draw, FontAt(12.5f), 12.5f,
                        ImVec2(timeline.min.x + 14.0f, timeline.min.y + 14.0f), timeline.width() - 28.0f,
                        ColorTextMuted(), "本章尚无镜头，时间线为空。", true);
        return;
    }
    // 时长条按**本章最长镜头**归一（旧版写死 60% = 假装 6s）。
    double longest = 0.0;
    for (const BookShot& row : s.shots) {
        longest = std::max(longest, row.durationSec);
    }
    float x = timeline.min.x + 10.0f;
    for (int i = 0; i < static_cast<int>(s.shots.size()); ++i) {
        if (x + 96.0f > timeline.max.x) {
            break;  // 横向超出视口就不画（外壳的 ScrollRegion 负责滚动）
        }
        const BookShot& row = s.shots[static_cast<std::size_t>(i)];
        const Rect card{x, timeline.min.y + 10.0f, x + 96.0f, timeline.max.y - 10.0f};
        const bool on = (i == s.selectedShot);
        DrawRoundRect(draw, card.min, card.max, 8.0f, ColorFillMuted(),
                      on ? ColorAccent() : ColorLineNormal(), on ? 1.5f : 1.0f);
        if (on) {
            DrawRoundRect(draw, card.min - ImVec2(2, 2), card.max + ImVec2(2, 2), 10.0f, 0,
                          ColorOf(theme::CurrentDerived().accentDim), 2.0f);
        }
        // ⚠️ 缩略图：真实分镜图要走 gpu 纹理链路（出图页才接），这里**不画假缩略图** ——
        // 显示"待出图"占位，与 webui 自己在没有图时的状态一致。
        const Rect thumb{card.min.x + 6.0f, card.min.y + 6.0f, card.max.x - 6.0f, card.min.y + 66.0f};
        DrawRoundRect(draw, thumb.min, thumb.max, 6.0f, ColorFillMuted(), ColorLineSubtle(), 1.0f);
        DrawIconCentered(draw, "image", thumb.center(), 20.0f, ColorTextMuted());
        // 镜码与侧栏树 / 检查器共用 WorkspacePages.h 的 ShotCode —— 同一个镜三处同名。
        const std::string code = ShotCode(row.ord);
        draw->AddText(MonoAt(11.5f), 11.5f, ImVec2(card.min.x + 8.0f, card.min.y + 70.0f),
                      ColorAccent(), code.data(), code.data() + code.size());
        // 时长条：按真实 durationSec / 本章最长。没有 duration 就画一条灰槽，不画 60%。
        const float barY = card.max.y - 10.0f;
        const float barMaxW = card.width() - 12.0f;
        const float ratio =
            (longest > 0.0 && row.durationSec > 0.0)
                ? std::clamp(static_cast<float>(row.durationSec / longest), 0.08f, 1.0f)
                : 0.0f;
        DrawRoundRect(draw, ImVec2(card.min.x + 6.0f, barY),
                      ImVec2(card.min.x + 6.0f + barMaxW, barY + 4.0f), 2.0f, ColorFillSelected());
        if (ratio > 0.0f) {
            DrawRoundRect(draw, ImVec2(card.min.x + 6.0f, barY),
                          ImVec2(card.min.x + 6.0f + barMaxW * ratio, barY + 4.0f), 2.0f, ColorAccent());
        }
        if (Clicked(card, "sb-tl-" + std::to_string(i))) {
            // 走 SelectBookShot 而不是就地写 `s.selectedShot` + RebuildBookSide：
            // 那两行本来是 SelectBookShot 的复制品，少了夹取边界检查，再多一份就要
            // 靠「记得同步」维持。选中态只有一条路径。
            SelectBookShot(i);
        }
        x += 104.0f;
    }
}

} // namespace shine::pages
