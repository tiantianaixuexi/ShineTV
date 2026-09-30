// shine::pages —— 外壳的**侧栏与检查器**
//
// 两块放一起的理由比「都在左右」更强：它们是同一份选中态的两个视图 ——
// 侧栏点一下改的是 `BookSideView::selected*`，检查器读的是同一份。分成两个文件
// 之后「谁写、谁读」会看不明白，而这一层正是最容易出现「侧栏高亮和检查器属性
// 不是同一个东西」那类缺陷的地方。
//
// 侧栏树的扁平序号 ↔ (章, 镜) 的互转函数（FlatTreeIndex / ResolveTreeIndex）
// 住在这里的匿名命名空间：只有这两块用得到，导出就等于允许第二处实现。
#include "ui/imgui/pages/Shell.h"

#include "ui/imgui/pages/Shell_Layout.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "ui/imgui/kit/Anim.h"
#include "ui/imgui/kit/Overlays.h"
#include "ui/imgui/kit/Scroll.h"
#include "ui/imgui/pages/BookData.h"
#include "ui/imgui/pages/WorkspacePages.h"
#include "util/Encoding.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace shine::pages {

using namespace shine::kit;

// ---------------------------------------------------------------- P4.4 侧栏
// ---------------------------------------------------------------- P4.5 侧栏
namespace {

// 侧栏树的「可见节点扁平序号」（先序）↔ (章下标, 镜下标) 的互转。
// kit::Tree 只认一个 int，且只画**展开**节点的子节点 —— 所以两处都按同一套先序走，
// 收起节点的子树不占序号。镜下标 = -1 表示命中的是章节点；命中卷节点时两个都置 -1。
//
// ⚠️ 树是**三层**（卷 → 章 → 镜，设计稿 Shell.jsx:583-662 的 NovelSideTree）。
//    而 `chapter` 是 `BookSideView::chapters` 里的**全局下标**，不是「某卷内的下标」——
//    所以必须用独立的 `seen` 计数器走遍**所有**卷，哪怕卷收起了（收起的卷不占扁平
//    序号，但它的章仍然存在于 chapters 里）。用「当前卷内下标」比就会在第二卷
//    之后整体错位：点第 5 章高亮第 1 章。
int FlatTreeIndex(const std::vector<TreeNode>& nodes, int chapter, int shot) {
    int i = 0;     // 可见节点的扁平序号
    int seen = 0;  // 已走过的章数（= chapters 的全局下标）
    for (const TreeNode& vol : nodes) {
        i++;  // 卷节点本身占一个号
        if (!vol.expanded) {
            seen += static_cast<int>(vol.children.size());
            continue;
        }
        for (std::size_t c = 0; c < vol.children.size(); ++c) {
            const TreeNode& chap = vol.children[c];
            const int at = i++;
            if (seen == chapter) {
                if (shot < 0) {
                    return at;
                }
                if (chap.expanded && shot < static_cast<int>(chap.children.size())) {
                    return at + 1 + shot;
                }
                return at;
            }
            ++seen;
            if (chap.expanded) {
                i += static_cast<int>(chap.children.size());
            }
        }
    }
    return -1;
}

void ResolveTreeIndex(const std::vector<TreeNode>& nodes, int index, int& chapterOut,
                      int& shotOut) {
    int i = 0;
    int seen = 0;
    for (const TreeNode& vol : nodes) {
        if (i++ == index) {
            // 命中卷节点：只切展开 / 收起，不选中任何章（与资产树的分组行同语义）。
            chapterOut = -1;
            shotOut = -1;
            return;
        }
        if (!vol.expanded) {
            seen += static_cast<int>(vol.children.size());
            continue;
        }
        for (std::size_t c = 0; c < vol.children.size(); ++c) {
            const TreeNode& chap = vol.children[c];
            if (i++ == index) {
                chapterOut = seen;
                shotOut = -1;
                return;
            }
            ++seen;
            if (!chap.expanded) {
                continue;
            }
            for (std::size_t k = 0; k < chap.children.size(); ++k) {
                if (i++ == index) {
                    chapterOut = seen - 1;  // seen 在上面已经为这一章 +1 过
                    shotOut = static_cast<int>(k);
                    return;
                }
            }
        }
    }
    chapterOut = -1;
    shotOut = -1;
}

// 镜码 S001 —— 实现与注释都在 WorkspacePages.h（Shell / 故事板 / 检查器共用一份）。
using pages::ShotCode;

// 点中的是分组节点（设计稿的分组行只切展开 / 收起，不选中任何资产：
// Shell.jsx:554 的 onClick 只 setOpen，没有 setSelEntity）。返回它在 nodes 里的下标。
int AssetGroupAt(const std::vector<TreeNode>& nodes, int flat) {
    int i = 0;
    for (std::size_t g = 0; g < nodes.size(); ++g) {
        if (i++ == flat) {
            return static_cast<int>(g);
        }
        if (!nodes[g].expanded) {
            continue;
        }
        i += static_cast<int>(nodes[g].children.size());
    }
    return -1;
}

// 资产 kind 树的两向展平（与上面章 / 镜树同一套先序约定）。
//
// leafAsset 是**叶子序号**（只数叶子，不数分组）→ BookSideView::assets 下标。
// 返回值是 kit::Tree 用的**可见节点扁平序号**（分组节点也占号），所以两个数不是一回事：
// 传错一个就是「点 A 高亮 B」。命中分组节点返回 -1（设计稿的 .node.on 只加在叶子上，
// 分组节点只有展开 / 收起，没有选中态）。
int AssetLeafFlat(const std::vector<TreeNode>& nodes, const std::vector<int>& leafAsset,
                  int assetIndex) {
    int flat = 0;
    std::size_t leaf = 0;
    for (const TreeNode& group : nodes) {
        const int at = flat++;
        if (!group.expanded) {
            continue;
        }
        for (std::size_t k = 0; k < group.children.size(); ++k, ++flat, ++leaf) {
            if (assetIndex >= 0 && leaf < leafAsset.size() && leafAsset[leaf] == assetIndex) {
                return at + 1 + static_cast<int>(k);
            }
        }
    }
    return -1;
}

int AssetLeafAt(const std::vector<TreeNode>& nodes, const std::vector<int>& leafAsset, int flat) {
    int i = 0;
    std::size_t leaf = 0;
    for (const TreeNode& group : nodes) {
        if (i++ == flat) {
            return -1;  // 命中分组节点
        }
        if (!group.expanded) {
            continue;
        }
        for (std::size_t k = 0; k < group.children.size(); ++k) {
            if (i++ == flat) {
                return leaf < leafAsset.size() ? leafAsset[leaf] : -1;
            }
            ++leaf;
        }
    }
    return -1;
}

// 库里是空串时给「—」，不留空白格（KeyValues 的值列空着看不出是"没值"还是"漏了"）。
std::string OrDash(const std::string& value) { return value.empty() ? "—" : value; }

// 设计稿的侧栏树分两种：小说 / 分镜 / 出图 / 出片是「章 → 镜」树（ShotSideTree），
// 资产是「kind 筛选 + 分组树」（AssetsSideTree，Shell.jsx:519-580）。
// 总控与项目中心没有侧栏树。
bool WorkspaceHasSideTree(int workspace) {
    return workspace == static_cast<int>(Workspace::Novel) ||
           workspace == static_cast<int>(Workspace::Storyboard) ||
           workspace == static_cast<int>(Workspace::ImageFlow) ||
           workspace == static_cast<int>(Workspace::VideoFlow);
}

bool WorkspaceHasAssetTree(int workspace) {
    return workspace == static_cast<int>(Workspace::Assets);
}

// 资产 kind 分组的展开态。存的是「被收起的」（设计稿默认全展开），key 用**原始 kind**
// 英文值 —— AssetKindLabel 把 item / prop / treasure 都映成「物品」，用标签当 key
// 的话收起一个会连着收起三个。
bool KindCollapsed(const std::vector<std::string>& collapsed, const std::string& kind) {
    return std::find(collapsed.begin(), collapsed.end(), kind) != collapsed.end();
}

} // namespace

