#include "ui/pages/novel/NovelWorkspace.h"
#include "ui/kit/theme/CssColor.h"

#include "ui/pages/novel/ChapterFlowView.h"
#include "ui/pages/novel/DraftView.h"
#include "ui/pages/novel/InitChainView.h"
#include "ui/pages/novel/ModelPromptView.h"
#include "ui/pages/novel/ReviewView.h"
#include "ui/pages/novel/StateDiffView.h"
#include "ui/pages/novel/AutoRunPanel.h"
#include "ui/pages/novel/WorldBoardView.h"
#include "db/sqlite/SqliteDb.h"
#include "ui/kit/data/Panels.h"
#include "ui/kit/data/Table.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "novel/NovelDb.h"
#include "novel/NovelGraph.h"
#include "project/Project.h"

#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItem>
#include <QVBoxLayout>

#include <utility>

namespace shine::app {
namespace {

// UI.md §1 章卡片状态点：未写 / 草稿 / 待评审 / 已提交 / 失败 —— 全走 status.* token
[[nodiscard]] std::pair<QString, std::uint32_t> StatusView(const QString& status, bool emptyBody) {
    const theme::ColorToken& t = theme::Current();
    if (emptyBody || status == QLatin1String("outline")) {
        return {QStringLiteral("未写"), t.statusIdle};
    }
    if (status == QLatin1String("review")) {
        return {QStringLiteral("待评审"), t.statusBusy};
    }
    if (status == QLatin1String("done")) {
        return {QStringLiteral("已提交"), t.statusOk};
    }
    if (status == QLatin1String("failed")) {
        return {QStringLiteral("失败"), t.statusDanger};
    }
    return {QStringLiteral("草稿"), t.statusWarn}; // draft / writing / 未知值
}

[[nodiscard]] QIcon StatusDot(std::uint32_t token) {
    QPixmap px(10, 10);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setPen(Qt::NoPen);
    p.setBrush(widgets::TokenQColor(token));
    p.drawEllipse(1, 1, 8, 8);
    return QIcon(px);
}



} // namespace

NovelWorkspace::NovelWorkspace(QWidget* parent) : QWidget(parent) {
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(theme::space::kSteps[2], theme::space::kSteps[2],
                              theme::space::kSteps[2], theme::space::kSteps[2]);
    outer->setSpacing(theme::space::kSteps[2]);

    // ── 左栏：书→卷→章 树 + 章节卡片列表 ──
    auto* left = new QSplitter(Qt::Vertical, this);

    auto* treeBox = new QWidget(left);
    auto* tl = new QVBoxLayout(treeBox);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(theme::space::kSteps[1]);
    auto* treeCap = new QLabel(QStringLiteral("书 → 卷 → 章"), treeBox);
    tree_ = new data::DataTree(treeBox);
    tree_->SetColumns({QStringLiteral("章节"), QStringLiteral("状态")});
    tl->addWidget(treeCap);
    tl->addWidget(tree_, 1);

    auto* cardBox = new QWidget(left);
    auto* cl = new QVBoxLayout(cardBox);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(theme::space::kSteps[1]);
    auto* cardCap = new QLabel(QStringLiteral("章节卡片"), cardBox);
    cards_ = new QListWidget(cardBox);
    cards_->setViewMode(QListView::ListMode);
    cards_->setUniformItemSizes(true); // 千章滚动：统一行高走虚拟化快路径
    cards_->setWrapping(false);
    cards_->setSpacing(2);
    cl->addWidget(cardCap);
    cl->addWidget(cards_, 1);

    left->addWidget(treeBox);
    left->addWidget(cardBox);
    left->setSizes({300, 320});

    // ── 中栏：模式按钮行 + [章节]/[设定] 内容栈 ──
    auto* center = new QWidget(this);
    auto* ml = new QVBoxLayout(center);
    ml->setContentsMargins(theme::space::kSteps[3], 0, theme::space::kSteps[3], 0);
    ml->setSpacing(theme::space::kSteps[2]);

    // 模式按钮行（UI.md §2.1）：本 S 只接 [章节]（现有内容）与 [设定]（设定台 WorldBoardView）；
    // 其余按钮 disabled + tooltip「P04-Sx 接入」。
    // webui .novel-modes .ntab：p8 12 / f13 / w600 / muted，选中 accent + 2px 下划线；
    // 整条 h44 + 底部发丝线（modeRow 的 ntabbar 样式）。
    auto* modeRow = new QWidget(center);
    widgets::SetKind(modeRow, "ntabbar");
    modeRow->setFixedHeight(44);
    auto* mr = new QHBoxLayout(modeRow);
    mr->setContentsMargins(0, 0, 0, 0);
    mr->setSpacing(2);
    const auto makeMode = [modeRow](const QString& text) {
        auto* b = new widgets::Button(text, widgets::Button::Variant::Ghost,
                                      widgets::Button::Size::Sm, modeRow);
        widgets::SetKind(b, "ntab"); // 覆盖 button 样式：下划线页签，不是按钮
        b->setCheckable(true);
        return b;
    };
    const auto later = [](QPushButton* b, const QString& tip) {
        b->setEnabled(false);
        b->setToolTip(tip);
    };
    modeChapters_ = makeMode(QStringLiteral("章节"));
    modeWorld_ = makeMode(QStringLiteral("设定"));
    modeInit_ = makeMode(QStringLiteral("初始化"));
    modeFlow_ = makeMode(QStringLiteral("流水线"));
    auto* modeReview = makeMode(QStringLiteral("评审"));
    auto* modeModel = makeMode(QStringLiteral("模型"));
    auto* modeState = makeMode(QStringLiteral("状态"));
    modeAuto_ = makeMode(QStringLiteral("自动"));
    modeReview_ = modeReview;
    modeModel_ = modeModel;
    modeState_ = modeState;
    // [初始化]（P04-S4）实装：I1–I16 流水线 + 门禁 N1–N14 + 前情导入（InitChainView）
    modeInit_->setToolTip(QStringLiteral("初始化链：I1–I16 流水线 / 门禁 N1–N14 / 前情导入（P04-S4）"));
    // [流水线]（P04-S5）实装：T1–T17 StageFlow + 阶段产物 + 断点续跑（ChapterFlowView）
    modeFlow_->setToolTip(QStringLiteral("章节流水线：T1–T17 阶段状态 / 产物检视 / 断点续跑（P04-S5）"));
    // [评审]（P04-S7）实装：rubric 8 维 + K01–K29 + 修复轮入口（ReviewView）
    modeReview->setToolTip(QStringLiteral("评审与校验：rubric 8 维阈值 / K01–K29 快检 / 修复轮 ≤3（P04-S7）"));
    // [模型]（P04-S8）实装：四档模型分层 + auto 规则灯 + 外置 Prompt + Key 掩码（ModelPromptView）
    modeModel->setToolTip(QStringLiteral("模型与 Prompt：规划/写作/评审/提取 四档；"
                                         "评审≠写作才允许 auto；Key 只显掩码（P04-S8）"));
    // [状态]（P04-S9）实装：StateDiff + G1–G5 门禁 + 唯一 COMMIT 入口 + 回滚（StateDiffView）
    modeState->setToolTip(QStringLiteral("状态提交：StateDiff 展示 / G1–G5 逐条门禁 / "
                                         "唯一 COMMIT 入口（不调 LLM）/ 回滚快照（P04-S9）"));
    modeAuto_->setToolTip(QStringLiteral("无人值守：手动 / 半自动 / 全自动；预算、S1–S12、"
                                         "检查点与报告（P04-S10）"));
    auto* genBtn = new widgets::Button(QStringLiteral("▶ 生成本章"), widgets::Button::Variant::Primary,
                                       widgets::Button::Size::Sm, modeRow);
    genBtn->setToolTip(QStringLiteral("走章节流水线 T1–T17 生成当前章（P04-S5）"));
    for (QPushButton* b : {static_cast<QPushButton*>(modeChapters_),
                           static_cast<QPushButton*>(modeWorld_), static_cast<QPushButton*>(modeInit_),
                           static_cast<QPushButton*>(modeFlow_), static_cast<QPushButton*>(modeReview),
                           static_cast<QPushButton*>(modeModel_), static_cast<QPushButton*>(modeState_),
                           static_cast<QPushButton*>(modeAuto_)}) {
        mr->addWidget(b);
    }
    mr->addStretch(1);
    mr->addWidget(genBtn);
    ml->addWidget(modeRow);

    centerStack_ = new QStackedWidget(center);
    auto* chapterPage = new QWidget(centerStack_);
    auto* cpl = new QVBoxLayout(chapterPage);
    cpl->setContentsMargins(0, 0, 0, 0);
    cpl->setSpacing(theme::space::kSteps[2]);
    auto* headRow = new QWidget(chapterPage);
    auto* hr = new QHBoxLayout(headRow);
    hr->setContentsMargins(0, 0, 0, 0);
    hr->setSpacing(theme::space::kSteps[2]);
    title_ = new QLabel(QStringLiteral("未选择章节"), headRow);
    QFont tf = title_->font();
    tf.setPointSize(tf.pointSize() + 2);
    tf.setBold(true);
    title_->setFont(tf);
    status_ = new QLabel(headRow);
    hr->addWidget(title_, 1);
    hr->addWidget(status_, 0, Qt::AlignVCenter);
    summary_ = new QLabel(chapterPage);
    summary_->setWordWrap(true);
    // webui .chap-summary：fill-muted 底 + 3px accent 左条（引用块）
    widgets::SetKind(summary_, "chapsummary");
    // 正文区（P04-S6 DraftView 接管）：流式逐 token 只追加末段 + 呼吸光 + 中断落盘 + 重试
    draft_ = new DraftView(chapterPage);
    cpl->addWidget(headRow);
    cpl->addWidget(summary_);
    cpl->addWidget(draft_, 1);
    world_ = new WorldBoardView(centerStack_);   // [设定] 页：设定台（P04-S2）
    init_ = new InitChainView(centerStack_);     // [初始化] 页：初始化链（P04-S4）
    flow_ = new ChapterFlowView(centerStack_);   // [流水线] 页：T1–T17 章节流水线（P04-S5）
    review_ = new ReviewView(centerStack_);      // [评审] 页：rubric 8 维 + K01–K29（P04-S7）
    modelPrompt_ = new ModelPromptView(centerStack_); // [模型] 页：四档模型 + Prompt（P04-S8）
    stateDiff_ = new StateDiffView(centerStack_);   // [状态] 页：G1–G5 + COMMIT + 回滚（P04-S9）
    autoRun_ = new AutoRunPanel(centerStack_); // [自动] 页：无人值守（P04-S10）
    centerStack_->addWidget(chapterPage);
    centerStack_->addWidget(world_);
    centerStack_->addWidget(init_);
    centerStack_->addWidget(flow_);
    centerStack_->addWidget(review_);
    centerStack_->addWidget(modelPrompt_);
    centerStack_->addWidget(stateDiff_);
    centerStack_->addWidget(autoRun_);
    ml->addWidget(centerStack_, 1);
    connect(modeChapters_, &widgets::Button::clicked, this, [this] { SwitchCenter(0); });
    connect(modeWorld_, &widgets::Button::clicked, this, [this] { SwitchCenter(1); });
    connect(modeInit_, &widgets::Button::clicked, this, [this] { SwitchCenter(2); });
    connect(modeFlow_, &widgets::Button::clicked, this, [this] { SwitchCenter(3); });
    connect(modeReview_, &widgets::Button::clicked, this, [this] { SwitchCenter(4); });
    connect(modeModel_, &widgets::Button::clicked, this, [this] { SwitchCenter(5); });
    connect(modeState_, &widgets::Button::clicked, this, [this] { SwitchCenter(6); });
    connect(modeAuto_, &widgets::Button::clicked, this, [this] { SwitchCenter(7); });
    // ▶ 生成本章：跳到 [流水线] 页跑当前章的 T1–T17（与 ChapterFlowView 按钮同一路径）
    connect(genBtn, &widgets::Button::clicked, this, [this] {
        SwitchCenter(3);
        if (flow_ != nullptr) {
            flow_->GenerateCurrent(false);
        }
    });
    SwitchCenter(0);

    // ── 右栏：不再由本页自建 ──
    // 上一版这里自己又摆了一列「属性 / 预览区（P05/P07 接入…）」，和外壳的右栏叠在一起，
    // 一屏就变成 活动栏 + 页内左 + 页内中 + 页内右 + 外壳右 共五列，中央被挤扁。
    // 现在页面只留「章节导航 | 内容」两列，属性改由外壳的右侧检查器承载（InspectorBody()）。

    auto* split = new QSplitter(Qt::Horizontal, this);
    left->setMinimumWidth(240);
    center->setMinimumWidth(720); // 硬下限：低于此值内部表格与门禁行必然互相压扁
    split->addWidget(left);
    split->addWidget(center);
    split->setStretchFactor(1, 1); // 只有内容列可拉伸
    split->setCollapsible(0, true);
    split->setSizes({300, 1200});
    outer->addWidget(split, 1);
    // 章节导航（书→卷→章 树 + 章节卡片）交给外壳左侧栏（SidePanel::AdoptNav 借走）。
    // 所有权留在本页：RebuildTree / RebuildCards / SelectChapterAt 直接刷新 tree_/cards_，
    // 侧栏只负责摆放；导航不再占页内一列，模式行与内容区拿到整幅宽度。
    nav_host_ = left;
    nav_box_ = split;

    // 属性内容交给外壳检查器承载（默认收起，Ctrl+I / 顶栏「检查器」打开）。
    // 仍然在本页构建：RefreshProps 一类的更新路径不用改，只是父级换成了检查器。
    auto* inspector = new QWidget(this);
    auto* il = new QVBoxLayout(inspector);
    il->setContentsMargins(0, 0, 0, 0);
    il->setSpacing(theme::space::kSteps[2]);
    props_ = new data::KeyValue(inspector);
    il->addWidget(props_);
    il->addStretch();
    inspector_body_ = inspector;

    // 选章即时切换：树 ↔ 卡片双向同步（syncing_ 防回环）
    connect(cards_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!syncing_) {
            SelectChapterAt(row);
        }
    });
    connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& cur, const QModelIndex&) {
                if (syncing_) {
                    return;
                }
                const QVariant v = tree_->model()->data(cur, Qt::UserRole);
                if (v.isValid()) {
                    SelectChapterAt(v.toInt());
                }
            });
}

