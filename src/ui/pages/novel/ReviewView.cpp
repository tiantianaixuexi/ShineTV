// P04-S7 `ReviewView` 实现：rubric 8 维（`06` §2.4 阈值+总判定首次落地）+ K01–K29
// 机器校验清单 + 修复轮入口（≤3 轮，走同一 GenerateChapter 真实管线）。
// 颜色零内联：条形/刻度/文字一律 theme token 派生。
#include "ui/pages/novel/ReviewView.h"
#include "ui/kit/theme/CssColor.h"

#include "core/Async.h"
#include "db/sqlite/SqliteDb.h"
#include "ui/kit/data/Table.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/layout/QtLayout.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/layout/QtLayout.h"
#include "ui/kit/controls/Inputs.h"
#include "ui/layout/QtLayout.h"
#include "ui/kit/controls/Surfaces.h"
#include "ui/layout/QtLayout.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "ui/layout/QtLayout.h"
#include "novel/NovelGraph.h"
#include "novel/NovelStageLedger.h"
#include "novel/NovelTypes.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Json.h"
#include "util/Time.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace {

using shine::novelcore::CheckOutcome;



[[nodiscard]] QString OutcomeText(CheckOutcome o) {
    using namespace shine::novelcore;
    const std::string_view n = CheckOutcomeName(o);
    return QString::fromUtf8(n.data(), static_cast<int>(n.size()));
}

} // namespace

