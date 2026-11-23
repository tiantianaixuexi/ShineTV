// shine::pages —— 外壳的**检查器**（P4.5）
//
// 2026-10-01 自 Shell_Panels.cpp 拆出（「侧栏与检查器合一份」按职责分成
// Shell_Sidebar.cpp / Shell_Inspector.cpp）。检查器只**读** BookSideView 的
// 选中态 —— 写入口全在侧栏与 BookData.cpp，文件边界就是这条读写边界。
//
// 显示缓存（ImGui 每帧绘制 ⇒ 不在 Draw 里现拼字符串）：
//   * 「属性」段 KV 表 → inspectorKv_（指纹没变整帧复用）；
//   * 「关联」段 tag 行 → relationTags_ / relationTones_。
// 指纹算法见 kit/Hash.h（FNV-1a + HashMix）。
#include "ui/imgui/pages/Shell.h"
#include "ui/imgui/kit/Scroll.h"

#include "ui/imgui/pages/Shell_Layout.h"

#include "ui/imgui/kit/Fmt.h"
#include "ui/imgui/kit/Hash.h"
#include "ui/imgui/kit/Text.h"
#include "ui/imgui/pages/BookData.h"
#include "ui/imgui/pages/PageCommon.h"

#include <cstring>
#include <string>