void NovelWorkspace::LoadFromRef(const project::ProjectRef& ref, const std::string& lastNovel) {
    QElapsedTimer timer;
    timer.start();
    books_.clear();
    chapters_.clear();
    if (modelPrompt_ != nullptr) {
        modelPrompt_->SetProjectDir(ref.rootDir); // Prompt 外置覆盖落 `<工程>/prompts/<role>.md`
    }
    if (stateDiff_ != nullptr) {
        stateDiff_->SetProjectDir(ref.rootDir); // 快照 `snapshots/ch<NNN>.json` 所在工程根
    }

    for (const project::BookRef& book : project::ListBooks(ref)) {
        BookNode b;
        b.title = QString::fromStdString(book.title);
        b.isDefault = book.isDefault;
        b.dbPath = book.dbPath;
        b.workDir = book.workDir;

        // 一部一库：每本书一个 SQLite 文件；卷→章 两级一次读全（千章级仍是单表顺序读）
        db::sqlite::Database db;
        std::vector<ChapterNode> chs;
        if (auto r = db.Open({.path = book.dbPath}); r) {
            novelcore::NovelGraph g(db);
            QHash<qint64, QString> volTitle;
            if (auto st = db.Prepare("SELECT id,ord,title FROM volumes ORDER BY ord,id")) {
                for (;;) {
                    auto s = st->Step();
                    if (!s || *s == db::sqlite::StepResult::Done) {
                        break;
                    }
                    VolumeNode v;
                    v.id = st->ColumnInt(0);
                    v.ord = static_cast<int>(st->ColumnInt(1));
                    v.title = QString::fromStdString(st->ColumnText(2));
                    volTitle.insert(v.id, v.title);
                    b.volumes.push_back(std::move(v));
                }
            }
            if (auto list = g.ListChapters(200000)) {
                for (const novelcore::ChapterRow& c : *list) {
                    ChapterNode n;
                    n.id = c.id;
                    n.volumeId = c.volume_id;
                    n.book = b.title;
                    n.volumeTitle = volTitle.value(c.volume_id);
                    n.ord = c.ord;
                    n.title = QString::fromStdString(c.title);
                    n.status = QString::fromStdString(c.status);
                    n.summary = QString::fromStdString(c.summary);
                    n.body = QString::fromStdString(c.body);
                    n.words = c.words;
                    chs.push_back(std::move(n));
                }
            }
        }
        for (ChapterNode& n : chs) {
            chapters_.push_back(std::move(n));
        }
        books_.push_back(std::move(b));
    }

    RebuildTree();
    RebuildCards();

    // 预选 lastNovel（ui.lastNovel）的首章；空 = 默认书
    int pick = 0;
    const QString want = QString::fromStdString(lastNovel);
    if (!want.isEmpty()) {
        for (int i = 0; i < static_cast<int>(chapters_.size()); ++i) {
            if (chapters_[static_cast<std::size_t>(i)].book == want) {
                pick = i;
                break;
            }
        }
    }
    if (!chapters_.empty()) {
        SelectChapterAt(pick);
    } else {
        title_->setText(QStringLiteral("这本书还没有章节"));
        summary_->setText(QStringLiteral("章节由初始化链（P04-S4）与生成流水线（P04-S5）创建。"));
        draft_->SelectChapter(0, {}, {});
        props_->SetPairs({});
    }
    if (world_ != nullptr) { // 设定台同书作用域：开当前书 db/novel.db 的可写句柄
        world_->LoadFromRef(ref, lastNovel);
    }
    if (init_ != nullptr) { // 初始化链同书作用域（P04-S4）：书根 init/ 产物 + work/ 配置与前情导入
        init_->LoadFromRef(ref, lastNovel);
    }
    if (flow_ != nullptr) { // 章节流水线同书作用域（P04-S5）：work/ch<NNN>/ 产物 + _manifest.json
        flow_->LoadFromRef(ref, lastNovel);
    }
    load_ms_ = timer.elapsed();
}

