#include "ui/pages/assets/AssetDetailView.h"

#include "ui/pages/assets/AssetPolicyPanel.h"
#include "ui/pages/novel/WorldBoardShared.h"
#include "ui/kit/data/Flow.h"
#include "ui/kit/data/Panels.h"
#include "ui/kit/images/Sheet.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/Surfaces.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "db/sqlite/SqliteDb.h"
#include "novel/NovelGraph.h"
#include "novel/NovelImageStore.h"
#include "util/Encoding.h"

#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QStringList>


#include <string_view>
#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <yyjson.h>

namespace shine::app {
namespace {

struct LayerSpec {
    novelcore::AssetLayer layer;
    std::string_view key;
    std::string_view title;
    std::string_view parent;
};

constexpr std::array<LayerSpec, 4> kLayers{{
    {novelcore::AssetLayer::Front, "front", "正脸", ""},
    {novelcore::AssetLayer::Turnaround, "turnaround", "四视图", "front"},
    {novelcore::AssetLayer::BaseBody, "base_body", "基础身体", "turnaround"},
    {novelcore::AssetLayer::Wardrobe, "wardrobe", "服装", "base_body"},
}};

// views.css:747 .vsec { gap: 10px } —— 10 不在 kSteps 刻度里（0/2/4/8/12/16…），
// 按 Token.h 的约定用命名常量，不去插 kSteps 下标。
constexpr int kVSecGap = 10;
// views.css:711 .asset-grid gap 14px：设定集里的形象层卡片同样按 14 排，
// 与资产卡网格保持同一节奏。
constexpr int kLayerGridGap = 14;
// views.css:764 .derive .dnode：缩略 64 + 标签 p6 8 + 行高 ≈15 + 上下边框 2 ≈ 93，
// 宿主横向滚动区固定到这个高度（overflow-x: auto 不改纵向尺寸）。
constexpr int kDeriveHeight = 96;

[[nodiscard]] QString ArtifactState(std::string_view status) {
    if (status == "DONE") return QStringLiteral("已就绪");
    if (status == "RUNNING") return QStringLiteral("生成中");
    if (status == "FAILED") return QStringLiteral("失败");
    return QStringLiteral("缺层");
}

[[nodiscard]] const char* ArtifactTone(std::string_view status) {
    if (status == "DONE") return "ok";
    if (status == "RUNNING") return "accent";
    if (status == "FAILED") return "danger";
    return "warn";
}

// webui .vsec-h：accent 字符图标 + 13.5px w700 标题 + 右侧补充说明
// （sub 对应 Assets.jsx 里 .vsec-h 末尾的 `.tiny.dim` 补充说明，缺省不显示）
// ⚠️ `.vsec-h .t` 的 13.5px 无法逐值复刻：QFont / QSS 的 font-size 只到整数像素，
//    这里沿用 QssBuilder 已落地的 13px（半像素差在灰度上不可见），不再内联覆盖。
QWidget* MakeVSecHead(const QString& icon, const QString& text, QWidget* parent,
                      QLabel** title_out = nullptr, const QString& sub = QString{}) {
    auto* head = new QWidget(parent);
    auto* row = new QHBoxLayout(head);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(9);
    auto* glyph = new QLabel(icon, head);
    widgets::SetKind(glyph, "vsecicon");
    glyph->setFixedWidth(15);
    row->addWidget(glyph);
    auto* title = new QLabel(text, head);
    widgets::SetKind(title, "vsechead");
    row->addWidget(title);
    if (!sub.isEmpty()) {
        // `.tiny.dim` 是**纯文字**（muted、无底无框）。原来这里用的是 statedetail
        // ——那是一个 fill-muted 底 + 1px 框 + r6 的胶囊，和设计稿完全不是一回事。
        auto* note = new QLabel(sub, head);
        widgets::SetKind(note, "statemeta");
        note->setStyleSheet(QStringLiteral("font-size: 12px;"));
        row->addWidget(note);
    }
    row->addStretch(1);
    if (title_out != nullptr) {
        *title_out = title;
    }
    return head;
}

[[nodiscard]] bool ShotHasEntity(std::string_view json, shine::novelcore::RowId entityId) {
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if (doc == nullptr) {
        return false;
    }
    bool found = false;
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (yyjson_is_arr(root)) {
        std::size_t index = 0;
        yyjson_val* child = nullptr;
        while ((child = yyjson_arr_get(root, index++)) != nullptr) {
            if (yyjson_get_num(child) == static_cast<long long>(entityId)) {
                found = true;
                break;
            }
        }
    }
    yyjson_doc_free(doc);
    return found;
}

// ============================================================
// 关联时间线（webui views.css:766-847 `.tl` / `.tl-below`）
//
// CSS 用 absolute + % 定位，Qt 的布局系统没有等价的百分比锚点，
// 所以这里保留「子控件 + 手动几何」的做法：Build() 建控件并记下锚点，
// Layout() 在 resizeEvent 里按容器宽度换算 x。
//
// 纵向几何逐值取自 views.css：
//   .axis   top 44 h2          → 轴
//   .tick   top 38 w2 h14      → 刻度（横跨轴）
//   .tick-l top 58            → 刻度文字
//   .ev     top 12            → 事件列（pin 10 + gap 4 + 文字）
//   .stem   top 30 h14        → 针到轴的连线
// ⚠️ .stem(30…44) 与 .ev 的文字行(26…39)在 CSS 里是重叠的（针会穿过标签）。
//    这里把针下移到 40…44 接住轴心，标签 26 起不受影响；其余数值不变。
// ============================================================
class RelTimeline : public QWidget {
  public:
    struct Event {
        int chapter = 0;
        QString label;
        QString tip;
        bool hot = false;
    };

    explicit RelTimeline(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedHeight(kHeight);
        axis_ = new QFrame(this);
        widgets::SetKind(axis_, "tlaxis");
        axis_->setFixedHeight(2);
        // 手工挂载的子控件没有布局兜底：Qt 只会把「首次 show 时已在树里」的子控件
        // 点亮，之后 new 出来的默认仍带 WA_WState_Hidden（评审抓图里表现为
        // 「轴在、刻度与事件全没了」）。这里逐个显式 show。
        axis_->show();
    }

