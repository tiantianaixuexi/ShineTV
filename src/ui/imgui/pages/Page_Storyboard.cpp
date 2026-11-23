// shine::pages —— P5.4 分镜（storyboard）
//
// 全部字段来自 novel.db：章/场/镜（NovelGraph::ListChapters/ListScenes +
// NovelVisual::ListShotsByChapter）、V1–V7 阶段产物（ListStageArtifacts）、
// V8 连续性（RunContinuityChecks 的真实三态）。旧版的 "S012 · 转身"、6.0s、
// 6 条 "C1 服装一致"、8 张 S010–S017 缩略图全是写死的，已全部删除。
//
// 模块化（2026-10-01）：
//   * 常量（文案 / 几何）集中在文件顶部 —— 「待出图」这类字面量不再埋在分支里；
//   * 小组件 `PendingThumb` 单独成函数（缩略图的「还没出图」占位栈）；
//   * 所有会显示的字符串走 RefreshTexts 的**数据指纹缓存**（kit/Hash.h）：
//     ImGui 每帧绘制，Draw 里不再现拼 `std::to_string` / vector<pair>；
//   * 格式化统一走 kit::Fmt（fmt），浮点转字符串走 kit::Fixed（zmij，
//     一位小数 = 设计稿 mock 数据的口径）。
#include "ui/imgui/pages/BookData.h"

#include "ui/imgui/pages/PageCommon.h"

#include "ui/imgui/kit/Fmt.h"
#include "ui/imgui/kit/Hash.h"
#include "ui/imgui/kit/Image.h"  // 逐镜成片图（真图 / Art / 「待出图」三分支）
#include "ui/imgui/kit/Scroll.h"
#include "ui/imgui/kit/Text.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace shine::pages {

using namespace shine::kit;