void NovelWorkspace::SwitchCenter(int index) {
    centerStack_->setCurrentIndex(index);
    // webui .ntab：选中 = accent 字 + 2px accent 下划线（颜色全走 QSS，不内联）
    const auto mark = [](QPushButton* b, bool on) {
        b->setChecked(on);
        b->setProperty("selected", on ? QStringLiteral("true") : QString{});
        widgets::Repolish(b);
    };
    mark(modeChapters_, index == 0);
    mark(modeWorld_, index == 1);
    mark(modeInit_, index == 2);
    mark(modeFlow_, index == 3);
    mark(modeReview_, index == 4);
    mark(modeModel_, index == 5);
    mark(modeState_, index == 6);
    mark(modeAuto_, index == 7);
    SyncPage(index); // 激活页按需同步当前章（不可见页的账留到此刻算）
}

void NovelWorkspace::RebuildTree() {
    tree_item_.clear();
    tree_->model()->removeRows(0, tree_->model()->rowCount());
    tree_chapters_ = 0;
    for (const BookNode& b : books_) {
        auto* bookItem = tree_->AddTop({b.title, QString{}}, false);
        bookItem->setEditable(false);
        const QBrush bookFg = widgets::TokenQColor(theme::Current().textMuted);
        bookItem->setForeground(bookFg);
        for (const VolumeNode& v : b.volumes) {
            auto* volItem = new QStandardItem(QStringLiteral("第 %1 卷　%2").arg(v.ord).arg(v.title));
            volItem->setEditable(false);
            auto* volState = new QStandardItem();
            volState->setEditable(false);
            volState->setForeground(bookFg);
            bookItem->appendRow({volItem, volState});
            for (int i = 0; i < static_cast<int>(chapters_.size()); ++i) {
                const ChapterNode& n = chapters_[static_cast<std::size_t>(i)];
                if (n.volumeId != v.id || n.book != b.title) {
                    continue;
                }
                const auto [label, token] = StatusView(n.status, n.body.isEmpty());
                auto* chItem = new QStandardItem(QStringLiteral("第 %1 章　%2").arg(n.ord).arg(n.title));
                chItem->setEditable(false);
                chItem->setData(i, Qt::UserRole); // 平铺下标（UserRole+1 是 DataTree 懒加载位）
                chItem->setIcon(StatusDot(token));
                auto* stItem = new QStandardItem(label);
                stItem->setEditable(false);
                stItem->setForeground(widgets::TokenQColor(token));
                volItem->appendRow({chItem, stItem});
                tree_item_.insert(i, chItem);
                ++tree_chapters_;
            }
        }
        tree_->expand(bookItem->index());
    }
}