namespace shine::app {

// —— rubric 8 维（`06` §2.4；world=80 是本 rubric 的硬阈值）——
const std::vector<RubricDim>& RubricDims() {
    static const std::vector<RubricDim> dims = {
        {"plot", "剧情目标", 60},   {"character", "人物一致", 60},
        {"causality", "因果", 60},  {"world", "世界观", 80},
        {"timeline", "时间线", 70}, {"foreshadow", "伏笔", 60},
        {"pacing", "节奏", 50},     {"style", "文风", 50},
    };
    return dims;
}

std::vector<RubricScore> ParseRubric(std::string_view criticJson) {
    // 格式①：`{"rubric":{"plot":72,…},"passed":…,"issues":[…]}`（新契约）
    // 格式②：维度直接平铺在根（`{"plot":72,…}`）
    std::vector<RubricScore> out;
    const auto doc = util::json::ParseDoc(criticJson);
    if (!doc) {
        return out;
    }
    yyjson_val* root = doc.root();
    yyjson_val* obj = util::json::GetObj(root, "rubric");
    if (obj == nullptr) {
        obj = root;
    }
    for (const RubricDim& d : RubricDims()) {
        RubricScore s;
        s.key = d.key;
        s.name = QString::fromUtf8(d.name);
        s.threshold = d.threshold;
        s.score = util::json::GetInt(obj, d.key, 0); // 缺维按 0 = FAIL（不伪造通过）
        s.below = s.score < s.threshold;
        out.push_back(std::move(s));
    }
    return out;
}

bool RubricVerdictFail(const std::vector<RubricScore>& scores, std::string_view criticJson,
                       QString* why) {
    QStringList below;
    for (const RubricScore& s : scores) {
        if (s.below) {
            below << QStringLiteral("%1 %2/%3").arg(s.name).arg(s.score).arg(s.threshold);
        }
    }
    // high issue → FAIL（`06` §2.4「任一 high issue 即 FAIL」）
    bool hasHigh = false;
    const auto doc = util::json::ParseDoc(criticJson);
    if (doc) {
        if (yyjson_val* issues = util::json::GetArr(doc.root(), "issues"); issues != nullptr) {
            for (std::size_t i = 0; i < yyjson_arr_size(issues); ++i) {
                if (util::json::GetStr(yyjson_arr_get(issues, i), "severity") == "high") {
                    hasHigh = true;
                    break;
                }
            }
        }
    }
    const bool fail = !below.isEmpty() || hasHigh;
    if (why != nullptr) {
        QStringList all;
        if (!below.isEmpty()) {
            all << QStringLiteral("低于阈值：%1").arg(below.join(QStringLiteral("、")));
        }
        if (hasHigh) {
            all << QStringLiteral("存在 high issue");
        }
        *why = fail ? all.join(QStringLiteral("；"))
                    : QStringLiteral("8 维均达阈值且无 high issue（`06` §2.4）");
    }
    return fail;
}

const char* ReviewVerdictText(ReviewVerdict v) {
    switch (v) {
        case ReviewVerdict::Pass:
            return "通过";
        case ReviewVerdict::Conditional:
            return "有条件通过";
        case ReviewVerdict::Fail:
            break;
    }
    return "不通过";
}

namespace {

// widgets::Tag 只暴露只读 Text()，改文案 / 色调走「内部 QLabel + 动态属性 +
// repolish」，与 kit 里 Chip 的 tone 用法同构，不动 kit 本身
// （同 src/ui/pages/storyboard/StoryboardWorkspace.cpp 的 SetTagText）。
void SetTagText(widgets::Tag* tag, const QString& text, const char* tone) {
    if (tag == nullptr) {
        return;
    }
    if (auto* label = tag->findChild<QLabel*>(); label != nullptr) {
        label->setText(text);
    }
    tag->setProperty("tone", QString::fromLatin1(tone));
    widgets::Repolish(tag);
}

// 遗留 issue 的最高级别（空串 = 无 issue）。
// 字段名两处都读：设计稿 mock.js:527-529 用 `level`，真实 critic 产物与
// P04ReviewChecks 的用例用 `severity`（`06` §2.4）。取三档里最高的一档。
[[nodiscard]] QString MaxIssueLevel(std::string_view criticJson) {
    constexpr const char* kRanks[3] = {"low", "medium", "high"}; // 升序
    const auto doc = util::json::ParseDoc(criticJson);
    yyjson_val* issues = doc ? util::json::GetArr(doc.root(), "issues") : nullptr;
    if (issues == nullptr) {
        return {};
    }
    int best = -1;
    for (std::size_t i = 0; i < yyjson_arr_size(issues); ++i) {
        yyjson_val* item = yyjson_arr_get(issues, i);
        std::string_view sev = util::json::GetStr(item, "severity");
        if (sev.empty()) {
            sev = util::json::GetStr(item, "level");
        }
        for (int r = 0; r < 3; ++r) {
            if (sev == kRanks[r]) {
                best = std::max(best, r);
            }
        }
    }
    return best < 0 ? QString{} : QString::fromLatin1(kRanks[best]);
}

// rubric 8 维的算术平均 = 汇总进度条与 rubric Tag 的取值。
// 设计稿 webui/src/data/mock.js:246-251 的 8 个分（86/78/91/84/72/95/80/88）
// 均值 84.25，mock.js:524 的 `rubric.average` 写作 84.3 —— 即保留一位小数。
[[nodiscard]] double RubricAverage(const std::vector<RubricScore>& scores) {
    if (scores.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const RubricScore& s : scores) {
        sum += s.score;
    }
    return sum / static_cast<double>(scores.size());
}

// 三态判定：FAIL 优先（rubric 低于阈值 / high issue / 机器 high 失败，
// 判据见 RubricVerdictFail 与 `06` §2.4）；没到 FAIL 但还留着 issue 或机器
// 校验有非 high 的失败项 → 中态「有条件通过」；两处都干净才是「通过」。
// 设计稿给的就是中态那一例（mock.js:516-533：机器 25/26 提示级不阻塞 +
// 留一条 medium issue）。
[[nodiscard]] ReviewVerdict DeriveVerdict(bool fail, const QString& issueLevel,
                                          int machineFailCount) {
    if (fail) {
        return ReviewVerdict::Fail;
    }
    return (!issueLevel.isEmpty() || machineFailCount > 0) ? ReviewVerdict::Conditional
                                                           : ReviewVerdict::Pass;
}

// —— rubric 条形 + 阈值刻度线（token 派生，零内联）——
// webui views.css:615 `.rubric .r-row { grid-template-columns: 76px 1fr 44px; gap:12px }`：
// 名称与分数各自定宽居中，只有中间的进度条伸缩。条形本体不再自带文字
// （旧版把「名称 分数/阈值」画在整条 bar 上，导致宽窄不一时文字跟着漂）。
class RubricBar : public QWidget {
  public:
    explicit RubricBar(const RubricScore& s, QWidget* parent) : QWidget(parent), score_(s) {
        setMinimumHeight(20);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const int h = height();
        // webui `.progress.thin`：细条居中，上下留白对称
        const int barH = std::clamp(h - 8, 4, 10);
        const int y = (h - barH) / 2;
        const int w = std::max(1, width());

        // 底槽（bg.elevated；r-pill 与进度条同档）
        p.setPen(Qt::NoPen);
        p.setBrush(shine::widgets::TokenQColor(theme::Current().bgElevated));
        p.drawRoundedRect(QRect(0, y, w, barH), barH / 2.0, barH / 2.0);
        // 得分条（低于阈值 = danger，否则 = accent primary；r-pill 圆头）
        const auto c = score_.below
                           ? shine::widgets::TokenQColor(theme::Current().statusDanger)
                           : shine::widgets::TokenQColor(theme::Current().accentPrimary);
        p.setBrush(c);
        p.drawRoundedRect(QRect(0, y, std::max(barH, w * std::clamp(score_.score, 0, 100) / 100),
                                barH),
                          barH / 2.0, barH / 2.0);

        // 阈值刻度线（text.secondary，贯穿条高的竖线）
        const int tx = w * std::clamp(score_.threshold, 0, 100) / 100;
        p.setPen(shine::widgets::TokenQColor(theme::Current().textSecondary));
        p.drawLine(tx, y - 2, tx, y + barH + 2);
    }