// 小说侧栏的「快速跳转」按钮组（Shell.jsx:605-624）。
//
// 2×2 grid gap 6，四个按钮：资产 / 分镜 / 出图 / 出片。
// 视觉走 .jump-btn（shell.css:640-648）：h24 r6 1px accent-glow 边 + accent-dim 底 +
// accent 字；hover 底换 jumpBtnBg（accent 22%）。
// ⚠️ ButtonVariant 只有 Primary/Secondary/Ghost/Danger，**没有** jump 这一档 —— 所以
//    这里自绘，不去硬凑一个 Ghost 变体（那会把「强调跳转」画成普通幽灵按钮）。
//    jumpBtnBg 是主题加载时算好的派生色（Theme.cpp），直接取。
void Shell::DrawJumpButtons(Rect bounds, ImDrawList* draw) {
    struct Jump {
        const char* icon;
        const char* label;
        Workspace target;
    };
    static constexpr char kLabel[] = "快速跳转";
    draw->AddText(FontBoldAt(11.0f), 11.0f, ImVec2(bounds.min.x + 8.0f, bounds.min.y),
                  ColorTextMuted(), kLabel, kLabel + sizeof(kLabel) - 1);

    const Jump jumps[] = {{"masks", "资产", Workspace::Assets},
                          {"clapper", "分镜", Workspace::Storyboard},
                          {"image", "出图", Workspace::ImageFlow},
                          {"film", "出片", Workspace::VideoFlow}};
    const float top = bounds.min.y + 18.0f;
    const float gap = 6.0f;
    const float cellW = (bounds.width() - gap) * 0.5f;
    const float cellH = 24.0f;  // .btn.sm

    for (int i = 0; i < 4; ++i) {
        const int column = i % 2;
        const int row = i / 2;
        const float left = bounds.min.x + (cellW + gap) * static_cast<float>(column);
        const float boxTop = top + (cellH + gap) * static_cast<float>(row);
        const Rect box{left, boxTop, left + cellW, boxTop + cellH};
        const std::string hitId = "jump-" + std::to_string(i);
        // ⚠️ 一帧里只能 HitTest 一次。以前这里先 `HitTest(...).hovered` 画完再
        //    `Clicked(...)`，等于用**同一个 id** 注册了两个 InvisibleButton。实测后果：
        //    悬停态压根不亮（`hover-jump-btn` 与静息态逐像素零差异），按钮只是
        //    "点得到但看不出按下"。hovered / clicked 从同一次命中测试里取。
        const kit::Hit hit = ChromeHit(box, hitId);
        DrawRoundRect(draw, box.min, box.max, 6.0f,
                      hit.hovered ? ColorOf(theme::CurrentDerived().jumpBtnBg) : ColorAccentDim(),
                      ColorAccentGlow(), 1.0f);

        ImFont* font = FontBoldAt(12.0f);
        const char* label = jumps[i].label;
        const float text =
            font->CalcTextSizeA(12.0f, 1e9f, 0.0f, label, label + std::strlen(label)).x;
        const float iconSize = 13.0f;
        // 图标与文字 gap 4（.btn.sm 的 gap），两者合计在格子里居中。
        const float left2 = box.min.x + (cellW - (iconSize + 4.0f + text)) * 0.5f;
        const float cy = box.center().y;
        DrawIcon(draw, jumps[i].icon, ImVec2(left2, cy - iconSize * 0.5f), iconSize, ColorAccent());
        draw->AddText(font, 12.0f, ImVec2(left2 + iconSize + 4.0f, cy - 6.0f), ColorAccent(), label,
                      label + std::strlen(label));

        if (hit.clicked) {
            // 设计稿就是 setWorkspace(ws) + notify(一句话)，**不带 tab、不带选中项**：
            // 章节上下文靠全局选中态自然带过去（Shell.jsx:619）。
            const BookSideView& book = BookSide();
            std::string context = "未选章节";
            if (book.selectedChapter >= 0 &&
                book.selectedChapter < static_cast<int>(book.chapters.size())) {
                const BookChapterView& chapter =
                    book.chapters[static_cast<std::size_t>(book.selectedChapter)];
                context = "第 " + std::to_string(chapter.ord) + " 章";
                if (!chapter.title.empty()) {
                    context += "「" + chapter.title + "」";
                }
            }
            SetWorkspace(static_cast<int>(jumps[i].target));
            Notify(std::string(label) + " · 上下文：" + context, theme::Tone::Ok);
        }
    }
}

