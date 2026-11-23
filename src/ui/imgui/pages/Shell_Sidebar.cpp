// shine::pages —— 外壳的**侧栏**：面板壳、跳转按钮组、侧栏树的绘制与命中
//
// 2026-10-01 自 Shell_Panels.cpp 拆出（原来的「侧栏与检查器合一份」按职责
// 分成 Shell_Sidebar.cpp / Shell_Inspector.cpp 两个文件）：
//   * 侧栏树只**写**选中态（BookSideView::selected*），检查器只**读** ——
//     拆开后这条「谁写、谁读」的边界就是文件边界；
//   * 侧栏的先序互转（FlatTreeIndex / ResolveTreeIndex / AssetLeaf*）与
//     检查器毫无交集，原先共处一个匿名命名空间纯属文件太大。
//
// 2026-10-02 再拆一次（这一份一度 735 行，是 pages 最大的文件）：
//   * 节点的**造**（两棵树的指纹缓存）与**先序互转**搬去 Shell_Sidebar_Tree.cpp
//     —— 那一半一个像素都不画，本文件只管「画在哪、点中算什么」；
//   * 先序约定本身搬进 Shell_Sidebar_Tree.h（原先在匿名命名空间里，
//     跨不过文件边界）。
//
// 选中态唯一写入口是 SelectBook*（BookData.cpp）与 SelectAsset —— 侧栏高亮与
// 主区详情对不上的那类缺陷，都堵在这一条路上。
#include "ui/imgui/pages/Shell.h"

#include "ui/imgui/pages/Shell_Layout.h"
#include "ui/imgui/pages/Shell_Sidebar_Tree.h"

#include "ui/imgui/kit/Anim.h"
#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Fmt.h"
#include "ui/imgui/kit/Hash.h"
#include "ui/imgui/kit/Overlays.h"
#include "ui/imgui/kit/Scroll.h"
#include "ui/imgui/kit/Text.h"
#include "ui/imgui/kit/UiBridge.h"
#include "ui/imgui/pages/BookData.h"
#include "ui/imgui/pages/PageCommon.h"
#include "ui/imgui/pages/WorkspacePages.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace shine::pages {

using namespace shine::kit;

// 设计稿的侧栏树分两种：小说 / 分镜 / 出图 / 出片是「卷 → 章 → 镜」树，
// 资产是「kind 筛选 + 分组树」；总控与项目中心没有侧栏树。
// 判式声明在 Shell.h（侧栏挑树与检查器分流共用这一份），定义在此（外部链接）。
bool WorkspaceHasSideTree(int workspace) {
    return workspace == static_cast<int>(Workspace::Novel) ||
           workspace == static_cast<int>(Workspace::Storyboard) ||
           workspace == static_cast<int>(Workspace::ImageFlow) ||
           workspace == static_cast<int>(Workspace::VideoFlow);
}

bool WorkspaceHasAssetTree(int workspace) {
    return workspace == static_cast<int>(Workspace::Assets);
}

