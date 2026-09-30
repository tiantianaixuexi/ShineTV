#pragma once
// shine::kit::Widget_Tree —— 树（Tree）
//
// 从 kit/Widgets.cpp 拆出。

#include "ui/imgui/kit/Widget_Core.h"

#include <string>
#include <string_view>
#include <vector>

namespace shine::kit {

// ---- 22. Tree（ui.css:770-811 .tree）----
// 节点 h28 pad 0 8 r6 gap6 12.5；hover = fill-hover + primary；
// 选中 = fill-selected + primary + 左侧 2px accent；tw 14×14 muted，展开旋转 90°；
// 子层 margin-left 14 + 1px line-subtle 竖线 + padding-left 6。
struct TreeNode {
    std::string label;
    std::string icon;     // 可空（叶子）
    std::string trailing; // 右侧计数/状态文字（Shell.jsx:490 的 3/8）
    bool hasChildren = false;
    bool expanded = false;
    std::vector<TreeNode> children;
};
// selectedIndex 是**可见节点**的扁平序号（先序），点击后写回。返回占用高度。
// 展开态存在 nodes 上（调用方持有），组件本身无跨帧状态 —— 与 Views.h 的约定一致。
float Tree(ImDrawList* draw, Rect bounds, std::vector<TreeNode>& nodes, int& selectedIndex,
           std::string_view id);
[[nodiscard]] float TreeNodeHeight();

} // namespace shine::kit
