// P04-S9 `StateDiffView` 实现：StateDiff 展示 + 门禁 G1–G5 逐条 + 唯一 COMMIT 入口
// （`novelcore::CommitChapterState`，**不调 LLM**）+ 回滚到 `snapshots/ch<NNN>.json`。
// 颜色零内联：门禁红绿/文字一律 theme token 派生。
#include "ui/pages/novel/StateDiffView.h"
#include "ui/kit/theme/CssColor.h"

#include "db/sqlite/SqliteDb.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/Surfaces.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "ui/layout/QtLayout.h"
#include "novel/NovelChecks.h"
#include "novel/NovelCommit.h"
#include "novel/NovelGraph.h"
#include "novel/NovelStageLedger.h"
#include "novel/NovelTypes.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Json.h"

#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>
#include <utility>

namespace {

using namespace shine::novelcore;

// CSS 色串：token 色 + 运行期 alpha（QSS 没有 color-mix，pass / fail 的描边
// 与 fail 底色都靠「状态色按设计稿比例降 alpha / 与底色混合」等效实现）。
[[nodiscard]] QString CssOf(const QColor& c) {
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(QString::number(static_cast<double>(c.alpha()) / 255.0, 'f', 3));
}

// webui `.gcode` / `.r-score` 一类的等宽字（--font-mono）。kit 未提供字体工厂，
// 这里按 theme::font::kMonoFamily 的族序逐档回退，页内各处共用这一份。
[[nodiscard]] QFont MonoFont(const QFont& base, int pixelSize, QFont::Weight weight = QFont::Normal) {
    QFont f = base;
    f.setFamilies({QString::fromLatin1("Cascadia Code"), QString::fromLatin1("JetBrains Mono"),
                    QString::fromLatin1("Consolas"), QString::fromLatin1("monospace")});
    f.setPixelSize(pixelSize);
    f.setWeight(weight);
    return f;
}

// webui views.css:658 `.gates .gate.pass { border-color: ok 25% }` /
// :660 `.gates .gate.fail { border-color: danger 35%; background: danger 7% + fill-muted }`
// pass = nullopt = 还没跑判定：留在中性底（line-subtle 描边），不预判红绿。
void PaintGateRow(QFrame* row, std::optional<bool> pass) {
    const shine::theme::ColorToken& t = shine::theme::Current();
    QColor bg = shine::widgets::TokenQColor(t.fillMuted);
    QColor edge = shine::widgets::TokenQColor(t.lineSubtle);
    if (pass.value_or(false)) {
        edge = shine::widgets::TokenQColor(t.statusOk);
        edge.setAlphaF(0.25);
    } else if (pass.has_value()) {
        const QColor danger = shine::widgets::TokenQColor(t.statusDanger);
        edge = danger;
        edge.setAlphaF(0.35);
        constexpr double kFailTint = 0.07; // color-mix(danger 7%, fill-muted)
        bg = QColor::fromRgbF(bg.redF() * (1.0 - kFailTint) + danger.redF() * kFailTint,
                              bg.greenF() * (1.0 - kFailTint) + danger.greenF() * kFailTint,
                              bg.blueF() * (1.0 - kFailTint) + danger.blueF() * kFailTint);
    }
    row->setStyleSheet(QStringLiteral("QFrame { background-color: %1; border: 1px solid %2;"
                                     " border-radius: %3px; }")
                           .arg(CssOf(bg), CssOf(edge))
                           .arg(shine::theme::radius::kSm));
}

} // namespace

