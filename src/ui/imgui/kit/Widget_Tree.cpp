#include "ui/imgui/kit/Widget_Tree.h"

#include "ui/imgui/kit/Anim.h"
#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Icon.h"
#include "ui/imgui/kit/Widget_Color.h"
#include "ui/imgui/kit/Widget_Core.h"

#include <cstdio>
#include <string>

namespace shine::kit {

// ------------------------------------------------------------------ 22 Tree
float TreeNodeHeight() {
    // .tree .node（ui.css:775-786）：height 28。
    return 28.0f;
}

namespace {

// 先序遍历的扁平计数 + 画一层的递归。visibleIndex 用来把「可见序号」和
// 选中项对齐：折叠的子树不占序号（与 Shell.jsx 里 open[ch] 决定渲染一致）。
float TreeWalk(ImDrawList* draw, Rect bounds, std::vector<TreeNode>& nodes, int& selectedIndex,
               int& counter, int depth, std::string_view parentKey) {
    const float rowH = TreeNodeHeight();
    float y = bounds.min.y;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        // ⚠️⚠️ **不许越过 `bounds.max.y`** —— 绘制与命中一起截断。
        //
        // 这里原来一行行往下铺、既不画也不判界，于是「树比可用高度长」时：
        //   · 行被画到面板**外面**（侧栏的行压到底栏上）；
        //   · 更要命的是**热区也登记在外面** —— `HitTestItem` 不看裁剪区，
        //     而 ImGui 是**先注册者独占**，于是底栏那一带的控件（页签、报告行）
        //     在那段 x 区间里**点不动**：控件画得出来、就是按不下去。
        //
        // 实测（2026-09-30，把 fixture 从 3 章铺到 12 章之后）：
        //   `duplicate-hits` 从 **0 跳到 425**，诊断指名道姓 ——
        //   `dock-report-0 min=(14,793) max=(574,819) 与 min=(66,786) max=(276,814) 交叠 210x21`，
        //   对方那串 210×28、纵向步进 28 的矩形正是**侧栏树的章行**。
        //   ⚠️ 而**截图上看不出来**：树被裁掉了（所以图上那片区域是干净的），
        //      只有热区登记漏在外面。这正是「判据抓得住、眼睛看不出来」的那一类。
        //
        // 边界：完全放不下就整行**不登记命中**（不画半行 —— 半个行高的可点区域比没有更糟）。
        //
        // ⚠️ 但**只跳命中，不跳累加**：`counter` 与 `y` 都无条件推进。两条都必要：
        //   · `counter` 无条件 —— `selectedIndex` 是按**完整**扁平序算出来的
        //     （调用方的 `FlatTreeIndex`），少算一行就会选错节点，而且只在滚下来之后才看得出；
        //   · `y` 无条件 —— 返回值是**完整内容高度**，调用方拿它喂
        //     `ScrollRegion::setContentHeight`，内容才滚得动（不给高度 = 这个区域永远滚不动，
        //     见 Scroll.h 的说明）。
        // 早先这里是 `return y`：高度被截断成「可见高度」⇒ 套上滚动区也滚不动，
        // 而且下半截内容**永远够不着**。
        const bool visible = (y + rowH <= bounds.max.y);
        TreeNode& node = nodes[i];
        const int index = counter++;
        const Rect row = RectAt(bounds.min.x + 20.0f * static_cast<float>(depth), y,
                                bounds.width() - 20.0f * static_cast<float>(depth), rowH);
        const bool on = selectedIndex == index;
        // 行 id = **先序路径键**（"<父行键>##<同父下标>"，顶层父键 = 控件 id）在栈上拼：
        // 每帧 × 每行都走这里，不许堆分配；缓冲装不下才退回堆版本，产物逐字节一致。
        //
        // ⚠️ 原格式 "<id>#<depth>##<i>" 只带深度与同父下标、**不带父节点身份** ⇒
        //    跨父撞车：「第一卷第 0 章」与「第二卷第 0 章」同键（side-tree#1##0），
        //    两行同时可见时 ImGui 报 conflicting ID、后注册那行**点不动**（间歇出现，
        //    取决于滚动位置与抓帧；第二十三轮定位、本轮修）。路径键天然唯一。
        //    「改 id 会动 hover 探针读数」的顾虑已核：hover-tree-node 按**坐标**注入
        //    （ReviewHoverProbe 表：kWsAssets 170,210），不认 id 串，改键安全。
        char rowBuf[224];
        std::string rowOverflow;
        const int rn = std::snprintf(rowBuf, sizeof(rowBuf), "%.*s##%d",
                                     static_cast<int>(parentKey.size()), parentKey.data(),
                                     static_cast<int>(i));
        const bool rowKeyFits = rn >= 0 && static_cast<std::size_t>(rn) < sizeof(rowBuf);
        if (!rowKeyFits) {
            rowOverflow = detail::UniqueId(parentKey, static_cast<int>(i));
        }
        const std::string_view rowKey(rowKeyFits ? rowBuf : rowOverflow.c_str(),
                                      rowKeyFits ? static_cast<std::size_t>(rn) : rowOverflow.size());
        const Hit hit = [&] {
            if (!visible) {
                return Hit{};
            }
            return detail::HitTestItemId(row, rowKey.data());
        }();

        // 折叠箭头单独占一个 hit item（.tw，14×14）：它**套在行矩形里面**，
        // 所以点它会连带把整行的 hit 也触发 → 折叠的同时还会改选中项。
        // 设计稿里两者是同一个 onClick（Shell.jsx:486），但 ImGui 的 item 栈
        // 会让两个都算「点到」，所以这里显式让位：点箭头只翻折叠。
        bool twistClicked = false;
        if (visible && node.hasChildren) {
            const Rect twist = RectAt(row.min.x + 8.0f, row.center().y - 7.0f, 14.0f, 14.0f);
            // 折叠箭头的 id 同样栈上拼（"<行键>#t"；行键已含父路径，天然唯一）。
            char twistBuf[224];
            std::string twistOverflow;
            const int tn = std::snprintf(twistBuf, sizeof(twistBuf), "%.*s#t",
                                         static_cast<int>(rowKey.size()), rowKey.data());
            const std::string_view twistId =
                tn >= 0 && static_cast<std::size_t>(tn) < sizeof(twistBuf)
                    ? std::string_view(twistBuf, static_cast<std::size_t>(tn))
                    : (twistOverflow = std::string(rowKey) + "#t",
                       std::string_view(twistOverflow));
            // ⚠️ 必须声明：twisty 是**故意**套在行矩形里面的（见本段开头那段说明），
            //    而 `kit::HitTestItem` 会把每个矩形登记进本帧重叠自检表。
            //    不声明的话，小说侧栏那两个箭头会被逐帧记成「重复命中」，
            //    取证的 duplicate-hits 判据恒红（实测 815 次/轮，全是这里）。
            DeclareNestedHit(twistId);
            twistClicked = detail::HitTestItem(twist, twistId).clicked;
            if (twistClicked) {
                node.expanded = !node.expanded;
            }
        }
        if (hit.clicked && !twistClicked) {
            selectedIndex = index;
        }

        if (on) {
            draw->AddRectFilled(row.min, row.max, ColorFillSelected());
            draw->AddRectFilled(ImVec2(row.min.x, row.min.y), ImVec2(row.min.x + 2.0f, row.max.y),
                                ColorAccent());
        } else {
            // 补间版：节点 hover 填色硬切换走 ch0 色补间（kDurFast，含 alpha）；settle 在
            // 「无填色」一侧目标是全透明 ⇒ 不画，与原样逐像素相同。
            // ⚠️ 纯展示行（!visible ⇒ Hit{}，tid=0）不接补间 —— id=0 会跨控件撞车；
            //    走原硬切换，而它的 hit.hovered 恒 false，与原样一样什么都不画。
            const ImU32 rowFillNow =
                (hit.tid != 0)
                    ? TransitionColorTo(hit.tid, 0, hit.hovered ? ColorFillHover() : ImU32(0),
                                        kDurFast)
                    : (hit.hovered ? ColorFillHover() : ImU32(0));
            if (rowFillNow != 0) {
                draw->AddRectFilled(row.min, row.max, rowFillNow);
            }
        }
        // 补间版：节点文字色硬切换走 ch3 色补间（kDurFast）。目标式与原硬切换逐字相同
        // （选中行恒 ColorText()）；tid=0 的行同上走原式。
        const ImU32 fg =
            (hit.tid != 0)
                ? TransitionColorTo(
                      hit.tid, 3,
                      on ? ColorText() : (hit.hovered ? ColorText() : ColorTextSecondary()), kDurFast)
                : (on ? ColorText() : (hit.hovered ? ColorText() : ColorTextSecondary()));
        float x = row.min.x + 8.0f;
        if (node.hasChildren) {
            // .tw（ui.css:796-806）：14×14 muted，收起朝右，展开旋转 90° 朝下。
            // 不复用 DrawIcon("chevron")：图标是按路径描边的，没有旋转入口，
            // 只能自己画两条线段 —— 两态各给一组端点，避免在运行时做三角函数。
            const float cx = x + 7.0f;
            const float cy = row.center().y;
            const float r = 3.5f;
            const ImVec2 apex = node.expanded ? ImVec2(cx - r, cy) : ImVec2(cx, cy);
            const ImVec2 a0 = node.expanded ? ImVec2(cx, cy - r) : ImVec2(cx - r, cy - r);
            const ImVec2 a1 = node.expanded ? ImVec2(cx, cy + r) : ImVec2(cx - r, cy + r);
            draw->AddLine(a0, apex, ColorTextMuted(), 1.5f);
            draw->AddLine(a1, apex, ColorTextMuted(), 1.5f);
            x += 14.0f + 6.0f; // .tw 宽 14 + .node 的 gap 6
        }
        if (!node.icon.empty()) {
            // Shell.jsx:488 里的节点头图标是 12px（比 .tw 小）。
            DrawIcon(draw, node.icon, ImVec2(x, row.center().y - 6.0f), 12.0f, ColorTextMuted());
            x += 12.0f + 6.0f;
        }
        const float trailingWidth =
            node.trailing.empty()
                ? 0.0f
                : FontAt(12.0f)->CalcTextSizeA(12.0f, 1e9f, 0.0f, node.trailing.data(),
                                               node.trailing.data() + node.trailing.size())
                          .x;
        ImFont* font = FontAt(12.5f);
        // 原来写死 `row.center().y - 7.5f` —— 12.5px 字该减 6.25，偏上 1.25px。
        DrawTextClipped(draw, font, 12.5f, ImVec2(x, CenterTextY(font, 12.5f, row.center().y)),
                        row.max.x - x - 8.0f - trailingWidth, fg, node.label);
        if (!node.trailing.empty()) {
            // 右侧计数用 .tiny dim（12px muted）。
            // 原来写死 `row.center().y - 7.0f` —— 12px 字该减 6.0，偏上 1.0px。
            ImFont* tfont = FontAt(12.0f);
            DrawTextClipped(draw, tfont, 12.0f,
                            ImVec2(row.max.x - 8.0f - trailingWidth,
                                   CenterTextY(tfont, 12.0f, row.center().y)),
                            trailingWidth, ColorTextMuted(), node.trailing);
        }
        y += rowH;

        if (node.hasChildren && node.expanded) {
            // .kids（ui.css:807-811）：margin-left 14 + 1px line-subtle 竖线 + padding-left 6。
            // 我们把行整体右移了 20（14+6），竖线画在子层最左。
            const float lineX = row.min.x + 14.0f;
            const float childTop = y;
            y = TreeWalk(draw, Rect{ImVec2(bounds.min.x, y), ImVec2(bounds.max.x, bounds.max.y)},
                         node.children, selectedIndex, counter, depth + 1, rowKey);
            draw->AddLine(ImVec2(lineX, childTop), ImVec2(lineX, y), ColorLineSubtle(), 1.0f);
        }
    }
    return y;
}

} // namespace

float Tree(ImDrawList* draw, Rect bounds, std::vector<TreeNode>& nodes, int& selectedIndex,
           std::string_view id) {
    if (nodes.empty()) {
        return 0.0f;
    }
    int counter = 0;
    return TreeWalk(draw, bounds, nodes, selectedIndex, counter, 0, id) - bounds.min.y;
}

} // namespace shine::kit