namespace {

// ==================== 文案常量（集中文件顶部，不埋在分支里）====================

// 时间线卡「还没出图」的占位小字（Storyboard.jsx:165 同款）。
static constexpr char kPending[] = "待出图";
// 详情卡里 timeline_json 那一栏的栏头。
static constexpr char kBeatLabel[] = "BEAT 时间轴（shots.timeline_json）";
// 时间线卡头右侧的 extra 提示（Storyboard.jsx:145）。
static constexpr char kTlHint[] = "按住卡片拖动 · 时长条按比例";
// timeline_json 没写时详情卡里的诚实占位。
static constexpr char kBeatEmpty[] = "这一镜没有 timeline_json。";
// V1–V8 阶段条的段名（StageNode.name 只显示这一段，静态常量省去每帧 8 次拼接）。
static constexpr char kStageNames[8][4] = {"V1", "V2", "V3", "V4", "V5", "V6", "V7", "V8"};

// ==================== 几何常量（逐项来自 CSS，出处写在行尾）====================

constexpr float kTlCardW = 128.0f;       // .tl-card width（views.css:959）
constexpr float kTlCardR = 10.0f;        // --r-md（tokens.css:11）
constexpr float kTlGap = 10.0f;          // .timeline gap（views.css:953）
constexpr float kTlThumbH = 72.0f;       // .tthumb height（views.css:977）
constexpr float kTlInfoPadY = 7.0f;      // .tinfo padding 7px 9px 9px（views.css:980）
constexpr float kTlInfoPadX = 9.0f;
constexpr float kTlInfoPadB = 9.0f;
constexpr float kTlCodeH = 11.0f * 1.6f;      // .tcode mono 11/700 → 17.6
constexpr float kTlActionGap = 2.0f;          // .taction margin-top（views.css:991）
constexpr float kTlActionH = 11.5f * 1.6f;    // .taction 11.5 → 18.4
constexpr float kTlDurGap = 6.0f;             // .tdur margin-top（views.css:997）
constexpr float kTlDurH = 10.5f * 1.6f;       // .tdur 行盒 10.5 → 16.8（里面的 .prog.thin 只有 4px）
// 卡的外框是 border-box：上下各 1px 边 + 缩略图 + `.tinfo` 的三段
constexpr float kTlCardH = 2.0f + kTlThumbH + kTlInfoPadY + kTlCodeH + kTlActionGap +
                           kTlActionH + kTlDurGap + kTlDurH + kTlInfoPadB;  // = 150.8
constexpr float kTlStripPadX = 2.0f;     // .timeline padding 左右 2
constexpr float kTlStripPadTop = 8.0f;   // .timeline padding 上 8
constexpr float kTlStripPadBottom = 12.0f;  // .timeline padding 下 12
constexpr float kCardHeadH = 46.6f;      // kit::Card 的 `.card-h`
constexpr float kCardBodyPad = 16.0f;    // `.card-b { padding: 16px }`
constexpr float kTimelinePanelH = kCardHeadH + kCardBodyPad + kTlStripPadTop + kTlCardH +
                                  kTlStripPadBottom + kCardBodyPad;  // = 249.4
constexpr float kStripH = kTlStripPadTop + kTlCardH + kTlStripPadBottom;
// 「待出图」占位栈的三段（图标 20 + 间隙 4 + 12px 字的行盒）。
constexpr float kPendingIcon = 20.0f;
constexpr float kPendingGap = 4.0f;
constexpr float kPendingLineH = 12.0f * 1.6f;  // = 19.2
constexpr float kPendingStackH = kPendingIcon + kPendingGap + kPendingLineH;

// ---- 小组件：缩略图里的「待出图」占位栈 ----
//
// 图标 + 一行 12px 小字，整体在缩略图内居中（Storyboard.jsx:163-166 的 else 分支）。
// 原先是 Draw 内层循环里的一段内联代码 —— 抽出来后循环体只剩「这一格是什么状态」
// 的三分支判断。
void PendingThumb(ImDrawList* draw, Rect thumb) {
    const float stackTop = thumb.center().y - kPendingStackH * 0.5f;
    DrawIconCentered(draw, "image", ImVec2(thumb.center().x, stackTop + kPendingIcon * 0.5f),
                     kPendingIcon, ColorTextMuted());
    const float textTop = stackTop + kPendingIcon + kPendingGap;
    // R3 收编：`.tiny` = 12px（kTiny 档）。量宽与画共用同一份 TextStyle ——
    // 改前是「量用 LabelWidth(font, 12.0f) / 画用 AddText(font, 12.0f)」两处各写一遍
    // 同一个数字，改一档漏另一档就画在 B 档量在 A 档（Text.h 的像素不变契约：逐位等价）。
    const TextStyle tinyStyle{.size = theme::font::kTiny, .color = ColorTextMuted()};
    const std::string_view pendingText(kPending, sizeof(kPending) - 1);
    const float tinyW = TextWidth(pendingText, tinyStyle);
    Text(draw,
         ImVec2(thumb.center().x - tinyW * 0.5f,
                CenterY(textTop + kPendingLineH * 0.5f, tinyStyle)),
         pendingText, tinyStyle);
}

} // namespace