namespace shine::app {

std::vector<GateLine> EvalGates(db::sqlite::Database& db, const StateDiff& diff, bool reviewPass,
                                const QString& reviewVerdict, const ValidationReport& checks,
                                bool snapshotExists, const std::string& snapshotPath,
                                const std::string& projectDir) {
    std::vector<GateLine> g;
    // G1：评审 PASS（`06` §2.4 verdict；**不重跑模型**）
    g.push_back({"G1", "评审结论 PASS", reviewPass,
                 reviewPass ? QStringLiteral("评审 %1（调用方给，本视图不调 LLM）").arg(reviewVerdict)
                            : QStringLiteral("评审未通过（%1）—— 先在[评审]页修到 PASS")
                                   .arg(reviewVerdict)});
    // G2：K01–K29 无阻断（low 可记但放行；`CheckPassed` 单一判定器）
    //   ⚠️ **K13 豁免**：K13 的受检对象是「COMMIT 前的章级快照」，而快照恰恰由本次提交
    //   事务在 G2 之前写下（`07` §2.2 ③ 的顺序：先快照 → 再跑 K，`g2_source="inline"`）。
    //   预检阶段快照必然不存在 ⇒ K13 此时红是**预期态**，拿它挡门就是先鸡先蛋。
    //   权威 G2 由 `CommitChapterState` 在快照落盘后自己跑的那份报告给。
    {
        int fail = 0;
        QStringList failedIds;
        for (const CheckResult& r : checks.checks) {
            if (r.check_id == "K13") {
                continue; // 预检豁免（见上）
            }
            if (!CheckPassed(r)) {
                ++fail;
                failedIds << QString::fromStdString(r.check_id);
            }
        }
        g.push_back({"G2", "K01–K29 无阻断", fail == 0,
                     fail == 0 ? QStringLiteral("K 全过（含 NotApplicable 放行；K13 快照前态"
                                                "由提交事务内 inline 校验负责）")
                               : QStringLiteral("%1 条未过：%2")
                                     .arg(fail)
                                     .arg(failedIds.join(QStringLiteral("、")))});
    }
    // G3：章级快照就绪（`07` §2.4 目录能写；不变式 I11）
    g.push_back({"G3", "章级快照就绪（I11）", snapshotExists,
                 snapshotExists
                     ? QStringLiteral("快照目录就绪：%1")
                           .arg(QString::fromStdString(snapshotPath.empty() ? "?" : snapshotPath))
                     : QStringLiteral("没有快照目录 —— 提交会写 snapshots/ch<NNN>.json，"
                                      "但工作区不可写就无法回滚（I11）")});
    // G4：StateDiff 契约有效且非空（或显式声明无变化）
    {
        const std::vector<CommitIssue> issues = ValidateStateDiff(db, diff);
        const bool valid = issues.empty() && (diff.HasAnyDelta() || diff.no_change_declared);
        QString detail;
        if (!issues.empty()) {
            detail = QStringLiteral("契约问题 %1 条：%2")
                         .arg(issues.size())
                         .arg(QString::fromStdString(issues.front().code + " " + issues.front().detail));
        } else if (!diff.HasAnyDelta() && !diff.no_change_declared) {
            detail = QStringLiteral("既没有差分也没显式声明无变化（I10/D7）");
        } else {
            detail = QStringLiteral("契约通过：%1").arg(QString::fromStdString(diff.Summary()));
        }
        g.push_back({"G4", "StateDiff 契约有效", valid, detail});
    }
    // G5：无未解决的 high issue
    {
        const std::vector<CommitIssue> issues = ValidateStateDiff(db, diff);
        int high = 0;
        QString firstHigh;
        for (const CommitIssue& i : issues) {
            if (i.severity == "high") {
                ++high;
                if (firstHigh.isEmpty()) {
                    firstHigh = QString::fromStdString(i.detail);
                }
            }
        }
        g.push_back({"G5", "无未解决 high issue", high == 0,
                     high == 0 ? QStringLiteral("无 high issue")
                               : QStringLiteral("%1 条 high（首条：%2）").arg(high).arg(firstHigh)});
    }
    (void)projectDir;
    return g;
}

StateDiffView::StateDiffView(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    // 页面级留白 / 分区间距统一走 layout helper（对齐 webui .vw：20 24 26 / gap 16）
    util::PageMargins(outer);
    util::PageSpacing(outer);

    // —— 头部：总判定 + 提交/回滚 ——
    auto* head = new QWidget(this);
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(theme::space::kSteps[1]);
    verdict_ = new QLabel(QStringLiteral("门禁: —"), head);
    {
        QFont f = verdict_->font();
        f.setBold(true);
        verdict_->setFont(f);
    }
    widgets::SetTextColor(verdict_, theme::Current().textSecondary);
    commit_btn_ = new widgets::Button(QStringLiteral("提交状态（唯一入口 · 不调 LLM）"),
                                      widgets::Button::Variant::Primary,
                                      widgets::Button::Size::Sm, head);
    commit_btn_->setToolTip(QStringLiteral("`CommitChapterState` 14 块事务 + 幂等 + 快照（I11）"));
    commit_btn_->setEnabled(false);
    connect(commit_btn_, &widgets::Button::clicked, this, [this] { Commit(); });

    rollback_btn_ = new widgets::Button(QStringLiteral("回滚到本章快照"),
                                        widgets::Button::Variant::Danger,
                                        widgets::Button::Size::Sm, head);
    rollback_btn_->setToolTip(QStringLiteral("按 snapshots/ch<NNN>.json 的 Before 值回滚实体（破坏性）"));
    rollback_btn_->setEnabled(false);
    connect(rollback_btn_, &widgets::Button::clicked, this, [this] { Rollback(); });
    hint_ = new widgets::ElidedLabel(QString{}, head);
    widgets::SetKind(hint_, "fieldhelp");
    hl->addWidget(verdict_);
    hl->addWidget(commit_btn_);
    hl->addWidget(rollback_btn_);
    hl->addWidget(hint_, 1);
    outer->addWidget(head);

    // —— 单列滚动 + 两张 SectionCard ——
    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* column = new QWidget(scroll);
    auto* col = new QVBoxLayout(column);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(theme::space::kSteps[3]);

    // —— 门禁 G1–G5 逐条 ——
    auto* gate_card = new widgets::SectionCard(QStringLiteral("提交门禁 G1–G5"), this);
    gate_card->SetSubtitle(QStringLiteral("唯一 COMMIT 入口的前置"));
    gate_card->SetCollapsible(true);
    gate_card->SetContentMinWidth(680);
    QVBoxLayout* gl = gate_card->BodyLayout();
    // webui views.css:634 `.gates { display:flex; flex-direction:column; gap:5px }`
    gl->setSpacing(5);
    struct GateDef {
        const char* gate;
        const char* name;
    };
    static constexpr GateDef kGates[] = {
        {"G1", "评审结论 PASS"}, {"G2", "K01–K29 无阻断"}, {"G3", "章级快照就绪（I11）"},
        {"G4", "StateDiff 契约有效"}, {"G5", "无未解决 high issue"}};
    for (const GateDef& d : kGates) {
        GateRowUi ui;
        // webui views.css:639 `.gate { display:flex; align-items:center; gap:9px;
        //                               padding:6px 10px; border-radius:var(--r-sm);
        //                               font-size:12.5px; color:var(--text-secondary);
        //                               background:var(--fill-muted);
        //                               border:1px solid var(--line-subtle) }`
        ui.row = new QFrame(gate_card);
        ui.row->setFrameShape(QFrame::NoFrame); // 底板完全由下面的 token 样式给出
        auto* rl = new QHBoxLayout(ui.row);
        rl->setContentsMargins(10, 6, 10, 6);
        rl->setSpacing(9);
        ui.mark = new QLabel(QStringLiteral("—"), ui.row);
        ui.mark->setFixedWidth(12);
        ui.mark->setAlignment(Qt::AlignCenter);
        ui.code = new QLabel(QString::fromLatin1(d.gate), ui.row);
        // `.gate .gcode { font-family:var(--font-mono); font-size:11px; font-weight:700;
        //                  color:var(--text-muted); width:34px; flex:none }`
        ui.code->setFixedWidth(34);
        ui.code->setFont(MonoFont(ui.code->font(), 11, QFont::Bold));
        auto* name = new QLabel(QString::fromUtf8(d.name), ui.row);
        {
            QFont f = ui.row->font();
            f.setPixelSize(12); // 12.5px 按「就近取整」落 12px 档
            name->setFont(f);
        }
        widgets::SetTextColor(name, theme::Current().textSecondary);
        name->setMinimumWidth(200);
        ui.detail = new widgets::ElidedLabel(QString{}, ui.row);
        widgets::SetKind(ui.detail, "fieldhelp");
        rl->addWidget(ui.mark);
        rl->addWidget(ui.code);
        rl->addWidget(name);
        rl->addWidget(ui.detail, 1);
        PaintGateRow(ui.row, std::nullopt); // 未判定 = 中性描边
        gl->addWidget(ui.row);
        gate_widgets_.push_back(ui);
    }
    col->addWidget(gate_card);

    // —— StateDiff 展示（原文 + 摘要）——
    auto* diff_card = new widgets::SectionCard(QStringLiteral("StateDiff"), this);
    diff_card->SetSubtitle(QStringLiteral("`work/chNNN/12_state_diff.json`"));
    diff_card->SetCollapsible(true);
    diff_card->SetContentMinWidth(680);
    diff_view_ = new QPlainTextEdit(diff_card);
    diff_view_->setReadOnly(true);
    diff_view_->setMinimumHeight(280);
    diff_view_->setPlaceholderText(
        QStringLiteral("StateDiff（`work/chNNN/12_state_diff.json`）—— 跑完 T14 提取后在此查看；"
                       "提交走上方唯一入口（本视图不调 LLM）"));
    diff_card->BodyLayout()->addWidget(diff_view_);
    col->addWidget(diff_card);

    col->addStretch(1);
    scroll->setWidget(column);
    outer->addWidget(scroll, 1);
}

StateDiffView::~StateDiffView() {
    CloseDb();
}

void StateDiffView::SetProjectDir(const std::filesystem::path& dir) {
    project_dir_ = dir;
}

bool StateDiffView::OpenBook(const std::filesystem::path& dbPath, QString* err) {
    if (db_ && db_path_ == dbPath) {
        return true;
    }
    CloseDb();
    db_path_ = dbPath;
    db_ = std::make_unique<db::sqlite::Database>();
    if (auto r = db_->Open({.path = dbPath}); !r) {
        if (err != nullptr) {
            *err = QString::fromStdString(r.error().message);
        }
        db_.reset();
        db_path_.clear();
        return false;
    }
    return true;
}

void StateDiffView::CloseDb() noexcept {
    db_.reset();
}

std::filesystem::path StateDiffView::SnapshotPath() const {
    if (project_dir_.empty() || chapter_id_ <= 0) {
        return {};
    }
    return project_dir_ / "snapshots" /
           (QStringLiteral("ch%1.json").arg(chapter_id_, 3, 10, QLatin1Char('0')).toStdString());
}

bool StateDiffView::ReadDiff() {
    diff_.reset();
    if (chapter_ord_ <= 0 || project_dir_.empty()) {
        return false;
    }
    auto art = ReadStageArtifact(project_dir_, chapter_ord_, "STATE_EXTRACT");
    if (!art) {
        return false;
    }
    auto d = std::make_unique<StateDiff>();
    if (!StateDiffFromJson(art->payload, *d)) {
        return false;
    }
    diff_ = std::move(d);
    return true;
}

void StateDiffView::RunChecks() {
    checks_ = ValidationReport{};
    if (!db_ || chapter_id_ <= 0) {
        return;
    }
    CheckInputs in;
    in.chapter_id = static_cast<RowId>(chapter_id_);
    in.chapter_ord = chapter_ord_;
    in.project_dir = util::PathToUtf8(project_dir_);
    in.snapshot_dir = util::PathToUtf8(project_dir_ / "snapshots");
    in.word_target = 3000;
    if (diff_) {
        in.diff = diff_.get(); // K01/K11/K17 有内存 diff 就不看盘
    }
    checks_ = RunChapterChecks(*db_, in);
}

void StateDiffView::SelectChapter(qint64 chapterId, int chapterOrd) {
    chapter_id_ = chapterId;
    chapter_ord_ = chapterOrd;
    last_commit_.clear();
    last_rollback_.clear();
    committed_now_ = false;
    skipped_idempotent_ = false;
    if (chapter_ord_ <= 0 && db_ && chapter_id_ > 0) {
        NovelGraph g(*db_);
        if (auto ch = g.GetChapter(static_cast<RowId>(chapter_id_)); ch) {
            chapter_ord_ = ch->ord;
        }
    }
    Refresh();
}

void StateDiffView::SetReviewVerdict(bool pass, const QString& verdictText) {
    review_pass_ = pass;
    review_verdict_ = verdictText;
    Refresh();
}

void StateDiffView::Refresh() {
    const bool gotDiff = ReadDiff();
    RunChecks();
    snapshot_exists_ = !project_dir_.empty() && chapter_id_ > 0 &&
                       std::filesystem::exists(SnapshotPath());
    snapshot_path_ = util::PathToUtf8(SnapshotPath());

    // —— UI：diff 原文 + 摘要 ——
    if (gotDiff) {
        diff_view_->setPlainText(QString::fromStdString(StateDiffToJson(*diff_)));
        diff_summary_ = QString::fromStdString(diff_->Summary());
    } else {
        diff_view_->clear();
        diff_summary_ = QStringLiteral("（本章还没有 StateDiff 产物）");
    }

    // —— 门禁 ——
    if (db_ && diff_) {
        gates_ = EvalGates(*db_, *diff_, review_pass_, review_verdict_, checks_, !project_dir_.empty(),
                           snapshot_path_, util::PathToUtf8(project_dir_));
    } else {
        gates_.clear();
    }
    bool allOk = !gates_.empty() && std::all_of(gates_.begin(), gates_.end(),
                                                [](const GateLine& g) { return g.pass; });
    const int passCount = static_cast<int>(
        std::count_if(gates_.begin(), gates_.end(), [](const GateLine& g) { return g.pass; }));
    verdict_->setText(gates_.empty()
                          ? QStringLiteral("门禁: —")
                          : QStringLiteral("门禁: %1（%2/%3 过）")
                                .arg(allOk ? QStringLiteral("可提交") : QStringLiteral("未过"))
                                .arg(passCount)
                                .arg(static_cast<int>(gates_.size())));
    widgets::SetTextColor(verdict_, gates_.empty() ? theme::Current().textSecondary
                                      : (allOk ? theme::Current().statusOk
                                               : theme::Current().statusDanger));
    for (std::size_t i = 0; i < gates_.size() && i < gate_widgets_.size(); ++i) {
        const GateLine& g = gates_[i];
        GateRowUi& ui = gate_widgets_[i];
        ui.mark->setText(g.pass ? QStringLiteral("✔") : QStringLiteral("✘"));
        widgets::SetTextColor(ui.mark,
                              g.pass ? theme::Current().statusOk : theme::Current().statusDanger);
        // `.gcode`：默认 text-muted；pass → status-ok；fail → status-danger
        widgets::SetTextColor(ui.code,
                              g.pass ? theme::Current().statusOk
                                     : (theme::Current().statusDanger));
        ui.detail->SetFullText(g.detail);
        ui.row->setToolTip(QStringLiteral("%1 %2 —— %3")
                               .arg(QString::fromLatin1(g.gate), QString::fromUtf8(g.name))
                               .arg(g.detail.isEmpty() ? (g.pass ? QStringLiteral("通过")
                                                                  : QStringLiteral("未过"))
                                                       : g.detail));
        PaintGateRow(ui.row, g.pass); // pass / fail 换整行描边与底色
    }
    commit_btn_->setEnabled(allOk && !running_commit_ && diff_ != nullptr);
    rollback_btn_->setEnabled(snapshot_exists_ && !running_commit_);
    SetHint(gates_.empty()
                ? QStringLiteral("选一章后即时预检 G1–G5（只读，不落库）")
                : (allOk ? QStringLiteral("G1–G5 全过 —— 可[提交状态]（不调 LLM）")
                         : QStringLiteral("门禁未过 —— 点[提交状态]会被拒；先按红条处理")),
            theme::Current().textSecondary);
}

void StateDiffView::SetHint(const QString& text, std::uint32_t token) {
    hint_->SetFullText(text);
    widgets::SetTextColor(hint_, token);
}

bool StateDiffView::Commit() {
    if (running_commit_ || !db_ || !diff_) {
        return false;
    }
    if (gates_.empty() || !std::all_of(gates_.begin(), gates_.end(),
                                       [](const GateLine& g) { return g.pass; })) {
        SetHint(QStringLiteral("拒绝提交：G1–G5 未全过"), theme::Current().statusDanger);
        return false;
    }
    running_commit_ = true;
    commit_btn_->setEnabled(false);
    CommitContext ctx;
    ctx.chapter_id = static_cast<RowId>(chapter_id_);
    ctx.review_pass = review_pass_;
    ctx.review_verdict = review_verdict_.toStdString();
    // **不传 ctx.validation**（`07` §2.2 ③）：预检报告是提交**前**的快照，K13 在那一刻必然红
    // （章级快照正是本次事务要写的东西）。契约的权威口径是「事务内先写快照 → 再自己跑
    // RunChapterChecks（`g2_source="inline"`）」，这样任何入口都是同一套判据、不留后门。
    ctx.validation = nullptr;
    ctx.project_dir = util::PathToUtf8(project_dir_);
    ctx.snapshot_dir = util::PathToUtf8(project_dir_ / "snapshots");
    ctx.canon_mode = "manual"; // `07` §2.5 C5：auto 提交不在本视图
    if (db_) {
        NovelGraph g(*db_);
        if (auto ch = g.GetChapter(static_cast<RowId>(chapter_id_)); ch) {
            ctx.chapter_body = ch->body;
            ctx.chapter_summary = ch->summary;
            ctx.chapter_words = ch->words;
            ctx.chapter_pov_entity_id = ch->pov_entity_id;
        }
    }
    const CommitResult res = CommitChapterState(*db_, *diff_, ctx);
    running_commit_ = false;
    applied_blocks_ = static_cast<int>(res.applied.size());
    g2_source_ = QString::fromStdString(res.gates.g2_source); // inline = 事务内权威校验
    committed_now_ = res.ok && !res.skipped;
    skipped_idempotent_ = res.skipped;
    last_commit_ = res.ok
                       ? QStringLiteral("提交成功：%1 块 · 快照 %2%3")
                             .arg(res.applied.size())
                             .arg(QString::fromStdString(res.snapshot_path))
                             .arg(res.skipped ? QStringLiteral("（幂等命中，未重写）") : QString())
                       : QStringLiteral("提交被拒：%1（门禁 %2）")
                             .arg(QString::fromStdString(res.error),
                                  QString::fromStdString(res.gates.Describe()));
    SetHint(last_commit_, res.ok ? theme::Current().statusOk : theme::Current().statusDanger);
    Refresh();
    return res.ok;
}

bool StateDiffView::Rollback() {
    if (running_commit_ || !db_ || chapter_id_ <= 0) {
        return false;
    }
    const std::string path = util::PathToUtf8(SnapshotPath());
    auto bytes = util::ReadFileBytes(SnapshotPath());
    if (!bytes) {
        last_rollback_ = QStringLiteral("回滚失败：没有本章快照（%1）")
                             .arg(QString::fromStdString(path));
        SetHint(last_rollback_, theme::Current().statusDanger);
        return false;
    }
    const auto doc = util::json::ParseDoc(*bytes);
    if (!doc) {
        last_rollback_ = QStringLiteral("回滚失败：快照 JSON 解析不了");
        SetHint(last_rollback_, theme::Current().statusDanger);
        return false;
    }
    // 回滚定位（`07` §2.4）：快照内嵌 `diff` → 涉及实体 → 该章提交时采的 Before 版本行。
    // ⚠️ 章级快照里的 `diff` 是 **JSON 对象**（不是字符串）；直接 GetStrCopy 会得到空串。
    const yyjson_val* diffNode = util::json::GetObj(doc.root(), "diff");
    std::string diffRaw;
    if (diffNode != nullptr) {
        std::size_t diffLen = 0;
        char* diffText = yyjson_val_write(const_cast<yyjson_val*>(diffNode), 0, &diffLen);
        if (diffText != nullptr) {
            diffRaw.assign(diffText, diffLen);
            std::free(diffText);
        }
    } else {
        // 兼容旧快照：早期探针曾把 diff 序列化为字符串。
        diffRaw = util::json::GetStrCopy(doc.root(), "diff");
    }
    StateDiff snapDiff;
    if (diffRaw.empty() || !StateDiffFromJson(diffRaw, snapDiff)) {
        last_rollback_ = QStringLiteral("回滚失败：快照里的 diff 解析不了");
        SetHint(last_rollback_, theme::Current().statusDanger);
        return false;
    }
    std::vector<RowId> entities;
    const auto push = [&entities](RowId id) {
        if (id > 0 && std::find(entities.begin(), entities.end(), id) == entities.end()) {
            entities.push_back(id);
        }
    };
    for (const CharacterDelta& c : snapDiff.characters) {
        push(c.entity_id);
    }
    for (const RelationDelta& r : snapDiff.relationships) {
        push(r.from_id);
        push(r.to_id);
    }
    for (const ItemDelta& i : snapDiff.items) {
        push(i.item_id);
    }
    for (const ForeshadowDelta& f : snapDiff.foreshadows) {
        push(f.foreshadow_id); // 0 = 新建伏笔（非既有实体，跳过）
    }
    for (const EventDelta& e : snapDiff.events) {
        for (const EventParticipantDelta& p : e.participants) {
            push(p.entity_id);
        }
    }
    running_commit_ = true;
    int restored = 0;
    QStringList failures;
    {
        NovelGraph g(*db_);
        const std::string wantNote =
            QStringLiteral("ch%1 commit (before)").arg(chapter_id_).toStdString();
        for (const RowId entityId : entities) {
            // 定位本章节提交时采的那一版 Before 行（注释匹配；否则取最新版本）
            RowId versionId = 0;
            if (auto versions = g.ListEntityVersions(entityId, 50); versions) {
                for (const EntityVersionRow& row : *versions) {
                    if (row.note == wantNote) {
                        versionId = row.id;
                        break;
                    }
                }
                if (versionId == 0 && !versions->empty()) {
                    versionId = versions->front().id;
                }
            }
            if (versionId == 0) {
                failures << QStringLiteral("实体 %1 没有可回滚的版本行").arg(entityId);
                continue;
            }
            auto snap = g.LoadEntitySnapshot(versionId);
            if (!snap) {
                failures << QStringLiteral("版本 %1 读不出").arg(versionId);
                continue;
            }
            const bool okEntity = static_cast<bool>(g.UpsertEntity(snap->entity));
            (void)g.UpsertPersona(snap->persona);
            (void)g.UpsertCharacterStatus(snap->status);
            if (okEntity) {
                ++restored;
            } else {
                failures << QStringLiteral("实体 %1 写回失败").arg(snap->entity.id);
            }
        }
    }
    running_commit_ = false;
    applied_blocks_ = restored;
    last_rollback_ = failures.isEmpty()
                         ? QStringLiteral("回滚完成：按快照恢复 %1 个实体的 Before 值（%2）")
                               .arg(restored)
                               .arg(QString::fromStdString(path))
                         : QStringLiteral("回滚部分失败：成功 %1 / 失败 %2（%3）")
                               .arg(restored)
                               .arg(failures.size())
                               .arg(failures.join(QStringLiteral("；")));
    SetHint(last_rollback_,
            failures.isEmpty() ? theme::Current().statusOk : theme::Current().statusDanger);
    Refresh();
    return failures.isEmpty();
}

// ————————————————————————————————————————————— 探针

QString StateDiffView::GateProbe() const {
    QString out = QStringLiteral("gates=%1\n").arg(gates_.size());
    for (const GateLine& g : gates_) {
        out += QStringLiteral("gate %1|%2|pass=%3|%4\n")
                   .arg(QString::fromLatin1(g.gate), QString::fromUtf8(g.name))
                   .arg(g.pass ? 1 : 0)
                   .arg(g.detail);
    }
    return out;
}

QString StateDiffView::DiffProbe() const {
    if (!diff_) {
        return QStringLiteral("diff none\n");
    }
    return QStringLiteral("diff chapter=%1 deltas=%2 hash=%3 summary=%4\n")
        .arg(diff_->chapter_id)
        .arg(diff_->DeltaCount())
        .arg(QString::fromStdString(diff_->Hash()))
        .arg(QString::fromStdString(diff_->Summary()));
}

QString StateDiffView::CommitProbe() const {
    return QStringLiteral("commit last=%1|now=%2|skipped=%3|g2-source=%4|snapshot=%5|blocks=%6|rollback=%7\n")
        .arg(last_commit_)
        .arg(committed_now_ ? 1 : 0)
        .arg(skipped_idempotent_ ? 1 : 0)
        .arg(g2_source_)
        .arg(QString::fromStdString(snapshot_path_))
        .arg(applied_blocks_)
        .arg(last_rollback_);
}

} // namespace shine::app
