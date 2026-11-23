// shine::pages —— P5.3 资产（assets）—— 第一处真正用 src/gpu 的页面
//
// 同样从 WorkspaceB.cpp 搬来。旧版 12 张 "资产 N" 卡、沈砚/老沈/第 1 章、
// 基线 v2 / 当前 v3 / 差异 0.2418、S010–S014 chip、4 张参考图全是写死的。
// 现在逐项来自 entities + visual_assets + visual_artifacts。
#include "ui/imgui/pages/BookData.h"

#include "ui/imgui/kit/Image.h"
#include "ui/imgui/kit/Text.h"
#include "ui/imgui/pages/MediaViewer.h"  // 查看器（点链上某一层 / 基线图 / 导出整版）
#include "ui/imgui/pages/PageCommon.h"

#include "util/Encoding.h"

#include "ui/imgui/kit/Fmt.h"
#include "ui/imgui/kit/Hash.h"

#include <algorithm>
#include <string>

namespace shine::pages {

using namespace shine::kit;

// ---- 详情页显示缓存（第二十轮波 3）：指纹没变就整帧复用 ----
void AssetsPage::RefreshTexts() {
    BookState& s = Book();
    if (s.assets.empty()) {
        return;
    }
    const int index = std::clamp(selected_, 0, static_cast<int>(s.assets.size()) - 1);
    const BookAsset& asset = s.assets[static_cast<std::size_t>(index)];
    const auto& kindFilter = BookKindFilter();
    std::uint64_t key = HashMix(0, static_cast<std::uint64_t>(index));
    key = HashMix(key, static_cast<std::uint64_t>(asset.entityId));
    key = HashMix(key, Fnv1a(asset.name));
    key = HashMix(key, Fnv1a(asset.kind));
    key = HashMix(key, Fnv1a(asset.entityStatus));
    key = HashMix(key, Fnv1a(asset.assetName));
    key = HashMix(key, Fnv1a(asset.canonStatus));
    key = HashMix(key, Fnv1a(asset.assetStatus));
    key = HashMix(key, static_cast<std::uint64_t>(asset.layersDone));
    key = HashMix(key, static_cast<std::uint64_t>(asset.layers));
    key = HashMix(key, Fnv1a(asset.sheetRelPath));
    key = HashMix(key, asset.degraded ? 1ull : 0ull);
    key = HashMix(key, kindFilter ? Fnv1a(*kindFilter) : 0);
    if (key == textKey_) {
        return;
    }
    textKey_ = key;

    entityName_ = asset.name.empty() ? std::string(kDash) : asset.name;
    AssetTone(asset.hasAsset ? asset.assetStatus : std::string(), statusLabel_, statusTone_);
    layers_ = asset.hasAsset ? kit::Fmt("{} / {} 层就绪", asset.layersDone, asset.layers)
                             : std::string(kDash);
    // 当前选中项被 kind 筛选排除在外时，明说它不在筛选结果里（Assets.jsx:131 的 cur 口径）。
    // 与主区 visible 的判式同源：kind 匹配的下标里有没有 selected_。
    const bool filteredOut = kindFilter && asset.kind != *kindFilter;
    filterNote_ = filteredOut
                      ? "当前这一项不在「" + AssetKindLabel(*kindFilter) + "」的筛选结果里"
                      : std::string();
    kv_[0] = {"类别", AssetKindLabel(asset.kind)};
    kv_[1] = {"实体 ID", kit::Fmt("#{}", asset.entityId)};
    kv_[2] = {"实体状态", asset.entityStatus.empty() ? std::string(kDash) : asset.entityStatus};
    kv_[3] = {"视觉资产", asset.hasAsset ? (asset.assetName.empty() ? std::string(kDash)
                                                                    : asset.assetName)
                                         : std::string("尚未建立")};
    kv_[4] = {"Canon", asset.canonStatus.empty() ? std::string(kDash) : asset.canonStatus};
    kv_[5] = {"生产状态", asset.hasAsset ? statusLabel_ : std::string(kDash)};
    kv_[6] = {"形象层", layers_};
    kv_[7] = {"设定图", asset.sheetRelPath.empty() ? std::string(kDash) : asset.sheetRelPath};
    kv_[8] = {"降级", asset.degraded ? std::string("有降级产物") : std::string("无")};
    missingNote_ =
        asset.hasAsset
            ? kit::Fmt("对比需要同一角色的两层成图（visual_artifacts 里 front + turnaround 都 "
                       "DONE）并算出**像素差**；当前该资产形象层 {}/{} 就绪。这两层的图**已经能"
                       "逐层看到**（点派生链上的节点进查看器），缺的是对比本身：把两张图按同一"
                       "几何对齐、算差异分布并出结论 —— 这一步在本仓库里还没有实现（不是缺数据"
                       "接口）。",
                       asset.layersDone, asset.layers)
            : std::string(
                "对比需要同一角色的两层成图（visual_artifacts 里 front + turnaround 都 DONE）"
                "并算出**像素差**；当前该资产形象层 尚未建立。这两层的图**已经能逐层看到**"
                "（点派生链上的节点进查看器），缺的是对比本身：把两张图按同一几何对齐、"
                "算差异分布并出结论 —— 这一步在本仓库里还没有实现（不是缺数据接口）。");
}

void AssetsPage::Draw(Rect area, ImDrawList* draw) {
    BookState& s = Book();
    Rect actions;
    const Rect body = ViewHeader(area, draw, "masks", "视觉资产",
                                 "查找实体、对照设定图和派生结果", &actions);
    // 选项表恒定：提 static（第二十轮波 3）。
    static const std::vector<SegmentOption> options{{"d", "详情"}, {"o", "总览"}};
    const float tabsW = SegmentedWidth(options);
    const std::string_view picked =
        Segmented(draw, RectAt(actions.max.x - tabsW, actions.min.y, tabsW, actions.height()),
                  options, overview_ ? "o" : "d", "assets-seg");
    overview_ = (picked == "o");

    if (!s.bound || !s.error.empty() || s.assets.empty()) {
        BookEmpty(body, draw, "masks", "还没有实体资产。跑一次初始化与资产流水线后这里才有内容。");
        SetPageContentHeight(WorkspaceViewportHeight());
        return;
    }
    selected_ = std::clamp(selected_, 0, static_cast<int>(s.assets.size()) - 1);
    RefreshTexts();

    // ⚠️ kind 筛选是**侧栏与主区共享**的一份状态（设计稿 Assets.jsx:130 和 Shell.jsx:523
    //    读同一个 entityKind）。只筛侧栏的话，点完 chip 主区纹丝不动，看着像 chip 坏了。
    //    这里读同一份 BookKindFilter()，不另存一份 —— 存两份迟早只改得动一边。
    //
    //    详情**不**跟着筛：设计稿的 cur 取自未筛选的全集（Assets.jsx:131），所以筛到一个
    //    不含当前选中项的 kind 时详情页不空。照这个口径，否则点完 chip 主体变空态。
    const auto& kindFilter = BookKindFilter();
    std::vector<int> visible;
    visible.reserve(s.assets.size());
    for (int i = 0; i < static_cast<int>(s.assets.size()); ++i) {
        if (!kindFilter || s.assets[static_cast<std::size_t>(i)].kind == *kindFilter) {
            visible.push_back(i);
        }
    }

    const BookAsset& asset = s.assets[static_cast<std::size_t>(selected_)];

    if (overview_) {
        if (visible.empty()) {
            BookEmpty(body, draw, "masks",
                      "这一类还没有实体。在左侧侧栏换回「全部」看看这个工程里有哪些实体");
            SetPageContentHeight(WorkspaceViewportHeight());
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
            const Hit hit = HitTest(card, "asset-card-", i);
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
            // `box-shadow: var(--shadow-accent)`。总览网格卡的原型是
            // `webui/src/views/Assets.jsx:151` 的 `card asset-card hoverable`，两条规则都命中。
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
            // 名字/状态原样传 view，不再每卡拷串（第二十轮波 3）。
            const std::string_view name = row.name.empty() ? std::string_view(kDash) : row.name;
            kit::TextClipped(draw, ImVec2(card.min.x + 12.0f, card.min.y + 164.0f), cardW - 24.0f,
                             name,
                             TextStyle{.size = theme::font::kBody, .bold = true,
                                       .color = ColorText()},
                             true);
            std::string statusLabel;
            theme::Tone statusTone = theme::Tone::Idle;
            AssetTone(row.hasAsset ? row.assetStatus : std::string(), statusLabel, statusTone);
            const std::string_view sub = row.hasAsset ? std::string_view(statusLabel)
                                                      : std::string_view("无视觉资产");
            kit::TextClipped(draw, ImVec2(card.min.x + 12.0f, card.min.y + 180.0f), cardW - 24.0f,
                             sub, TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()},
                             true);
            if (hit.clicked) {
                selected_ = i;
            }
        }
        const int rows = (static_cast<int>(visible.size()) + columns - 1) / columns;
        SetPageContentHeight(body.min.y - area.min.y + rows * 206.0f + kGap);
        return;
    }

