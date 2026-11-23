// shine::pages —— 侧栏两棵树的**数据准备**（2026-10-02 从 Shell_Sidebar.cpp 拆出）
//
// 拆分理由与 `Shell_Sidebar_Tree.h` 顶上一致：这一份只回答「这一帧侧栏树上该有
// 哪些节点、点中第几个扁平序号对应哪个业务下标」，**一个像素都不画**。绘制与
// 命中在 Shell_Sidebar.cpp。
//
// ⚠️ 两棵树的节点**都按指纹缓存**（算法见 kit/Hash.h）：指纹覆盖所有被显示的
//    字段，没变就整帧复用上一帧的树 / chip 行 —— 与 Page_Storyboard::RefreshTexts、
//    Shell::RefreshInspectorKv 同一范式。第二十轮波 2 就是这一步（原先每帧 Fmt
//    重建一遍）。
//
// ⚠️ 展开态（collapsedKinds_ / collapsedVolumes_）也在指纹里：点折叠箭头走
//    「toggle 集合 → 指纹变 → 下一帧重建」，与原先每帧重建的语义一致。
//    `kit::Tree` 会就地翻 node.expanded，翻完的帧内缓存节点带着脏值 —— 但命中
//    处理器必然同时 toggle 了对应集合 ⇒ 指纹必变 ⇒ 下一帧重建，不会滞留。
#include "ui/imgui/pages/Shell.h"

#include "ui/imgui/pages/BookData.h"
#include "ui/imgui/pages/PageCommon.h"
#include "ui/imgui/pages/Shell_Sidebar_Tree.h"

#include "ui/imgui/kit/Fmt.h"
#include "ui/imgui/kit/Hash.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace shine::pages {

using namespace shine::kit;

namespace {

using pages::ShotCode;  // 镜码 S001 —— 实现与注释都在 WorkspacePages.h

} // namespace

// ---- 先序互转（约定写在 Shell_Sidebar_Tree.h 顶部）----

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

int VolumeAt(const std::vector<TreeNode>& nodes, int flat) {
    // 与 TreeWalk / 本文件其它互转同一套先序口径：卷行 → 它的章行 → 章下展开的镜行。
    // 教训（2026-11-23）：这里原来没有独立函数，调用方用 `flat < volumes.size()` 当
    // 判据并拿 flat 直接当下标去查 volumeOrder —— 先序号里卷行不占前 N（卷 0 的章
    // 排在卷 1 前面），于是「点第 1 章翻第二卷的折叠、点第二卷翻第三卷、点第三卷
    // 没反应」三种错法同时存在，而取证不点树行、96 张判据全绿地把它放了过去。
    // 判「点没点中顶层行」只能按先序数着走，没有捷径。
    int i = 0;
    for (std::size_t v = 0; v < nodes.size(); ++v) {
        if (i++ == flat) {
            return static_cast<int>(v);
        }
        if (!nodes[v].expanded) {
            continue;
        }
        for (const TreeNode& chap : nodes[v].children) {
            ++i;  // 章行本身占一个号
            if (chap.expanded) {
                i += static_cast<int>(chap.children.size());
            }
        }
    }
    return -1;
}

