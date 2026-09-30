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
            twistClicked = detail::HitTestItem(twist, detail::UniqueId(std::string(id) + "#t" + std::to_string(depth),
                                                    static_cast<int>(i)))
                               .clicked;
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