    // ---- 详情 ----
    float y = body.min.y;
    DrawIcon(draw, "masks", ImVec2(body.min.x, y), 16.0f, ColorAccent());
    // 名字 / 状态标签来自 RefreshTexts 缓存（第二十轮波 3）。
    const std::string& entityName = entityName_;
    // 画与量宽共用一份 style：Tag 的落点就是实体名的像素宽推出来的，两处不再各写
    // 一次 `FontBoldAt(15.0f), 15.0f`。
    const TextStyle entityStyle{.size = theme::font::kModal, .bold = true, .color = ColorText()};

    // 头部右侧：「查看大图」+「导出整版」——设计稿 `.vsec-h` 里的两颗按钮
    // （Assets.jsx:177-181 的「导出整版」与 :215 的「查看大图」）。都是**查看器**的入口，
    // 所以这里不另开一条通路。
    {
        struct HeadButton {
            const char* id;
            const char* label;
            const char* icon;
            bool sheet;
        };
        static const HeadButton kHeadButtons[2] = {
            {"asset-export-sheet", "导出整版", "download", true},
            {"asset-view-large", "查看大图", "eye", false},
        };
        // 一张图都没有（既没整版也没任何一层）时两颗都禁用 —— 打开一个必然空的面板
        // 不如直接说「这里现在没东西可看」。
        bool anyImage = !asset.sheetRelPath.empty();
        for (const BookArtifact& a : asset.artifacts) {
            anyImage = anyImage || !a.relPath.empty();
        }
        float bx = body.max.x;
        // `.btn.btn-sm` 12/700：两颗按钮共用一份 style。
        const TextStyle labelStyle{.size = theme::font::kTiny, .bold = true};
        for (int i = 0; i < 2; ++i) {
            const HeadButton& def = kHeadButtons[i];
            ButtonSpec spec;
            spec.variant = ButtonVariant::Secondary;
            spec.size = ButtonSize::Small;
            spec.icon = def.icon;
            spec.disabled = !anyImage;
            const float w = ButtonWidth(ButtonSize::Small, 13.0f,
                                        kit::TextWidth(def.label, labelStyle));
            bx -= w;
            const Rect r = RectAt(bx, y, w, ButtonHeight(ButtonSize::Small));
            if (Button(draw, r, def.label, spec, def.id)) {
                OpenMediaViewer(def.sheet ? AssetSheetViewerRequest(selected_)
                                          : AssetViewerRequest(selected_));
            }
            bx -= 8.0f;
        }
        const float tagW = TagWidth(statusLabel_, false, true);
        const float nameX = body.min.x + 24.0f;
        const float nameWidth = bx - nameX - tagW - 12.0f;
        const float drawnWidth =
            TextClipped(draw, ImVec2(nameX, y - 1.0f), nameWidth, entityName, entityStyle);
        Tag(draw, RectAt(nameX + drawnWidth + 12.0f, y - 1.0f, tagW, 20.0f),
            statusLabel_, statusTone_, false, true);
    }
    y += 28.0f;
    draw->AddLine(ImVec2(body.min.x, y), ImVec2(body.max.x, y), ColorLineSubtle(), 1.0f);
    y += 14.0f;

