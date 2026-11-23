// shine::pages —— 查看器的三个 ViewerRequest 构造器（第二十二轮自 MediaViewer.cpp 拆出）
//
// 拆的理由是**读者不同**：这三个函数只读 `Book()`、往 `ViewerRequest` 里拼页，
// 不碰 `ViewerState`、不画任何东西 —— 与 MediaViewer.cpp 里那批「状态 + 几何 +
// 绘制」是两类改动原因。逐行逻辑与拆出前一致，只是搬家。
//
// 三条入口（调用点见各工作区）：
//   AssetViewerRequest      资产 → 第 0 页基线设定图 + 第 1..N 页 V0 派生链四层
//   ShotViewerRequest       分镜 → 本章各镜的成片图（页序与镜序同序）
//   AssetSheetViewerRequest 资产 → 联系表模式（cells，只读不翻页）
#include "ui/imgui/pages/MediaViewer.h"

#include "ui/imgui/pages/BookData.h"
#include "ui/imgui/pages/PageCommon.h"       // kDash / kit::Fmt（经 Fmt.h）
#include "ui/imgui/pages/WorkspacePages.h"   // AssetImagePath / ShotCode

#include "util/Encoding.h"

#include <string>
#include <utility>

namespace shine::pages {

ViewerRequest AssetViewerRequest(int assetIndex) {
    ViewerRequest req;
    const BookState& book = Book();
    if (assetIndex < 0 || assetIndex >= static_cast<int>(book.assets.size())) {
        return req;
    }
    const BookAsset& asset = book.assets[static_cast<std::size_t>(assetIndex)];
    const std::string title = asset.name.empty() ? std::string(kDash) : asset.name;
    req.name = title;

    // 第 0 页：基线设定图（`visual_assets.sheet_rel_path`）
    {
        const std::filesystem::path p = AssetImagePath(book.root, asset.sheetRelPath);
        ViewerImage img;
        img.path = p;
        img.name = p.empty() ? ("设定集整版 · " + title) : util::FileNameToUtf8(p);
        img.meta = "设定集整版 · " + title;
        img.seed = asset.entityId;
        req.gallery.push_back(std::move(img));
    }
    // 第 1..N 页：V0 派生链四层（`visual_artifacts.rel_path`）
    for (int i = 0; i < kChainLayerCount; ++i) {
        const BookArtifact* art = nullptr;
        for (const BookArtifact& a : asset.artifacts) {
            if (a.layer == kChainLayerKeys[i]) {
                art = &a;
                break;
            }
        }
        const std::filesystem::path p =
            AssetImagePath(book.root, art != nullptr ? art->relPath : std::string{});
        ViewerImage img;
        img.path = p;
        // 层名与状态都进标题与说明行：链上点进来时，看的是「哪一层的哪一张」。
        const std::string layerLabel = std::string(kChainLayerLabels[i]) + " · " + title;
        img.name = p.empty() ? (layerLabel + " · 待出图") : util::FileNameToUtf8(p);
        img.meta = "V0 派生链 · " + layerLabel +
                   (art != nullptr && !art->status.empty() ? (" · " + art->status) : std::string());
        // 占位画的 seed 与资产页 `.dthumb` 的 `seed={5+i}`（Assets.jsx:224）对齐：
        // 同一个位置（链上第 i 层 / 查看器里第 i+1 页）在没图时应画出同一张占位画，
        // 否则「点进去」会看到一张与缩略图完全不同的画，看着像跳错层了。
        img.seed = 5 + i;
        req.gallery.push_back(std::move(img));
    }
    return req;
}

ViewerRequest ShotViewerRequest(int shotIndex) {
    ViewerRequest req;
    const BookState& book = Book();
    if (shotIndex < 0 || shotIndex >= static_cast<int>(book.shots.size())) {
        return req;
    }
    const BookShot& selected = book.shots[static_cast<std::size_t>(shotIndex)];
    const std::string selectedCode = ShotCode(selected.ord);
    req.name = selectedCode + "_render";
    const BookChapter* chapter = book.chapter();
    for (const BookShot& shot : book.shots) {
        const std::filesystem::path p = AssetImagePath(book.root, shot.imageRelPath);
        const std::string code = ShotCode(shot.ord);
        ViewerImage img;
        img.path = p;
        // 标题用**真文件名**（头部那行是等宽文件名）；这一镜还没出图时说清楚是空的，
        // 不去拼一个设计稿里的 `_render_v3.png` —— 那个版本号库里没有对应列。
        img.name = p.empty() ? (code + " · 待出图") : util::FileNameToUtf8(p);
        std::string meta = code + " 出图";
        if (chapter != nullptr && chapter->ord > 0) {
            meta += kit::Fmt(" · 第 {} 章", chapter->ord);
        }
        meta += " · " + (shot.canonStatus.empty() ? std::string(kDash) : shot.canonStatus);
        img.meta = std::move(meta);
        img.seed = shot.ord; // 占位画的种子按镜序派生，同一镜永远同一张
        req.gallery.push_back(std::move(img));
    }
    return req;
}

ViewerRequest AssetSheetViewerRequest(int assetIndex) {
    ViewerRequest req = AssetViewerRequest(assetIndex);
    req.cells.reserve(req.gallery.size());
    for (std::size_t i = 0; i < req.gallery.size(); ++i) {
        ViewerSheetCell cell;
        cell.path = req.gallery[i].path;
        cell.seed = req.gallery[i].seed;
        // 设计稿的格子标签：第一格 `基线 · 文生图`，其余取 DERIVE_CHAIN 的名字
        // （Assets.jsx:180）。
        cell.label = i == 0 ? std::string("基线 · 文生图")
                            : std::string(kChainLayerLabels[i - 1]);
        req.cells.push_back(std::move(cell));
    }
    req.gallery.clear();  // 联系表模式只看 cells（PageCount 也据此返回 1）
    if (!req.name.empty()) {
        req.name += "_SheetGrid";
    }
    req.exportButtons = true;
    return req;
}

} // namespace shine::pages