namespace shine::pages {

using namespace shine::kit;

void Shell::SetInspectorSection(int index, bool open) {
    if (index >= 0 && index < 3) {
        sectionOpen_[index] = open;
    }
}

// ---------------------------------------------------------------- P4.5 检查器
// `.inspector .sect { border-bottom: 1px solid var(--line-subtle) }`
// （shell.css:343-345）—— 分隔线在**整段底部**，不在段头正下方。
//
// ⚠️ 原实现把它画在 `header.max.y`，于是每段都多出一条设计稿里不存在的线，
//    而段底（`.sect-b` 的 padding-bottom 之后）反而没有线。展开段和折叠段
//    的收尾都要用到，抽出来免得两处各写一遍又走样。
namespace {

void DrawSectionDivider(const Rect& area, float y, ImDrawList* draw) {
    // 满宽：`.sect` 是 `.inspector` 的直接子元素，没有左右内边距。
    draw->AddLine(ImVec2(area.min.x, y), ImVec2(area.max.x, y), ColorLineSubtle(), 1.0f);
}

} // namespace

// ---- 「属性」段 KV 表：数据指纹没变就整帧复用 ----
//
// 三条分支（资产实体 / 镜 / 章）共用同一张 9 行表，行数记在 inspectorKvCount_。
// 指纹覆盖分支判别式与**所有被显示**的字段 —— 任何字段一变指纹就变，下一帧重建；
// 正确性不靠「记得在哪里刷新」。
void Shell::RefreshInspectorKv(const BookSideView& book) {
    const bool assetWorkspace = WorkspaceHasAssetTree(layout_.workspace);
    const bool hasAssetRow =
        assetWorkspace && book.selectedAsset >= 0 &&
        book.selectedAsset < static_cast<int>(book.assets.size());
    const bool hasShot = !assetWorkspace && !book.shots.empty() && book.selectedShot >= 0 &&
                         book.selectedShot < static_cast<int>(book.shots.size());
    const bool hasChapter = !assetWorkspace && book.selectedChapter >= 0 &&
                            book.selectedChapter < static_cast<int>(book.chapters.size());

    std::uint64_t key = HashMix(0, book.bound ? 1ull : 0ull);
    key = HashMix(key, hasAssetRow ? 1ull : (hasShot ? 2ull : (hasChapter ? 3ull : 0ull)));
    if (hasAssetRow) {
        const BookAssetView& row = book.assets[static_cast<std::size_t>(book.selectedAsset)];
        key = HashMix(key, static_cast<std::uint64_t>(row.entityId));
        key = HashMix(key, static_cast<std::uint64_t>(row.layers));
        key = HashMix(key, static_cast<std::uint64_t>(row.layersDone));
        key = HashMix(key, row.hasAsset ? 1ull : 0ull);
        key = HashMix(key, row.degraded ? 1ull : 0ull);
        key = HashMix(key, Fnv1a(row.name));
        key = HashMix(key, Fnv1a(row.kind));
        key = HashMix(key, Fnv1a(row.summary));
        key = HashMix(key, Fnv1a(row.statusLabel));
    } else if (hasShot) {
        const BookShotView& shot = book.shots[static_cast<std::size_t>(book.selectedShot)];
        key = HashMix(key, static_cast<std::uint64_t>(shot.ord));
        key = HashMix(key, static_cast<std::uint64_t>(shot.sceneOrd));
        key = HashMix(key, shot.durationSec);
        key = HashMix(key, Fnv1a(shot.action));
        key = HashMix(key, Fnv1a(shot.expression));
        key = HashMix(key, Fnv1a(shot.mood));
        key = HashMix(key, Fnv1a(shot.dialogue));
        key = HashMix(key, Fnv1a(shot.narration));
        key = HashMix(key, Fnv1a(shot.canonStatus));
        key = HashMix(key, Fnv1a(shot.durationNote));
    } else if (hasChapter) {
        const BookChapterView& chapter =
            book.chapters[static_cast<std::size_t>(book.selectedChapter)];
        key = HashMix(key, static_cast<std::uint64_t>(chapter.ord));
        key = HashMix(key, static_cast<std::uint64_t>(chapter.words));
        key = HashMix(key, Fnv1a(chapter.title));
        key = HashMix(key, Fnv1a(chapter.status));
    }
    if (key == inspectorKey_) {
        return;
    }
    inspectorKey_ = key;

    inspectorKv_ = {};
    if (hasAssetRow) {
        const BookAssetView& row = book.assets[static_cast<std::size_t>(book.selectedAsset)];
        inspectorKvCount_ = 8;
        inspectorKv_[0] = {"名称", OrDash(row.name)};
        inspectorKv_[1] = {"类别", OrDash(AssetKindLabel(row.kind))};
        inspectorKv_[2] = {"实体 ID", kit::Fmt("#{}", row.entityId)};
        inspectorKv_[3] = {"摘要", OrDash(row.summary)};
        inspectorKv_[4] = {"视觉资产", row.hasAsset ? "已建立" : "尚未建立"};
        inspectorKv_[5] = {"生产状态", row.hasAsset ? row.statusLabel : std::string("—")};
        inspectorKv_[6] = {"形象层", row.layers > 0
                                         ? kit::Fmt("{} / {} 层就绪", row.layersDone, row.layers)
                                         : std::string("—")};
        inspectorKv_[7] = {"降级产物", row.degraded ? "有" : "无"};
    } else if (hasShot) {
        const BookShotView& shot = book.shots[static_cast<std::size_t>(book.selectedShot)];
        inspectorKvCount_ = 9;
        // 时长：备注原文优先，没有备注而库里有秒数才换算 —— **保留一位小数**，
        // 和故事板页同口径（DurationText 全树一份）。这里原来取整成 "5s"，
        // 同一个镜在两个面板上是两个时长。
        std::string duration = shot.durationNote;
        if (duration.empty() && shot.durationSec > 0.0) {
            duration = DurationText(shot.durationSec);
        }
        inspectorKv_[0] = {"镜码", ShotCode(shot.ord)};
        inspectorKv_[1] = {"场", shot.sceneOrd > 0 ? kit::Fmt("第 {} 场", shot.sceneOrd)
                                                   : std::string("—")};
        inspectorKv_[2] = {"动作", OrDash(shot.action)};
        inspectorKv_[3] = {"表情", OrDash(shot.expression)};
        inspectorKv_[4] = {"情绪", OrDash(shot.mood)};
        inspectorKv_[5] = {"时长", OrDash(duration)};
        inspectorKv_[6] = {"台词", OrDash(shot.dialogue)};
        inspectorKv_[7] = {"旁白", OrDash(shot.narration)};
        inspectorKv_[8] = {"连贯性", OrDash(shot.canonStatus)};
    } else if (hasChapter) {
        const BookChapterView& chapter =
            book.chapters[static_cast<std::size_t>(book.selectedChapter)];
        inspectorKvCount_ = 4;
        inspectorKv_[0] = {"章序", kit::Fmt("第 {} 章", chapter.ord)};
        inspectorKv_[1] = {"标题", OrDash(chapter.title)};
        inspectorKv_[2] = {"状态", OrDash(chapter.status)};
        inspectorKv_[3] = {"字数", chapter.words > 0 ? kit::Fmt("{}", chapter.words)
                                                     : std::string("—")};
    } else {
        inspectorKvCount_ = 0;  // 空态分支：DrawInspector 画 Empty，不画表
    }
}

// ---- 「关联」段 tag 行：同上，指纹没变整帧复用 ----
void Shell::RefreshRelationTags(const BookSideView& book) {
    const BookRelationView& relation = book.relation;
    std::uint64_t key = HashMix(0, static_cast<std::uint64_t>(relation.sceneOrd));
    key = HashMix(key, Fnv1a(relation.shotCode));
    key = HashMix(key, Fnv1a(relation.sceneTitle));
    key = HashMix(key, relation.foreshadows.size());
    for (const std::string& f : relation.foreshadows) {
        key = HashMix(key, Fnv1a(f));
    }
    if (key == relationKey_) {
        return;
    }
    relationKey_ = key;

    // 设计稿是一行平铺三个 Tag（伏笔 #3 / 场景 12 / 镜 S05），**内联字面量**：
    // 没有数据结构、不可点、也不分组。所以这里不造一个「关联列表」出来 ——
    // 形态照抄（一行 .tag.sm，gap 8，可换行），内容换成真数据。
    //
    // 取不到的组**不出 tag**。三组同时空时给一行说明，而不是摆三个占位 tag：
    // 「伏笔 #3」这种假 tag 比没有更糟，它会让人以为库里真有这条伏笔。
    relationTags_.clear();
    relationTones_.clear();
    for (const std::string& title : relation.foreshadows) {
        relationTags_.push_back(kit::Fmt("伏笔 · {}", title));
        relationTones_.push_back(theme::Tone::Warn);
    }
    if (relation.sceneOrd > 0) {
        relationTags_.push_back(kit::Fmt(
            "场景 {}{}", relation.sceneOrd,
            relation.sceneTitle.empty() ? std::string() : kit::Fmt(" · {}", relation.sceneTitle)));
        relationTones_.push_back(theme::Tone::Idle);
    }
    if (!relation.shotCode.empty()) {
        relationTags_.push_back(kit::Fmt("镜 {}", relation.shotCode));
        relationTones_.push_back(theme::Tone::Idle);
    }
}

void Shell::DrawInspector(Rect area, ImDrawList* draw) {
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorSurface());
    draw->AddLine(ImVec2(area.min.x + 0.5f, area.min.y), ImVec2(area.min.x + 0.5f, area.max.y),
                  ColorLineSubtle(), 1.0f);

