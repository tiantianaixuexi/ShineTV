#pragma once
// shine::pages —— 侧栏两棵树的**先序约定与展平互转**（2026-10-02 从
// Shell_Sidebar.cpp 拆出）
//
// 为什么单独一份头：这一族函数原先住在 Shell_Sidebar.cpp 的匿名命名空间里，
// 而拆完之后**两个文件都要用它们** ——
//   * Shell_Sidebar_Tree.cpp：造节点（Refresh*SideTree 的指纹缓存）；
//   * Shell_Sidebar.cpp：把「点中第几个可见节点」翻译回「选中第几章 / 第几镜 /
//     第几个资产」，那是命中处理，不该住在数据准备那一半。
// 匿名命名空间跨不过文件边界，所以它们必须先有声明。
//
// ⚠️ **它们对外是 `shine::pages` 里的内部工具，不是公共 API**：没有别的 .cpp
//    应当调用它们。声明公开只是为了让两份实现文件看见同一份约定 —— 与
//    `PageCommon.h` 收 `LabelWidth` 是同一个理由。
//
// ---- 一句话约定 ----
// `kit::Tree` 只认**一个 int**，且只画**展开**节点的子节点。所以「用户点中的
// 那个节点」在这两棵树里都不是业务下标，而是**可见节点的先序（扁平）序号**。
// 下面六个别名是**同一套先序**的互转：漏一个方向就是「点 A 高亮 B」。
#include "ui/imgui/kit/Widget_Tree.h"
#include "ui/imgui/pages/BookData.h"

#include <string>
#include <vector>

namespace shine::pages {

// 树是**三层**（卷 → 章 → 镜，设计稿 Shell.jsx:583-662 的 NovelSideTree）。
// ⚠️ `chapter` 是 `BookSideView::chapters` 里的**全局下标**，不是「某卷内的下标」
//    —— 所以内部用独立的 `seen` 计数器走遍**所有**卷，哪怕卷收起了（收起的卷不占
//    扁平序号，但它的章仍然存在于 chapters 里）。用「当前卷内下标」比就会在
//    第二卷之后整体错位：点第 5 章高亮第 1 章。
//
// 扁平序号 → (章下标, 镜下标)。镜下标 = -1 表示命中的是章节点；命中卷节点时
// 两个都置 -1。查不到返回 -1。
int FlatTreeIndex(const std::vector<kit::TreeNode>& nodes, int chapter, int shot);

// 上面那个互转的**反向**：扁平序号 → (章下标, 镜下标)，写进这两个出参。
void ResolveTreeIndex(const std::vector<kit::TreeNode>& nodes, int index, int& chapterOut,
                      int& shotOut);

// 点中的是**分组节点**时，返回它在 nodes 里的下标（设计稿的分组行只切展开 / 收起，
// 不选中任何资产：Shell.jsx:554 的 onClick 只 setOpen，没有 setSelEntity）。-1 = 没命中。
int AssetGroupAt(const std::vector<kit::TreeNode>& nodes, int flat);

// 卷树版的「点中的是顶层卷行吗」：返回卷在 nodes 里的下标，-1 = 命中的是章 / 镜行。
//
// ⚠️ **不能**用 `flat < volumes.size()` 这种捷径判卷行（2026-11-23 修）：先序扁平号
//    里卷行**不占前 N 个号** —— 卷 0 占 0，它的章占 1..k，卷 1 才占 k+1。原捷径
//    （注释还写着「顶层节点总是前 N 个」）在 fixture 上的实际错法：点「第 1 章」
//    （flat=1 < 3）被当成卷行去翻**第二卷**的折叠；点「第二卷」（flat=2）翻**第三卷**；
//    点「第三卷」（flat=7 ≥ 3）落进 ResolveTreeIndex 的卷命中分支直接 return、无反应。
//    取证的 hover-tree-node 只悬停不点、chapter-switch 直调 SelectBookChapter，
//    96 张判据**都覆盖不到树行点击**，所以这个 bug 一直隐形。判卷行必须走
//    与 walker counter 同口径的先序数（本函数），与 AssetGroupAt 同一职责。
int VolumeAt(const std::vector<kit::TreeNode>& nodes, int flat);

// 资产 kind 树的两向展平（与上面章 / 镜树同一套先序约定）。
//
// `leafAsset` 是**叶子序号**（含折叠组的叶子）→ `BookSideView::assets` 下标。
// 返回值是 `kit::Tree` 用的**可见节点扁平序号**（分组节点也占号），所以两个数
// 不是一回事：传错一个就是「点 A 高亮 B」。命中分组节点返回 -1（设计稿的
// `.node.on` 只加在叶子上，分组节点没有选中态）。
int AssetLeafFlat(const std::vector<kit::TreeNode>& nodes, const std::vector<int>& leafAsset,
                  int assetIndex);
int AssetLeafAt(const std::vector<kit::TreeNode>& nodes, const std::vector<int>& leafAsset, int flat);

// 资产 kind 分组的展开态。以原始 kind 记录收起的组。
bool KindCollapsed(const std::vector<std::string>& collapsed, const std::string& kind);

} // namespace shine::pages