void NovelWorkspace::RebuildCards() {
    card_item_.clear();
    cards_->clear();
    for (int i = 0; i < static_cast<int>(chapters_.size()); ++i) {
        const ChapterNode& n = chapters_[static_cast<std::size_t>(i)];
        const auto [label, token] = StatusView(n.status, n.body.isEmpty());
        auto* item = new QListWidgetItem(
            StatusDot(token),
            QStringLiteral("第 %1 章　%2").arg(n.ord).arg(n.title), cards_);
        item->setData(Qt::UserRole, i);
        item->setToolTip(QStringLiteral("%1 · %2").arg(n.book, label));
        card_item_.insert(i, item);
    }
}

bool NovelWorkspace::SelectChapterAt(int flatIndex) {
    if (flatIndex < 0 || flatIndex >= static_cast<int>(chapters_.size())) {
        return false;
    }
    current_ = flatIndex;
    syncing_ = true;
    if (QStandardItem* it = tree_item_.value(flatIndex, nullptr); it != nullptr) {
        tree_->setCurrentIndex(it->index());
        tree_->scrollTo(it->index());
    }
    if (QListWidgetItem* ci = card_item_.value(flatIndex, nullptr); ci != nullptr) {
        cards_->setCurrentItem(ci);
        cards_->scrollToItem(ci);
    }
    syncing_ = false;
    ShowCurrent();
    return true;
}