void Shell::SelectAsset(int index) {
    const BookSideView& book = BookSide();
    if (index < 0 || index >= static_cast<int>(book.assets.size())) {
        return;
    }
    SelectBookAsset(index);
    assets_.setSelected(index);
    // 设计稿点侧栏叶子会退回详情态（Assets.jsx:151 的 setOverview(false)）——
    // 停在总览网格上点侧栏、主体没反应，看起来像侧栏高亮是假的。
    assets_.setOverview(false);
}

void Shell::SetInspectorSection(int index, bool open) {
    if (index >= 0 && index < 3) {
        sectionOpen_[index] = open;
    }
}

void Shell::Notify(std::string text, theme::Tone tone) {
    toastText_ = std::move(text);
    toastTone_ = tone;
    toastTimer_ = 3.2f;
}

// 资产侧栏：kind 筛选 chip 行 + 「kind 分组 → 实体」两层树（Shell.jsx:519-580）。
//
// ⚠️ 设计稿的 chip 是写死的 5 个候选（`['全部','人物','地点','物品','势力']`，Shell.jsx:522），
//    而分组名是**从数据里推**的（Shell.jsx:523-525）。真实工程的 entities.kind 是
//    `namespace kind` 里的 30 个英文值之一，库里无 CHECK 约束 —— 照抄那 5 个 chip 会让
//    库里 95% 的 kind 根本筛不出来。所以 chip 同样从数据推：全部 + 当前实际出现的 kind。
void Shell::DrawAssetSideTree(Rect area, ImDrawList* draw, float y) {
    const BookSideView& book = BookSide();
    const float x = area.min.x + 10.0f;
    const float w = area.max.x - 20.0f - x;

    // ---- 标题行：masks 图标 + 视觉资产 + {ready}/{total} 就绪 ----
    // 设计稿是 space-between（Shell.jsx:530），所以就绪数**右对齐**到行尾；
    // 画在 x 上会跟图标和标题叠在一起（侧栏只有 240px，叠了就是一团糊）。
    DrawIcon(draw, "masks", ImVec2(x, y), 14.0f, ColorAccent());
    static constexpr char kTitle[] = "视觉资产";
    draw->AddText(FontBoldAt(12.5f), 12.5f, ImVec2(x + 20.0f, y), ColorText(), kTitle,
                  kTitle + sizeof(kTitle) - 1);
    int ready = 0;
    for (const BookAssetView& asset : book.assets) {
        if (asset.tone == theme::Tone::Ok) {
            ++ready;
        }
    }
    const std::string readyText = std::to_string(ready) + "/" +
                                  std::to_string(static_cast<int>(book.assets.size())) + " 就绪";
    const float readyW = MeasureClipped(FontAt(11.0f), 11.0f, w, readyText);
    DrawTextClipped(draw, FontAt(11.0f), 11.0f, ImVec2(x + w - readyW, y), w, ColorTextMuted(),
                    readyText);
    y += 20.0f;

    // ---- 进度条（.prog.thin 4px）----
    const float total = static_cast<float>(book.assets.size());
    Progress(draw, Rect{x, y, x + w, y + ProgressHeight(true)},
             total > 0.0f ? static_cast<float>(ready) / total : 0.0f, false, true);
    y += ProgressHeight(true) + 10.0f;

    // ---- chip 行：全部 + 实际出现的 kind（分组顺序 = 首次出现的顺序，与 ListEntities
    //      的 ORDER BY id 一致，不另排字母序）----
    std::vector<std::string> allKinds;
    for (const BookAssetView& asset : book.assets) {
        if (std::find(allKinds.begin(), allKinds.end(), asset.kind) == allKinds.end()) {
            allKinds.push_back(asset.kind);
        }
    }
    const std::string& filter = BookKindFilter();
    {
        float cx = x;
        const float cy = y;
        const float ch = ChipHeight(true);
        ChipSpec all{true, true, ""};
        if (Chip(draw, Rect{cx, cy, cx + ChipWidth("全部", all), cy + ch}, "全部", all, "chip-all")) {
            SetBookKindFilter({});
        }
        cx += ChipWidth("全部", all) + 6.0f;
        for (const std::string& kind : allKinds) {
            const std::string label = AssetKindLabel(kind);
            const int count = static_cast<int>(std::count_if(
                book.assets.begin(), book.assets.end(),
                [&kind](const BookAssetView& a) { return a.kind == kind; }));
            ChipSpec spec{filter == kind, true, std::to_string(count)};
            const float cw = ChipWidth(label, spec);
            // chip 行会换行（.chips 是 flex-wrap），放不下就折到下一行，别画出侧栏。
            if (cx + cw > x + w) {
                cx = x;
                y += ch + 6.0f;
            }
            if (Chip(draw, Rect{cx, y, cx + cw, y + ch}, label, spec, "chip-" + kind)) {
                SetBookKindFilter(filter == kind ? std::string{} : kind);
            }
            cx += cw + 6.0f;
        }
        y += ch + 10.0f;
    }

    // ---- 树：kind 分组 → 实体叶子 ----
    std::vector<TreeNode> nodes;
    std::vector<int> leafAsset;     // 叶子序号 → BookSideView::assets 下标
    std::vector<std::string> kinds;  // 分组序号 → **原始 kind 英文值**
    // ⚠️ 展开态的 key 必须用原始 kind，不能用分组标签：AssetKindLabel 把 item / prop /
    //    treasure 都映成「物品」，用标签当 key 的话点开一个会连着开三个。
    for (const std::string& kind : allKinds) {
        if (!filter.empty() && kind != filter) {
            continue;
        }
        TreeNode group;
        group.label = AssetKindLabel(kind);
        group.icon = "layers";
        int count = 0;
        for (const BookAssetView& asset : book.assets) {
            if (asset.kind != kind) {
                continue;
            }
            ++count;
            group.hasChildren = true;
            group.children.push_back(TreeNode{asset.name.empty() ? "—" : asset.name, {}, {}, false,
                                              false, {}});
            leafAsset.push_back(asset.index);
        }
        group.trailing = std::to_string(count);
        group.expanded = !KindCollapsed(collapsedKinds_, kind);
        nodes.push_back(std::move(group));
        kinds.push_back(kind);
    }

    if (nodes.empty()) {
        Empty(draw, Rect{x, y, x + w, y + 76.0f}, "masks", "没有这一类资产",
              "换回「全部」看看这个工程里有哪些实体");
        y += 84.0f;
    } else {
        // 可见节点扁平序号（先序）↔ 资产下标。kit::Tree 只认一个 int，且收起的子树
        // 不占序号 —— 与上面章 / 镜树同一套先序约定，这里是它的资产版。
        const int wanted = AssetLeafFlat(nodes, leafAsset, book.selectedAsset);
        int picked = wanted;
        y += Tree(draw, Rect{x, y, x + w, area.max.y - 34.0f}, nodes, picked, "asset-tree");
        if (picked != wanted) {
            // 分组行只切展开 / 收起（设计稿的 onClick 只有 setOpen，没有 setSelEntity）；
            // 叶子行才选资产。展开态存成员上，存局部变量的话下一帧就收回。
            const int group = AssetGroupAt(nodes, picked);
            if (group >= 0) {
                const std::string& key = kinds[static_cast<std::size_t>(group)];
                const auto at = std::find(collapsedKinds_.begin(), collapsedKinds_.end(), key);
                if (at == collapsedKinds_.end()) {
                    collapsedKinds_.push_back(key);
                } else {
                    collapsedKinds_.erase(at);
                }
                return;
            }
            const int index = AssetLeafAt(nodes, leafAsset, picked);
            if (index >= 0) {
                SelectAsset(index);
            }
        }
    }
}