// ---- 显示文本缓存：指纹没变就整帧复用 ----
//
// 指纹覆盖**所有被显示**的字段（数值 + 字符串的 FNV）。重建是 O(显示文本总量)，
// 只在数据真变时发生；指纹本身是 O(同样总量) 的纯哈希，无分配 —— 两头都比
// 「每帧拼一遍字符串」便宜，而且**正确性不靠「记得在哪里刷新」**：任何字段一变
// 指纹就变，下一帧自然重建。
void StoryboardPage::RefreshTexts() {
    BookState& s = Book();
    const BookChapter* chapter = s.chapter();
    const BookShot* shot = s.shot();

    std::uint64_t key = HashMix(0, s.shots.size());
    key = HashMix(key, static_cast<std::uint64_t>(s.selectedShot));
    key = HashMix(key, Fnv1a(s.dbPath));
    if (chapter != nullptr) {
        key = HashMix(key, static_cast<std::uint64_t>(chapter->ord));
        key = HashMix(key, Fnv1a(chapter->title));
    }
    if (shot != nullptr) {
        key = HashMix(key, static_cast<std::uint64_t>(shot->id));
        key = HashMix(key, static_cast<std::uint64_t>(shot->sceneOrd));
        key = HashMix(key, static_cast<std::uint64_t>(shot->ord));
        key = HashMix(key, static_cast<std::uint64_t>(shot->cameraId));
        key = HashMix(key, static_cast<std::uint64_t>(shot->lightingId));
        key = HashMix(key, Fnv1a(shot->action));
        key = HashMix(key, Fnv1a(shot->expression));
        key = HashMix(key, Fnv1a(shot->mood));
        key = HashMix(key, Fnv1a(shot->narration));
        key = HashMix(key, Fnv1a(shot->durationNote));
        key = HashMix(key, Fnv1a(shot->canonStatus));
        key = HashMix(key, shot->durationSec);
    }
    for (const BookShot& row : s.shots) {
        key = HashMix(key, static_cast<std::uint64_t>(row.ord));
        key = HashMix(key, row.durationSec);
    }
    key = HashMix(key, s.continuity.ran ? 1ull : 0ull);
    key = HashMix(key, static_cast<std::uint64_t>(s.continuity.shotsSeen));
    key = HashMix(key, static_cast<std::uint64_t>(s.continuity.pairsChecked));
    key = HashMix(key, static_cast<std::uint64_t>(s.continuity.failed));
    key = HashMix(key, static_cast<std::uint64_t>(s.continuity.unverified));
    for (const novelcore::ContinuityIssue& issue : s.continuity.issues) {
        key = HashMix(key, Fnv1a(issue.code));
        key = HashMix(key, Fnv1a(issue.detail));
    }
    for (const std::string& note : s.continuity.notes) {
        key = HashMix(key, Fnv1a(note));
    }
    if (key == textKey_) {
        return;
    }
    textKey_ = key;

    // ---- 页眉标题 / 副行：没有选中镜头时说清是哪种空态，不用占位镜头号 ----
    // 副行恒为"章 + 已落库镜头数 + 库路径"（全是真实计数），标题随选中镜头变。
    title_ = "分镜";
    subtitle_ = "Scene → Sequence → Shot";
    if (chapter != nullptr) {
        title_ = chapter->title.empty() ? kit::Fmt("第 {} 章", chapter->ord)
                                        : kit::Fmt("第 {} 章 · {}", chapter->ord, chapter->title);
        subtitle_ = kit::Fmt("已落库镜头 {} 个 · {}", s.shots.size(), s.dbPath);
    }
    if (shot != nullptr) {
        title_ = kit::Fmt("{} · {}", ShotCode(shot->ord),
                          shot->action.empty() ? kDash : shot->action);
    }
    // 状态标签：取镜头的 canon_status（shots 表的规范值），映射不到就原样显示。
    canonLabel_ = shot != nullptr && !shot->canonStatus.empty() ? shot->canonStatus
                                                                : std::string(kDash);

    // ---- 「镜头详情」KV 表（9 行，行序即显示序）----
    detailKv_ = {};
    if (shot != nullptr) {
        // 时长：优先 timeline_json.duration_s（秒，一位小数），退回 duration_note
        // 原文，都没有 → kDash。DurationText 是全树唯一的一份（见 PageCommon.h）。
        std::string duration = std::string(kDash);
        if (shot->durationSec > 0.0) {
            duration = DurationText(shot->durationSec);
        } else if (!shot->durationNote.empty()) {
            duration = shot->durationNote;
        }
        detailKv_[0] = {"镜头", kit::Fmt("#{}", shot->id)};
        detailKv_[1] = {"场 / 序", kit::Fmt("第 {} 场 · 第 {} 镜", shot->sceneOrd, shot->ord)};
        detailKv_[2] = {"动作", shot->action.empty() ? std::string(kDash) : shot->action};
        detailKv_[3] = {"表演", shot->expression.empty() ? std::string(kDash) : shot->expression};
        detailKv_[4] = {"机位", OrDashId(shot->cameraId)};
        detailKv_[5] = {"光线", OrDashId(shot->lightingId)};
        detailKv_[6] = {"时长", std::move(duration)};
        detailKv_[7] = {"情绪", shot->mood.empty() ? std::string(kDash) : shot->mood};
        detailKv_[8] = {"旁白", shot->narration.empty() ? std::string(kDash) : shot->narration};
    }

    // ---- 时间线逐镜两段文案 + 命中 id ----
    cells_.clear();
    cells_.reserve(s.shots.size());
    for (std::size_t i = 0; i < s.shots.size(); ++i) {
        const BookShot& row = s.shots[i];
        cells_.push_back(ShotCell{ShotCode(row.ord), DurationText(row.durationSec),
                                  kit::Fmt("sb-tl-{}", i)});
    }

    // ---- 连续性汇总行 + issue 行（镜对在这里换算成镜码）----
    summaryText_ = kit::Fmt("镜 {} · 比对 {} 对 · 失败 {} · 数据不足 {}", s.continuity.shotsSeen,
                            s.continuity.pairsChecked, s.continuity.failed,
                            s.continuity.unverified);
    issueLabels_.clear();
    issueLabels_.reserve(s.continuity.issues.size());
    for (const novelcore::ContinuityIssue& issue : s.continuity.issues) {
        // 镜对换成镜码（`镜 #128→#131：` → `镜 S004 → S005：`）：这条 issue 说的
        // 就是「这两镜之间」出了什么事，写库主键的话没人对得上号。解析不出来就
        // 原样显示 detail —— 见 ShotPairToCodes 的注释。
        issueLabels_.push_back(kit::Fmt("{} {}", issue.code, ShotPairToCodes(issue.detail, s.shots)));
    }
}