    void SetChapters(int n) {
        chapters_ = n;
        ClearTicks();
        if (chapters_ <= 0) {
            return;
        }
        for (int i = 1; i <= chapters_; ++i) {
            auto* tick = new QFrame(this);
            widgets::SetKind(tick, "tltick");
            tick->setFixedSize(2, 14);
            auto* text = new QLabel(QStringLiteral("第 %1 章").arg(i), this);
            widgets::SetKind(text, "tlcap");
            text->setAlignment(Qt::AlignCenter);
            text->adjustSize();
            tick->show();
            text->show();
            ticks_.push_back({.tick = tick, .text = text, .chapter = i});
        }
        Layout();
    }

    void SetEvents(std::vector<Event> events) {
        ClearEvents();
        for (Event& event : events) {
            auto* stem = new QFrame(this);
            widgets::SetKind(stem, "tlaxis"); // .stem 与 .axis 同色同宽语义
            // 必须连高度一起定死：只给 setFixedWidth 的话 QFrame 在无布局的父里
            // 会撑满剩余高度（92px），竖线从 kStemTop 一路画到底，
            // 正好压穿下方 kTickLabelTop 处的「第 N 章」刻度标签
            // （评审抓图里表现为一条竖线从字上穿过）。
            stem->setFixedSize(2, kStemH);
            auto* pin = new QLabel(this);
            widgets::SetKind(pin, "tlpin");
            pin->setProperty("hot", event.hot ? QStringLiteral("true") : QString{});
            widgets::Repolish(pin);
            pin->setFixedSize(10, 10);
            auto* label = new QLabel(event.label, this);
            widgets::SetKind(label, "tlcap");
            label->setProperty("hot", event.hot ? QStringLiteral("true") : QString{});
            widgets::Repolish(label);
            label->setAlignment(Qt::AlignCenter);
            label->adjustSize();
            label->setToolTip(event.tip);
            if (!event.hot) {
                pin->setToolTip(event.tip);
            }
            stem->show();
            pin->show();
            label->show();
            events_.push_back({.chapter = event.chapter,
                               .stem = stem,
                               .pin = pin,
                               .label = label});
        }
        Layout();
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        Layout();
    }

  private:
    struct Tick {
        QFrame* tick = nullptr;
        QLabel* text = nullptr;
        int chapter = 0;
    };
    struct Placed {
        int chapter = 0;
        QFrame* stem = nullptr;
        QLabel* pin = nullptr;
        QLabel* label = nullptr;
    };

    // webui pct(ch) = ((ch - 0.5) / CH) * 100 —— 刻度落在等分格的中心
    [[nodiscard]] double Pct(int chapter) const {
        if (chapters_ <= 0) {
            return 50.0;
        }
        const int clamped = chapter < 1 ? 1 : (chapter > chapters_ ? chapters_ : chapter);
        return (static_cast<double>(clamped) - 0.5) / static_cast<double>(chapters_) * 100.0;
    }

    void Layout() {
        const int width = std::max(this->width(), 1);
        axis_->setGeometry(0, kAxisTop, width, 2);
        for (const Tick& tick : ticks_) {
            const int x = static_cast<int>(Pct(tick.chapter) / 100.0 * width);
            tick.tick->move(x - tick.tick->width() / 2, kTickTop);
            tick.text->move(x - tick.text->width() / 2, kTickLabelTop);
        }
        for (const Placed& placed : events_) {
            const int x = static_cast<int>(Pct(placed.chapter) / 100.0 * width);
            placed.pin->move(x - placed.pin->width() / 2, kPinTop);
            placed.stem->move(x - placed.stem->width() / 2, kStemTop);
            placed.label->move(x - placed.label->width() / 2, kEventLabelTop);
        }
    }

    void ClearTicks() {
        for (const Tick& tick : ticks_) {
            delete tick.tick;
            delete tick.text;
        }
        ticks_.clear();
    }

    void ClearEvents() {
        for (const Placed& placed : events_) {
            delete placed.stem;
            delete placed.pin;
            delete placed.label;
        }
        events_.clear();
    }

    static constexpr int kHeight = 92;         // views.css:768 .tl height
    static constexpr int kAxisTop = 44;        // views.css:775
    static constexpr int kTickTop = 38;        // views.css:782
    static constexpr int kTickLabelTop = 58;   // views.css:790
    static constexpr int kPinTop = 12;         // views.css:798 .ev top
    static constexpr int kEventLabelTop = 26;  // .ev 列内 pin(10)+gap(4) 之后
    static constexpr int kStemTop = 40;        // 见文件头说明（CSS 为 30，见「与 .stem 重叠」注）
    static constexpr int kStemH = 4;           // 40→44：正好搭到 kAxisTop(44) 的轴线上，不越界到刻度标签

    QFrame* axis_ = nullptr;
    int chapters_ = 0;
    std::vector<Tick> ticks_;
    std::vector<Placed> events_;
};

} // namespace
std::string AssetDetailView::ArtifactPath(const LayerData& layer) {
    return layer.artifact ? layer.artifact->rel_path : std::string{};
}

AssetDetailView::AssetDetailView(QWidget* parent) : QWidget(parent) {
    BuildUi();
    Clear();
}

void AssetDetailView::BuildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(theme::space::kSteps[2], theme::space::kSteps[2],
                              theme::space::kSteps[2], theme::space::kSteps[2]);
    outer->setSpacing(theme::space::kSteps[2]);

    auto* header = new QWidget(this);
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->setSpacing(theme::space::kSteps[1]);