    // 当前选中项被 kind 筛选排除在外时，明说它不在筛选结果里 —— 照设计稿的 cur 口径
    // （Assets.jsx:131 从未筛选的全集取）详情照样显示，但用户会以为筛选没生效。
    if (kindFilter && std::find(visible.begin(), visible.end(), selected_) == visible.end()) {
        kit::TextClipped(draw, ImVec2(body.min.x, y), body.width(), filterNote_,
                         TextStyle{.size = theme::font::kLabel, .color = ColorTextMuted()}, true);
        y += 18.0f;
    }

    // 真实字段：类别 / 实体状态 / 资产名 / canon / 生产态 / 设定图路径 / 形象层进度。
    // 旧版的"别名 老沈""出处 第 1 章""降级策略 保留上一版"在 entities 表里**没有列**，
    // 所以这些行不画 —— 不用相邻字段冒充。KV 表来自 RefreshTexts 缓存（第二十轮波 3）。
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
    // ⚠️ 文案写着「点击查看大图」，就**必须真的点得动**：注册热区（`cursor: zoom-in`
    //    的那块 `.art`，Assets.jsx:186-202）。以前这行字只是文案，点上去什么也不会发生。
    const Hit baseHit = HitTest(baseArt, "asset-base-art");
    // 底色先铺：设计稿的 `.art` 自带 `background: var(--fill-muted)` + r-sm（ui.css:1074）。
    DrawRoundRect(draw, baseArt.min, baseArt.max, 6.0f,
                  baseHit.hovered ? ColorFillHover() : ColorFillMuted());
    const ImageSlot baseSheet = RequestImage(AssetImagePath(s.root, asset.sheetRelPath));
    if (baseSheet.ready()) {
        DrawImage(draw, baseArt, baseSheet, 6.0f, ImageFit::Cover);
    } else {
        Art(draw, baseArt, asset.entityId, /*cover=*/true);
    }
    if (baseHit.clicked) {
        // 第 0 页就是这张基线设定图；进查看器之后可以 ←→ 翻到各层。
        OpenMediaViewer(AssetViewerRequest(selected_));
    }
    kit::TextClipped(draw, ImVec2(body.min.x, baseArt.max.y + 8.0f), setLeftW,
                     "基线图 · 点击查看大图",
                     TextStyle{.size = theme::font::kMicro, .color = ColorTextMuted()}, true);