  private:
    RubricScore score_;
};

// .rubric 的三列行：名称（76px / w600 / text-secondary）· 条 · 分数（44px 右对齐 /
// 等宽 / 11px / text-muted）。行间距 9px 由外层 QVBoxLayout 给。
[[nodiscard]] QWidget* MakeRubricRow(const RubricScore& s, QWidget* parent) {
    auto* row = new QWidget(parent);
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(theme::space::kSteps[4]); // 12px：.r-row 的 gap

    auto* name = new QLabel(s.name, row);
    name->setFixedWidth(76); // grid 第一列
    name->setFont([parent] {
        QFont f = parent->font();
        f.setPixelSize(12); // 12.5px 按「就近取整」落 12px 档
        f.setWeight(QFont::DemiBold);
        return f;
    }());
    widgets::SetTextColor(name, theme::Current().textSecondary);
    name->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto* score = new QLabel(QString::number(s.score), row);
    score->setFixedWidth(44); // grid 第三列
    score->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QFont sf = score->font();
    sf.setPixelSize(11); // 11.5px → 11px
    sf.setFamilies({QString::fromLatin1("Cascadia Code"), QString::fromLatin1("JetBrains Mono"),
                    QString::fromLatin1("Consolas"), QString::fromLatin1("monospace")});
    score->setFont(sf);
    widgets::SetTextColor(score,
                          s.below ? theme::Current().statusDanger : theme::Current().textMuted);

    rl->addWidget(name);
    rl->addWidget(new RubricBar(s, row), 1);
    rl->addWidget(score);
    // 阈值不再常驻成第三段文字（`.r-score` 只有分数一列），信息落到 tooltip 上，
    // hover 就能看到「得分 / 阈值 / 是否达标」，与判据（`06` §2.4）对得上。
    row->setToolTip(QStringLiteral("%1：%2 / %3 阈值（%4）")
                        .arg(s.name)
                        .arg(s.score)
                        .arg(s.threshold)
                        .arg(s.below ? QStringLiteral("低于阈值") : QStringLiteral("达标")));
    return row;
}

} // namespace

ReviewView::ReviewView(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    // 页面级留白 / 分区间距统一走 layout helper（对齐 webui .vw：20 24 26 / gap 16）
    util::PageMargins(outer);
    util::PageSpacing(outer);
    outer->addWidget(BuildHead());

    // —— 单列滚动 + 两张 SectionCard ——
    // 上一版是两个裸 QLabel 标题 + 两个滚动区直接纵向排开，没有卡片边界，
    // 「哪块是哪块」要看字才分得清。
    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* column = new QWidget(scroll);
    auto* col = new QVBoxLayout(column);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(theme::space::kSteps[3]);

    // —— rubric 区（8 维条形）——
    auto* rubric_card = new widgets::SectionCard(QStringLiteral("语义 rubric"), this);
    rubric_card->SetSubtitle(QStringLiteral("8 维 · `06` §2.4 阈值"));
    rubric_card->SetCollapsible(true);
    rubric_scroll_ = new QScrollArea(rubric_card);
    rubric_scroll_->setFrameShape(QFrame::NoFrame);
    rubric_scroll_->setWidgetResizable(true);
    rubric_scroll_->setMinimumHeight(240);
    rubric_stack_ = new QStackedWidget(rubric_scroll_);
    auto* empty = new widgets::EmptyState(
        QStringLiteral("📋"), QStringLiteral("还没评审"),
        QStringLiteral("先跑章节流水线 T12 生成评审产物，或点「进入修复轮」重评。"),
        QStringLiteral(""), rubric_stack_);
    rubric_rows_ = new QWidget(rubric_stack_);
    rubric_stack_->addWidget(empty);
    rubric_stack_->addWidget(rubric_rows_);
    rubric_stack_->setCurrentWidget(empty);
    rubric_scroll_->setWidget(rubric_stack_);
    rubric_card->BodyLayout()->addWidget(rubric_scroll_);
    col->addWidget(rubric_card);

    // —— 机器校验区（K01–K29）——
    auto* check_card = new widgets::SectionCard(QStringLiteral("机器校验 K01–K29"), this);
    check_card->SetSubtitle(QStringLiteral("`06` §2.3 · low 级可记但放行"));
    check_card->SetCollapsible(true);
    check_card->SetContentMinWidth(720); // 5 列详情表：低于此宽度会互相压扁
    check_scroll_ = new QScrollArea(check_card);
    check_scroll_->setFrameShape(QFrame::NoFrame);
    check_scroll_->setWidgetResizable(true);
    check_scroll_->setMinimumHeight(280);
    check_table_ = new data::DataTable(QStringLiteral("p04.review.checks"), check_scroll_);
    check_table_->SetColumns({{QStringLiteral("id"), QStringLiteral("编号")},
                              {QStringLiteral("name"), QStringLiteral("检查")},
                              {QStringLiteral("outcome"), QStringLiteral("结论")},
                              {QStringLiteral("severity"), QStringLiteral("级别")},
                              {QStringLiteral("detail"), QStringLiteral("详情")}});
    check_scroll_->setWidget(check_table_);
    check_card->BodyLayout()->addWidget(check_scroll_);
    col->addWidget(check_card);

    col->addStretch(1);
    scroll->setWidget(column);
    outer->addWidget(scroll, 1);
}

ReviewView::~ReviewView() {
    CloseDb();
}

QWidget* ReviewView::BuildHead() {
    // 汇总区结构照设计稿 webui Novel.jsx:208-218：卡片体是 `col gap-3`
    // —— 上面一行 `row gap-3`（结论 + 三个 Tag + 修复轮 + 动作），
    // 下面一条汇总进度条。gap-3 = --sp-3 = 12px（tokens.css:18）→
    // theme::space::kSteps[4]。
    auto* head = new QWidget(this);
    auto* hv = new QVBoxLayout(head);
    hv->setContentsMargins(0, 0, 0, 0);
    hv->setSpacing(theme::space::kSteps[4]); // 12px：.col gap-3

    auto* row = new QWidget(head);
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(theme::space::kSteps[4]); // 12px：.row gap-3

    // Novel.jsx:210 `<span className="strong" style={{ fontSize: 15 }}>结论：<span
    // style={{ color: 'var(--warn)' }}>有条件通过</span></span>`：
    // 前缀「结论：」只有 .strong（base.css:122 = font-weight 600）+ 15px，
    // 不改字色；tone 只落在右侧那个状态词上。15 是整数 px，不用取整。
    verdict_ = new QLabel(QStringLiteral("结论："), row);
    QFont vf = verdict_->font();
    vf.setPixelSize(15); // Novel.jsx:210 内联 fontSize 15
    vf.setWeight(QFont::DemiBold); // base.css:122 .strong = 600
    verdict_->setFont(vf);
    verdict_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    widgets::SetTextColor(verdict_, theme::Current().textPrimary);

    verdict_word_ = new QLabel(QString::fromLatin1(ReviewVerdictText(verdict_state_)), row);
    verdict_word_->setFont(vf);
    verdict_word_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    widgets::SetTextColor(verdict_word_, theme::Current().statusDanger);

    // 三个汇总 Tag（Novel.jsx:211-213，tone 逐个照抄）：
    //   rubric → info / 机器 → ok / issue → warn。
    // Tag 自身的几何/配色由 kit + QSS 落地（QssBuilder.cpp:368-371 的
    // .tag h20 p0 8 r-pill f11.5→11 w600 + 底 tone 12% / 边 tone 35%）。
    // 「还没评审」时用 idle（QssBuilder.cpp:386 的 .tag.idle 中性档）。
    rubric_tag_ = new widgets::Tag(QStringLiteral("rubric —"), "idle", false, row);
    machine_tag_ = new widgets::Tag(QStringLiteral("机器 0/0"), "idle", false, row);
    issue_tag_ = new widgets::Tag(QStringLiteral("无 issue"), "idle", false, row);

    round_ = new QLabel(QStringLiteral("修复轮 0/3"), row);
    widgets::SetTextColor(round_, theme::Current().textSecondary);

    repair_btn_ = new widgets::Button(QStringLiteral("进入修复轮"), widgets::Button::Variant::Primary,
                                      widgets::Button::Size::Sm, row);
    repair_btn_->setToolTip(QStringLiteral("按 FAIL 项重评 + 最小修改（累计 ≤3 轮；T12 FAIL → T13）"));
    repair_btn_->setEnabled(false);
    connect(repair_btn_, &widgets::Button::clicked, this, [this] { Repair(); });

    ignore_btn_ = new widgets::Button(QStringLiteral("忽略并继续"),
                                      widgets::Button::Variant::Ghost,
                                      widgets::Button::Size::Sm, row);
    ignore_btn_->setToolTip(QStringLiteral("人工确认放行本次评审（只改 UI 态，不动产物与账）"));
    ignore_btn_->setEnabled(false);
    connect(ignore_btn_, &widgets::Button::clicked, this, [this] { IgnoreAndContinue(); });

    hint_ = new QLabel(QStringLiteral("选一章后自动读 `work/chNNN/10_review.json` 并跑 K01–K29。"),
                       row);
    hint_->setWordWrap(true);
    widgets::SetTextColor(hint_, theme::Current().textSecondary);

    hl->addWidget(verdict_);
    hl->addWidget(verdict_word_);
    hl->addWidget(rubric_tag_);
    hl->addWidget(machine_tag_);
    hl->addWidget(issue_tag_);
    hl->addWidget(round_);
    hl->addWidget(repair_btn_);
    hl->addWidget(ignore_btn_);
    hl->addWidget(hint_, 1);
    hv->addWidget(row);

    // Novel.jsx:217 `<Progress value={84.3} />` —— 非 thin 的 .prog：
    // h6 / r-pill / 底 fill-muted / 条 accent（ui.css:430-443）。几何走
    // kit ProgressBar + QSS（QssBuilder.cpp:547-550），这里只给值。
    // 设计稿的 .prog 内无文字（UI.jsx:145-151 只渲染一条 <i>），故关掉百分比。
    summary_bar_ = new widgets::ProgressBar(head);
    summary_bar_->setTextVisible(false);
    summary_bar_->setValue(0);
    summary_bar_->setToolTip(QStringLiteral("rubric 8 维均值（设计稿 rubric.average）"));
    hv->addWidget(summary_bar_);
    return head;
}

void ReviewView::SetLlm(agent::LlmCallFn call, agent::LlmStreamFn stream) {
    call_ = std::move(call);
    stream_ = std::move(stream);
}

bool ReviewView::OpenBook(const std::filesystem::path& dbPath, const std::filesystem::path& projectDir,
                          QString* err) {
    if (db_ && db_path_ == dbPath) {
        project_dir_ = projectDir; // 同库只更新工程根（可能是另一本书的 work）
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
    project_dir_ = projectDir;
    return true;
}

void ReviewView::CloseDb() noexcept {
    db_.reset();
}

void ReviewView::SelectChapter(qint64 chapterId, int chapterOrd) {
    chapter_id_ = chapterId;
    chapter_ord_ = chapterOrd;
    ignored_ = false;
    injected_ = false;
    if (chapter_ord_ <= 0 || project_dir_.empty()) {
        if (chapter_ord_ <= 0 && db_ && chapter_id_ > 0) {
            novelcore::NovelGraph g(*db_);
            if (auto ch = g.GetChapter(static_cast<novelcore::RowId>(chapter_id_)); ch) {
                chapter_ord_ = ch->ord;
            }
        }
    }
    Refresh();
}

void ReviewView::InjectReviewArtifact(std::string_view criticJson, int revisionsUsed) {
    injected_ = true;
    critic_json_ = std::string(criticJson);
    revisions_used_ = revisionsUsed;
    Refresh();
}

void ReviewView::Refresh() {
    // ① rubric 源：work/chNNN/10_review.json（P1 包装 → payload 即 critic JSON）
    //    injected_ = 探针直喂的评审产物（不重读盘，避免覆盖）
    if (!injected_) {
        critic_json_.clear();
        if (chapter_ord_ > 0 && !project_dir_.empty()) {
            if (auto art = novelcore::ReadStageArtifact(project_dir_, chapter_ord_, "CHAPTER_REVIEW")) {
                critic_json_ = art->payload;
            }
        }
    }
    // 修复轮数：11_repair_receipt.json 的 round（无 = 0 轮）
    revisions_used_ = 0;
    if (chapter_ord_ > 0 && !project_dir_.empty()) {
        if (auto art = novelcore::ReadStageArtifact(project_dir_, chapter_ord_, "CHAPTER_REPAIR")) {
            const auto doc = util::json::ParseDoc(art->payload);
            if (doc) {
                revisions_used_ = std::max(util::json::GetInt(doc.root(), "round", 0), 0);
            }
        }
    }
    scores_ = ParseRubric(critic_json_);

    QString why;
    const bool rubricFail = RubricVerdictFail(scores_, critic_json_, &why);
    RunMachineChecks();
    // 总判定：rubric FAIL **或** 机器校验有 high 失败 → FAIL（`06` §2.4 + `07` §2.3 G2）
    verdict_fail_ = rubricFail || machine_high_fail_ > 0;
    verdict_text_ = verdict_fail_ ? QStringLiteral("FAIL") : QStringLiteral("PASS");
    verdict_why_ = why;
    // 三态判定（中态补齐）：FAIL 判据一字未改，在其之上按遗留 issue 与机器
    // 校验的非 high 失败项落「有条件通过」（DeriveVerdict，词表见
    // ReviewVerdictText）。汇总 Tag / 进度条都取自这四个值。
    issue_level_ = MaxIssueLevel(critic_json_);
    verdict_state_ = DeriveVerdict(verdict_fail_, issue_level_, fail_count_);
    rubric_avg_ = RubricAverage(scores_);

    // —— 刷新 UI ——
    auto* rows = new QVBoxLayout(rubric_rows_);
    // webui .rubric { display:flex; flex-direction:column; gap:9px }
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(9);
    while (rows->count() > 0) {
        QLayoutItem* it = rows->takeAt(0);
        delete it->widget();
        delete it;
    }
    if (!critic_json_.empty()) {
        for (const RubricScore& s : scores_) {
            rows->addWidget(MakeRubricRow(s, rubric_rows_));
        }
        rubric_stack_->setCurrentWidget(rubric_rows_);
    } else {
        rubric_stack_->setCurrentIndex(0);
    }

    std::vector<std::vector<QString>> table;
    table.reserve(checks_.size());
    for (const CheckLine& c : checks_) {
        table.push_back({c.id, c.name, c.outcome, c.severity, c.detail});
    }
    check_table_->SetRows(std::move(table));

    // —— 汇总区（webui Novel.jsx:210-217）——
    const bool noReview = critic_json_.empty();
    // 状态词上色：通过=ok / 有条件通过=warn（Novel.jsx:210 的 --warn）/
    // 不通过=danger。还没评审时是「—」+ 中性字色。
    const std::uint32_t stateToken = verdict_state_ == ReviewVerdict::Pass
                                         ? theme::Current().statusOk
                                         : (verdict_state_ == ReviewVerdict::Conditional
                                                ? theme::Current().statusWarn
                                                : theme::Current().statusDanger);
    verdict_word_->setText(noReview ? QStringLiteral("—")
                                    : QString::fromLatin1(ReviewVerdictText(verdict_state_)));
    widgets::SetTextColor(verdict_word_, noReview ? theme::Current().textMuted : stateToken);

    // rubric Tag（Novel.jsx:211 `tone="info"` · "rubric 84.3"）：均值保留一位
    // 小数 —— mock.js:524 的 average 就是这么写的（8 维均值 84.25 → 84.3）。
    if (noReview) {
        SetTagText(rubric_tag_, QStringLiteral("rubric —"), "idle");
    } else {
        SetTagText(rubric_tag_, QStringLiteral("rubric %1").arg(rubric_avg_, 0, 'f', 1), "info");
    }
    // 机器 Tag（Novel.jsx:212 `tone="ok"` · "机器 25/26"）：设计稿那一例缺的
    // 1 条是「提示级，不阻塞」（mock.js:526），所以仍给 ok；只有真判 FAIL
    // （含机器 high 失败）才转 danger。
    SetTagText(machine_tag_, QStringLiteral("机器 %1/%2")
                                  .arg(ran_count_ - fail_count_)
                                  .arg(ran_count_),
               noReview ? "idle" : (verdict_fail_ ? "danger" : "ok"));
    // issue Tag（Novel.jsx:213 `tone="warn"` · "存在 medium issue"）：文案跟着
    // 遗留 issue 的最高级别走；一条不留时是「无 issue / ok」。
    if (noReview) {
        SetTagText(issue_tag_, QStringLiteral("无 issue"), "idle");
    } else if (issue_level_.isEmpty()) {
        SetTagText(issue_tag_, QStringLiteral("无 issue"), "ok");
    } else {
        SetTagText(issue_tag_, QStringLiteral("存在 %1 issue").arg(issue_level_),
                   issue_level_ == QStringLiteral("high") ? "danger" : "warn");
    }

    // 汇总进度条（Novel.jsx:217 `value={84.3}`）：QProgressBar 取 int，个位取整。
    summary_bar_->setValue(std::clamp(static_cast<int>(std::lround(rubric_avg_)), 0, 100));
    summary_bar_->setVisible(!noReview);

    round_->setText(QStringLiteral("修复轮 %1/%2%3")
                        .arg(revisions_used_)
                        .arg(max_revisions_)
                        .arg(ignored_ ? QStringLiteral("（已人工确认放行）") : QString()));
    repair_btn_->setEnabled(verdict_fail_ && revisions_used_ < max_revisions_ && !running_ &&
                            static_cast<bool>(call_));
    ignore_btn_->setEnabled(verdict_fail_ && !ignored_ && !running_);
    if (verdict_fail_) {
        SetHint(QStringLiteral("%1 —— 修复轮 %2/%3 可用；出路：[进入修复轮] 或人工确认后[忽略并继续]")
                    .arg(why.isEmpty() ? QStringLiteral("评审 FAIL") : why)
                    .arg(revisions_used_)
                    .arg(max_revisions_),
                theme::Current().statusWarn);
    } else if (verdict_state_ == ReviewVerdict::Conditional) {
        SetHint(QStringLiteral("%1 —— 结论：有条件通过；不阻塞放行，可进修复轮消掉")
                    .arg(why),
                theme::Current().statusWarn);
    } else if (!noReview) {
        SetHint(QStringLiteral("rubric 8 维达阈值、无 high issue（`06` §2.4）"), theme::Current().statusOk);
    }
}

void ReviewView::RunMachineChecks() {
    checks_.clear();
    fail_count_ = 0;
    ran_count_ = 0;
    machine_high_fail_ = 0;
    if (!db_ || chapter_id_ <= 0) {
        return;
    }
    novelcore::CheckInputs in;
    in.chapter_id = static_cast<novelcore::RowId>(chapter_id_);
    in.chapter_ord = chapter_ord_;
    in.project_dir = project_dir_;
    in.snapshot_dir = project_dir_ / "snapshots";
    in.word_target = 3000;
    const novelcore::ValidationReport rep = novelcore::RunChapterChecks(*db_, in);
    for (const novelcore::CheckResult& r : rep.checks) {
        CheckLine c;
        c.id = QString::fromStdString(r.check_id);
        c.name = QString::fromStdString(r.name);
        c.outcome = OutcomeText(r.outcome);
        c.severity = QString::fromStdString(r.severity);
        c.detail = QString::fromStdString(r.detail);
        if (r.outcome != CheckOutcome::NotApplicable) {
            ++ran_count_;
        }
        if (!novelcore::CheckPassed(r)) {
            ++fail_count_;
            c.outcome = QStringLiteral("✘ ") + c.outcome;
            if (r.severity == "high") {
                ++machine_high_fail_;
            }
        }
        checks_.push_back(std::move(c));
    }
}

void ReviewView::ShowRepairRunning() {
    running_ = true;
    repair_btn_->setEnabled(false);
    ignore_btn_->setEnabled(false);
    repair_btn_->SetLoading(true);
    SetHint(QStringLiteral("修复轮跑中（T12 重评 → FAIL 则 T13 最小修改，累计 ≤3）…"),
            theme::Current().statusBusy);
}

void ReviewView::SetHint(const QString& text, std::uint32_t token) {
    hint_->setText(text);
    widgets::SetTextColor(hint_, token);
}

void ReviewView::IgnoreAndContinue() {
    ignored_ = true;
    verdict_text_ = QStringLiteral("PASS（人工确认）");
    verdict_word_->setText(QStringLiteral("通过（人工确认放行）"));
    widgets::SetTextColor(verdict_word_, theme::Current().statusWarn);
    round_->setText(QStringLiteral("修复轮 %1/%2（已人工确认放行）").arg(revisions_used_).arg(max_revisions_));
    SetHint(QStringLiteral("已人工确认放行（不动产物与账；`03` §2.7 的审计由门禁侧记）"),
            theme::Current().textSecondary);
}

void ReviewView::Repair() {
    if (running_ || !call_ || !db_ || chapter_id_ <= 0 || chapter_ord_ <= 0) {
        return;
    }
    if (revisions_used_ >= max_revisions_) {
        SetHint(QStringLiteral("修复轮已用满（%1/%2）—— 出路：手动改稿后重跑流水线，或人工确认放行。")
                    .arg(revisions_used_)
                    .arg(max_revisions_),
                theme::Current().statusWarn);
        return;
    }
    ShowRepairRunning();
    // 作废旧评审产物（人工要求重评）：P1 哈希账的输入没变，不清掉就会复用旧 verdict
    if (project_dir_.empty()) {
        running_ = false;
        return;
    }
    const auto reviewPath =
        novelcore::ChapterWorkDir(project_dir_, chapter_ord_) / "10_review.json";
    std::error_code ec;
    std::filesystem::remove(reviewPath, ec);

    // 剩余轮次预算 = 3 - 已用（累计口径；`06` §2.6「累计 ≤ max_revisions=3」）
    const int budget = max_revisions_ - revisions_used_;
    const qint64 cid = chapter_id_;
    const int ord = chapter_ord_;
    auto call = call_;
    auto stream = stream_;
    auto root = project_dir_;
    async::RunOnWorker([this, cid, ord, budget, call, stream, root] {
        agent::NovelDirector dir(*db_, call, stream);
        agent::GenerateChapterRequest req;
        req.chapter_id = cid;
        req.resume = true; // 复用 T1–T11 产物，只重跑 T12→T13
        req.project_dir = util::PathToUtf8(root);
        req.snapshot_dir = util::PathToUtf8(root / "snapshots");
        req.canon_mode = "manual";
        req.max_revisions = budget;
        const auto res = dir.GenerateChapter(req, nullptr);
        const bool ok = res.has_value();
        const int revs = ok ? res->revisions : 0;
        async::PostToUi([this, ok, revs] {
            running_ = false;
            repair_btn_->SetLoading(false);
            if (ok) {
                ++repairs_run_;
                SetHint(QStringLiteral("修复轮跑完（本页第 %1 次发起 · 本次 %2 轮）—— 重新对账 rubric 与 K01–K29")
                            .arg(repairs_run_)
                            .arg(revs),
                        theme::Current().statusOk);
            } else {
                SetHint(QStringLiteral("修复轮失败 —— 出路：检查 LLM 通路后重试，或人工确认放行。"),
                        theme::Current().statusDanger);
            }
            Refresh();
        });
    });
}

// ————————————————————————————————————————————— 探针

QString ReviewView::RubricProbe() const {
    QString out = QStringLiteral("rubric verdict=%1 dims=%2\n")
                      .arg(verdict_fail_ ? QStringLiteral("FAIL") : QStringLiteral("PASS"))
                      .arg(scores_.size());
    // 汇总区三态 + 三个 Tag / 进度条的取值（`verdict=` 那一行保持二态不变，
    // P04ReviewChecks.cpp:139-141 按字面量断言 verdict=PASS / verdict=FAIL）。
    out += QStringLiteral("rubric verdict3=%1 rubric-avg=%2 issue=%3 machine=%4/%5\n")
               .arg(QString::fromLatin1(ReviewVerdictText(verdict_state_)),
                    QString::number(rubric_avg_, 'f', 1))
               .arg(issue_level_.isEmpty() ? QStringLiteral("none") : issue_level_)
               .arg(ran_count_ - fail_count_)
               .arg(ran_count_);
    for (const RubricScore& s : scores_) {
        out += QStringLiteral("dim %1|%2|%3|%4|below=%5\n")
                   .arg(QString::fromStdString(s.key), s.name)
                   .arg(s.score)
                   .arg(s.threshold)
                   .arg(s.below ? 1 : 0);
    }
    out += QStringLiteral("issues-present=%1\n").arg(critic_json_.empty() ? 0 : 1);
    out += QStringLiteral("reason=%1\n").arg(verdict_why_);
    return out;
}

QString ReviewView::CheckProbe() const {
    QString out = QStringLiteral("checks ran=%1 fail=%2 all-pass=%3\n")
                      .arg(ran_count_)
                      .arg(fail_count_)
                      .arg(fail_count_ == 0 ? 1 : 0);
    for (const CheckLine& c : checks_) {
        out += QStringLiteral("check %1|%2|%3|%4|%5\n")
                   .arg(c.id, c.name, c.outcome, c.severity, c.detail);
    }
    return out;
}

QString ReviewView::RepairProbe() const {
    bool receipt = false;
    int round = 0;
    if (chapter_ord_ > 0 && !project_dir_.empty()) {
        if (auto art = novelcore::ReadStageArtifact(project_dir_, chapter_ord_, "CHAPTER_REPAIR")) {
            receipt = true;
            const auto doc = util::json::ParseDoc(art->payload);
            if (doc) {
                round = util::json::GetInt(doc.root(), "round", 0);
            }
        }
    }
    return QStringLiteral("repair rounds-used=%1 max=%2 receipt=%3 page-runs=%4 running=%5\n")
        .arg(revisions_used_)
        .arg(max_revisions_)
        .arg(receipt ? 1 : 0)
        .arg(round)
        .arg(repairs_run_)
        .arg(running_ ? 1 : 0);
}

} // namespace shine::app