    auto* titles = new QWidget(header);
    auto* title_layout = new QVBoxLayout(titles);
    title_layout->setContentsMargins(0, 0, 0, 0);
    title_layout->setSpacing(2);
    // 分区一「设定集」：webui .vsec —— 无卡片框，靠底部发丝线与下一区分开
    auto* vsec = new QWidget(this);
    widgets::SetKind(vsec, "vsec");
    auto* sec_layout = new QVBoxLayout(vsec);
    sec_layout->setContentsMargins(0, 0, 0, 0);
    sec_layout->setSpacing(kVSecGap);
    title_layout->addWidget(
        MakeVSecHead(QStringLiteral("◈"), QStringLiteral("资产详情 · 未选择"), titles, &title_));
    subtitle_ = new QLabel(QStringLiteral("选择资产后显示正脸、四视图、基础身体与服装。"), titles);
    subtitle_->setWordWrap(true);
    // webui `.vsec-h` 末尾的 `.tiny.dim`：纯文字 muted，无底无框
    widgets::SetKind(subtitle_, "statemeta");
    subtitle_->setStyleSheet(QStringLiteral("font-size: 12px;"));
    title_layout->addWidget(subtitle_);
    runtime_label_ = new QLabel(titles);
    runtime_label_->setWordWrap(true);
    widgets::SetKind(runtime_label_, "statemeta");
    runtime_label_->setStyleSheet(QStringLiteral("font-size: 12px;"));
    runtime_label_->hide();
    title_layout->addWidget(runtime_label_);
    header_layout->addWidget(titles, 1);

    generate_all_ = new widgets::Button(QStringLiteral("生成完整链"),
                                        widgets::Button::Variant::Primary,
                                        widgets::Button::Size::Sm, header);
    generate_all_->setToolTip(QStringLiteral("按正脸 → 四视图 → 基础身体 → 服装推进真实 V0 流水线"));
    generate_all_->setEnabled(false);
    header_layout->addWidget(generate_all_);

    export_ = new widgets::Button(QStringLiteral("导出整版"), widgets::Button::Variant::Secondary,
                                  widgets::Button::Size::Sm, header);
    export_->setToolTip(QStringLiteral("使用 SheetGrid 导出当前可用层与缺层占位"));
    export_->setEnabled(false);
    header_layout->addWidget(export_);
    sec_layout->addWidget(header);

    // webui Assets.jsx 的 <KV rows={类别 / 别名 / 出处 / 降级策略}>：
    // ui.css:1015 `.kv { grid auto 1fr; gap 6 14; f12.5 }` —— 复用 kit::data::KeyValue，
    // 不在页面里再造第二份两列表。取值全部来自真库行，不填占位。
    facts_ = new data::KeyValue(vsec);
    sec_layout->addWidget(facts_);

    outer->addWidget(vsec, 1);

    auto* scroll = new QScrollArea(vsec);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    cards_ = new QWidget(scroll);
    cards_layout_ = new QGridLayout(cards_);
    cards_layout_->setContentsMargins(0, 0, 0, 0);
    cards_layout_->setHorizontalSpacing(kLayerGridGap);
    cards_layout_->setVerticalSpacing(kLayerGridGap);
    cards_layout_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    scroll->setWidget(cards_);
    sec_layout->addWidget(scroll, 1);

    // 分区二「V0 派生链」：webui .derive —— 108px 节点 + 22px 连线
    auto* chain_sec = new QWidget(this);
    widgets::SetKind(chain_sec, "vsec");
    auto* chain_layout = new QVBoxLayout(chain_sec);
    chain_layout->setContentsMargins(0, 0, 0, 0);
    chain_layout->setSpacing(kVSecGap);
    chain_layout->addWidget(MakeVSecHead(
        QStringLiteral("⑂"), QStringLiteral("V0 派生链 · 正脸 → 四视图 → 基础身体 → 服装"),
        chain_sec));
    derive_ = new QWidget(chain_sec);
    derive_->setObjectName(QStringLiteral("assetDeriveChain"));
    derive_row_ = new QHBoxLayout(derive_);
    derive_row_->setContentsMargins(2, 6, 2, 6); // views.css:855 .derive { padding: 6px 2px }
    derive_row_->setSpacing(0);                  // 节点与连线之间靠 .dlink 自身的 22px 宽度
    derive_row_->addStretch(1);
    // views.css:854 .derive { overflow-x: auto }：四节点 108 + 三连线 22 = 498px，
    // 窄栏放不下时要能横向滚，而不是把节点压扁。
    auto* derive_scroll = new QScrollArea(chain_sec);
    derive_scroll->setFrameShape(QFrame::NoFrame);
    derive_scroll->setWidgetResizable(true);
    derive_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    derive_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    derive_scroll->setFixedHeight(kDeriveHeight);
    derive_scroll->setWidget(derive_);
    // 固定高度 + 按需出现的横向滚动条会互相抢位：滚动条一出现就吃掉 12~16px，
    // 底部标签行被裁。这里按「滚动条是否真的有滚动范围」动态给高度，
    // 放得下时不留空、放不下时正好够滚动条占位。
    connect(derive_scroll->horizontalScrollBar(), &QScrollBar::rangeChanged, derive_scroll,
            [derive_scroll](int range) {
                const int bar = range > 0 ? derive_scroll->horizontalScrollBar()->sizeHint().height()
                                          : 0;
                const int want = kDeriveHeight + bar;
                if (derive_scroll->height() != want) {
                    derive_scroll->setFixedHeight(want);
                }
            });
    chain_layout->addWidget(derive_scroll);

    // 分区三「关联时间线」：webui views.css:766-847 的 .tl（事件轴）+ .tl-below（绑定镜头 / 参考图）
    auto* tl_sec = new QWidget(this);
    widgets::SetKind(tl_sec, "vsec");
    tl_sec->setProperty("last", QStringLiteral("true"));
    widgets::Repolish(tl_sec);
    auto* tl_sec_layout = new QVBoxLayout(tl_sec);
    tl_sec_layout->setContentsMargins(0, 0, 0, 0);
    tl_sec_layout->setSpacing(kVSecGap);
    tl_sec_layout->addWidget(MakeVSecHead(
        QStringLiteral("◷"), QStringLiteral("关联时间线"), tl_sec, nullptr,
        QStringLiteral("外观基线 / 出处 / 绑定镜头 / 参考图 · 全部条目可点击")));
    timeline_body_ = new QWidget(tl_sec);
    timeline_body_->setObjectName(QStringLiteral("assetRelTimeline"));
    timeline_layout_ = new QVBoxLayout(timeline_body_);
    // views.css:769 `.tl { margin: 2px 10px 0 }` / views.css:841 `.tl-below { padding: 2px 10px 0 }`
    timeline_layout_->setContentsMargins(10, theme::space::kSteps[1], 10, 0);
    timeline_layout_->setSpacing(0);
    timeline_ = new RelTimeline(timeline_body_);
    timeline_layout_->addWidget(timeline_);
    tl_sec_layout->addWidget(timeline_body_);
    // webui Assets.jsx 的分区顺序 = 设定集（含派生链）→ 一致性对比 → 关联时间线。
    // 「一致性对比」在本页是独立检查器页，故这里保持 设定集 → 派生链 → 关联时间线。
    outer->addWidget(chain_sec);
    outer->addWidget(tl_sec);