    const TextStyle title{.size = theme::font::kModal, .bold = true, .color = ColorText()};
    Text(draw, ImVec2(area.min.x + 14.0f, area.min.y + 14.0f), "检查器", title);
    ScrollRegion scroll("shell-inspector", Rect{ImVec2(area.min.x, area.min.y + 44.0f), area.max});
    if (!scroll) {
        return;
    }
    area = scroll.content();
    draw = scroll.drawList();
    // 左右内边距 = 14px。段头 `.sect-h { padding:10px 14px }` 与段内
    // `.sect-b { padding:2px 14px 14px }` **同宽**，所以段头文字与段内正文左对齐
    // （原来的 16px 也是对齐的，只是整体宽了 2px）。
    constexpr float kSectPadX = 14.0f;
    const float x = area.min.x + kSectPadX;
    const float w = area.width() - kSectPadX * 2.0f;
    // 每段内容通过 Stack 预留高度，折行文字按实际宽度测量。
    Stack stack{Rect{x, area.min.y + 12.0f, x + w, area.max.y}};

    // 3 段可折叠：属性 / 预览 / 关联。展开态在 sectionOpen_（成员）上。
    //
    // ⚠️ 早先这里是 `const Section sections[] = {{"属性",true},...}` —— 每帧新建的
    //    **const 局部数组**，段头画了折叠箭头却没有任何东西写回展开态，也没有点击处理。
    //    于是「关联」段永远打不开，箭头纯装饰。默认初值照设计稿（Shell.jsx:146
    //    的 {a:true, b:true, c:false}），但这次是真能点的。
    static constexpr char kSectionTitles[3][8] = {"属性", "预览", "关联"};
    // 段头命中 id 同样是静态常量（每帧递给 ChromeHit 的是同一份字符串）。
    static constexpr char kHeadIds[3][20] = {"inspector-head-0", "inspector-head-1",
                                             "inspector-head-2"};
    const BookSideView& book = BookSide();
    // 缓存先备好（指纹没变 = 纯复用上一帧的字符串），下面两个分支直接画。
    RefreshInspectorKv(book);
    RefreshRelationTags(book);