void Shell::DrawSidePanel(Rect area, ImDrawList* draw) {
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorSurface());
    draw->AddLine(ImVec2(area.max.x - 0.5f, area.min.y), ImVec2(area.max.x - 0.5f, area.max.y),
                  ColorLineSubtle(), 1.0f);

    // 折叠把手 20×48 贴在右缘中点
    const Rect handle{area.max.x - 20.0f, area.center().y - 24.0f, area.max.x, area.center().y + 24.0f};
    DrawRoundRect(draw, handle.min, handle.max, 6.0f, ColorPanel(), ColorLineNormal(), 1.0f);
    DrawIconCentered(draw, "chevron", handle.center(), 12.0f, ColorTextMuted());
    if (ChromeClicked(handle, "side-collapse")) {
        ToggleSidePanel();
    }

    const float x = area.min.x + 10.0f;
    float y = area.min.y + 10.0f;
    const std::string root = layout_.projectName.empty() ? "项目" : layout_.projectName;
    draw->AddText(FontBoldAt(11.5f), 11.5f, ImVec2(x + 14.0f, y), ColorTextMuted(), root.data(),
                  root.data() + root.size());
    y += 22.0f;

    const char* scope = WorkspaceLabel(layout_.workspace);
    draw->AddText(FontAt(12.5f), 12.5f, ImVec2(x + 6.0f, y), ColorTextSecondary(), scope,
                  scope + std::strlen(scope));
    y += 26.0f;

    if (layout_.projectRoot.empty()) {
        Empty(draw, Rect{x, y, area.max.x - 10.0f, y + 132.0f}, "book", "未打开项目",
              "从顶栏项目胶囊回项目中心新建或打开，这里会列出真实的章节 / 镜头 / 资产");
        return;
    }
    if (!WorkspaceHasSideTree(layout_.workspace) && !WorkspaceHasAssetTree(layout_.workspace)) {
        Empty(draw, Rect{x, y, area.max.x - 10.0f, y + 132.0f}, "book", "此工作区没有侧栏树",
              "总控是 KPI 面板；项目中心是整屏，不套侧栏");
        return;
    }

    // 小说侧栏的「快速跳转」2×2 按钮组（Shell.jsx:605-624）。设计稿只挂在小说工作区，
    // 位置在章节进度条之下、章节树之上。点击 = setWorkspace(ws) + 一条 toast，
    // 不带 tab、不带选中项 —— 章节上下文靠全局选中态自然带过去（与设计稿同语义）。
    //
    // ⚠️ 必须排在下面所有空态 `return` **之前**。原来它排在
    // 「这本小说还没有章」那个空态之后，于是没章节 / 没绑定 / 读取中 / 读取失败
    // 四种状态下这四个按钮**整组不画** —— 而「快速跳转」与「有没有章节」毫无
    // 关系：它跳的是资产 / 分镜 / 出图 / 出片四个工作区。
    //
    //    症状极其隐蔽：界面看着正常（有个空态提示），而 `hover-jump-btn` 探针
    //    报「一个控件都没命中」—— 判据说是「探针坐标偏了，别改产品」，于是
    //    去挪坐标，挪到哪都不对。用 `SHINE_SCAN` 把侧栏整片扫一遍才发现
    //    `jump-*` 这个 id 在那个工作区**压根不存在**。
    //    **「点空了」有三种原因，凭日志分不出来，只能把热区扫出来看。**
    if (layout_.workspace == static_cast<int>(Workspace::Novel)) {
        DrawJumpButtons(Rect{x, y, area.max.x - 20.0f, y + 54.0f}, draw);
        y += 64.0f;
    }

    const BookSideView& book = BookSide();
    if (!book.bound) {
        Empty(draw, Rect{x, y, area.max.x - 10.0f, y + 132.0f}, "book", "结构尚未载入",
              "切到小说 / 资产任一页会读取 novel.db 里的真实结构");
        return;
    }
    if (book.loading) {
        Empty(draw, Rect{x, y, area.max.x - 10.0f, y + 132.0f}, "book", "正在读取 novel.db",
              "库打开后会在这里列出章节与镜头");
        return;
    }
    if (!book.error.empty()) {
        Empty(draw, Rect{x, y, area.max.x - 10.0f, y + 132.0f}, "book", "读取失败", book.error);
        return;
    }

    // 资产工作区走 kind 筛选树（Shell.jsx:519-580），不吃章 / 镜那一套。
    if (WorkspaceHasAssetTree(layout_.workspace)) {
        if (book.assets.empty()) {
            Empty(draw, Rect{x, y, area.max.x - 10.0f, y + 132.0f}, "masks", "这个工程还没有实体",
                  "跑一次初始化链（T1–T17）之后这里才有实体与资产");
            return;
        }
        DrawAssetSideTree(area, draw, y);
        return;
    }

    if (book.chapters.empty()) {
        Empty(draw, Rect{x, y, area.max.x - 10.0f, y + 132.0f}, "book", "这本小说还没有章",
              "跑一次 T1–T17 之后这里才有章节与镜头");
        return;
    }

    // ⚠️ 原来这里又画了一遍跳转按钮组 —— 与上面那段重复。同一个 id 注册两次
    //    InvisibleButton = 第二个永远 clicked=false（ImGui 先注册者独占）。
    //    整段删掉，唯一实现是上面那处（在所有空态 return 之前）。

    // 设计稿的 NovelSideTree 是「书 / 卷 / 章 / 镜」三层 + 叶（Shell.jsx:583-662）。
    //
    // ⚠️ 早先这里注释写着「卷这一层要 novelcore::NovelGraph::ListVolumes()，而它不存在，
    //    补它要动 shine_core，所以画两层」—— **那个归因是错的**。卷根本不需要新接口：
    //    `ChapterRow::volume_id` 早就有（`src/novel/NovelTypes.h:115`，`ListChapters` 的
    //    SELECT 也带了它），`volumes` 表也一直在。缺的只是 UI 侧把这两列读出来。
    //    一个被记成「要改业务层」的缺口，其实是**两行没搬的字段**。
    //
    // 卷默认全展开：多一层点击才能看到章，与设计稿的意图不符；展开态暂不持久化。
    //
    // 镜只挂在**当前选中章**下面：场/镜是对选中章取的，没选过的章没有镜数据，
    // 画一排空节点等于骗人。
    const bool withShots = layout_.workspace != static_cast<int>(Workspace::Novel);
    // 先按 volumeId 归拢；volumeId == 0 或查不到卷名的一律归到「未归卷」——
    // 老工程的 volumes 表可能是空的，那时如实显示「未归卷」而不是编一个卷名。
    std::vector<TreeNode> nodes;
    std::vector<std::pair<int, std::string>> volumeOrder;  // volumeId → 标题（保序、去重）
    std::vector<int> chapterBucket(book.chapters.size(), 0);  // 章下标 → 哪个卷桶
    for (std::size_t i = 0; i < book.chapters.size(); ++i) {
        const BookChapterView& chapter = book.chapters[i];
        const int key = chapter.volumeId;
        const std::string title =
            chapter.volumeTitle.empty() ? std::string("未归卷") : chapter.volumeTitle;
        int bucket = -1;
        for (std::size_t k = 0; k < volumeOrder.size(); ++k) {
            if (volumeOrder[k].first == key) {
                bucket = static_cast<int>(k);
                break;
            }
        }
        if (bucket < 0) {
            bucket = static_cast<int>(volumeOrder.size());
            volumeOrder.emplace_back(key, title);
        }
        chapterBucket[i] = bucket;

        TreeNode chap;
        chap.label = "第 " + std::to_string(chapter.ord) + " 章" +
                     (chapter.title.empty() ? "" : " · " + chapter.title);
        chap.icon = "book";
        chap.trailing = OrDash(chapter.status);
        chap.expanded = withShots && static_cast<int>(i) == book.selectedChapter;
        chap.hasChildren = chap.expanded;
        if (chap.expanded) {
            for (const BookShotView& shot : book.shots) {
                TreeNode leaf;
                leaf.label = ShotCode(shot.ord) + (shot.action.empty() ? "" : " · " + shot.action);
                leaf.icon = "clapper";
                chap.children.push_back(std::move(leaf));
            }
        }
        nodes.push_back(std::move(chap));
    }
    // 把章按卷重新装桶：卷 → 章 → 镜。
    std::vector<TreeNode> volumes;
    volumes.reserve(volumeOrder.size());
    for (std::size_t k = 0; k < volumeOrder.size(); ++k) {
        TreeNode vol;
        vol.label = volumeOrder[k].second;
        vol.icon = "layers";
        for (std::size_t i = 0; i < book.chapters.size(); ++i) {
            if (chapterBucket[i] != static_cast<int>(k)) {
                continue;
            }
            vol.children.push_back(std::move(nodes[i]));
        }
        vol.trailing = std::to_string(vol.children.size());
        vol.hasChildren = !vol.children.empty();
        vol.expanded =
            std::find(collapsedVolumes_.begin(), collapsedVolumes_.end(), volumeOrder[k].first) ==
            collapsedVolumes_.end();
        volumes.push_back(std::move(vol));
    }

    // ⚠️ 先序遍历里**顶层节点总是前 N 个**（它们的子节点排在它们后面），所以扁平序号
    //    就等于在 volumes 里的下标 —— 命中卷节点不必再遍历一遍。
    const auto wanted = withShots
                            ? FlatTreeIndex(volumes, book.selectedChapter, book.selectedShot)
                            : FlatTreeIndex(volumes, book.selectedChapter, -1);
    int picked = wanted;
    const Rect treeArea{x, y, area.max.x - 20.0f, area.max.y - 10.0f};
    Tree(draw, treeArea, volumes, picked, "side-tree");
    if (picked == wanted) {
        return;  // 没点中
    }
    if (picked >= 0 && picked < static_cast<int>(volumes.size())) {
        // 卷行只切展开 / 收起，不选中任何章（与资产树的 kind 分组行同语义，
        // 设计稿的卷行 onClick 也只有 setOpen）。
        const int key = volumeOrder[static_cast<std::size_t>(picked)].first;
        const auto at = std::find(collapsedVolumes_.begin(), collapsedVolumes_.end(), key);
        if (at == collapsedVolumes_.end()) {
            collapsedVolumes_.push_back(key);
        } else {
            collapsedVolumes_.erase(at);
        }
        return;
    }
    int chapterIndex = -1;
    int shotIndex = -1;
    ResolveTreeIndex(volumes, picked, chapterIndex, shotIndex);
    if (chapterIndex < 0) {
        return;
    }
    if (shotIndex < 0) {
        SelectBookChapter(chapterIndex);
    } else {
        SelectBookShot(shotIndex);  // 只改下标，不重取（同一章的镜已经在快照里）
    }
}