    connect(export_, &QPushButton::clicked, this, &AssetDetailView::ExportSheet);
    policy_ = new AssetPolicyPanel(this);
    outer->addWidget(policy_);
    connect(generate_all_, &QPushButton::clicked, this, [this] {
        if (on_generate_all_) {
            on_generate_all_();
        }
    });
}

bool AssetDetailView::ShowAsset(db::sqlite::Database& db, novelcore::NovelVisual& visual,
                                const novelcore::VisualAssetRow& asset, novelcore::RowId entityId,
                                const std::filesystem::path& projectDir, QString* error) {
    Clear();
    if (error != nullptr) {
        error->clear();
    }
    if (asset.id <= 0) {
        const QString detail = QStringLiteral("资产 id 无效。");
        if (error != nullptr) {
            *error = detail;
        }
        ShowError(detail);
        return false;
    }

    auto listed = visual.ListArtifacts(asset.id);
    if (!listed) {
        const QString detail = QStringLiteral("读取资产 #%1 的形象层失败：%2")
                                  .arg(asset.id)
                                  .arg(QString::fromStdString(listed.error().message));
        if (error != nullptr) {
            *error = detail;
        }
        ShowError(detail);
        return false;
    }

    asset_ = asset;
    entity_id_ = entityId;
    projectDir_ = projectDir;
    // .kv「类别」行要实体 kind。AssetWorkspace 只把 asset 行 + entityId 递进来，
    // 这里按 id 读一次（只读；实体被删时留空，KindLabelOf 会原样回显 key）。
    entity_kind_.clear();
    if (entityId > 0) {
        novelcore::NovelGraph entity_graph(db);
        if (auto row = entity_graph.GetEntity(entityId); row) {
            entity_kind_ = QString::fromStdString(row->kind);
        }
    }
    layers_.clear();
    layers_.reserve(kLayers.size());
    for (const LayerSpec& spec : kLayers) {
        LayerData data;
        data.layer = spec.layer;
        data.key = std::string{spec.key};
        data.title = QString::fromUtf8(spec.title.data(), static_cast<int>(spec.title.size()));
        data.parent_key = std::string{spec.parent};
        for (const novelcore::VisualArtifactRow& artifact : *listed) {
            if (artifact.layer == data.key) {
                data.artifact = artifact;
                break;
            }
        }
        if (!data.artifact && data.key == "front" && !asset.sheet_rel_path.empty()) {
            novelcore::VisualArtifactRow legacy;
            legacy.asset_id = asset.id;
            legacy.layer = data.key;
            legacy.rel_path = asset.sheet_rel_path;
            legacy.status = "DONE";
            legacy.note = "兼容旧工程：visual_assets.sheet_rel_path 作为正脸层";
            data.artifact = std::move(legacy);
            data.legacy_front = true;
        }

        const std::filesystem::path path = ResolvePath(ArtifactPath(data));
        std::error_code ec;
        data.ready = data.artifact && data.artifact->status == "DONE" &&
                     !data.artifact->rel_path.empty() && std::filesystem::is_regular_file(path, ec);
        layers_.push_back(std::move(data));
    }

    CollectTimeline(db, visual, asset, entityId);
    Rebuild();
    return true;
}