    const float keyValuesH = KeyValuesHeight(kv_.size());
    KeyValues(draw, RectAt(rightCol.min.x, y, rightCol.width(), keyValuesH), kv_.data(), kv_.size());

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
    // 链的层名与中文标签：**只此一份**，在 `MediaViewer.h` 里（资产页画链、查看器
    // 按层取图、联系表排格子三处共用）。这里早先存过一份局部数组 —— 两处各存一份，
    // 顺序一调就会出现「点第 3 格看的是第 4 层的图」。
    const float chainLineH = kChainFont * 1.6f;              // base.css 的全局行高
    const float chainLabelH = chainLineH + 6.0f * 2.0f;      // `.dlabel` padding 6px 8px
    const float chainNodeH = kChainThumbH + chainLabelH;

    const float captionTop = y + keyValuesH + 14.0f;
    constexpr char kChainCaption[] = "V0 派生链 · 正脸 → 四视图 → 基础身体 → 服装";
    const float captionHeight =
        DrawTextClipped(draw, FontBoldAt(kChainFont), kChainFont, ImVec2(rightCol.min.x, captionTop),
                        rightCol.width(), ColorTextSecondary(), kChainCaption, true);
    const float nodeTop = captionTop + captionHeight + 6.0f + kChainPadY;  // 说明行 + margin 6 + `.derive` padding
    float nx = rightCol.min.x + 2.0f;                                   // `.derive` padding 横向 2
    for (int i = 0; i < kChainLayerCount; ++i) {
        const BookArtifact* art = nullptr;
        for (const BookArtifact& a : asset.artifacts) {
            if (a.layer == kChainLayerKeys[i]) {
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
        // 点击 = 看这一层的大图（设计稿 `Assets.jsx:223` 的 `title="点击查看该层"`）。
        // ⚠️ hover 与 click 走**同一次** `HitTest`（与资产卡同一条纪律）：早先这里只有
        //    `node.contains(MousePos)` 的手算 hover、不注册热区，理由写在旧注释里
        //    「没有动作却注册热区就是『画得出来、点不动』」—— 现在动作有了（查看器），
        //    热区就必须跟上，否则同样是一类缺陷（画得出、点不动）。
        const Hit nodeHit = HitTest(node, "asset-chain-", i);
        const bool hovered = nodeHit.hovered;
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
        const std::string_view chainLabel = kChainLayerLabels[i];
        // `.dlabel` 11/700 —— size 仍是 kChainFont：chainLineH / chainLabelH 都由它推。
        // 画与居中（`CenterY` = `CenterTextY` 的 token 版）从同一份 style 出发。
        const TextStyle chainLabelStyle{.size = kChainFont, .bold = true,
                                        .color = ColorTextSecondary()};
        kit::Text(draw, ImVec2(dotCx + 3.5f + 5.0f, kit::CenterY(labelCy, chainLabelStyle)),
                  chainLabel, chainLabelStyle);
        nx += kChainNodeW;
        if (nodeHit.clicked) {
            // 页面第 1..N 页依次是四层 —— `AssetViewerRequest` 的页序就是这么排的
            // （0 = 基线整版，1..4 = 派生链），所以这里 +1。
            OpenMediaViewer(AssetViewerRequest(selected_), i + 1);
        }
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
    y += std::max(220.0f + kBaseCaptionH, keyValuesH + 14.0f + chainBlockH);
    draw->AddLine(ImVec2(body.min.x, y), ImVec2(body.max.x, y), ColorLineSubtle(), 1.0f);
    y += 14.0f;

    // 一致性对比 / 关联时间线 / 绑定镜头 chip：**没有可用来源**，逐条说清缺什么。
    constexpr char kMissingTitle[] = "尚无一致性对比数据";
    const Rect missing = RectAt(body.min.x, y, body.width(),
                                EmptyHeight(body.width(), kMissingTitle, missingNote_));
    Empty(draw, missing, "compare", kMissingTitle, missingNote_);
    y = missing.max.y + kGap;
    draw->AddLine(ImVec2(body.min.x, y), ImVec2(body.max.x, y), ColorLineSubtle(), 1.0f);
    y += 14.0f;

    static const char* kGapsTitle = "尚未接入的区块";
    // DrawTextClipped 收 string_view，自己算结束指针；末参是 wrap(bool)，不是 text_end。
    kit::TextClipped(draw, ImVec2(body.min.x, y), body.width(), kGapsTitle,
                     TextStyle{.size = theme::font::kHint, .bold = true, .color = ColorText()},
                     true);
    y += 20.0f;
    for (const char* gap : {"关联时间线：需要按章的事件 + 分镜引用，资产页还没有这条查询。",
                            "绑定镜头 chip：需要 shots.reference_json 解析成 visual_assets 路径。",
                            "参考图：项目参考库 refs.json 的读取接口未接到 ImGui 侧。"}) {
        y += kit::TextClipped(draw, ImVec2(body.min.x, y), body.width(), gap,
                             TextStyle{.size = theme::font::kTiny, .color = ColorTextSecondary()},
                             true) + 4.0f;
    }
    SetPageContentHeight(y - area.min.y + kGap);
}

} // namespace shine::pages