// ---------------------------------------------------------------- P4.5 检查器
// `.inspector .sect { border-bottom: 1px solid var(--line-subtle) }`
// （shell.css:343-345）—— 分隔线在**整段底部**，不在段头正下方。
//
// ⚠️ 原实现把它画在 `header.max.y`，于是每段都多出一条设计稿里不存在的线，
//    而段底（`.sect-b` 的 padding-bottom 之后）反而没有线。展开段和折叠段
//    的收尾都要用到，抽出来免得两处各写一遍又走样。
void DrawSectionDivider(const Rect& area, float y, ImDrawList* draw) {
    // 满宽：`.sect` 是 `.inspector` 的直接子元素，没有左右内边距。
    draw->AddLine(ImVec2(area.min.x, y), ImVec2(area.max.x, y), ColorLineSubtle(), 1.0f);
}

void Shell::DrawInspector(Rect area, ImDrawList* draw) {
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorSurface());
    draw->AddLine(ImVec2(area.min.x + 0.5f, area.min.y), ImVec2(area.min.x + 0.5f, area.max.y),
                  ColorLineSubtle(), 1.0f);

    // 左右内边距 = 14px。段头 `.sect-h { padding:10px 14px }` 与段内
    // `.sect-b { padding:2px 14px 14px }` **同宽**，所以段头文字与段内正文左对齐
    // （原来的 16px 也是对齐的，只是整体宽了 2px）。
    constexpr float kSectPadX = 14.0f;
    const float x = area.min.x + kSectPadX;
    const float w = area.width() - kSectPadX * 2.0f;
    float y = area.min.y + 12.0f;

    // 3 段可折叠：属性 / 预览 / 关联。展开态在 sectionOpen_（成员）上。
    //
    // ⚠️ 早先这里是 `const Section sections[] = {{"属性",true},...}` —— 每帧新建的
    //    **const 局部数组**，段头画了折叠箭头却没有任何东西写回展开态，也没有点击处理。
    //    于是「关联」段永远打不开，箭头纯装饰。默认初值照设计稿（Shell.jsx:146
    //    的 {a:true, b:true, c:false}），但这次是真能点的。
    static constexpr char kSectionTitles[3][8] = {"属性", "预览", "关联"};
    const BookSideView& book = BookSide();

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
    //    记在 refactor/PROGRESS.md 的「文字垂直居中」一节。
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
    constexpr float kSectFont = 12.0f;           // 写 12.5f 会被 LookupNearest 顶到 13px

    for (int s = 0; s < 3; ++s) {
        const char* title = kSectionTitles[s];
        const bool open = sectionOpen_[s];
        const Rect header{x, y, x + w, y + kSectHeadH};
        const std::string headId = "inspector-head-" + std::to_string(s);
        // 同上：一帧里只 HitTest 一次，双注册会让 hover 失效。
        const kit::Hit headHit = ChromeHit(header, headId);
        // hover 底色**故意不画**：设计稿这一条只有 `:hover { color: text-primary }`。
        const ImU32 headFg = headHit.hovered ? ColorText() : ColorTextSecondary();
        // align-items:center ⇒ 图标中心与文字行盒中心同一水平线。
        DrawIcon(draw, open ? "chevdown" : "chevron",
                 ImVec2(x, header.center().y - kSectIconSize * 0.5f), kSectIconSize,
                 headHit.hovered ? ColorText() : ColorTextMuted());
        draw->AddText(FontBoldAt(kSectFont), kSectFont,
                      ImVec2(x + kSectIconSize + kSectGap,
                             kit::CenterTextY(FontBoldAt(kSectFont), kSectFont, header.center().y)),
                      headFg, title, title + std::strlen(title));
        y += kSectHeadH;
        if (headHit.clicked) {
            sectionOpen_[s] = !open;
        }
        if (!sectionOpen_[s]) {
            // 折叠：Shell.jsx:153 只在 open 时才渲染 .sect-b ⇒ 段头下面直接就是分隔线。
            DrawSectionDivider(area, y, draw);
            y += 1.0f;
            continue;
        }
        y += kSectBodyPadTop;
        if (s == 0) {
            // ⚠️ 这里原先写死 {代码:S012, 动作:转身, 时长:6.0s, 情绪:克制} —— 一组
            //    编出来的镜头属性，在任何工程、任何项目下都长这样，点了也不跟着选中项变。
            // 现在读侧栏那份只读快照：选中了镜就给镜的字段，只选了章就给章的字段，
            // 两者都没有才给空态。空串一律显示「—」，不靠留白表示"没值"。
            //
            // 资产工作区看**实体**：上一段选中的镜还留在快照里，不按工作区分流的话，
            // 在资产页点实体、右侧却还显示某个镜的属性 —— 两边说的不是一回事。
            const bool assetWorkspace = WorkspaceHasAssetTree(layout_.workspace);
            const bool hasAssetRow =
                assetWorkspace && book.selectedAsset >= 0 &&
                book.selectedAsset < static_cast<int>(book.assets.size());
            const bool hasShot = !assetWorkspace && !book.shots.empty() && book.selectedShot >= 0 &&
                                 book.selectedShot < static_cast<int>(book.shots.size());
            const bool hasChapter = !assetWorkspace && book.selectedChapter >= 0 &&
                                    book.selectedChapter < static_cast<int>(book.chapters.size());
            if (!book.bound || (!hasShot && !hasChapter && !hasAssetRow)) {
                Empty(draw, Rect{x, y, x + w, y + 76.0f}, "target",
                      layout_.projectRoot.empty() ? "未打开工程" : "未选中条目",
                      layout_.projectRoot.empty()
                          ? "打开工程并选中一个条目后，这里显示它的真实属性"
                          : (assetWorkspace ? "在左侧侧栏树里选中一个实体后，这里显示它的真实属性"
                                            : "在左侧侧栏树里选中章节 / 镜头后，这里显示它的真实属性"));
                y += 84.0f;
            } else if (hasAssetRow) {
                const BookAssetView& row = book.assets[static_cast<std::size_t>(book.selectedAsset)];
                const std::string layers =
                    row.layers > 0 ? (std::to_string(row.layersDone) + " / " +
                                      std::to_string(row.layers) + " 层就绪")
                                   : std::string("—");
                KeyValues(draw, Rect{x, y, x + w, y + 176.0f},
                          {{"名称", OrDash(row.name)},
                           {"类别", OrDash(AssetKindLabel(row.kind))},
                           {"实体 ID", "#" + std::to_string(row.entityId)},
                           {"摘要", OrDash(row.summary)},
                           {"视觉资产", row.hasAsset ? "已建立" : "尚未建立"},
                           {"生产状态", row.hasAsset ? row.statusLabel : "—"},
                           {"形象层", layers},
                           {"降级产物", row.degraded ? "有" : "无"}});
                y += 184.0f;
            } else if (hasShot) {
                const BookShotView& shot = book.shots[static_cast<std::size_t>(book.selectedShot)];
                std::string duration = shot.durationNote;
                if (duration.empty() && shot.durationSec > 0.0) {
                    // ⚠️ 保留一位小数，和故事板页的 `%.1fs` 同口径。
                    //    这里原来取整成 "5s"，同一个镜在两个面板上是两个时长。
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "%.1fs", shot.durationSec);
                    duration = buf;
                }
                KeyValues(draw, Rect{x, y, x + w, y + 176.0f},
                          {{"镜码", ShotCode(shot.ord)},
                           {"场", shot.sceneOrd > 0 ? "第 " + std::to_string(shot.sceneOrd) + " 场" : "—"},
                           {"动作", OrDash(shot.action)},
                           {"表情", OrDash(shot.expression)},
                           {"情绪", OrDash(shot.mood)},
                           {"时长", OrDash(duration)},
                           {"台词", OrDash(shot.dialogue)},
                           {"旁白", OrDash(shot.narration)},
                           {"连贯性", OrDash(shot.canonStatus)}});
                y += 184.0f;
            } else {
                const BookChapterView& chapter =
                    book.chapters[static_cast<std::size_t>(book.selectedChapter)];
                KeyValues(draw, Rect{x, y, x + w, y + 84.0f},
                          {{"章序", "第 " + std::to_string(chapter.ord) + " 章"},
                           {"标题", OrDash(chapter.title)},
                           {"状态", OrDash(chapter.status)},
                           {"字数", chapter.words > 0 ? std::to_string(chapter.words) : "—"}});
                y += 92.0f;
            }
        } else if (s == 1) {
            Art(draw, Rect{x, y, x + w, y + 110.0f}, 5, true);
            // 上面那块是设计稿自带的示意插画（Art()），不是这个工程的出图结果。
            // 不写这句，读者会以为它是该镜的真实渲染。
            DrawTextClipped(draw, FontAt(10.5f), 10.5f, ImVec2(x, y + 96.0f), w, ColorTextMuted(),
                            "示意插画 · 非本工程出图结果");
            y += 118.0f;
        } else {
            // ---- 关联段（Shell.jsx:173-179）----
            //
            // 设计稿是一行平铺三个 Tag（伏笔 #3 / 场景 12 / 镜 S05），**内联字面量**：
            // 没有数据结构、不可点、也不分组。所以这里不造一个「关联列表」出来 ——
            // 形态照抄（一行 .tag.sm，gap 8，可换行），内容换成真数据。
            //
            // 取不到的组**不出 tag**。三组同时空时给一行说明，而不是摆三个占位 tag：
            // 「伏笔 #3」这种假 tag 比没有更糟，它会让人以为库里真有这条伏笔。
            const BookRelationView& relation = book.relation;
            std::vector<std::string> tags;
            std::vector<theme::Tone> tones;
            for (const std::string& title : relation.foreshadows) {
                tags.push_back("伏笔 · " + title);
                tones.push_back(theme::Tone::Warn);
            }
            if (relation.sceneOrd > 0) {
                tags.push_back("场景 " + std::to_string(relation.sceneOrd) +
                               (relation.sceneTitle.empty() ? "" : " · " + relation.sceneTitle));
                tones.push_back(theme::Tone::Idle);
            }
            if (!relation.shotCode.empty()) {
                tags.push_back("镜 " + relation.shotCode);
                tones.push_back(theme::Tone::Idle);
            }

            if (tags.empty()) {
                DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(x, y + 2.0f), w, ColorTextMuted(),
                                book.bound ? "这一项没有可查到的关联（伏笔表为空或未跑 T9）"
                                            : "打开工程并选中章节 / 镜头后，这里显示它的真实关联",
                                true);
                y += 24.0f;
            } else {
                float tx = x;
                float ty = y;
                const float th = TagHeight(true);
                for (std::size_t i = 0; i < tags.size(); ++i) {
                    const float tw = TagWidth(tags[i], true, false);
                    if (tx + tw > x + w) {
                        tx = x;
                        ty += th + 8.0f;  // .row.gap-2 = 8px
                    }
                    Tag(draw, Rect{tx, ty, tx + tw, ty + th}, tags[i], tones[i], true);
                    tx += tw + 8.0f;
                }
                y = ty + th + 6.0f;
            }
        }
        y += kSectBodyPadBottom;
        DrawSectionDivider(area, y, draw);
        y += 1.0f;
    }
}


} // namespace shine::pages