// 关联时间线的数据来源（全部只读真库，不造假数据）：
//   visual_states            → 外观基线事件（from_chapter 落在哪一章）
//   chapters + shots         → 绑定镜头（shots.character_ids_json 含本实体）
//   generated_images(shot)   → 这些镜头已出的图，作「参考图」缩略
void AssetDetailView::CollectTimeline(db::sqlite::Database& db, novelcore::NovelVisual& visual,
                                      const novelcore::VisualAssetRow& asset,
                                      novelcore::RowId entityId) {
    timeline_chapters_ = 0;
    timeline_events_.clear();
    bound_shots_.clear();
    ref_images_.clear();

    novelcore::NovelGraph graph(db);
    auto chapters = graph.ListChapters(1000);
    std::unordered_map<novelcore::RowId, int> chapter_ord;
    if (chapters) {
        for (const novelcore::ChapterRow& chapter : *chapters) {
            // ord 才是「第 N 章」；同 ord 重复时保留先到的，避免刻度重号
            if (chapter_ord.emplace(chapter.id, chapter.ord).second) {
                ++timeline_chapters_;
            }
        }
    }

    const auto ord_of = [&chapter_ord](novelcore::RowId chapterId) {
        const auto found = chapter_ord.find(chapterId);
        return found == chapter_ord.end() ? 0 : found->second;
    };

    // —— 外观基线：visual_states 每行 = 一段从 from_chapter 起生效的外观 ——
    auto states = visual.ListStates(asset.id);
    int latest = 0;
    if (states) {
        for (const novelcore::VisualStateRow& state : *states) {
            const int ord = ord_of(state.from_chapter);
            if (ord <= 0) {
                continue;
            }
            latest = std::max(latest, ord);
            const std::string& stage =
                state.stage_label.empty() ? state.appearance : state.stage_label;
            timeline_events_.push_back(
                {.chapter = ord,
                 .label = QStringLiteral("外观基线 · %1").arg(QString::fromStdString(stage)),
                 .tip = QStringLiteral("第 %1 章起 · %2")
                            .arg(ord)
                            .arg(QString::fromStdString(state.appearance.empty() ? stage
                                                                                 : state.appearance)),
                 .hot = false});
        }
    }
    // 最后一枚基线 = 当前生效（webui 的「外观基线 ② · 当前」/ accent 针）
    if (!timeline_events_.empty()) {
        for (TimelineEvent& event : timeline_events_) {
            if (event.chapter == latest) {
                event.hot = true;
                event.label += QStringLiteral(" · 当前");
            }
        }
    }

    // —— 绑定镜头：逐章扫 shots，character_ids_json 含本实体即算 ——
    if (entityId > 0 && chapters) {
        for (const novelcore::ChapterRow& chapter : *chapters) {
            auto shots = visual.ListShotsByChapter(chapter.id);
            if (!shots) {
                continue;
            }
            for (const novelcore::ShotRow& shot : *shots) {
                if (!ShotHasEntity(shot.character_ids_json, entityId)) {
                    continue;
                }
                bound_shots_.push_back({.chapter = chapter.ord,
                                        .ord = shot.ord,
                                        .id = shot.id,
                                        .image_rel = {}});
            }
        }
    }

    // —— 参考图：绑定镜头已生成的图（generated_images.source_kind='shot'）——
    std::unordered_set<novelcore::RowId> shot_ids;
    for (const BoundShot& shot : bound_shots_) {
        shot_ids.insert(shot.id);
    }
    if (!shot_ids.empty()) {
        auto images = novelcore::ListGeneratedImages(db, 5000);
        if (images) {
            for (const novelcore::GeneratedImageRow& image : *images) {
                if (image.source_kind != "shot" || image.rel_path.empty() ||
                    shot_ids.find(image.source_id) == shot_ids.end()) {
                    continue;
                }
                const std::filesystem::path path = ResolvePath(image.rel_path);
                std::error_code ec;
                if (!std::filesystem::is_regular_file(path, ec)) {
                    continue;
                }
                const QString rel = QString::fromStdString(image.rel_path);
                for (BoundShot& shot : bound_shots_) {
                    if (shot.id == image.source_id && shot.image_rel.isEmpty()) {
                        shot.image_rel = rel;
                        break;
                    }
                }
                if (ref_images_.size() < 8 &&
                    std::find(ref_images_.begin(), ref_images_.end(), rel) == ref_images_.end()) {
                    ref_images_.push_back(rel);
                }
            }
        }
    }
}

void AssetDetailView::Clear() {
    asset_ = {};
    entity_id_ = 0;
    entity_kind_.clear();
    projectDir_.clear();
    layers_.clear();
    timeline_chapters_ = 0;
    timeline_events_.clear();
    bound_shots_.clear();
    ref_images_.clear();
    runtime_phase_.clear();
    runtime_detail_.clear();
    runtime_layer_.clear();
    runtime_history_.clear();
    runtime_active_ = false;
    runtime_degraded_ = false;
    if (title_ != nullptr) {
        title_->setText(QStringLiteral("资产详情 · 未选择"));
    }
    if (subtitle_ != nullptr) {
        subtitle_->setText(QStringLiteral("选择资产后显示正脸、四视图、基础身体与服装。"));
    }
    if (export_ != nullptr) {
        export_->setEnabled(false);
    }
    if (runtime_label_ != nullptr) {
        runtime_label_->clear();
        runtime_label_->hide();
    }
    if (generate_all_ != nullptr) {
        generate_all_->setEnabled(false);
    }
    if (cards_layout_ != nullptr) {
        ClearLayout(cards_layout_);
    }
    if (facts_ != nullptr) {
        facts_->SetPairs({});
        facts_->hide();
    }
    if (policy_ != nullptr) {
        policy_->SetRuntimeState({}, {}, {}, false, false);
    }
    if (derive_row_ != nullptr) {
        while (QLayoutItem* item = derive_row_->takeAt(0)) {
            if (QWidget* widget = item->widget()) {
                widget->deleteLater();
            }
            delete item;
        }
        derive_row_->addStretch(1);
    }
    if (timeline_layout_ != nullptr) {
        // 保留 index 0（.tl 轴），其余是上次建的 .tl-below 行
        while (timeline_layout_->count() > 1) {
            QLayoutItem* item = timeline_layout_->takeAt(1);
            if (QWidget* widget = item->widget()) {
                widget->deleteLater();
            }
            delete item;
        }
    }
    if (auto* axis = static_cast<RelTimeline*>(timeline_); axis != nullptr) {
        axis->SetChapters(0);
        axis->SetEvents({});
    }
}