void NovelWorkspace::ShowCurrent() {
    if (current_ < 0 || current_ >= static_cast<int>(chapters_.size())) {
        return;
    }
    const ChapterNode& n = chapters_[static_cast<std::size_t>(current_)];
    const auto [label, token] = StatusView(n.status, n.body.isEmpty());
    title_->setText(QStringLiteral("第 %1 章 · %2").arg(n.ord).arg(n.title));
    status_->setText(label);
    status_->setStyleSheet(QStringLiteral("color:%1;").arg(shine::widget::CssRgb(token)));
    summary_->setText(n.summary.isEmpty() ? QStringLiteral("（暂无摘要）") : n.summary);
    props_->SetPairs({{QStringLiteral("书"), n.book},
                      {QStringLiteral("卷"), n.volumeTitle},
                      {QStringLiteral("章序"), QString::number(n.ord)},
                      {QStringLiteral("状态"), label},
                      {QStringLiteral("字数"), QString::number(n.words)},
                      {QStringLiteral("行 id"), QString::number(n.id)}});
    // 只同步**当前可见页**（S1 判据：千章 100 次选章总耗时有上限）。
    // 不可见页留到 [模式] 被激活时再按需同步（见 SyncPage）——避免每次选章都
    // 重开 SQLite / 读阶段产物 / 跑 K01–K29 全表。
    SyncPage(centerStack_->currentIndex());
}

