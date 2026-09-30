// shine::pages —— P5.3 资产（assets）—— 第一处真正用 src/gpu 的页面
//
// 同样从 WorkspaceB.cpp 搬来。旧版 12 张 "资产 N" 卡、沈砚/老沈/第 1 章、
// 基线 v2 / 当前 v3 / 差异 0.2418、S010–S014 chip、4 张参考图全是写死的。
// 现在逐项来自 entities + visual_assets + visual_artifacts。
#include "ui/imgui/pages/BookData.h"

#include "ui/imgui/kit/Image.h"
#include "ui/imgui/pages/PageCommon.h"

#include "util/Encoding.h"

#include <algorithm>
#include <string>

namespace shine::pages {

using namespace shine::kit;

void AssetsPage::Draw(Rect area, ImDrawList* draw) {
    BookState& s = Book();
    const std::vector<SegmentOption> options{{"d", "详情"}, {"o", "总览"}};
    const std::string_view picked =
        Segmented(draw, RectAt(area.min.x, area.min.y, SegmentedWidth(options), 32.0f), options,
                  overview_ ? "o" : "d", "assets-seg");
    overview_ = (picked == "o");

    const Rect body{area.min.x, area.min.y + 44.0f, area.max.x, area.max.y};
    if (!s.bound || !s.error.empty() || s.assets.empty()) {
        BookEmpty(body, draw, "masks", "还没有实体资产。跑一次初始化与资产流水线后这里才有内容。");
        return;
    }
    selected_ = std::clamp(selected_, 0, static_cast<int>(s.assets.size()) - 1);

    // ⚠️ kind 筛选是**侧栏与主区共享**的一份状态（设计稿 Assets.jsx:130 和 Shell.jsx:523
    //    读同一个 entityKind）。只筛侧栏的话，点完 chip 主区纹丝不动，看着像 chip 坏了。
    //    这里读同一份 BookKindFilter()，不另存一份 —— 存两份迟早只改得动一边。
    //
    //    详情**不**跟着筛：设计稿的 cur 取自未筛选的全集（Assets.jsx:131），所以筛到一个
    //    不含当前选中项的 kind 时详情页不空。照这个口径，否则点完 chip 主体变空态。
    const std::string& kindFilter = BookKindFilter();
    std::vector<int> visible;
    visible.reserve(s.assets.size());
    for (int i = 0; i < static_cast<int>(s.assets.size()); ++i) {
        if (kindFilter.empty() || s.assets[static_cast<std::size_t>(i)].kind == kindFilter) {
            visible.push_back(i);
        }
    }

    const BookAsset& asset = s.assets[static_cast<std::size_t>(selected_)];

    if (overview_) {
        if (visible.empty()) {
            BookEmpty(body, draw, "masks",
                      "这一类还没有实体。在左侧侧栏换回「全部」看看这个工程里有哪些实体");
            return;
        }
        const int columns = AutoGridCols(body.width(), 210.0f, 14.0f);
        const float cardW =
            (body.width() - 14.0f * static_cast<float>(columns - 1)) / static_cast<float>(columns);
        for (int slot = 0; slot < static_cast<int>(visible.size()); ++slot) {
            const int i = visible[static_cast<std::size_t>(slot)];  // 真实下标
            const BookAsset& row = s.assets[static_cast<std::size_t>(i)];
            const int column = slot % columns;
            const int r = slot / columns;
            // ⚠️ 这里原来把宽高直接写进了 Rect 的四参构造，而 kit::Rect 的四参是
            //    **(minX, minY, maxX, maxY)**，不是 (x, y, w, h)。于是 max.x = cardW(208)
            //    小于 min.x = 296，DrawRoundRect 的 `max.x <= min.x` 直接 return：
            //    总览网格的卡片**整张没画、也点不到**，界面上只剩名字和状态两行文字浮在
            //    背景上。thumb 用了 card.max.x，同一个原因一起消失。
            //    编译不报错、运行不崩，截图看得出「卡没了」但看不出是哪儿 —— 见
            //    kit::Rect 四参构造上的警告。宽高一律走 RectAt。
            Rect card = RectAt(body.min.x + (cardW + 14.0f) * static_cast<float>(column),
                               body.min.y + (192.0f + 14.0f) * static_cast<float>(r), cardW,
                               192.0f);
            const bool on = (i == selected_);
            // ⚠️ 一次 HitTest 取齐 hovered + clicked。悬停描边是设计稿里 .card:hover
            //    的一部分（与项目中心 DrawHubCard 同一条规则：hover 把 border 从
            //    line-subtle 提到 accent-glow）。原来这里只有 `Clicked(card, ...)`，
            //    整条卡 hover 链路是断的 —— 鼠标划过去一点反应都没有，而**静息态
            //    截图完全看不出来**（hover 前后的差别本来就只在那一帧）。取证靠
            //    「帧内注入 MousePos + 钉住动画时钟」才逼出来：坐标在热区内、
            //    页面静止，两帧像素却逐字节相同。
            const Hit hit = HitTest(card, "asset-card-" + std::to_string(i));
            // `.card.hoverable:hover { transform: translateY(-2px) }`（ui.css:196-200）。
            // ⚠️ 位移必须在 **HitTest 之后**：先命中、再抬高绘制矩形（`kit::Card` 同构）。
            //    反过来写会让热区跟着鼠标动 —— 鼠标停在卡片下缘 2px 内时，
            //    hovered 把卡片推上去、鼠标出界、又落回来，状态在帧间来回抖。
            //    位移对卡内全部元素生效（缩略图 / 名称 / 状态行都从 `card` 推位置），
            //    所以这里直接改 `card` 本身，而不是只改下面那一行 DrawShadowed。
            const float lift = kit::CardHoverLift(hit.hovered, true);
            card.min.y += lift;
            card.max.y += lift;
            // ui.css:196-200 `.card.hoverable:hover` → border 提到 **--line-strong**
            // + `box-shadow: var(--shadow-1)`；views.css:723-725 `.asset-card.on` →
            // `box-shadow: var(--shadow-accent)`。JSX 那边 class 是
            // `card asset-card hoverable`（Assets.jsx:151），两条规则都命中。
            //
            // ⚠️ 这里的 hover 边框原来用的是 accent-glow —— 那是「让 hover 看得出来」
            //    时自己挑的颜色，设计稿写的是 line-strong。两者差得很远，1:1 以 CSS 为准。
            //    `.card.hoverable:hover` 的另一半 translateY(-2px) 也已接上（见上面几行）。
            DrawShadowed(draw, card.min, card.max, 10.0f, ColorPanel(),
                         on ? ColorAccent() : (hit.hovered ? ColorLineStrong() : ColorLineSubtle()),
                         1.0f, on ? theme::ShadowTier::Accent
                                  : (hit.hovered ? theme::ShadowTier::Card
                                                 : theme::ShadowTier::None));
            // ---- 缩略图：**真设定图优先，没有就画设计稿那张 `Art` 占位** ----
            //
            // 这里原来写的是「不拿 Art() 占位画冒充这个角色的设定图」+ 一个 masks 字形。
            // 那条顾虑是对的（占位画不该冒充真图），但**结论选错了对象**：
            // 设计稿这一格本来就是 `<Art seed={e.art}>`（Assets.jsx:152）——
            // `Art` 是设计稿自己的占位画，画它不属于冒充，画一个设计稿里没有的
            // masks 字形才是两处都不对（既不是真图、也不是设计稿）。
            //
            // 现在按「真图 > 设计稿占位」的次序：
            //   ① `visual_assets.sheet_rel_path` 那个文件解出来了 → 画**真图**
            //      （`.art` 的 `--r-sm` 圆角 + cover 裁切，与设计稿同一套几何）
            //   ② 还没就绪 / 文件不存在 / 解不出来 → 画 `Art` 占位
            // ②包含了「没有产出图」那条诚实状态 —— 卡片下方的状态行照旧说明白了。
            const Rect thumb{card.min.x + 8.0f, card.min.y + 8.0f, card.max.x - 8.0f, card.min.y + 158.0f};
            const ImageSlot sheet = RequestImage(AssetImagePath(s.root, row.sheetRelPath));
            // 底色先铺：`Art` 是纯几何绘制、不铺底，而设计稿的 `.art` 自带
            // `background: var(--fill-muted)`（ui.css:1074）——真图与占位都靠它兜底。
            DrawRoundRect(draw, thumb.min, thumb.max, 6.0f, ColorFillMuted()); // --r-sm = 6
            if (sheet.ready()) {
                DrawImage(draw, thumb, sheet, 6.0f, ImageFit::Cover);
            } else {
                // seed 用 `entityId`：设计稿的 `e.art` 是 mock 数据的字段
                // （mock.js:309-317 的 5..12），库里**没有**对应列 ——
                // 按实体 id 派生是确定的（同一实体永远同一张），不编随机数。
                // ⚠️ 本版 ImGui 没有 push clip path，`Art` 的四个角会盖住圆角一点点；
                //    与 HubCard 封面同一个已知降级。
                Art(draw, thumb, row.entityId, /*cover=*/true);
            }
            const std::string name = row.name.empty() ? std::string(kDash) : row.name;
            DrawTextClipped(draw, FontBoldAt(13.0f), 13.0f, ImVec2(card.min.x + 12.0f, card.min.y + 164.0f),
                            cardW - 24.0f, ColorText(), name, true);
            std::string statusLabel;
            theme::Tone statusTone = theme::Tone::Idle;
            AssetTone(row.hasAsset ? row.assetStatus : std::string(), statusLabel, statusTone);
            const std::string sub = row.hasAsset ? statusLabel : "无视觉资产";
            DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(card.min.x + 12.0f, card.min.y + 180.0f),
                            cardW - 24.0f, ColorTextMuted(), sub, true);
            if (hit.clicked) {
                selected_ = i;
            }
        }
        return;
    }

