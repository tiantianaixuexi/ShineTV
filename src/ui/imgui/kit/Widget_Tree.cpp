#include "ui/imgui/kit/Widget_Tree.h"

#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Icon.h"
#include "ui/imgui/kit/Widget_Color.h"
#include "ui/imgui/kit/Widget_Core.h"

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
               int& counter, int depth, std::string_view id) {
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
        // 边界：完全放不下就整行不画也不登记（不画半行 —— 半个行高的可点区域比没有更糟）。
        if (y + rowH > bounds.max.y) {
            return y;
        }
        TreeNode& node = nodes[i];
        const int index = counter++;
        const Rect row = RectAt(bounds.min.x + 20.0f * static_cast<float>(depth), y,
                                bounds.width() - 20.0f * static_cast<float>(depth), rowH);
        const bool on = selectedIndex == index;
        const Hit hit =
            detail::HitTestItem(row, detail::UniqueId(std::string(id) + "#" + std::to_string(depth), static_cast<int>(i)));

        // 折叠箭头单独占一个 hit item（.tw，14×14）：它**套在行矩形里面**，
        // 所以点它会连带把整行的 hit 也触发 → 折叠的同时还会改选中项。
        // 设计稿里两者是同一个 onClick（Shell.jsx:486），但 ImGui 的 item 栈
        // 会让两个都算「点到」，所以这里显式让位：点箭头只翻折叠。
        bool twistClicked = false;
        if (node.hasChildren) {
            const Rect twist = RectAt(row.min.x + 8.0f, row.center().y - 7.0f, 14.0f, 14.0f);
            const std::string twistId =
                detail::UniqueId(std::string(id) + "#t" + std::to_string(depth),
                                 static_cast<int>(i));
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
        } else if (hit.hovered) {
            draw->AddRectFilled(row.min, row.max, ColorFillHover());
        }
        const ImU32 fg = on ? ColorText() : (hit.hovered ? ColorText() : ColorTextSecondary());
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
                         node.children, selectedIndex, counter, depth + 1, id);
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