void NovelWorkspace::SyncPage(int page) {
    if (current_ < 0 || current_ >= static_cast<int>(chapters_.size())) {
        return;
    }
    const ChapterNode& n = chapters_[static_cast<std::size_t>(current_)];
    if (page == 0 && draft_ != nullptr && draft_->CurrentChapterId() != n.id) {
        // 正文草稿（P04-S6）：开该书库（保存用）+ 载入正文与基线哈希；同章不重载（保住手改）
        for (const BookNode& b : books_) {
            if (b.title == n.book) {
                QString err;
                if (!draft_->OpenBook(b.dbPath, &err)) {
                    qWarning("DraftView: OpenBook failed");
                }
                break;
            }
        }
        draft_->SelectChapter(n.id, n.title,
                              n.body.isEmpty() ? QStringLiteral("（本章尚未落正文）") : n.body);
    } else if (page == 3 && flow_ != nullptr && flow_->CurrentChapterId() != n.id) {
        flow_->SelectChapter(n.id); // 流水线跟随当前章（树/卡点选即换）
    } else if (page == 4 && review_ != nullptr) {
        // 评审页跟随当前章：开该书库（机器校验查库）+ 选章（读评审产物 + 跑 K01–K29）
        for (const BookNode& b : books_) {
            if (b.title == n.book) {
                QString err;
                if (!review_->OpenBook(b.dbPath, b.workDir, &err)) {
                    qWarning("ReviewView: OpenBook failed");
                }
                break;
            }
        }
        review_->SelectChapter(n.id, n.ord);
    } else if (page == 6 && stateDiff_ != nullptr) {
        // 状态页跟随当前章：开该书库（提交/回滚要写库）+ 选章（读 StateDiff + 预检门禁）
        for (const BookNode& b : books_) {
            if (b.title == n.book) {
                QString err;
                if (!stateDiff_->OpenBook(b.dbPath, &err)) {
                    qWarning("StateDiffView: OpenBook failed");
                }
                break;
            }
        }
        stateDiff_->SelectChapter(n.id, n.ord);
    } else if (page == 7 && autoRun_ != nullptr) {
        // 自动页跟随当前书：从当前章开始，预算与报告写回该书根。
        for (const BookNode& b : books_) {
            if (b.title == n.book) {
                QString err;
                if (!autoRun_->OpenBook(b.dbPath, b.workDir.parent_path(), &err)) {
                    qWarning("AutoRunPanel: OpenBook failed");
                }
                break;
            }
        }
        autoRun_->SetStartChapter(n.ord);
    }
}

int NovelWorkspace::CardCount() const {
    return cards_->count();
}

QString NovelWorkspace::SelectionProbe() const {
    if (current_ < 0 || current_ >= static_cast<int>(chapters_.size())) {
        return QStringLiteral("none");
    }
    const ChapterNode& n = chapters_[static_cast<std::size_t>(current_)];
    const auto [label, token] = StatusView(n.status, n.body.isEmpty());
    (void)token;
    return QStringLiteral("book=%1|vol=%2|ord=%3|title=%4|status=%5|body=%6|words=%7")
        .arg(n.book, n.volumeTitle)
        .arg(n.ord)
        .arg(n.title, label)
        .arg(n.body.size())
        .arg(n.words);
}

} // namespace shine::app