// webui .tl-below（views.css:836-847）：一行 flex-wrap —— 「绑定镜头」chip 行 +
// 「参考图」缩略 + 右侧「全部条目可点击」提示。
void AssetDetailView::RebuildTimeline() {
    if (timeline_layout_ == nullptr) {
        return;
    }
    auto* axis = static_cast<RelTimeline*>(timeline_);
    if (axis == nullptr) {
        return;
    }
    axis->SetChapters(timeline_chapters_);
    std::vector<RelTimeline::Event> events;
    events.reserve(timeline_events_.size());
    for (const TimelineEvent& event : timeline_events_) {
        events.push_back({.chapter = event.chapter,
                          .label = event.label,
                          .tip = event.tip,
                          .hot = event.hot});
    }
    axis->SetEvents(std::move(events));

    if (timeline_chapters_ <= 0) {
        auto* empty = new widgets::EmptyState(
            QStringLiteral("◷"), QStringLiteral("还没有章节时间轴"),
            QStringLiteral("当前书库没有 chapters 记录；导入章节后这里会按章显示外观基线与绑定镜头。"),
            QString{}, timeline_body_);
        timeline_layout_->addWidget(empty);
        return;
    }

    auto* below = new QWidget(timeline_body_);
    auto* row = new QHBoxLayout(below);
    row->setContentsMargins(0, theme::space::kSteps[1], 0, 0);
    row->setSpacing(8); // views.css:839 .tl-below gap

    const auto caption = [below](const QString& text) {
        auto* label = new QLabel(text, below);
        widgets::SetKind(label, "tlcap");
        return label;
    };

    if (bound_shots_.empty()) {
        row->addWidget(caption(QStringLiteral("绑定镜头 无")));
    } else {
        row->addWidget(caption(QStringLiteral("绑定镜头")));
        for (const BoundShot& shot : bound_shots_) {
            // 复刻 kit::widgets::Chip（不得在页面里新造第二份药丸）。
            // 文案压到「第1章·镜1」：设计稿的 .tl-below chip 是 S01/S02 这种短码，
            // 评审抓图实测带空格的「第 1 章 · 镜 1」会被 chip 截掉末位（chip 的
            // sizeHint 比实测文本窄，行一挤就截字）。
            auto* chip = new widgets::Chip(QStringLiteral("第%1章·镜%2")
                                               .arg(shot.chapter)
                                               .arg(shot.ord),
                                           "", below);
            chip->setToolTip(shot.image_rel.isEmpty()
                                 ? QStringLiteral("镜头 #%1 · 尚未出图").arg(shot.id)
                                 : QStringLiteral("镜头 #%1 · %2").arg(shot.id).arg(shot.image_rel));
            // webui .tl-below 是 flex-wrap；Qt 没有流式布局，行放不下时 QHBoxLayout
            // 会压缩子控件把 chip 里的字截断（评审抓图实测过）。横向策略设为 Minimum，
            // 即「sizeHint 就是下限、只许变宽」，让它宁可在窄栏溢出也不截字。
            chip->setSizePolicy(QSizePolicy::Minimum, chip->sizePolicy().verticalPolicy());
            connect(chip, &widgets::Chip::clicked, this, [this, shot] {
                widgets::Toast::Show(
                    shot.image_rel.isEmpty()
                        ? QStringLiteral("第 %1 章 · 镜 %2（镜头 #%3）尚未出图")
                              .arg(shot.chapter)
                              .arg(shot.ord)
                              .arg(shot.id)
                        : QStringLiteral("第 %1 章 · 镜 %2 → %3")
                              .arg(shot.chapter)
                              .arg(shot.ord)
                              .arg(shot.image_rel),
                    widgets::Toast::Tone::Info);
            });
            row->addWidget(chip);
        }
    }

    row->addSpacing(theme::space::kXs); // webui Assets.jsx:99 的 marginLeft 10
    row->addWidget(caption(QStringLiteral("参考图")));
    if (ref_images_.empty()) {
        row->addWidget(caption(QStringLiteral("无")));
    } else {
        for (const QString& rel : ref_images_) {
            auto* thumb = new QLabel(below);
            widgets::SetKind(thumb, "tlref");
            thumb->setFixedSize(52, 36); // webui Assets.jsx:101
            thumb->setAlignment(Qt::AlignCenter);
            thumb->setToolTip(QStringLiteral("查看 %1").arg(rel));
            const std::filesystem::path abs = ResolvePath(rel.toStdString());
            QImageReader reader(QString::fromStdString(util::PathToUtf8(abs)));
            reader.setAutoTransform(true);
            const QImage image = reader.read();
            if (!image.isNull()) {
                thumb->setPixmap(QPixmap::fromImage(
                    image.scaled(52, 36, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)));
            } else {
                thumb->setText(QStringLiteral("▧"));
            }
            row->addWidget(thumb);
        }
    }
    row->addStretch(1);
    timeline_layout_->addWidget(below);
}

// webui .derive：横向节点链（节点 108px + 连线 22px）；已就绪的层填 accent 连线
void AssetDetailView::RebuildDerive() {
    if (derive_row_ == nullptr) {
        return;
    }
    while (QLayoutItem* item = derive_row_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        LayerData& data = layers_[i];
        if (i > 0) {
            auto* link = new QFrame(derive_);
            widgets::SetKind(link, "derivelink");
            link->setFixedSize(22, 2); // views.css:883 .dlink 22×1.5（Qt 取 2px，整数像素下最接近）
            if (data.ready) {
                link->setProperty("fill", QStringLiteral("true"));
                widgets::Repolish(link);
            }
            derive_row_->addWidget(link, 0, Qt::AlignVCenter);
        }
        auto* node = new QFrame(derive_);
        widgets::SetKind(node, "derivenode");
        node->setFixedWidth(108);
        node->setToolTip(QStringLiteral("%1 · %2").arg(data.title, ArtifactState(
            data.artifact ? data.artifact->status : "PENDING")));
        auto* body = new QVBoxLayout(node);
        body->setContentsMargins(0, 0, 0, 0);
        body->setSpacing(0);

        auto* thumb = new QLabel(QStringLiteral("▧"), node);
        widgets::SetKind(thumb, "tlthumb");
        thumb->setFixedHeight(64);
        thumb->setAlignment(Qt::AlignCenter);
        if (data.ready) {
            const std::filesystem::path path = ResolvePath(ArtifactPath(data));
            QImageReader reader(QString::fromStdString(util::PathToUtf8(path)));
            reader.setAutoTransform(true);
            const QImage image = reader.read();
            if (!image.isNull()) {
                thumb->setPixmap(QPixmap::fromImage(
                    image.scaled(106, 64, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)));
            }
        }
        body->addWidget(thumb);

        auto* label = new QWidget(node);
        auto* label_row = new QHBoxLayout(label);
        label_row->setContentsMargins(8, 6, 8, 6);
        label_row->setSpacing(5);
        auto* dot = new QLabel(data.ready ? QStringLiteral("●") : QStringLiteral("○"), label);
        widgets::SetKind(dot, "tlpin");
        dot->setProperty("hot", data.ready ? QStringLiteral("true") : QString{});
        widgets::Repolish(dot);
        dot->setFixedSize(10, 10);
        label_row->addWidget(dot);
        auto* text = new widgets::ElidedLabel(data.title, label);
        text->SetExpandable(false);
        label_row->addWidget(text);
        body->addWidget(label);

        // webui views.css:850 .derive { align-items: center }：
        // 节点与连接线都垂直居中，QHBoxLayout 默认顶对齐会让连接线浮在节点上沿。
        derive_row_->addWidget(node, 0, Qt::AlignVCenter);
    }
    derive_row_->addStretch(1);
}

