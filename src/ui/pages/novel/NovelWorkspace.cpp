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
#include "novel/NovelStageLedger.h" // StageFileName：`03` §2.7 产物文件名的单一权威
#include "project/Project.h"
#include "util/Encoding.h" // PathToUtf8：路径 → UI 文本

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
#include <QTimer>
#include <QVBoxLayout>

#include <iterator> // std::size（kChapterArtifacts 的行数）
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

// 阅读测量线：webui .chap-summary（views.css:598）与 .draft（:572）同取 720px，
// 正文与摘要因此始终排在同一条竖线上。DraftView.cpp 里另有一份同名常量。
constexpr int kMeasureMaxW = 720;

// webui `.mono { font-family:var(--font-mono); font-size:12px }`（base.css:114-117）。
// kit 未提供字体工厂，这里按 theme::font::kMonoFamily 的族序逐档回退（与 StateDiffView
// 的同名辅助同口径）。
[[nodiscard]] QFont MonoFont(const QFont& base, int pixelSize) {
    QFont f = base;
    f.setFamilies({QString::fromLatin1("Cascadia Code"), QString::fromLatin1("JetBrains Mono"),
                    QString::fromLatin1("Consolas"), QString::fromLatin1("monospace")});
    f.setPixelSize(pixelSize);
    return f;
}

// 本章产物一行（webui Novel.jsx:471 的 `[[f, t, icon]]` 三元组）。
// 文件名**不写死**：一律问 novelcore::StageFileName 要（`03` §2.7 的单一权威）。
struct ChapterArtifact {
    const char* code;  // T 段号（设计稿「大纲 · T3」那一列）
    const char* label; // 阶段中文名
    const char* stage; // §2.2 阶段代码 → StageFileName；空串 = 库内字段（不落 work/）
    const char* glyph; // 行首字符图标（kit 字符图标约定：<Icon name> 的字符替身）
};
// T3 那行填 SCENE_EVENT_ORDER：`03` §2.2 的 T2–T4/T6–T9 合并执行，大纲没有自己的
// 文件、产物并入 09_chapter_plan.json —— 与 ChapterFlowView::ShowStage 同一条规则
// （ChapterFlowView.cpp:562-563）。T11 正文是库内字段 chapters.body：P4 正文落盘前
// 不写库，§2.7 的文件名表里也没有草稿（NovelStageLedger.h:12-13）。
// glyph：设计稿 text / chip 两个 <Icon> → 仓内已在用的字符 ▤（VideoFlowWorkspace.cpp:149）
// 与 ◈（FilmStrip.cpp:57）。
constexpr ChapterArtifact kChapterArtifacts[] = {
    {"T3", "大纲", "SCENE_EVENT_ORDER", "▤"},
    {"T11", "正文", "", "▤"},
    {"T12", "评审", "CHAPTER_REVIEW", "◈"},
};

[[nodiscard]] QString ArtifactFileName(const ChapterArtifact& a) {
    if (a.stage[0] == '\0') {
        return QStringLiteral("chapters.body");
    }
    const std::string_view f = novelcore::StageFileName(a.stage);
    return f.empty() ? QStringLiteral("—")
                     : QString::fromLatin1(f.data(), static_cast<int>(f.size()));
}

} // namespace

// ── StatusTagRow ──────────────────────────────────────────────────
// 每个取值预建一个 Tag、只切可见性：kit::Tag 的文案与 tone 都是构造期固定的，
// 运行期换文案只能重建控件（会漏控件、会打断布局），状态取值又是有限枚举。
StatusTagRow::StatusTagRow(const QStringList& keys, const QStringList& texts,
                           const QStringList& tones, QWidget* parent) : QWidget(parent) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(theme::space::kXs);
    for (int i = 0; i < keys.size() && i < texts.size(); ++i) {
        const QByteArray tone = i < tones.size() ? tones.at(i).toLatin1() : QByteArray{};
        auto* tag = new widgets::Tag(texts.at(i), tone.constData(), false, this);
        tag->setVisible(false);
        row->addWidget(tag);
        tag_of_.insert(keys.at(i), tag);
    }
    current_ = QStringLiteral("\x01"); // 哨兵：强制首次 Show 执行一次「全隐藏」
    Show(QString{});                   // 未选章时不显示任何状态标记
}