    // ---- 详情 ----
    float y = body.min.y;
    DrawIcon(draw, "masks", ImVec2(body.min.x, y), 16.0f, ColorAccent());
    const std::string entityName = asset.name.empty() ? kDash : asset.name;
    draw->AddText(FontBoldAt(15.0f), 15.0f, ImVec2(body.min.x + 24.0f, y - 1.0f), ColorText(),
                  entityName.data(), entityName.data() + entityName.size());
    std::string statusLabel;
    theme::Tone statusTone = theme::Tone::Idle;
    AssetTone(asset.hasAsset ? asset.assetStatus : std::string(), statusLabel, statusTone);
    const float tagW = TagWidth(statusLabel, false, true);
    Tag(draw, RectAt(body.min.x + 24.0f + LabelWidth(FontBoldAt(15.0f), 15.0f, entityName.c_str()) + 12.0f,
                    y - 1.0f, tagW, 20.0f),
        statusLabel, statusTone, false, true);
    y += 28.0f;
    draw->AddLine(ImVec2(body.min.x, y), ImVec2(body.max.x, y), ColorLineSubtle(), 1.0f);
    y += 14.0f;

    // 当前选中项被 kind 筛选排除在外时，明说它不在筛选结果里 —— 照设计稿的 cur 口径
    // （Assets.jsx:131 从未筛选的全集取）详情照样显示，但用户会以为筛选没生效。
    if (!kindFilter.empty() && std::find(visible.begin(), visible.end(), selected_) == visible.end()) {
        DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(body.min.x, y), body.width(),
                        ColorTextMuted(),
                        "当前这一项不在「" + AssetKindLabel(kindFilter) + "」的筛选结果里", true);
        y += 18.0f;
    }

    // 真实字段：类别 / 实体状态 / 资产名 / canon / 生产态 / 设定图路径 / 形象层进度。
    // 旧版的"别名 老沈""出处 第 1 章""降级策略 保留上一版"在 entities 表里**没有列**，
    // 所以这些行不画 —— 不用相邻字段冒充。
    const std::string layers = asset.hasAsset ? (std::to_string(asset.layersDone) + " / " +
                                                 std::to_string(asset.layers) + " 层就绪")
                                              : std::string(kDash);
    // ---- 设定集体：**两列网格**（Assets.jsx:184）----
    // `grid-template-columns: minmax(240px,320px) minmax(280px,1fr)`；gap 16；
    // `align-items: start`。
    //
    // ⚠️ 原来这里是**单列**：KV 直接铺满 640px，而左列那块「基线图」整块没画 ——
    //    它是这一页最大的一张图（`.art { width:100%; height:220 }`，
    //    Assets.jsx:186-202），也是「显示真实设定图」最该出效果的地方。
    constexpr float kSetGap = 16.0f;
    const float setLeftW = std::clamp(body.width() * 0.25f, 240.0f, 320.0f);
    const float setLeftRight = body.min.x + setLeftW;
    const Rect rightCol{setLeftRight + kSetGap, y, body.max.x, body.max.y};

    // 左列：基线图 + `.tiny dim` 说明行。
    const Rect baseArt{body.min.x, y, setLeftRight, y + 220.0f};
    // 底色先铺：设计稿的 `.art` 自带 `background: var(--fill-muted)` + r-sm（ui.css:1074）。
    DrawRoundRect(draw, baseArt.min, baseArt.max, 6.0f, ColorFillMuted());
    const ImageSlot baseSheet = RequestImage(AssetImagePath(s.root, asset.sheetRelPath));
    if (baseSheet.ready()) {
        DrawImage(draw, baseArt, baseSheet, 6.0f, ImageFit::Cover);
    } else {
        Art(draw, baseArt, asset.entityId, /*cover=*/true);
    }
    DrawTextClipped(draw, FontAt(11.0f), 11.0f, ImVec2(body.min.x, baseArt.max.y + 8.0f), setLeftW,
                    ColorTextMuted(), "基线图 · 点击查看大图", true);

    KeyValues(draw, Rect{rightCol.min.x, y, rightCol.max.x, y + 150.0f},
              {{"类别", AssetKindLabel(asset.kind)},
               {"实体 ID", "#" + std::to_string(asset.entityId)},
               {"实体状态", asset.entityStatus.empty() ? kDash : asset.entityStatus},
               {"视觉资产", asset.hasAsset ? (asset.assetName.empty() ? kDash : asset.assetName)
                                           : "尚未建立"},
               {"Canon", asset.canonStatus.empty() ? kDash : asset.canonStatus},
               {"生产状态", asset.hasAsset ? statusLabel : kDash},
               {"形象层", layers},
               {"设定图", asset.sheetRelPath.empty() ? kDash : asset.sheetRelPath},
               {"降级", asset.degraded ? "有降级产物" : "无"}});

    // ---- V0 派生链（Assets.jsx:217-234 + views.css:849-895）----
    //
    // ⚠️ 这一整块以前**没画**：`visual_artifacts` 只被投影成 `layers / layersDone`
    //    两个计数，链上每一层的图与层状态都到不了界面。fixture 一铺开就露出来了 ——
    //    那几层的图（`assets/chain/*.png`）在**所有**截图里的颜色计数都是 0。
    //
    // 几何逐条对着 CSS —— ⚠️ **JSX 内联会覆盖 CSS，两处都要看**：
    //   `.derive`   flex / gap 0 / overflow-x auto / padding 6px 2px
    //   `.dnode`    宽 **92**（CSS 写 108，被 JSX 内联覆盖）/ r-md=10 /
    //               1px line-normal 边 / fill-muted 底 / overflow hidden
    //   `.dthumb`   高 **50**（CSS 写 64）
    //   `.dlabel`   pad 6 8 / 11px **700** / text-secondary / gap 5
    //   `.dlink`    22×1.5 line-normal；`.fill::after` 盖 accent
    //               （JSX 的条件是 `d.state !== 'todo'`，即**这一层**不是待生成）
    constexpr float kChainNodeW = 92.0f;
    constexpr float kChainThumbH = 50.0f;
    constexpr float kChainLinkW = 22.0f;
    constexpr float kChainPadY = 6.0f;    // `.derive` 的 padding 6px 2px
    constexpr float kChainPadX = 8.0f;    // `.dlabel` 的左右内边距
    constexpr float kChainFont = 11.0f;   // `.dlabel` 11/700；同一个小字也用于说明行
    // 链的层名与中文标签。层名来自 `NovelAssetPipeline.cpp` 的 `kChain`
    // （stage / shot 两层属分镜侧，不在 V0 派生链里，这里不画）。
    static constexpr const char* kChainLayers[4] = {"front", "turnaround", "base_body", "wardrobe"};
    static constexpr const char* kChainLabels[4] = {"正脸", "四视图", "基础身体", "服装"};
    const float chainLineH = kChainFont * 1.6f;              // base.css 的全局行高
    const float chainLabelH = chainLineH + 6.0f * 2.0f;      // `.dlabel` padding 6px 8px
    const float chainNodeH = kChainThumbH + chainLabelH;

    const float captionTop = y + 164.0f;  // KV 之后（沿用这一列原来的占高口径）
    constexpr char kChainCaption[] = "V0 派生链 · 正脸 → 四视图 → 基础身体 → 服装";
    DrawTextClipped(draw, FontBoldAt(kChainFont), kChainFont, ImVec2(rightCol.min.x, captionTop),
                    rightCol.width(), ColorTextSecondary(), kChainCaption, true);
    const float nodeTop = captionTop + chainLineH + 6.0f + kChainPadY;  // 说明行 + margin 6 + `.derive` padding
    float nx = rightCol.min.x + 2.0f;                                   // `.derive` padding 横向 2
    for (int i = 0; i < 4; ++i) {
        const BookArtifact* art = nullptr;
        for (const BookArtifact& a : asset.artifacts) {
            if (a.layer == kChainLayers[i]) {
                art = &a;
                break;
            }
        }
        if (i > 0) {
            // `.dlink` 排在**节点之前**：它连的是「上一层 → 这一层」，填不填 accent
            // 按**这一层**的状态判（JSX 的 `d.state !== 'todo'`）。
            const ImU32 link =
                (art != nullptr && art->status != "PENDING") ? ColorAccent() : ColorLineNormal();
            const float linkY = nodeTop + kChainThumbH * 0.5f;
            draw->AddRectFilled(ImVec2(nx, linkY - 0.75f), ImVec2(nx + kChainLinkW, linkY + 0.75f),
                                link);
            nx += kChainLinkW;
        }
        const Rect node{nx, nodeTop, nx + kChainNodeW, nodeTop + chainNodeH};
        // `.dnode:hover`（views.css:867-870）：accent-glow 边 + shadow-1。
        // ⚠️ hover 只看鼠标位置、**不注册热区** —— 「点击查看该层」这个动作还没接
        //    （要看该层大图，需要查看器），注册了就是「画得出来、点不动」，
        //    而那正是本仓库反复强调、并且专门有判据在抓的一类缺陷。
        const bool hovered = node.contains(ImGui::GetIO().MousePos);
        DrawShadowed(draw, node.min, node.max, 10.0f, ColorFillMuted(),
                     hovered ? ColorAccentGlow() : ColorLineNormal(), 1.0f,
                     hovered ? theme::ShadowTier::Card : theme::ShadowTier::None);
        const Rect thumb{node.min.x, node.min.y, node.max.x, node.min.y + kChainThumbH};
        // `.dthumb`：真图优先，没有就画设计稿那张 `Art`（`seed={5+i}`）。
        const ImageSlot slot =
            RequestImage(AssetImagePath(s.root, art != nullptr ? art->relPath : std::string{}));
        if (slot.ready()) {
            DrawImage(draw, thumb, slot, 0.0f, ImageFit::Cover);
        } else {
            Art(draw, thumb, 5 + i, /*cover=*/true);
        }
        // `.dlabel`：状态点 + 层名。三态映射到 7 色调 ——
        //   DONE → ok；RUNNING → busy（带脉冲）；FAILED → danger；其余（含没有该层）→ idle。
        theme::Tone tone = theme::Tone::Idle;
        bool running = false;
        if (art != nullptr) {
            if (art->status == "DONE") {
                tone = theme::Tone::Ok;
            } else if (art->status == "RUNNING") {
                tone = theme::Tone::Busy;
                running = true;
            } else if (art->status == "FAILED") {
                tone = theme::Tone::Danger;
            }
        }
        const float labelCy = thumb.max.y + chainLabelH * 0.5f;
        const float dotCx = node.min.x + kChainPadX + 3.5f;
        StatusDot(draw, ImVec2(dotCx, labelCy), tone, running);
        ImFont* chainFont = FontBoldAt(kChainFont);
        const std::string_view chainLabel = kChainLabels[i];
        draw->AddText(chainFont, kChainFont,
                      ImVec2(dotCx + 3.5f + 5.0f, CenterTextY(chainFont, kChainFont, labelCy)),
                      ColorTextSecondary(), chainLabel.data(), chainLabel.data() + chainLabel.size());
        nx += kChainNodeW;
    }
    // `.tiny dim`：派生起点说明（margin-top 4）。
    constexpr char kChainNote[] = "派生起点：文生图 · 缺层时：生成这一层";
    DrawTextClipped(draw, FontAt(kChainFont), kChainFont,
                    ImVec2(rightCol.min.x, nodeTop + chainNodeH + kChainPadY + 4.0f),
                    rightCol.width(), ColorTextMuted(), kChainNote, true);

    // 两列取较高者推进 y —— CSS 网格就是这个行为（`align-items:start` 只影响对齐，
    // 网格区的行高仍由最高的那一格撑开）。右列 = KV(164) + 说明行 + 链 + 脚注。
    constexpr float kBaseCaptionH = 8.0f + 11.0f * 1.6f;  // 8 间距 + tiny 行盒
    const float chainBlockH = chainLineH + 6.0f + kChainPadY * 2.0f + chainNodeH + 4.0f + chainLineH;
    y += std::max(220.0f + kBaseCaptionH, 164.0f + chainBlockH);
    draw->AddLine(ImVec2(body.min.x, y), ImVec2(body.max.x, y), ColorLineSubtle(), 1.0f);
    y += 14.0f;

    // 一致性对比 / 关联时间线 / 绑定镜头 chip：**没有可用来源**，逐条说清缺什么。
    const Rect missing{body.min.x, y, body.max.x, body.min.y + 220.0f};
    Empty(draw, missing, "compare", "尚无一致性对比数据",
          "对比需要同一角色的两层成图（visual_artifacts 里 front + turnaround 都 DONE）"
          "并解码出像素差；当前该资产形象层 " +
              (asset.hasAsset ? (std::to_string(asset.layersDone) + "/" +
                                 std::to_string(asset.layers) + " 就绪")
                              : std::string("尚未建立")) +
              "，而且图层路径（visual_artifacts.rel_path）还没投影到 UI 侧 —— "
              "解码链已经通了（kit/Image.h），缺的是把这两张图的路径送上来。");
    y += 236.0f;
    draw->AddLine(ImVec2(body.min.x, y), ImVec2(body.max.x, y), ColorLineSubtle(), 1.0f);
    y += 14.0f;

    static const char* kGapsTitle = "尚未接入的区块";
    // DrawTextClipped 收 string_view，自己算结束指针；末参是 wrap(bool)，不是 text_end。
    DrawTextClipped(draw, FontBoldAt(12.5f), 12.5f, ImVec2(body.min.x, y), body.width(), ColorText(),
                    kGapsTitle, true);
    y += 20.0f;
    for (const char* gap : {"关联时间线：需要按章的事件 + 分镜引用，资产页还没有这条查询。",
                            "绑定镜头 chip：需要 shots.reference_json 解析成 visual_assets 路径。",
                            "参考图：项目参考库 refs.json 的读取接口未接到 ImGui 侧。"}) {
        DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(body.min.x, y), body.width(),
                        ColorTextMuted(), gap, true);
        y += 18.0f;
    }
}

} // namespace shine::pages