// webui Assets.jsx 设定集区的 <KV rows={类别 / 别名 / 出处 / 降级策略}>。
// Qt 端能取到的真值只有资产行 / 形象层 / 时间线三处，别名与降级策略不在本页数据源里，
// 因此不写这两项，也不编「—」以外的占位。
void AssetDetailView::RebuildKeyValue() {
    if (facts_ == nullptr) {
        return;
    }
    if (asset_.id <= 0) {
        facts_->SetPairs({});
        facts_->hide();
        return;
    }
    facts_->show();
    int ready = 0;
    for (const LayerData& data : layers_) {
        ready += data.ready ? 1 : 0;
    }
    const int events = static_cast<int>(timeline_events_.size());
    facts_->SetPairs({
        {QStringLiteral("类别"),
         entity_kind_.isEmpty()
             ? QString::fromStdString(asset_.kind)
             : QStringLiteral("%1 · %2").arg(QString::fromStdString(asset_.kind),
                                            KindLabelOf(entity_kind_.toStdString()))},
        {QStringLiteral("资产"), QStringLiteral("#%1 · %2")
                                     .arg(asset_.id)
                                     .arg(QString::fromStdString(asset_.name))},
        {QStringLiteral("生产状态"), QString::fromStdString(asset_.status)},
        {QStringLiteral("绑定实体"), QStringLiteral("#%1").arg(entity_id_)},
        {QStringLiteral("形象层"), QStringLiteral("%1 层 · 就绪 %2")
                                       .arg(static_cast<int>(layers_.size()))
                                       .arg(ready)},
        {QStringLiteral("外观基线"), timeline_chapters_ > 0
                                        ? QStringLiteral("%1 段 · 第 1–%2 章")
                                              .arg(events)
                                              .arg(timeline_chapters_)
                                        : QStringLiteral("暂无章节时间轴")},
        {QStringLiteral("绑定镜头"), QStringLiteral("%1 个").arg(
                                          static_cast<int>(bound_shots_.size()))},
    });
}

AssetPolicy AssetDetailView::Policy() const {
    return policy_ == nullptr ? AssetPolicy{} : policy_->Policy();
}

void AssetDetailView::SetPolicy(const AssetPolicy& policy) {
    if (policy_ != nullptr) {
        policy_->SetPolicy(policy);
    }
}

QString AssetDetailView::PolicyProbe() const {
    return policy_ == nullptr ? QStringLiteral("policy=unavailable") : policy_->PolicyProbe();
}

void AssetDetailView::SetRuntimeState(QString phase, QString detail, QStringList history,
                                      QString layer, bool active, bool degraded) {
    runtime_phase_ = std::move(phase);
    runtime_detail_ = std::move(detail);
    runtime_history_ = std::move(history);
    runtime_layer_ = std::move(layer);
    runtime_active_ = active;
    runtime_degraded_ = degraded;
    if (runtime_label_ != nullptr) {
        if (runtime_history_.isEmpty()) {
            runtime_label_->clear();
            runtime_label_->hide();
        } else {
            runtime_label_->setText(QStringLiteral("状态机：%1%2%3")
                                        .arg(runtime_history_.join(QLatin1String(" → ")),
                                             runtime_degraded_ ? QStringLiteral(" · ⚠ 降级") : QString{},
                                             runtime_detail_.isEmpty()
                                                 ? QString{}
                                                 : QStringLiteral(" · %1").arg(runtime_detail_)));
            runtime_label_->show();
        }
    }
    if (policy_ != nullptr) {
        policy_->SetRuntimeState(runtime_layer_, runtime_phase_, runtime_detail_, runtime_active_,
                                  runtime_degraded_);
    }
    if (generate_all_ != nullptr) {
        generate_all_->setEnabled(asset_.id > 0 && asset_.status != "READY" && !runtime_active_);
        generate_all_->setText(asset_.status == "READY" ? QStringLiteral("已完成")
                                                         : QStringLiteral("生成完整链"));
    }
}