int AssetLeafFlat(const std::vector<TreeNode>& nodes, const std::vector<int>& leafAsset,
                  int assetIndex) {
    int flat = 0;
    std::size_t leaf = 0;
    for (const TreeNode& group : nodes) {
        const int at = flat++;
        if (!group.expanded) {
            leaf += group.children.size();
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
            leaf += group.children.size();
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

bool KindCollapsed(const std::vector<std::string>& collapsed, const std::string& kind) {
    return std::find(collapsed.begin(), collapsed.end(), kind) != collapsed.end();
}

// ---- 两棵树的指纹缓存 ----

void Shell::RefreshAssetSideTree(const BookSideView& book) {
    std::uint64_t key = HashMix(0, book.assets.size());
    for (const BookAssetView& asset : book.assets) {
        key = HashMix(key, static_cast<std::uint64_t>(asset.index));
        key = HashMix(key, Fnv1a(asset.kind));
        key = HashMix(key, Fnv1a(asset.name));
        key = HashMix(key, static_cast<std::uint64_t>(static_cast<int>(asset.tone)));
    }
    const auto& filter = BookKindFilter();
    key = HashMix(key, filter ? Fnv1a(*filter) : 0);
    for (const std::string& kind : collapsedKinds_) {
        key = HashMix(key, Fnv1a(kind));
    }
    if (key == assetTreeKey_) {
        return;
    }
    assetTreeKey_ = key;

    // 分组保留 kind 首次出现的顺序。每个组的成员同时决定 chip 和树的计数。
    std::unordered_map<std::string_view, std::size_t> groupIndices;
    std::vector<std::vector<const BookAssetView*>> groups;
    int ready = 0;
    for (const BookAssetView& asset : book.assets) {
        if (asset.tone == theme::Tone::Ok) {
            ++ready;
        }
        const auto [at, inserted] = groupIndices.try_emplace(asset.kind, groups.size());
        if (inserted) {
            groups.emplace_back();
        }
        groups[at->second].push_back(&asset);
    }
    assetReadyCount_ = ready;
    assetReadyText_ = kit::Fmt("{}/{} 就绪", ready, book.assets.size());

    assetKindChips_.clear();
    assetKindChips_.reserve(groups.size());
    assetTreeNodes_.clear();
    assetTreeLeaf_.clear();
    assetTreeKinds_.clear();
    for (const auto& assets : groups) {
        const std::string& kind = assets.front()->kind;
        const AssetKindChip& chip = assetKindChips_.emplace_back(AssetKindChip{
            kind, AssetKindLabel(kind), kit::Fmt("{}", assets.size()),
            kit::Fmt("asset-kind-{}", assetKindChips_.size())});
        if (filter && kind != *filter) {
            continue;
        }
        TreeNode group;
        group.label = chip.label;
        group.icon = "layers";
        group.hasChildren = true;
        group.children.reserve(assets.size());
        for (const BookAssetView* asset : assets) {
            group.children.push_back(TreeNode{asset->name.empty() ? "—" : asset->name, {}, {},
                                              false, false, {}});
            assetTreeLeaf_.push_back(asset->index);
        }
        group.trailing = chip.count;
        group.expanded = !KindCollapsed(collapsedKinds_, kind);
        assetTreeNodes_.push_back(std::move(group));
        assetTreeKinds_.push_back(kind);
    }
}

void Shell::RefreshNovelSideTree(const BookSideView& book, bool withShots) {
    std::uint64_t key = HashMix(0, withShots ? 1ull : 0ull);
    if (withShots) {
        key = HashMix(key, static_cast<std::uint64_t>(book.selectedChapter));
        for (const BookShotView& shot : book.shots) {
            key = HashMix(key, static_cast<std::uint64_t>(shot.ord));
            key = HashMix(key, Fnv1a(shot.action));
        }
    }
    for (const BookChapterView& chapter : book.chapters) {
        key = HashMix(key, static_cast<std::uint64_t>(chapter.ord));
        key = HashMix(key, static_cast<std::uint64_t>(chapter.volumeId));
        key = HashMix(key, Fnv1a(chapter.title));
        key = HashMix(key, Fnv1a(chapter.volumeTitle));
        key = HashMix(key, Fnv1a(chapter.status));
    }
    for (const int volumeId : collapsedVolumes_) {
        key = HashMix(key, static_cast<std::uint64_t>(volumeId));
    }
    if (key == novelTreeKey_) {
        return;
    }
    novelTreeKey_ = key;

    // 先按 volumeId 归拢；volumeId == 0 或查不到卷名的一律归到「未归卷」——
    // 老工程的 volumes 表可能是空的，那时如实显示「未归卷」而不是编一个卷名。
    std::vector<TreeNode> nodes;
    std::vector<std::pair<int, std::string>> volumeOrder;  // volumeId → 标题（保序、去重）
    std::vector<int> chapterBucket(book.chapters.size(), 0);  // 章下标 → 哪个卷桶
    for (std::size_t i = 0; i < book.chapters.size(); ++i) {
        const BookChapterView& chapter = book.chapters[i];
        const int volumeKey = chapter.volumeId;
        const std::string title =
            chapter.volumeTitle.empty() ? std::string("未归卷") : chapter.volumeTitle;
        int bucket = -1;
        for (std::size_t k = 0; k < volumeOrder.size(); ++k) {
            if (volumeOrder[k].first == volumeKey) {
                bucket = static_cast<int>(k);
                break;
            }
        }
        if (bucket < 0) {
            bucket = static_cast<int>(volumeOrder.size());
            volumeOrder.emplace_back(volumeKey, title);
        }
        chapterBucket[i] = bucket;

        TreeNode chap;
        chap.label = kit::Fmt("第 {} 章{}", chapter.ord,
                              chapter.title.empty() ? "" : kit::Fmt(" · {}", chapter.title));
        chap.icon = "book";
        chap.trailing = OrDash(chapter.status);
        chap.expanded = withShots && static_cast<int>(i) == book.selectedChapter;
        chap.hasChildren = chap.expanded;
        if (chap.expanded) {
            // 镜只挂在**当前选中章**下面：场/镜是对选中章取的，没选过的章没有镜数据，
            // 画一排空节点等于骗人。
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
    novelTreeVolumeOrder_ = std::move(volumeOrder);
    novelTreeVolumes_.clear();
    novelTreeVolumes_.reserve(novelTreeVolumeOrder_.size());
    for (std::size_t k = 0; k < novelTreeVolumeOrder_.size(); ++k) {
        TreeNode vol;
        vol.label = novelTreeVolumeOrder_[k].second;
        vol.icon = "layers";
        for (std::size_t i = 0; i < book.chapters.size(); ++i) {
            if (chapterBucket[i] != static_cast<int>(k)) {
                continue;
            }
            vol.children.push_back(std::move(nodes[i]));
        }
        vol.trailing = kit::Fmt("{}", vol.children.size());
        vol.hasChildren = !vol.children.empty();
        vol.expanded = std::find(collapsedVolumes_.begin(), collapsedVolumes_.end(),
                                 novelTreeVolumeOrder_[k].first) == collapsedVolumes_.end();
        novelTreeVolumes_.push_back(std::move(vol));
    }
}

} // namespace shine::pages