void StoryboardPage::Draw(Rect area, ImDrawList* draw) {
    BookState& s = Book();
    const BookShot* shot = s.shot();

    // 本帧要显示的所有文案先备好（指纹没变 = 纯复用上一帧的字符串）。
    RefreshTexts();

    Rect right;
    Rect content = ViewHeader(area, draw, "clapper", title_.c_str(), subtitle_.c_str(), &right);

    const float tagW = TagWidth(canonLabel_, false, true);
    Tag(draw, RectAt(right.min.x, right.min.y + 5.0f, tagW, 20.0f), canonLabel_, theme::Tone::Idle,
        false, true);

    ButtonSpec run;
    run.variant = ButtonVariant::Primary;
    const char* runLabel = "重新读取";
    const float runW = ButtonWidth(
        ButtonSize::Medium, 0.0f,
        TextWidth(runLabel, TextStyle{.size = theme::font::kBody, .bold = true}));
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
    vs.reserve(8);
    for (int i = 0; i < 8; ++i) {
        vs.push_back(StageNode{kStageNames[i], "",
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
    const float contentBottom = std::max(ViewportBottom(area), detailTop + 310.0f + kTimelinePanelH);
    // ---- 时间线面板的几何：逐项来自 CSS，**不再是写死的 118**（常量见文件顶部）----
    //
    // 设计稿这一块是一张 `<Card title="故事板时间线 · 拖拽重排" icon="film" extra=…>`
    // （Storyboard.jsx:145），高度由内容撑开 ⇒ 面板 ≈ 249.4。
    //
    // ⚠️ 原来写死 `contentBottom - 118`，而 118 < 头(46.6) + 上下 padding(32) = 78.6
    //    加上时间线本身，实际可用的卡片区不足 40px —— 于是卡片被**拉伸**填满
    //    （98px 高），缩略图也只能内缩 6px 画 60px。三个数字互相耦合，所以一起改。
    const float timelineTop =
        std::max(contentBottom - kTimelinePanelH, std::min(content.min.y + 120.0f, contentBottom));
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
        // KV 表来自缓存（RefreshTexts），每帧只把指针递进来 —— 不再每帧构造
        // initializer_list<pair<string,string>>。
        KeyValues(draw, detailBody, detailKv_.data(), detailKv_.size());
        // Beat 时间轴：直接显示库里的 timeline_json 原文，不编一份示例 JSON。
        const float ty = detailBody.min.y + 190.0f;
        Text(draw, ImVec2(detailBody.min.x, ty), kBeatLabel,
             TextStyle{.size = theme::font::kLabel, .bold = true, .color = ColorTextMuted()});
        DrawRoundRect(draw, ImVec2(detailBody.min.x, ty + 16.0f),
                      ImVec2(detailBody.max.x, ty + 104.0f), 6.0f, ColorFillMuted(), ColorLineNormal(),
                      1.0f);
        if (shot->timelineJson.empty() || shot->timelineJson == "{}") {
            TextClipped(draw, ImVec2(detailBody.min.x + 10.0f, ty + 40.0f),
                        detailBody.width() - 20.0f, kBeatEmpty,
                        TextStyle{.size = theme::font::kHint, .color = ColorTextMuted()}, true);
        } else {
            TextClipped(draw, ImVec2(detailBody.min.x + 10.0f, ty + 26.0f),
                        detailBody.width() - 20.0f, shot->timelineJson,
                        TextStyle{.size = theme::font::kHint, .mono = true,
                                  .color = ColorTextSecondary()},
                        true);
        }
        // 空间 / 转场 / 走位**无专列**（V9 只存盘到 storyboard.json）—— 如实说明，不编值。
        TextClipped(draw, ImVec2(detailBody.min.x, ty + 112.0f), detailBody.width(),
                    "空间 / 转场 / 走位没有库字段，只在 work/ch<NNN>/storyboard.json",
                    TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()}, true);
    }

    // ---- 连续性 C1–C12：真实三态 ----
    Rect continuityBody = Card(draw, continuity, "连续性 C1-C12", "check", false, false);
    if (!s.continuity.ran) {
        Empty(draw, continuityBody, "check", "连续性未校验", "V8 需要该章的镜头数据。");
    } else {
        // 汇总行用真实计数（缓存：RefreshTexts）。
        TextClipped(draw, continuityBody.min, continuityBody.width(), summaryText_,
                    TextStyle{.size = theme::font::kHint, .color = ColorTextSecondary()}, true);
        float cy = continuityBody.min.y + 24.0f;
        if (s.continuity.issues.empty()) {
            // 三态里"没报 issue"不等于"通过"——数据不足时 RunContinuityChecks 记 unverified。
            const theme::Tone tone = s.continuity.unverified > 0 ? theme::Tone::Warn : theme::Tone::Ok;
            const char* label = s.continuity.unverified > 0 ? "无不一致，但有数据不足项" : "无不一致";
            StatusDot(draw, ImVec2(continuityBody.min.x + 4.0f, cy + 6.0f), tone, false);
            Text(draw, ImVec2(continuityBody.min.x + 16.0f, cy), label,
                 TextStyle{.size = theme::font::kHint, .color = ColorTextSecondary()});
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
                for (std::size_t i = 0; i < issueLabels_.size(); ++i) {
                    const theme::Tone tone =
                        s.continuity.issues[i].severity == "high" ? theme::Tone::Danger
                                                                  : theme::Tone::Warn;
                    StatusDot(ldraw, ImVec2(inner.min.x + 4.0f, ly + 6.0f), tone, false);
                    TextClipped(ldraw, ImVec2(inner.min.x + 16.0f, ly), inner.width() - 16.0f,
                                issueLabels_[i],
                                TextStyle{.size = theme::font::kHint, .color = ColorTextSecondary()},
                                true);
                    ly += 22.0f;
                }
                // unverified 的原因也照实列（它不是失败，但必须看得见）。
                for (const std::string& note : s.continuity.notes) {
                    TextClipped(ldraw, ImVec2(inner.min.x, ly), inner.width(), note,
                                TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()},
                                true);
                    ly += 20.0f;
                }
                // ⚠️ 不调这一行等于没修：自绘内容全程不给 ImGui 提交 item，ContentSize
                //    恒为 0 ⇒ ScrollMaxY 恒为 0 ⇒ 滚轮怎么转都停在原地（本仓已踩过两次）。
                issueScroll.setContentHeight(ly - inner.min.y);
            }
        }
    }

    // ---- 故事板时间线：一张真 `<Card>`，里面是 `.timeline` 横条 ----
    //
    // ⚠️ 原来这里只画了一块 `DrawShadowed` 面板，**没有表头** —— 设计稿的标题
    //    「故事板时间线 · 拖拽重排」与右侧提示「按住卡片拖动 · 时长条按比例」
    //    （Storyboard.jsx:145 的 title / icon / extra）整组都没画，
    //    高度也是写死的 118。改用 `kit::Card` 后，三件事一次对齐：
    //    表头由 Card 画、正文区由 Card 返回、面板高度按内容算。
    const Rect timeline{content.min.x, timelineTop, content.max.x, contentBottom};
    Rect tlBody = Card(draw, timeline, "故事板时间线 · 拖拽重排", "film", false, false);
    {
        // 头右侧的 `extra`（`.tiny dim` = 12px muted），右对齐在标题行里。
        const TextStyle hintStyle{.size = theme::font::kTiny, .color = ColorTextMuted()};
        const std::string_view hintText(kTlHint, sizeof(kTlHint) - 1);
        const float hintW = TextWidth(hintText, hintStyle);
        const Rect headRow = CardHeaderRow(timeline, "故事板时间线 · 拖拽重排", "film");
        Text(draw, ImVec2(headRow.max.x - hintW, CenterY(headRow.center().y, hintStyle)), hintText,
             hintStyle);
    }
    // 本页最深就是这条时间轴（两张卡的下边界是 timelineTop - kGap，更浅）。
    // 自报它的高度，工作区的滚动范围从此跟着视口走 —— 不再是「滚到 2400 才看得见
    // 时间轴、而那 1400px 里什么都没有」（与总控页同一处治理，注释见那里）。
    pages::SetPageContentHeight(contentBottom - area.min.y + kGap);
    if (s.shots.empty()) {
        TextClipped(draw, ImVec2(tlBody.min.x + kTlStripPadX, tlBody.min.y + kTlStripPadTop),
                    tlBody.width() - 2.0f * kTlStripPadX, "本章尚无镜头，时间线为空。",
                    TextStyle{.size = theme::font::kHint, .color = ColorTextMuted()}, true);
        return;
    }
    // 时长条按**本章最长镜头**归一（旧版写死 60% = 假装 6s）。
    double longest = 0.0;
    for (const BookShot& row : s.shots) {
        longest = std::max(longest, row.durationSec);
    }
    // `.timeline { padding: 8px 2px 12px }`（views.css:955）——
    // ⚠️ 早先这里从面板左沿直接 +10 起画、卡片高度拉伸到面板底，那是「面板里没有
    //    正文区概念」时的写法。现在有了 `tlBody`（Card 的正文盒），按 CSS 的
    //    padding 与 .tl-card 的固定高度来。
    // `.timeline { padding: 8px 2px 12px }`（views.css:955）+ `overflow-x: auto`（:954）。
    //
    // ⚠️ 原来是「放不下就 break」：本章镜多于视口能放下的数量时，后面的镜
    //    **不画也不提示**，界面上看不出还剩 N 个 —— 列表被截断且不提示 = 界面在骗人。
    //    现在走 `ScrollRegion(horizontalScroll=true)`；**必须**配 `setContentWidth`，
    //    只开那个 flag 会得到「滚条不出现 + 内容被裁且到不了」，比原来更糟（见 Scroll.h）。
    //
    // 区域的竖向边界要**含** `.timeline` 的上下 padding：选中态的 3px 扩展环画在卡片
    // 外框之外，贴着卡片画会被 child 裁掉（浏览器那边靠 padding 的 8px 让位）。
    const float stripLeft = tlBody.min.x + kTlStripPadX;
    const float stripRight = tlBody.max.x - kTlStripPadX;
    kit::ScrollRegion strip("sb-timeline-strip", Rect{stripLeft, tlBody.min.y, stripRight,
                                                     tlBody.min.y + kStripH},
                            /*borders=*/false, /*horizontalScroll=*/true);
    if (strip) {
        // ⚠️ 内容必须画在 child **自己的** draw list 上，否则裁剪完全无效
        //    （BeginChild 的裁剪矩形只写进它自己那条 list 的 CmdBuffer，见 Scroll.h）。
        ImDrawList* sdraw = strip.drawList();
        const Rect inner = strip.content();
        const float stripTop = inner.min.y + kTlStripPadTop;
        float x = inner.min.x;
        for (int i = 0; i < static_cast<int>(s.shots.size()); ++i) {
            const BookShot& row = s.shots[static_cast<std::size_t>(i)];
            const ShotCell& cell = cells_[static_cast<std::size_t>(i)];
            // `.tl-card` 高度由内容定（缩略图 72 + `.tinfo` 三段 + 上下 1px 边框 = 150.8），
            // **不是**拉伸填满面板 —— 那是面板高度不够时的连带写法。
            const Rect card{x, stripTop, x + kTlCardW, stripTop + kTlCardH};
            const bool on = (i == s.selectedShot);
            // 一帧只命中**一次**，hovered / clicked 都从它取（同时注册两次会让
            // hover 恒假 —— 这条在全树踩过好几轮，见 kit/Widget_Core.h 的自检）。
            const kit::Hit tlHit = HitTest(card, cell.hitId);
            // `.tl-card:hover { border-color: accent-glow; box-shadow: shadow-1; translateY(-2px) }`
            //   （views.css:967-971）
            // `.tl-card.on` 走的是 **inline style**（Storyboard.jsx:154）：
            //   `border-color: accent-glow` + `box-shadow: 0 0 0 3px var(--accent-dim)`。
            //   两点容易画错：
            //   ① `0 0 0 3px` 是**3px 扩展环**（blur 0），不是「2px 描边」；
            //      spread 会把外圆角一并放大 3px。
            //   ② inline 的 box-shadow **盖掉** `:hover` 那条 ⇒ 选中项 hover 时**没有** shadow-1。
            //      而 `transform` 不在 inline 里 ⇒ 选中项照样上浮。
            //   原来画的是「accent 色 1.5px 边框 + 2px 环」，两者都不对（颜色与厚度都错）。
            const float lift = kit::CardHoverLift(tlHit.hovered, true);
            const Rect ink{card.min.x, card.min.y + lift, card.max.x, card.max.y + lift};
            DrawShadowed(sdraw, ink.min, ink.max, kTlCardR, ColorFillMuted(),
                         (on || tlHit.hovered) ? ColorAccentGlow() : ColorLineNormal(), 1.0f,
                         (!on && tlHit.hovered) ? theme::ShadowTier::Card : theme::ShadowTier::None);
            if (on) {
                // 环的**路径**在 border-box 外 1.5px、线宽 3 ⇒ 实际铺满外扩 0..3px。
                // （先把路径放到 +3 再画 3px 线，会变成外扩 1.5..4.5，外侧多出 1.5px。）
                DrawRoundRect(sdraw, ink.min - ImVec2(1.5f, 1.5f), ink.max + ImVec2(1.5f, 1.5f),
                              kTlCardR + 1.5f, 0, ColorOf(theme::CurrentDerived().accentDim), 3.0f);
            }
            // ---- `.tthumb`：满幅、72 高、**无自己的底色与边框** ----
            //
            // CSS 里 `.tthumb` 只有一个 `height: 72px`（views.css:976-978），底色来自
            // `.tl-card { background: var(--fill-muted) }`；`overflow:hidden` 负责把
            // 上面两个圆角裁掉。原来这里画的是「内缩 6px 的 60px 块 + 自己的边与底」，
            // 那是面板高度不够、卡片被拉伸时的连带写法。
            // ⚠️ 这版 ImGui 没有 push clip path，圆角裁不掉（顶角会有一点方）——
            //    与 `HubCard` 封面的同一个降级，见那里的注释。
            const float innerL = ink.min.x + 1.0f;  // 让开卡片自己的 1px 边框
            const float innerR = ink.max.x - 1.0f;
            const Rect thumb{innerL, ink.min.y + 1.0f, innerR, ink.min.y + 1.0f + kTlThumbH};
            // ---- 这一镜的成片图（三条分支，别合并）----
            //
            // 来源：`generated_images` 的 `source_kind='shot'` + `source_id=shots.id`。
            // 业务层提交**单镜出图**时就把镜 id 记在这对列上
            // （`src/novel/NovelImageStore.cpp`：`if (in.shot_id > 0) { source_kind="shot" }`），
            // 表上还专门建了 `idx_gimg_source(source_kind, source_id)`。
            //
            // ⚠️ 这里原来写着「`BookShotView` 没有 status 字段 ⇒ 数据层给不出
            //    `s.status === 'done'` 这个判断，就不臆断成已出图去画插图」——
            //    **那个归因是错的**：判断与图**是同一个查询**（有没有
            //    `source_kind='shot'` 且 `rel_path<>''` 的行），而且那对列一直就在库里。
            //    当时的结论对（不臆断），但因此把整块留成了永久占位。
            //
            // 三条分支（按设计稿 Storyboard.jsx:163-166 的状态分支）：
            //   ① 有真图且解码成功        → 画它（`.tthumb` 满幅 cover）
            //   ② 登记过这一镜的图但解不出 → 画设计稿那张 `Art`（`done` 那一支）
            //   ③ 压根没登记（还没出图）   → 保持「待出图」那一格
            // ⚠️ ②③ 必须分开：合成一条就分不出「出过图但文件坏了」与「还没出图」——
            //    而这两种状态在界面上该是不同的东西（R8 那一族栽过一模一样的跟头）。
            const ImageSlot frame = RequestImage(AssetImagePath(s.root, row.imageRelPath));
            if (frame.ready()) {
                DrawImage(sdraw, thumb, frame, 0.0f, ImageFit::Cover);
            } else if (!row.imageRelPath.empty()) {
                // seed 用镜序：设计稿的 `s.id+4` 是 mock 字段，库里没有对应列 ——
                // 按镜序派生是确定的（同一镜永远同一张），不编随机数。
                Art(sdraw, thumb, row.ord, /*cover=*/true);
            } else {
                PendingThumb(sdraw, thumb);
            }
            // ---- `.tinfo { padding: 7px 9px 9px }`：镜码 / 动作 / 时长 三段 ----
            const float infoX = innerL + kTlInfoPadX;
            const float infoW = (innerR - kTlInfoPadX) - infoX;
            float ty = thumb.max.y + kTlInfoPadY;
            // `.tcode`：--font-mono 11px / **700** / accent
            {
                // 镜码与侧栏树 / 检查器共用 WorkspacePages.h 的 ShotCode —— 同一个镜三处同名。
                const TextStyle codeStyle{.size = theme::font::kMicro, .mono = true,
                                          .color = ColorAccent()};
                Text(sdraw, ImVec2(infoX, CenterY(ty + kTlCodeH * 0.5f, codeStyle)), cell.code,
                     codeStyle);
                ty += kTlCodeH + kTlActionGap;
            }
            // `.taction`：11.5px / text-secondary / 单行省略
            const TextStyle actionStyle{.size = theme::font::kLabel, .color = ColorTextSecondary()};
            TextClipped(sdraw, ImVec2(infoX, CenterY(ty + kTlActionH * 0.5f, actionStyle)), infoW,
                        row.action, actionStyle);
            ty += kTlActionH + kTlDurGap;
            // `.tdur`：flex / align-center / gap 6 / 10.5px muted ⇒ 左侧 `.prog.thin` 4px + 右侧时长
            {
                const TextStyle durStyle{.size = theme::font::kSmall, .color = ColorTextMuted()};
                const float durW = TextWidth(cell.durText, durStyle);
                const float rowCy = ty + kTlDurH * 0.5f;  // `.tdur` 行盒中心
                const float barW = std::max(30.0f, infoW - kTlDurGap - durW - kTlDurGap);  // min-width 30
                // `.prog.thin` 高 4，在行盒里垂直居中；比例按**本章最长镜头**归一
                // （没有 duration 就画一条空槽，不画 60%）。
                const float barY = rowCy - 2.0f;
                const float ratio =
                    (longest > 0.0 && row.durationSec > 0.0)
                        ? std::clamp(static_cast<float>(row.durationSec / longest), 0.0f, 1.0f)
                        : 0.0f;
                Progress(sdraw, Rect{infoX, barY, infoX + barW, barY + 4.0f}, ratio, false, true);
                Text(sdraw, ImVec2(infoX + barW + kTlDurGap, CenterY(rowCy, durStyle)), cell.durText,
                     durStyle);
            }
            if (tlHit.clicked) {
                // 走 SelectBookShot 而不是就地写 `s.selectedShot` + RebuildBookSide：
                // 那两行本来是 SelectBookShot 的复制品，少了夹取边界检查，再多一份就要
                // 靠「记得同步」维持。选中态只有一条路径。
                SelectBookShot(i);
            }
            x += kTlCardW + kTlGap;
        }
        // 报内容宽 —— **必须**调：只开 horizontalScroll 而内容宽恒为 0 时，滚条不出现、
        // 内容被裁且**到不了**，比「放不下就不画」更糟（见 kit/Scroll.h）。
        strip.setContentWidth(x - inner.min.x - kTlGap);
        strip.setContentHeight(kStripH);
    }
}

} // namespace shine::pages