    // ⚠️ 段头几何照 shell.css:346-357 的 `.inspector .sect-h` 重算过：
    //
    //   display:flex; align-items:center; gap:7px; padding:10px 14px;
    //   font-size:12px; font-weight:700; color:var(--text-secondary)
    //
    // 段头高 = 10 + 行盒 + 10，行盒 = font-size × line-height = 12 × **1.6** = 19.2
    // ⇒ **39.2px**。（`base.css:16` 的 `line-height:1.6` 来自 `body`，经
    // `base.css:23-29` 的 `button { font: inherit }` 带进这个 `<button>`。）
    //
    // 原实现画的是 24px，而且把文字**钉死**在 `header.min.y + 5.0f`。
    // ⚠️ 更正一条我自己写错的注释：这里曾经写着「墨迹中心在 y+5.8、偏上 6.2px」。
    //    那是**错的** —— 按行盒重算：文字 y = min.y+5、字号 12.5 ⇒ 行盒中心
    //    `min.y + 5 + 6.25 = min.y + 11.25`，框中心 `min.y + 12`，只偏 **0.75px**，
    //    落在已接受的光学偏差带里。真正的毛病是**整段高度差 15.2px**（24 vs 39.2），
    //    三段加起来检查器比设计稿短了 45.6px —— 是布局短，不是字没居中。
    //    「字没居中」量级最大的几处在别处（kit::Chip 低 6px、队列行高 6px），
    //    记在 refactor/进度.md 的「文字垂直居中」一节。
    //
    // 还有三处顺带订正：
    //   · 颜色：设计是 `text-secondary`，原来用的是 `ColorText()`（primary）。
    //   · 箭头：Shell.jsx:150 是 11×11，原来写 10。
    //   · hover：设计 `.sect-h:hover` **只改文字颜色**（`text-primary`），
    //     没有背景色；原来给整条段头铺了 `ColorFillHover()`。
    //     hover 探针仍会变色（文字），判据不受影响。
    //
    // 段头内边距也是 14px，与 `.sect-b`（`padding: 2px 14px 14px`）一致 ⇒
    // 段头文字与段内正文左对齐。原先两处都是 16px（对齐是对的，只是整体宽了 2px）。
    constexpr float kSectHeadH = 39.2f;
    constexpr float kSectIconSize = 11.0f;
    constexpr float kSectGap = 7.0f;
    constexpr float kSectBodyPadTop = 2.0f;     // .sect-b padding-top
    constexpr float kSectBodyPadBottom = 14.0f; // .sect-b padding-bottom