void AssetDetailView::Rebuild() {
    if (cards_layout_ == nullptr || derive_row_ == nullptr) {
        return;
    }
    ClearLayout(cards_layout_);
    RebuildTimeline();
    RebuildKeyValue();

    title_->setText(QStringLiteral("资产详情 · %1").arg(QString::fromStdString(asset_.name)));
    subtitle_->setText(QStringLiteral("资产 #%1 · %2 · 状态 %3")
                           .arg(asset_.id)
                           .arg(QString::fromStdString(asset_.kind),
                                QString::fromStdString(asset_.status)));

    int ready_count = 0;
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        LayerData& data = layers_[i];
        const std::string status = data.artifact ? data.artifact->status : std::string{"PENDING"};
        if (data.ready) {
            ++ready_count;
        }

        auto* card = new widgets::Card(widgets::Card::Variant::Outlined, cards_);
        card->setMinimumWidth(230);
        QVBoxLayout* body = card->BodyLayout();

        auto* head = new QWidget(card);
        auto* head_layout = new QHBoxLayout(head);
        head_layout->setContentsMargins(0, 0, 0, 0);
        auto* name = new QLabel(data.title, head);
        widgets::SetSemibold(name, true);
        head_layout->addWidget(name);
        head_layout->addStretch(1);
        head_layout->addWidget(new widgets::Tag(ArtifactState(status), ArtifactTone(status), false, head));
        body->addWidget(head);

        auto* preview = new QLabel(card);
        preview->setFixedHeight(176);
        preview->setAlignment(Qt::AlignCenter);
        preview->setWordWrap(true);
        widgets::SetKind(preview, "field");
        if (data.ready) {
            const std::filesystem::path path = ResolvePath(ArtifactPath(data));
            QImageReader reader(QString::fromStdString(util::PathToUtf8(path)));
            reader.setAutoTransform(true);
            const QImage image = reader.read();
            if (image.isNull()) {
                preview->setText(QStringLiteral("图像读取失败\n请检查文件或重新导入"));
            } else {
                data.decoded = true;
                preview->setPixmap(QPixmap::fromImage(
                    image.scaled(218, 164, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            }
        } else if (data.artifact && data.artifact->status == "RUNNING") {
            preview->setText(QStringLiteral("生成中…\n完成后自动刷新"));
        } else if (data.artifact && !data.artifact->rel_path.empty()) {
            preview->setText(QStringLiteral("产物文件缺失\n可重试该层"));
        } else {
            preview->setText(QStringLiteral("缺层\n下一步：生成这一层"));
        }
        body->addWidget(preview);

        QString parent_text = data.parent_key.empty()
                                  ? QStringLiteral("派生起点：文生图")
                                  : QStringLiteral("派生自：%1")
                                        .arg(QString::fromStdString(data.parent_key));
        if (data.artifact) {
            parent_text += QStringLiteral(" · artifact #%1").arg(data.artifact->id);
        }
        if (data.legacy_front) {
            parent_text += QStringLiteral(" · 旧单图兼容");
        }
        auto* parent = new QLabel(parent_text, card);
        parent->setWordWrap(true);
        widgets::SetKind(parent, "statedetail");
        body->addWidget(parent);

        if (!data.ready) {
            const QString action_text = status == "FAILED" ? QStringLiteral("重试")
                                                           : QStringLiteral("生成");
            auto* action = new widgets::Button(action_text, widgets::Button::Variant::Primary,
                                               widgets::Button::Size::Sm, card);
            action->setToolTip(QStringLiteral("为「%1」启动真实形象层任务；依赖与降级按 V0 策略处理")
                                   .arg(data.title));
            connect(action, &QPushButton::clicked, this, [this, layer = data.layer] {
                if (on_generate_) {
                    on_generate_(layer);
                }
            });
            body->addWidget(action);
        } else if (!data.artifact->note.empty()) {
            auto* note = new QLabel(QString::fromStdString(data.artifact->note), card);
            note->setWordWrap(true);
            body->addWidget(note);
        }
        body->addStretch(1);
        cards_layout_->addWidget(card, static_cast<int>(i / 2), static_cast<int>(i % 2));
    }
    RebuildDerive();
    export_->setEnabled(ready_count > 0);
    if (generate_all_ != nullptr) {
        generate_all_->setEnabled(asset_.status != "READY" && !runtime_active_);
        generate_all_->setText(asset_.status == "READY" ? QStringLiteral("已完成")
                                                         : QStringLiteral("生成完整链"));
    }
}

void AssetDetailView::ShowError(const QString& detail) {
    if (cards_layout_ == nullptr) {
        return;
    }
    ClearLayout(cards_layout_);
    auto* error = new widgets::ErrorState(QStringLiteral("读取资产详情失败"), detail, cards_);
    cards_layout_->addWidget(error, 0, 0, 1, 2);
    if (subtitle_ != nullptr) {
        subtitle_->setText(detail);
    }
    if (export_ != nullptr) {
        export_->setEnabled(false);
    }
    if (generate_all_ != nullptr) {
        generate_all_->setEnabled(false);
    }
}

void AssetDetailView::ExportSheet() {
    images::SheetGrid sheet(2, QSize(360, 280));
    for (LayerData& data : layers_) {
        if (!data.ready) {
            continue;
        }
        const std::filesystem::path path = ResolvePath(ArtifactPath(data));
        QImageReader reader(QString::fromStdString(util::PathToUtf8(path)));
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (!image.isNull()) {
            sheet.Add(image, data.title);
        }
    }
    if (sheet.Count() == 0) {
        widgets::Toast::Show(QStringLiteral("没有可导出的形象层；请先生成至少一层。"),
                             widgets::Toast::Tone::Warning);
        return;
    }

    const QString suggested = QString::fromStdString(asset_.name) + QStringLiteral("-角色设定集.png");
    const QString target = QFileDialog::getSaveFileName(this, QStringLiteral("导出整版设定集"),
                                                        suggested, QStringLiteral("PNG 图像 (*.png)"));
    if (target.isEmpty()) {
        return;
    }
    const std::filesystem::path path = util::PathFromUtf8(target.toStdString());
    if (sheet.ExportPng(path)) {
        widgets::Toast::Show(QStringLiteral("设定集已导出：%1").arg(target),
                             widgets::Toast::Tone::Success);
    } else {
        widgets::Toast::Show(QStringLiteral("导出失败，请检查目标路径是否可写。"),
                             widgets::Toast::Tone::Error);
    }
}

std::filesystem::path AssetDetailView::ResolvePath(std::string_view relative) const {
    const std::filesystem::path path = util::PathFromUtf8(relative);
    return path.is_absolute() ? path : projectDir_ / path;
}

QString AssetDetailView::DetailProbe() const {
    int ready = 0;
    int decoded = 0;
    int actions = 0;
    QStringList states;
    for (const LayerData& data : layers_) {
        const bool is_ready = data.ready;
        ready += is_ready ? 1 : 0;
        decoded += data.decoded ? 1 : 0;
        actions += is_ready ? 0 : 1;
        states.push_back(QStringLiteral("%1=%2")
                             .arg(QString::fromStdString(data.key),
                                  data.artifact && !data.artifact->status.empty()
                                      ? QString::fromStdString(data.artifact->status)
                                      : QStringLiteral("PENDING")));
    }
    return QStringLiteral("asset=#%1:%2; layers=%3; ready=%4; decoded=%5; missing=%6; actions=%7; links=3; chain=%8; states=%9; runtime=%10; history=%11; active=%12; degraded=%13")
        .arg(asset_.id)
        .arg(QString::fromStdString(asset_.name))
        .arg(static_cast<int>(layers_.size()))
        .arg(ready)
        .arg(decoded)
        .arg(static_cast<int>(layers_.size()) - ready)
        .arg(actions)
        .arg(QStringLiteral("front>turnaround>base_body>wardrobe"))
        .arg(states.join(QLatin1Char('>')))
        .arg(runtime_phase_.isEmpty() ? QStringLiteral("none") : runtime_phase_)
        .arg(runtime_history_.isEmpty() ? QStringLiteral("none")
                                        : runtime_history_.join(QLatin1Char('>')))
        .arg(runtime_active_ ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(runtime_degraded_ ? QStringLiteral("1") : QStringLiteral("0"));
}


} // namespace shine::app