void StatusTagRow::Show(const QString& key) {
    if (current_ == key) {
        return;
    }
    current_ = key;
    for (auto it = tag_of_.cbegin(); it != tag_of_.cend(); ++it) {
        it.value()->setVisible(it.key() == key);
    }
}

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
    // .novel-center 是 flex column：模式行贴顶、内容区吃掉剩余高度，本层不留白。
    // 留白由各自承载层给（模式行 0 16px = .novel-modes；章节页 16 18 24 = .novel-body），
    // 否则模式行会被内容区的页边距顶出一条不该有的偏移。
    ml->setContentsMargins(0, 0, 0, 0);
    ml->setSpacing(0);

    // 模式按钮行（UI.md §2.1）：本 S 只接 [章节]（现有内容）与 [设定]（设定台 WorldBoardView）；
    // 其余按钮 disabled + tooltip「P04-Sx 接入」。
    // webui .novel-modes .ntab：p8 12 / f13 / w600 / muted，选中 accent + 2px 下划线；
    // 整条 h44 + 底部发丝线（modeRow 的 ntabbar 样式）。
    auto* modeRow = new QWidget(center);
    widgets::SetKind(modeRow, "ntabbar");
    modeRow->setFixedHeight(44);
    auto* mr = new QHBoxLayout(modeRow);
    // webui .novel-modes { padding: 0 16px }：左右各 16px 与正文测量线对齐
    mr->setContentsMargins(theme::space::kSteps[5], 0, theme::space::kSteps[5], 0);
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
    // webui .novel-modes .ntop-right { display:flex; align-items:center; gap:8px;
    //                                   padding-bottom:6px }：
    // 章状态 Tag + 运行态 Tag + 生成钮同属右侧一组，底距 6px 让它们不压住那条发丝线。
    auto* topRight = new QWidget(modeRow);
    auto* trl = new QHBoxLayout(topRight);
    trl->setContentsMargins(0, 0, 0, theme::space::kXs);
    trl->setSpacing(theme::space::kSteps[4]);
    top_status_ = new StatusTagRow({QStringLiteral("idle"), QStringLiteral("draft"),
                                    QStringLiteral("review"), QStringLiteral("done"),
                                    QStringLiteral("failed")},
                                   {QStringLiteral("未写"), QStringLiteral("草稿"), QStringLiteral("待评审"),
                                    QStringLiteral("已提交"), QStringLiteral("失败")},
                                   {QStringLiteral("idle"), QStringLiteral("warn"), QStringLiteral("busy"),
                                    QStringLiteral("ok"), QStringLiteral("danger")},
                                   topRight);
    run_ = new StatusTagRow({QStringLiteral("idle"), QStringLiteral("run")},
                            {QStringLiteral("空闲"), QStringLiteral("运行中")},
                            {QStringLiteral("idle"), QStringLiteral("busy")}, topRight);
    run_->Show(QStringLiteral("idle"));
    trl->addWidget(top_status_);
    trl->addWidget(run_);
    trl->addWidget(genBtn);
    mr->addWidget(topRight, 0, Qt::AlignVCenter);
    ml->addWidget(modeRow);

    centerStack_ = new QStackedWidget(center);
    auto* chapterPage = new QWidget(centerStack_);
    auto* cpl = new QVBoxLayout(chapterPage);
    // webui views.css:561 .novel-body { padding: 16px 18px 24px; overflow-y:auto }
    // 上下左右逐值对齐；段间距不靠 layout spacing，CSS 里每一段都自带 margin
    // （.chap-head mb4 / .chap-summary 12 0 18），所以这里 spacing = 0。
    cpl->setContentsMargins(theme::space::kSteps[5], theme::space::kSteps[5], 18, 24);
    cpl->setSpacing(0);
    auto* headRow = new QWidget(chapterPage);
    auto* hr = new QHBoxLayout(headRow);
    // webui views.css:587 .chap-head { display:flex; align-items:center; gap:10px;
    //                                  margin-bottom:4px }
    hr->setContentsMargins(0, 0, 0, theme::space::kSteps[2]);
    hr->setSpacing(10);
    title_ = new QLabel(QStringLiteral("未选择章节"), headRow);
    QFont tf = title_->font();
    // webui .chap-title { font-size:20px; font-weight:800 }：
    // 走像素档（QSS 字号一律 QFont::setPixelSize，点值会随 DPI 再放大一档）；
    // 800 在 Qt 字体枚举里最高只能到 Bold(700)，这里取其上限。
    tf.setPixelSize(20);
    tf.setWeight(QFont::Bold);
    title_->setFont(tf);
    title_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    hr->addWidget(title_, 0);
    status_ = new StatusTagRow({QStringLiteral("idle"), QStringLiteral("draft"),
                                QStringLiteral("review"), QStringLiteral("done"),
                                QStringLiteral("failed")},
                               {QStringLiteral("未写"), QStringLiteral("草稿"), QStringLiteral("待评审"),
                                QStringLiteral("已提交"), QStringLiteral("失败")},
                               {QStringLiteral("idle"), QStringLiteral("warn"), QStringLiteral("busy"),
                                QStringLiteral("ok"), QStringLiteral("danger")},
                               headRow);
    hr->addWidget(status_, 0, Qt::AlignVCenter);
    // 正文实计字数（webui .chap-head 右侧的 tiny dim 计数）：用数据库里的真实 words，
    // 空正文显示 0，不编造数字。
    meta_ = new QLabel(headRow);
    hr->addStretch(1);
    hr->addWidget(meta_, 0, Qt::AlignVCenter);
    // .chap-summary { max-width:720px; margin:12px 0 18px }：
    // 宽度由摘要自己封顶 + 右侧留白，超宽窗口下摘要仍按 720 阅读宽度排，
    // 与下方 .draft 同一条测量线。
    auto* summaryRow = new QWidget(chapterPage);
    auto* sl = new QHBoxLayout(summaryRow);
    sl->setContentsMargins(0, 12, 0, 18);
    sl->setSpacing(0);
    summary_ = new QLabel(summaryRow);
    summary_->setWordWrap(true);
    summary_->setMaximumWidth(kMeasureMaxW);
    summary_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    // webui .chap-summary：fill-muted 底 + 3px accent 左条（引用块）
    widgets::SetKind(summary_, "chapsummary");
    sl->addWidget(summary_, 0);
    sl->addStretch(1);
    // 正文区（P04-S6 DraftView 接管）：流式逐 token 只追加末段 + 呼吸光 + 中断落盘 + 重试
    draft_ = new DraftView(chapterPage);
    cpl->addWidget(headRow);
    cpl->addWidget(summaryRow);
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
    // 顶栏「运行中 / 空闲」只报真值：发起后轮询 ChapterFlowView 的**内存**态
    // （LiveProbe 只读 live_log_，不做文件或网络 IO），连续两次没有 running 阶段
    // 就落回「空闲」并停表；不常驻轮询，也不臆造「Tn 运行中」里的阶段号。
    run_poll_ = new QTimer(this);
    run_poll_->setInterval(500);
    connect(run_poll_, &QTimer::timeout, this, [this] {
        if (flow_ == nullptr) {
            run_poll_->stop();
            return;
        }
        if (flow_->LiveProbe().contains(QStringLiteral(":running"))) {
            idle_ticks_ = 0;
            run_->Show(QStringLiteral("run"));
            return;
        }
        if (++idle_ticks_ >= 2) {
            run_->Show(QStringLiteral("idle"));
            run_poll_->stop();
        }
    });
    connect(genBtn, &widgets::Button::clicked, this, [this] {
        if (run_poll_ != nullptr) {
            idle_ticks_ = 0;
            run_poll_->start();
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
    // 段距逐条对齐设计稿的 margin（每段自带 margin，CSS 里没有 flex gap），
    // 所以这一层 spacing 归零，段间留白一律用下面的 addSpacing 显式给。
    il->setSpacing(0);
    props_ = new data::KeyValue(inspector);
    il->addWidget(props_);

    // —— 「本章产物」（webui Novel.jsx:469-479）——
    // ⚠️ 设计稿上一段的「预览」占位画（<Art seed>）本页**没实现**：占位画目前只有
    // QML 侧的实现（src/ui/qml/AssetsArt.qml），Widgets 侧无对应件，本页不拿假图充数。
    // 该段在设计稿里只有「占位画 + 一行写死的说明文案」，两者都无真实数据源。
    il->addSpacing(theme::space::kSteps[5]); // 16：段标题 margin-top 16px（Novel.jsx:469 的 '16px 0 8px'）
    auto* artHead = widgets::SectionTitle(QStringLiteral("本章产物"), inspector);
    {
        QFont f = artHead->font();
        // .small = 12.5px（base.css:121），按「就近取整」落 12px 档（Token.h:122-128）
        f.setPixelSize(theme::font::kSizes[0]);
        artHead->setFont(f);
    }
    il->addWidget(artHead);
    il->addSpacing(theme::space::kSteps[3]); // 8：段标题 margin-bottom 8px（同上）
    // .dlist：竖排、块间无 gap（views.css:293-296）
    auto* artList = new QWidget(inspector);
    auto* al = new QVBoxLayout(artList);
    al->setContentsMargins(0, 0, 0, 0);
    al->setSpacing(0);
    for (std::size_t i = 0; i < std::size(kChapterArtifacts); ++i) {
        const ChapterArtifact& a = kChapterArtifacts[i];
        // .drow（views.css:297-306）：p8 2 + gap 8 + 发丝线分隔。底色 / 字号 / 内距
        // 由 kit 的 shineKind="drow"（QssBuilder.cpp:779-782）给出，layout 不再叠一遍
        // 内距（否则 8px 变 16px）。QSS 没有 :last-child，末行由构造方摘掉底边。
        auto* row = new QWidget(artList);
        widgets::SetKind(row, "drow");
        if (i + 1 == std::size(kChapterArtifacts)) {
            row->setProperty("shineKind", QString{}); // views.css:307-309
        }
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(theme::space::kSteps[3]); // gap 8px（views.css:300）
        auto* mark = new QLabel(QString::fromUtf8(a.glyph), row);
        widgets::SetKind(mark, "stateicon");
        {
            QFont f = mark->font();
            f.setPixelSize(13); // 行首 <Icon> 13×13（Novel.jsx:473）
            mark->setFont(f);
        }
        widgets::SetTextColor(mark, theme::Current().accentSecondary); // var(--accent-2)（Novel.jsx:473）
        auto* name = new QLabel(ArtifactFileName(a), row);
        name->setFont(MonoFont(name->font(), theme::font::kSizes[0])); // .grow .mono（Novel.jsx:474）
        name->setMinimumWidth(0); // .grow 的 min-width:0（base.css:107）：窄栏下让行可压缩
        auto* sub = new QLabel(QStringLiteral("%1 · %2").arg(QLatin1String(a.label),
                                                            QLatin1String(a.code)),
                               row);
        widgets::SetKind(sub, "statemeta"); // .dim = --text-muted
        sub->setFont(MonoFont(sub->font(), theme::font::kSizes[0])); // .mono .tiny（Novel.jsx:475）
        // statemeta 的 QSS 字号是 11px（QssBuilder.cpp:574），.tiny 要 12px：按
        // PipelineWorkspace.cpp:504-505 的既有做法只顶字号，颜色仍留给 QSS 的 muted。
        sub->setStyleSheet(QStringLiteral("font-size: 12px;"));
        auto* chev = new QLabel(QStringLiteral("›"), row);
        widgets::SetKind(chev, "stateicon"); // --text-muted（Novel.jsx:476）
        {
            QFont f = chev->font();
            f.setPixelSize(10); // 尾部 <Icon> 10×10（Novel.jsx:476）
            chev->setFont(f);
        }
        rl->addWidget(mark, 0, Qt::AlignVCenter);
        rl->addWidget(name, 1);
        rl->addWidget(sub, 0, Qt::AlignVCenter);
        rl->addWidget(chev, 0, Qt::AlignVCenter);
        al->addWidget(row);
        artifact_rows_.push_back(row);
    }
    il->addWidget(artList);
    // 设计稿 Novel.jsx:480 的尾注「点击产物 → 格式化查看」本页不画：Widgets 侧还没有
    // 产物查看器（设计稿的全局 FileViewer 只存在于 webui），点不动的行不配承诺文案。
    il->addStretch();
    inspector_body_ = inspector;
    RefreshArtifacts();

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
    const QString label = StatusView(n.status, n.body.isEmpty()).first;
    title_->setText(QStringLiteral("第 %1 章 · %2").arg(n.ord).arg(n.title));
    status_->Show(label); // 章头状态标记（文案 = StatusView 的 label，键即 label）
    top_status_->Show(label); // 顶栏同一状态再标一次（设计稿两处都有）
    // 计数只报真值：正文实计字数 + 所属卷；数据库没有的字段（视角 / 修订轮次）不编。
    meta_->setText(QStringLiteral("%1 字（正文实计） · 出自：%2")
                       .arg(n.words)
                       .arg(n.volumeTitle.isEmpty() ? QStringLiteral("—") : n.volumeTitle));
    widgets::SetTextColor(meta_, theme::Current().textMuted);
    summary_->setText(n.summary.isEmpty() ? QStringLiteral("（暂无摘要）") : n.summary);
    props_->SetPairs({{QStringLiteral("书"), n.book},
                      {QStringLiteral("卷"), n.volumeTitle},
                      {QStringLiteral("章序"), QString::number(n.ord)},
                      {QStringLiteral("状态"), label},
                      {QStringLiteral("字数"), QString::number(n.words)},
                      {QStringLiteral("行 id"), QString::number(n.id)}});
    RefreshArtifacts();
    // 只同步**当前可见页**（S1 判据：千章 100 次选章总耗时有上限）。
    // 不可见页留到 [模式] 被激活时再按需同步（见 SyncPage）——避免每次选章都
    // 重开 SQLite / 读阶段产物 / 跑 K01–K29 全表。
    SyncPage(centerStack_->currentIndex());
}

// 「本章产物」三行的 tooltip：给当前章的**真实**产物路径（webui 的 title 是「查看 <文件名>」，
// 这里补上章号与目录，路径一律 PathToUtf8，别用 path.string()）。只刷 tooltip，不做
// 存在性探测 —— 那要同步读盘，UI 线程不干同步 IO（AGENTS.md「UI 线程不做同步 IO」）。
void NovelWorkspace::RefreshArtifacts() {
    if (artifact_rows_.size() != std::size(kChapterArtifacts)) {
        return;
    }
    int ord = 0;
    std::filesystem::path work;
    if (current_ >= 0 && current_ < static_cast<int>(chapters_.size())) {
        const ChapterNode& n = chapters_[static_cast<std::size_t>(current_)];
        ord = n.ord;
        for (const BookNode& b : books_) {
            if (b.title == n.book) {
                work = b.workDir; // BookRef::workDir = <书根>/work（project/Project.h:103）
                break;
            }
        }
    }
    const std::filesystem::path dir =
        ord > 0 ? work / std::filesystem::path(
                        QStringLiteral("ch%1").arg(ord, 3, 10, QLatin1Char('0')).toStdString())
                : std::filesystem::path{};
    for (std::size_t i = 0; i < artifact_rows_.size(); ++i) {
        const ChapterArtifact& a = kChapterArtifacts[i];
        QString tip;
        if (ord <= 0) {
            tip = QStringLiteral("先选一章，这里才有本章的产物");
        } else if (a.stage[0] == '\0') {
            // 库内字段：P4 正文落盘前不写 work/，也没有对应的库表路径
            tip = QStringLiteral("第 %1 章 · chapters.body（库内字段，不落 work/）").arg(ord);
        } else {
            tip = QString::fromStdString(util::PathToUtf8(
                dir / std::filesystem::path{ArtifactFileName(a).toStdString()}));
        }
        artifact_rows_[i]->setToolTip(tip);
    }
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