    // TEMP-DIAG（用后即删）：记录三段各自的矩形，用后即删。
    struct DiagSect {
        float headTop, headBot, bodyBot;
    };
    static DiagSect diagSects[3];
    static bool diagDone = false;
    const bool diag = !diagDone;
    if (diag) {
        diagSects[0] = diagSects[1] = diagSects[2] = DiagSect{0.0f, 0.0f, 0.0f};
    }
    // ⚠️ 分段标题这一档是 **12.0（theme::font::kTiny），不是 12.5** —— 12.5 在
    //    kUiSizes 之外，`LookupNearest` 会把它顶到 13px，比设计稿大一号。
    //    收编前这个事实记在本地常量 kSectFont 上，现在由 token 自己带（Tokens.h 的
    //    kTiny 行）。

    for (int s = 0; s < 3; ++s) {
        const char* title = kSectionTitles[s];
        const bool open = sectionOpen_[s];
        const Rect header = stack.Next(kSectHeadH);
        if (diag) {
            diagSects[s].headTop = header.min.y;
            diagSects[s].headBot = header.max.y;
        }
        // 同上：一帧里只 HitTest 一次，双注册会让 hover 失效。
        const kit::Hit headHit = ChromeHit(header, kHeadIds[s]);
        // hover 底色**故意不画**：设计稿这一条只有 `:hover { color: text-primary }`。
        const ImU32 headFg = headHit.hovered ? ColorText() : ColorTextSecondary();
        // align-items:center ⇒ 图标中心与文字行盒中心同一水平线。
        DrawIcon(draw, open ? "chevdown" : "chevron",
                 ImVec2(x, header.center().y - kSectIconSize * 0.5f), kSectIconSize,
                 headHit.hovered ? ColorText() : ColorTextMuted());
        const TextStyle sectStyle{.size = theme::font::kTiny, .bold = true, .color = headFg};
        Text(draw,
             ImVec2(x + kSectIconSize + kSectGap, CenterY(header.center().y, sectStyle)), title,
             sectStyle);
        if (headHit.clicked) {
            sectionOpen_[s] = !open;
        }
        if (!sectionOpen_[s]) {
            // 折叠：Shell.jsx:153 只在 open 时才渲染 .sect-b ⇒ 段头下面直接就是分隔线。
            DrawSectionDivider(area, stack.Cur(), draw);
            stack.Advance(1.0f);
            if (diag) {
                diagSects[s].bodyBot = stack.Cur();
            }
            continue;
        }
        stack.Advance(kSectBodyPadTop);
        if (s == 0) {
            // ⚠️ 这里原先写死 {代码:S012, 动作:转身, 时长:6.0s, 情绪:克制} —— 一组
            //    编出来的镜头属性，在任何工程、任何项目下都长这样，点了也不跟着选中项变。
            // 现在读侧栏那份只读快照：选中了镜就给镜的字段，只选了章就给章的字段，
            // 两者都没有才给空态。空串一律显示「—」，不靠留白表示"没值"。
            //
            // 资产工作区看**实体**：上一段选中的镜还留在快照里，不按工作区分流的话，
            // 在资产页点实体、右侧却还显示某个镜的属性 —— 两边说的不是一回事。
            // （分支判别与取值在 RefreshInspectorKv；这里只画。）
            const bool assetWorkspace = WorkspaceHasAssetTree(layout_.workspace);
            const bool hasAssetRow =
                assetWorkspace && book.selectedAsset >= 0 &&
                book.selectedAsset < static_cast<int>(book.assets.size());
            const bool hasShot = !assetWorkspace && !book.shots.empty() && book.selectedShot >= 0 &&
                                 book.selectedShot < static_cast<int>(book.shots.size());
            const bool hasChapter = !assetWorkspace && book.selectedChapter >= 0 &&
                                    book.selectedChapter < static_cast<int>(book.chapters.size());
            if (!book.bound || (!hasShot && !hasChapter && !hasAssetRow)) {
                const bool hasProject = !layout_.projectRoot.empty();
                const char* emptyTitle = hasProject ? "未选中条目" : "未打开工程";
                const char* emptyBody = !hasProject
                    ? "打开工程并选中一个条目后，这里显示它的真实属性"
                    : (assetWorkspace ? "在左侧侧栏树里选中一个实体后，这里显示它的真实属性"
                                      : "在左侧侧栏树里选中章节 / 镜头后，这里显示它的真实属性");
                Empty(draw, stack.Next(EmptyHeight(w, emptyTitle, emptyBody), 8.0f),
                      "target", emptyTitle, emptyBody);
            } else if (hasAssetRow) {
                KeyValues(draw, stack.Next(KeyValuesHeight(inspectorKv_.size()), 8.0f), inspectorKv_.data(),
                          static_cast<std::size_t>(inspectorKvCount_));
            } else if (hasShot) {
                KeyValues(draw, stack.Next(KeyValuesHeight(inspectorKv_.size()), 8.0f), inspectorKv_.data(),
                          static_cast<std::size_t>(inspectorKvCount_));
            } else {
                KeyValues(draw, stack.Next(84.0f, 8.0f), inspectorKv_.data(),
                          static_cast<std::size_t>(inspectorKvCount_));
            }
        } else if (s == 1) {
            const Rect artRect = stack.Next(110.0f, 8.0f);
            Art(draw, artRect, 5, true);
            const TextStyle noticeStyle{.size = theme::font::kMicro, .color = ColorTextMuted(),
                                        .wrapWidth = w};
            const Rect notice = stack.Next(TextSize(kArtNotice, noticeStyle).y);
            Text(draw, notice.min, kArtNotice, noticeStyle);
        } else {
            // ---- 关联段（Shell.jsx:173-179）----
            // tag 行来自缓存（RefreshRelationTags），这里只画。
            if (relationTags_.empty()) {
                const char* emptyRelations = book.bound
                    ? "这一项没有可查到的关联（伏笔表为空或未跑 T9）"
                    : "打开工程并选中章节 / 镜头后，这里显示它的真实关联";
                const TextStyle relationStyle{.size = theme::font::kLabel, .color = ColorTextMuted(),
                                              .wrapWidth = w};
                stack.Advance(2.0f);
                const Rect message = stack.Next(TextSize(emptyRelations, relationStyle).y);
                Text(draw, message.min, emptyRelations, relationStyle);
            } else {
                // R4：Tag 折行走 Flow（hGap/vGap = 8，.row.gap-2）；折行后的行顶
                // 通过 Seek 交还给 Stack —— 折行次数是运行态，不能预 advance。
                Flow tagFlow{Rect{x, stack.Cur(), x + w, area.max.y}, 8.0f, 8.0f};
                const float th = TagHeight(true);
                for (std::size_t i = 0; i < relationTags_.size(); ++i) {
                    Tag(draw, tagFlow.Place(TagWidth(relationTags_[i], true, false), th),
                        relationTags_[i], relationTones_[i], true);
                }
                stack.Seek(tagFlow.Cur() + th + 6.0f);
            }
        }
        stack.Advance(kSectBodyPadBottom);
        DrawSectionDivider(area, stack.Cur(), draw);
        stack.Advance(1.0f);
        if (diag) {
            diagSects[s].bodyBot = stack.Cur();
        }
    }
    scroll.setContentHeight(stack.Cur() - area.min.y);
    if (diag) {
        diagDone = true;
        if (FILE* f = std::fopen("build/_insp_layout.txt", "w")) {
            std::fprintf(f, "contentTop=%.1f contentH=%.1f\n", area.min.y,
                         stack.Cur() - area.min.y);
            for (int i = 0; i < 3; ++i) {
                std::fprintf(f, "sec%d head=%.1f..%.1f bodyBot=%.1f\n", i, diagSects[i].headTop,
                             diagSects[i].headBot, diagSects[i].bodyBot);
            }
            std::fclose(f);
        }
    }
}

} // namespace shine::pages