// 小说侧栏的「快速跳转」按钮组（Shell.jsx:605-624）。
//
// 2×2 grid gap 6，四个按钮：资产 / 分镜 / 出图 / 出片。
// 视觉走 .jump-btn（shell.css:640-648）：h24 r6 1px accent-glow 边 + accent-dim 底 +
// accent 字；hover 底换 jumpBtnBg（accent 22%）。
// ⚠️ ButtonVariant 只有 Primary/Secondary/Ghost/Danger，**没有** jump 这一档 —— 所以
//    这里自绘，不去硬凑一个 Ghost 变体（那会把「强调跳转」画成普通幽灵按钮）。
//    jumpBtnBg 是主题加载时算好的派生色（Theme.cpp），直接取。
float Shell::DrawJumpButtons(Rect bounds, ImDrawList* draw) {
    struct Jump {
        const char* icon;
        const char* label;
        Workspace target;
    };
    // 设计稿的整组（Shell.jsx:605-624）是三段，**逐段的尺寸都要算进占高**：
    //   ① `<div className="plabel" style={{padding:'2px 8px 2px'}}>快速跳转</div>`
    //      —— `.plabel` 是 10.5px/700（shell.css:583-588），行盒 10.5×1.6 = 16.8，
    //      加内联的上下各 2 ⇒ **20.8**。
    //   ② `grid 1fr 1fr / gap 6`，`margin:'0 8px 10px'`，格内是 `size="sm"` 按钮 ⇒
    //      高 24，网格 24×2 + 6 = **54**，左右各内缩 8。
    //   ③ `margin-bottom: 10` ⇒ **10**。
    //
    // ⚠️ 早先这里按「标签 18 + 两行 30 一行」画，调用方又按 64 推进 y —— 而真实占高
    //    是 84.8。于是第二行跳转按钮（y+48..y+72）**压在侧栏树第一行**（y+64 起）上
    //    18px，取证的重复命中自检逐个报出来（`side-tree#0##0` 与 jump 行交叠 102x8）。
    //    按设计稿的语义，下面这段的返回值才是唯一真相。
    constexpr float kLabelPadY = 2.0f;  // 内联 padding 2px
    constexpr float kGridInset = 8.0f;  // grid 的 margin 0 8px
    constexpr float kGap = 6.0f;        // grid gap
    constexpr float kCellH = 24.0f;     // .btn.sm
    constexpr float kMarginBottom = 10.0f;  // grid 的 margin-bottom
    // ⚠️ 行盒高由 `kit::LineBox(kSmall)` 推，不再本地写 `10.5f * 1.6f` ——
    //    那一行是设计稿的 line-height × 字号，而全局 line-height 只有一处定义。
    const float labelH = LineBox(theme::font::kSmall) + 2.0f * kLabelPadY;  // 16.8 + 4 = 20.8

    static constexpr char kLabel[] = "快速跳转";
    // 四个按钮的命中 id：静态常量（每帧递给 ChromeHit 的是同一份字符串，不再现拼）。
    static constexpr char kJumpIds[4][8] = {"jump-0", "jump-1", "jump-2", "jump-3"};
    Text(draw, ImVec2(bounds.min.x + kGridInset, bounds.min.y + kLabelPadY),
         std::string_view(kLabel, sizeof(kLabel) - 1),
         TextStyle{.size = theme::font::kSmall, .bold = true, .color = ColorTextMuted()});

    const Jump jumps[] = {{"masks", "资产", Workspace::Assets},
                          {"clapper", "分镜", Workspace::Storyboard},
                          {"image", "出图", Workspace::ImageFlow},
                          {"film", "出片", Workspace::VideoFlow}};
    const float gridLeft = bounds.min.x + kGridInset;
    const float gridWidth = bounds.width() - 2.0f * kGridInset;
    const float top = bounds.min.y + labelH;
    const float cellW = (gridWidth - kGap) * 0.5f;
    // `.btn.sm` 的字号：四个按钮共用一份 style —— 收编后这里**只有一个**档，
    // 画与量宽（`TextWidth`）从同一处出发，不会再分叉。
    const TextStyle labelStyle{.size = theme::font::kTiny, .bold = true, .color = ColorAccent()};

    for (int i = 0; i < 4; ++i) {
        const int column = i % 2;
        const int row = i / 2;
        const float left = gridLeft + (cellW + kGap) * static_cast<float>(column);
        const float boxTop = top + (kCellH + kGap) * static_cast<float>(row);
        const Rect box{left, boxTop, left + cellW, boxTop + kCellH};
        // ⚠️ 一帧里只能 HitTest 一次。以前这里先 `HitTest(...).hovered` 画完再
        //    `Clicked(...)`，等于用**同一个 id** 注册了两个 InvisibleButton。实测后果：
        //    悬停态压根不亮（`hover-jump-btn` 与静息态逐像素零差异），按钮只是
        //    "点得到但看不出按下"。hovered / clicked 从同一次命中测试里取。
        const kit::Hit hit = ChromeHit(box, kJumpIds[i]);
        DrawRoundRect(draw, box.min, box.max, 6.0f,
                      hit.hovered ? ColorOf(theme::CurrentDerived().jumpBtnBg) : ColorAccentDim(),
                      ColorAccentGlow(), 1.0f);

        const char* label = jumps[i].label;
        const float text = TextWidth(label, labelStyle);
        const float iconSize = 13.0f;
        // 图标与文字 gap 4（.btn.sm 的 gap），两者合计在格子里居中。
        // 走 `kit::PlaceIconText`：这条居中算式原先在这里是**第四份拷贝**
        // （Button / Tag / CenterTextX 各一份），现在只有一处实现。
        const IconTextLayout content = PlaceIconText(box, iconSize, 4.0f, text, IconTextAlign::Center);
        const float cy = box.center().y;
        DrawIcon(draw, jumps[i].icon, ImVec2(content.iconX, cy - iconSize * 0.5f), iconSize,
                 ColorAccent());
        // 竖直居中走 `kit::CenterY`（= `CenterTextY` 的 token 版，语义写在 Draw.h：
        // ImGui 的行盒高**就等于请求字号**，所以 `cy − size/2` 居中的是行盒）。
        Text(draw, ImVec2(content.textX, CenterY(cy, labelStyle)), label, labelStyle);

        if (hit.clicked) {
            // 设计稿就是 setWorkspace(ws) + notify(一句话)，**不带 tab、不带选中项**：
            // 章节上下文靠全局选中态自然带过去（Shell.jsx:619）。
            const BookSideView& book = BookSide();
            std::string context = "未选章节";
            if (book.selectedChapter >= 0 &&
                book.selectedChapter < static_cast<int>(book.chapters.size())) {
                const BookChapterView& chapter =
                    book.chapters[static_cast<std::size_t>(book.selectedChapter)];
                context = kit::Fmt("第 {} 章", chapter.ord);
                if (!chapter.title.empty()) {
                    context += kit::Fmt("「{}」", chapter.title);
                }
            }
            SetWorkspace(static_cast<int>(jumps[i].target));
            Notify(std::string(label) + " · 上下文：" + context, theme::Tone::Ok);
        }
    }
    // 标签 + 网格(2 行) + 底部外边距 —— 唯一一份占高算式，外面按它推进 y。
    return labelH + kCellH * 2.0f + kGap + kMarginBottom;
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
namespace {

// ---- 侧栏树：行为层换 ImGui 原生 TreeNodeEx（流式布局，2026-11-23）----
//
// 换掉 `kit::Tree`（自绘行 + HitTestItem + 手算内容高）的三个理由，正好是它的三个坑：
//   * 跨父 id 撞车 —— 行 id 由「PushID(同父下标) + 父行的 TreePushOverrideID」派生，
//     不同父下的同名行天然不同键；原实现得手拼先序路径键（`<父键>##<下标>`）防撞，
//     而 TreeNodeEx 的 ID 栈天生带父作用域；
//   * 热区越界 —— 行是真 item，命中与绘制同受 child 裁剪矩形管辖；原实现的
//     HitTestItem 不看裁剪区，热区登记漏到面板外会压住底栏控件（`duplicate-hits`
//     从 0 跳到 425 的那次），这条路现在根本不存在；
//   * setContentHeight —— 真 item 自动喂 ContentSize，滚动不再依赖手报内容高
//     （「漏调 = 滚不动」那个坑）。ScrollRegion 对没自报的区域回落到 ImGui 自己的
//     ScrollMax（见 Scroll.cpp 构造注释）。
//
// 行底与 hover / 选中色放在背景通道，TreeNodeEx 的箭头、图标和文字留在前景通道。
//
// ⚠️ hover 探针桥：原生 item 不走 kit::HitTestItem，命中计数不会自己进
//    `HoveredItemCount` —— 每行手动 `NoteHoveredItem` 一次，否则 hover-* 探针会把
//    「命中数=0」报成「探针坐标点空」，与真坐标偏移同形。
constexpr float kTreeRowH = 28.0f;  // .tree .node（ui.css:775-786）height 28

void DrawTreeRowDecor(ImDrawList* draw, const Rect& row, const TreeNode& node, bool on,
                      bool hovered, ImGuiID tid) {
    draw->ChannelsSetCurrent(0);
    draw->AddRectFilled(row.min, row.max, ColorSurface());
    if (on) {
        draw->AddRectFilled(row.min, row.max, ColorFillSelected());
        draw->AddRectFilled(ImVec2(row.min.x, row.min.y), ImVec2(row.min.x + 2.0f, row.max.y),
                            ColorAccent());
    } else {
        // hover 补间（ch0，kDurFast）；settle 在「无填充」一侧目标是全透明 ×不画。
        const ImU32 fillNow =
            TransitionColorTo(tid, 0, hovered ? ColorFillHover() : ColorTransparent(), kDurFast);
        if (fillNow != 0) {
            draw->AddRectFilled(row.min, row.max, fillNow);
        }
    }
    draw->ChannelsSetCurrent(1);

    // 文字色补间（ch3）：选中/悬停 primary，其余 secondary。
    const ImU32 fg = TransitionColorTo(tid, 3, on || hovered ? ColorText() : ColorTextSecondary(),
                                       kDurFast);
    float x = row.min.x + 8.0f;
    if (node.hasChildren) {
        x += 14.0f + theme::space::kXs;  // 预留原生箭头的位置
    }
    if (!node.icon.empty()) {
        DrawIcon(draw, node.icon, ImVec2(x, row.center().y - 6.0f), 12.0f, ColorTextMuted());
        x += 12.0f + 6.0f;
    }
    const TextStyle label{.size = theme::font::kHint, .color = fg};
    const TextStyle trailing{.size = theme::font::kTiny, .color = ColorTextMuted()};
    const bool hasTrailing = !node.trailing.empty();
    const float trailingW = hasTrailing ? TextWidth(node.trailing, trailing) : 0.0f;
    const float trailingX = row.max.x - 8.0f - trailingW;
    const float titleRight = hasTrailing ? trailingX - theme::space::kXs : trailingX;
    TextClipped(draw, ImVec2(x, CenterY(row.center().y, label)), titleRight - x, node.label, label);
    if (hasTrailing) {
        TextClipped(draw, ImVec2(trailingX, CenterY(row.center().y, trailing)), trailingW,
                    node.trailing, trailing);
    }
}

void DrawSideTreeLevel(ImDrawList* draw, std::vector<TreeNode>& nodes, int& counter,
                       int& selectedIndex, std::string_view idPrefix) {
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        TreeNode& node = nodes[i];
        // 先序扁平序号（收起的子树不占号），与 FlatTreeIndex / AssetLeaf* 同口径。
        const int index = counter++;
        const bool on = (index == selectedIndex);
        ImGui::PushID(static_cast<int>(i));
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                   ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_FramePadding;
        if (!node.hasChildren) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        if (on) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        if (node.hasChildren) {
            // 展开态的唯一真相是 nodes[i].expanded（折叠集合→指纹→重建），逐帧强制；
            // 点箭头时就地翻一次（与旧版 twisty 同语义，下次重建以集合为准）。
            ImGui::SetNextItemOpen(node.expanded, ImGuiCond_Always);
        }
        const bool open = ImGui::TreeNodeEx("row", flags, "%s", "");
        const bool toggled = ImGui::IsItemToggledOpen();
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked();
        const Rect row{ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
        const ImGuiID tid = ImGui::GetItemID();
        if (toggled) {
            node.expanded = !node.expanded;  // 点箭头只翻折叠（不改选中）
        } else if (clicked) {
            selectedIndex = index;  // 点行体只报命中，语义由调用方裁定
        }
        if (hovered) {
            char hitId[64];
            std::snprintf(hitId, sizeof(hitId), "%.*s#%d", static_cast<int>(idPrefix.size()),
                          idPrefix.data(), index);
            NoteHoveredItem(hitId);
        }
        DrawTreeRowDecor(draw, row, node, on, hovered, tid);
        if (open && node.hasChildren) {
            const float lineX = row.min.x + 14.0f;
            const float childTop = ImGui::GetCursorScreenPos().y;
            DrawSideTreeLevel(draw, node.children, counter, selectedIndex, idPrefix);
            const float childBottom = ImGui::GetCursorScreenPos().y;
            draw->AddLine(ImVec2(lineX, childTop), ImVec2(lineX, childBottom), ColorLineSubtle(),
                          1.0f);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

// 原生树入口：行高 28 由 FramePadding.y=(28-字号)/2 保证（TreeNodeEx 的行高 =
// 字号 + 2*padding.y），字号钉在 kHint（12.5）——行距与旧版 28 逐像素相同。
void DrawSideTree(ImDrawList* draw, std::vector<TreeNode>& nodes, int& selectedIndex,
                  std::string_view idPrefix) {
    if (nodes.empty()) {
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(8.0f, (kTreeRowH - theme::font::kHint) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 20.0f);  // 子层缩进 20/depth（.kids 14+6）
    PushUiFont(theme::font::kHint);
    ImGui::PushStyleColor(ImGuiCol_Text, ColorTextMuted());
    ImGui::PushStyleColor(ImGuiCol_Header, ColorTransparent());
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ColorTransparent());
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ColorTransparent());
    draw->ChannelsSplit(2);
    draw->ChannelsSetCurrent(1);
    int counter = 0;
    DrawSideTreeLevel(draw, nodes, counter, selectedIndex, idPrefix);
    draw->ChannelsMerge();
    ImGui::PopStyleColor(4);
    PopUiFont();
    ImGui::PopStyleVar(3);
}

} // namespace

void Shell::DrawAssetSideTree(Rect area, ImDrawList* draw, float y) {
    const BookSideView& book = BookSide();
    const float x = area.min.x + 10.0f;
    const float w = area.max.x - 20.0f - x;
    // R4：本函数的纵向推进收进 Stack（标题行盒 / 进度条+间距 / 空态+间距）。
    Stack side{Rect{x, y, x + w, area.max.y}};

    // ---- 标题行：masks 图标 + 视觉资产 + {ready}/{total} 就绪 ----
    // 设计稿是 space-between（Shell.jsx:530），所以就绪数**右对齐**到行尾；
    // 画在 x 上会跟图标和标题叠在一起（侧栏只有 240px，叠了就是一团糊）。
    DrawIcon(draw, "masks", ImVec2(x, side.Cur()), 14.0f, ColorAccent());
    static constexpr char kTitle[] = "视觉资产";
    Text(draw, ImVec2(x + 20.0f, side.Cur()), std::string_view(kTitle, sizeof(kTitle) - 1),
         TextStyle{.size = theme::font::kHint, .bold = true, .color = ColorText()});
    // 标题行 / 进度条 / chip 行 / 树节点全来自 RefreshAssetSideTree 的指纹缓存。
    RefreshAssetSideTree(book);
    const std::string& readyText = assetReadyText_;
    // 「量出来的宽」与「画出来的宽」现在走同一个 `TextStyle`（收编前是两个各自
    // 写着 `FontAt(11.0f), 11.0f` 的调用点 —— 改一档很容易漏另一档）。
    const TextStyle readyStyle{.size = theme::font::kMicro, .color = ColorTextMuted()};
    const float readyW = TextClippedWidth(w, readyText, readyStyle);
    TextClipped(draw, ImVec2(x + w - readyW, side.Cur()), w, readyText, readyStyle);
    // 行盒 = 12.5 × 1.6（kHint），原先写死 20.0f —— 同一个数，现在由 token 推。
    side.Advance(LineBox(theme::font::kHint));

    // ---- 进度条（.prog.thin 4px）----
    const float total = static_cast<float>(book.assets.size());
    Progress(draw, side.Next(ProgressHeight(true), 10.0f),
             total > 0.0f ? static_cast<float>(assetReadyCount_) / total : 0.0f, false, true);

    // ---- chip 行：全部 + 实际出现的 kind（分组顺序 = 首次出现的顺序，与 ListEntities
    //      的 ORDER BY id 一致，不另排字母序）—— 标签 / 计数 / 命中 id 全来自缓存 ----
    const auto& filter = BookKindFilter();
    {
        // R4：chip 行的 flex-wrap 收进 Flow —— 折行判式与「行高 ch + 6」原先
        // 手写在循环里，放不下才折、行距跟着 chip 高走，正是 Flow 的形状。
        Flow chips{Rect{x, side.Cur(), x + w, area.max.y}, 6.0f, 6.0f};
        const float ch = ChipHeight(true);
        ChipSpec all{!filter, true, ""};
        if (Chip(draw, chips.Place(ChipWidth("全部", all), ch), "全部", all, "asset-kind-all")) {
            SetBookKindFilter({});
        }
        for (const AssetKindChip& chip : assetKindChips_) {
            ChipSpec spec{filter == chip.kind, true, chip.count};
            if (Chip(draw, chips.Place(ChipWidth(chip.label, spec), ch), chip.label, spec, chip.id)) {
                SetBookKindFilter(spec.selected ? std::nullopt : std::optional(chip.kind));
            }
        }
        // 折行后的行顶交还 Stack，再加 chip 高 + 10（原 `y += ch + 10`）。
        side.Seek(chips.Cur() + ch + 10.0f);
    }

    // ---- 树：kind 分组 → 实体叶子（节点由 RefreshAssetSideTree 缓存）----
    std::vector<TreeNode>& nodes = assetTreeNodes_;
    std::vector<int>& leafAsset = assetTreeLeaf_;     // 叶子序号 → BookSideView::assets 下标
    std::vector<std::string>& kinds = assetTreeKinds_;  // 分组序号 → **原始 kind 英文值**

    if (nodes.empty()) {
        const Rect emptyArea{x, side.Cur(), x + w, area.max.y - 34.0f};
        Empty(draw, emptyArea, "masks", "没有这一类资产",
              "换回「全部」看看这个工程里有哪些实体");
    } else {
        // 可见节点扁平序号（先序）↔ 资产下标。侧栏树的行走器只认一个 int，且收起的
        // 子树不占序号 —— 与章 / 镜树同一套先序约定，这里是它的资产版（互转在
        // Shell_Sidebar_Tree.cpp）。
        const int wanted = AssetLeafFlat(nodes, leafAsset, book.selectedAsset);
        int picked = wanted;
        // ---- 资产树的可滚区（2026-09-30）----
        //
        // 这棵树**最容易溢出**：13 个 kind 组 + 17 个叶子 = 30 行（840px），而可视区
        // 只有 ~630px。以前它是「放不下就不画」—— 后面的组与叶子**在界面上够不着**，
        // 而截图上看不出区别（都只拍到前面几组）。
        // 与小说侧栏那棵共用同一套写法与同一个 `noMouseInputs` 判定；两棵树按工作区
        // 互斥出现，所以各自一个 ScrollRegion id（取证判据打的是**这一棵**，
        // 因为只有它在当前 fixture 下必然溢出）。
        const Rect treeArea{x, side.Cur(), x + w, area.max.y - 34.0f};
        kit::ScrollRegion treeScroll("asset-tree-scroll", treeArea, /*borders=*/false,
                                     /*horizontalScroll=*/false,
                                     /*noMouseInputs=*/OverlayBlocksMouse());
        if (treeScroll) {
            // 内容画在 child 自己的 draw list 上，否则裁剪无效（见 Scroll.h）。
            // 原生 TreeNodeEx 树（2026-11-23）：不再 walkBounds 截断、不再 setContentHeight
            // —— 真 item 自带裁剪与 ContentSize，滚动由 ScrollRegion 回落读 ImGui 的
            // ScrollMax（见 DrawSideTree 实现注释与 Scroll.cpp 构造回落块）。
            DrawSideTree(treeScroll.drawList(), nodes, picked, "asset-tree");
        }
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
    Text(draw, ImVec2(x + 14.0f, y), root,
         TextStyle{.size = theme::font::kLabel, .bold = true, .color = ColorTextMuted()});
    y += 22.0f;

    const char* scope = WorkspaceLabel(layout_.workspace);
    Text(draw, ImVec2(x + 6.0f, y), scope,
         TextStyle{.size = theme::font::kHint, .color = ColorTextSecondary()});
    y += 26.0f;

    const auto emptyArea = [&] { return Rect{x, y, area.max.x - 10.0f, area.max.y}; };
    if (layout_.projectRoot.empty()) {
        Empty(draw, emptyArea(), "book", "未打开项目",
              "从顶栏项目胶囊回项目中心新建或打开，这里会列出真实的章节 / 镜头 / 资产");
        return;
    }
    if (!WorkspaceHasSideTree(layout_.workspace) && !WorkspaceHasAssetTree(layout_.workspace)) {
        Empty(draw, emptyArea(), "book", "此工作区没有侧栏树",
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
        // 占高由 DrawJumpButtons 自己报（标签 + 网格 + 底部外边距 = 84.8）。
        // ⚠️ 这里曾写死 `y += 64.0f`，比真实占高少 20.8 —— 第二行按钮因此压住树的第一行。
        y += DrawJumpButtons(Rect{x, y, area.max.x - 20.0f, y + 84.8f}, draw);
    }

    const BookSideView& book = BookSide();
    if (!book.bound) {
        Empty(draw, emptyArea(), "book", "结构尚未载入",
              "切到小说 / 资产任一页会读取 novel.db 里的真实结构");
        return;
    }
    // 「正在读取」只在**还没有任何数据**时显示；已有数据时加载中照常显示旧树
    // （stale-while-revalidate）—— 切章 / 重读都会翻 loading，把整棵列表换成
    // 「正在读取」再重建，就是「每点一章列表刷新一次」的元凶（500 章时最明显）。
    if (book.loading && book.chapters.empty() && book.assets.empty()) {
        Empty(draw, emptyArea(), "book", "正在读取 novel.db",
              "库打开后会在这里列出章节与镜头");
        return;
    }
    if (!book.error.empty()) {
        Empty(draw, emptyArea(), "book", "读取失败", book.error);
        return;
    }

    // 资产工作区走 kind 筛选树（Shell.jsx:519-580），不吃章 / 镜那一套。
    if (WorkspaceHasAssetTree(layout_.workspace)) {
        if (book.assets.empty()) {
            Empty(draw, emptyArea(), "masks", "这个工程还没有实体",
                  "跑一次初始化链（T1–T17）之后这里才有实体与资产");
            return;
        }
        DrawAssetSideTree(area, draw, y);
        return;
    }

    if (book.chapters.empty()) {
        Empty(draw, emptyArea(), "book", "这本小说还没有章",
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
    // 卷 / 章 / 镜节点由 RefreshNovelSideTree 按指纹缓存（原先每帧 Fmt 重建一遍；
    // 实现搬去了 Shell_Sidebar_Tree.cpp）。
    RefreshNovelSideTree(book, withShots);
    std::vector<TreeNode>& volumes = novelTreeVolumes_;
    const std::vector<std::pair<int, std::string>>& volumeOrder = novelTreeVolumeOrder_;

    // ⚠️ 卷行的扁平号**不是** volumes 的下标（2026-11-23 修）：先序里卷 0 占 0、
    //    它的章占 1..k、卷 1 才占 k+1 —— 顶层节点不占前 N 个号。原注释
    //    「顶层节点总是前 N 个，扁平序号就等于 volumes 里的下标」是错的，
    //    配套的 `picked < volumes.size()` 捷径把三种点击全弄错（点第 1 章翻
    //    第二卷、点第二卷翻第三卷、点第三卷没反应）。判卷行走 VolumeAt
    //    （Shell_Sidebar_Tree.cpp，与 walker counter 同一套先序）。
    const auto wanted = withShots
                            ? FlatTreeIndex(volumes, book.selectedChapter, book.selectedShot)
                            : FlatTreeIndex(volumes, book.selectedChapter, -1);
    int picked = wanted;
    const Rect treeArea{x, y, area.max.x - 20.0f, area.max.y - 10.0f};
    // ---- 侧栏树的可滚区（2026-09-30）----
    //
    // ⚠️ 之前树**没有滚动**：把 fixture 从 3 章铺到 12 章之后，第 7 章起在侧栏里
    //    **够不着**（旧 `kit::Tree` 只按可用高度截断多余的行 —— 那一步是为了不让热区
    //    越界压到底栏上，见 `Widget_Tree.cpp`；但当时它连内容高度一起截断了，
    //    于是套滚动区也滚不动）。2026-11-23 换成原生 TreeNodeEx 后连这一层都不需要了：
    //    真 item 自动喂 ContentSize，滚动上限由 ScrollRegion 回落读 ImGui 的 ScrollMax
    //    —— 设计稿的侧栏本来也是可滚的。
    //
    // ⚠️ `noMouseInputs` 必须跟浮层开态走：这一层是 `BeginChild`，鼠标在它范围内时
    //    `g.HoveredWindow` 就是它，根窗口里那四个浮层的 item 会一律 hovered=false ⇒
    //    **按钮画得出来、点不动**（而这在静息截图上看不出来）。与工作区那层共用
    //    `OverlayBlocksMouse()`。
    kit::ScrollRegion treeScroll("side-tree-scroll", treeArea, /*borders=*/false,
                                 /*horizontalScroll=*/false, /*noMouseInputs=*/OverlayBlocksMouse());
    if (treeScroll) {
        // 内容画在 child **自己的** draw list 上，否则裁剪无效（见 Scroll.h）。
        // 原生 TreeNodeEx 树（2026-11-23）：不再 walkBounds 截断、不再 setContentHeight
        // —— 真 item 自带裁剪与 ContentSize，滚动由 ScrollRegion 回落读 ImGui 的
        // ScrollMax（见 DrawSideTree 实现注释与 Scroll.cpp 构造回落块）。
        DrawSideTree(treeScroll.drawList(), volumes, picked, "side-tree");
    }
    if (picked == wanted) {
        return;  // 没点中
    }
    const int volumeRow = VolumeAt(volumes, picked);
    if (volumeRow >= 0) {
        // 卷行只切展开 / 收起，不选中任何章（与资产树的 kind 分组行同语义，
        // 设计稿的卷行 onClick 也只有 setOpen）。
        const int key = volumeOrder[static_cast<std::size_t>(volumeRow)].first;
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

} // namespace shine::pages
