// shine::pages —— P5.3 资产（assets）—— 第一处真正用 src/gpu 的页面
//
// 同样从 WorkspaceB.cpp 搬来。旧版 12 张 "资产 N" 卡、沈砚/老沈/第 1 章、
// 基线 v2 / 当前 v3 / 差异 0.2418、S010–S014 chip、4 张参考图全是写死的。
// 现在逐项来自 entities + visual_assets + visual_artifacts。
#include "ui/imgui/pages/BookData.h"

#include "ui/imgui/pages/PageCommon.h"

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
            const Rect card = RectAt(body.min.x + (cardW + 14.0f) * static_cast<float>(column),
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
            // ui.css:196-200 `.card.hoverable:hover` → border 提到 **--line-strong**
            // + `box-shadow: var(--shadow-1)`；views.css:723-725 `.asset-card.on` →
            // `box-shadow: var(--shadow-accent)`。JSX 那边 class 是
            // `card asset-card hoverable`（Assets.jsx:151），两条规则都命中。
            //
            // ⚠️ 这里的 hover 边框原来用的是 accent-glow —— 那是「让 hover 看得出来」
            //    时自己挑的颜色，设计稿写的是 line-strong。两者差得很远，1:1 以 CSS 为准。
            //    （`.card.hoverable:hover` 还带 translateY(-2px)，那条还没接，见
            //    refactor/PROGRESS.md 的缺口表。）
            DrawShadowed(draw, card.min, card.max, 10.0f, ColorPanel(),
                         on ? ColorAccent() : (hit.hovered ? ColorLineStrong() : ColorLineSubtle()),
                         1.0f, on ? theme::ShadowTier::Accent
                                  : (hit.hovered ? theme::ShadowTier::Card
                                                 : theme::ShadowTier::None));
            // ⚠️ 缩略图：真实设定图要经 gpu 纹理链路解码（出图页才接）。没有就显示
            //    "无产出图"，不拿 Art() 占位画冒充这个角色的设定图。
            const Rect thumb{card.min.x + 8.0f, card.min.y + 8.0f, card.max.x - 8.0f, card.min.y + 158.0f};
            DrawRoundRect(draw, thumb.min, thumb.max, 8.0f, ColorFillMuted(), ColorLineSubtle(), 1.0f);
            DrawIconCentered(draw, "masks", thumb.center(), 24.0f, ColorTextMuted());
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
    KeyValues(draw, Rect{body.min.x, y, body.min.x + 640.0f, y + 150.0f},
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
    y += 164.0f;
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
              "，且帧图解码尚未接入 ImGui 侧。");
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
