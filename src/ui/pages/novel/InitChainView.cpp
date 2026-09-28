// P04-S4 初始化链：I1–I16 流水线 + N1–N14 门禁（可配置 + 人工审批）+ 前情导入（决策 §7）。
// 三块布局：阶段区（StageProbe 同源）/ 门禁区（GateProbe 同源）/ 前情导入区（ImportProbe 同源）。
// 公开 API 与探针同一路径；颜色零内联（check-layers rule 3）——token 拼串全在 WorldBoardShared.h。
#include "ui/pages/novel/InitChainView.h"

#include "ui/pages/novel/WorldBoardShared.h"

#include "db/sqlite/SqliteDb.h"
#include "ui/kit/data/Panels.h"
#include "ui/kit/data/Table.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/Inputs.h"
#include "novel/NovelGraph.h"
#include "novel/NovelInit.h"
#include "novel/NovelTypes.h"
#include "project/Project.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Json.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace shine::app {
namespace {

using novelcore::EntityRow;

// ———— N1–N14 规则目录（`10` §2.3 的单一文案来源；判定在 core 的 CheckInitGate）————
struct GateInfo {
    const char* id;
    const char* name;
};
constexpr GateInfo kGates[] = {
    {"N1", "world_meta 含 book_title（书名已设）"},
    {"N2", "writing_style 有且仅一行，pov_mode / pacing 非空"},
    {"N3", "author_rules 至少 1 条 severity=error 硬规则"},
    {"N4", "主角：entities(kind=person) ≥ 1"},
    {"N5", "主角人设 entity_personas 的 goal / desire / fear 非空"},
    {"N6", "初始地点：entities(kind=location) ≥ 1"},
    {"N7", "主线剧情线 plots(kind=main) 且 target_ch > 0"},
    {"N8", "mysteries ≥ 1（第 1 章的信息节奏）"},
    {"N9", "secrets 至少 1 条 scope=world"},
    {"N10", "volumes.ord 唯一且连续（从 1 起）"},
    {"N11", "PLANNED 伏笔 setup_ch ≥ 1 且 payoff_ch > setup_ch"},
    {"N12", "field_defs 系统种子 ≥ 11 条"},
    {"N13", "agent_defs 内置 Agent ≥ 15 且启用"},
    {"N14", "无悬空引用（引用完整性 `06` K02/K03 同口径）"},
};

// I1–I16 中文阶段名（`10` §2.2；代码/产物以 InitStageCatalog() 为单一来源）
constexpr const char* kStageNames[16] = {"概念",       "世界",     "力量体系", "人物",   "关系",
                                         "地点",       "势力",     "物品",     "秘密",   "谜团",
                                         "剧情线",     "文风",     "伏笔种子", "分卷",   "门禁",
                                         "提交"};

[[nodiscard]] QString GateIdAt(int i) {
    return QString::fromLatin1(kGates[i].id);
}

[[nodiscard]] int GateNum(const QString& nId) {
    bool ok = false;
    const int n = nId.mid(1).toInt(&ok);
    return ok ? n : 0;
}

[[nodiscard]] bool IsGateId(const QString& nId) {
    for (const GateInfo& g : kGates) {
        if (nId == QLatin1String(g.id)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] QString GateNameOf(const QString& nId) {
    for (const GateInfo& g : kGates) {
        if (nId == QLatin1String(g.id)) {
            return QString::fromUtf8(g.name);
        }
    }
    return nId;
}

// 单行化（探针/摘要按行组织）
[[nodiscard]] QString OneLine(const QString& s) {
    QString out = s;
    out.replace(QLatin1Char('\n'), QLatin1Char(' '));
    out.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return out.trimmed();
}

// 跨库稳定身份（决策 §7 / S2 约定）：{"canonical","aliases","source_book","source_ch"}
[[nodiscard]] std::string IdentityMetaJson(std::string_view canonical, std::string_view sourceBook,
                                           int sourceCh) {
    return "{\"canonical\":" + util::json::JsonQuote(canonical) + ",\"aliases\":[],\"source_book\":" +
           util::json::JsonQuote(sourceBook) + ",\"source_ch\":" + std::to_string(sourceCh) + '}';
}

struct MetaIdentity {
    std::string canonical;
    std::string sourceBook;
    std::int64_t sourceCh = 0;
};

[[nodiscard]] MetaIdentity ParseIdentity(std::string_view metaJson) {
    MetaIdentity out;
    util::json::OwnedDoc doc = util::json::ParseDoc(metaJson);
    if (!doc) {
        return out;
    }
    yyjson_val* root = doc.root();
    out.canonical = util::json::GetStrCopy(root, "canonical");
    out.sourceBook = util::json::GetStrCopy(root, "source_book");
    out.sourceCh = util::json::GetI64(root, "source_ch");
    return out;
}

[[nodiscard]] std::int64_t QueryI64(db::sqlite::Database& db, std::string_view sql) {
    std::int64_t n = 0;
    if (auto st = db.Prepare(sql)) {
        auto s = st->Step();
        if (s && *s == db::sqlite::StepResult::Row) {
            n = st->ColumnInt(0);
        }
    }
    return n;
}

// 前作条目的分类（决策 §7 的「前情圣经」四类）
[[nodiscard]] QString CategoryOf(const QString& kind) {
    if (kind == QLatin1String("person")) return QStringLiteral("人物终态卡");
    if (kind == QLatin1String("foreshadowing")) return QStringLiteral("已完结伏笔");
    if (kind == QLatin1String("event")) return QStringLiteral("结案摘要");
    for (const char* k : {"universe", "world_rule", "history", "culture", "language", "religion",
                          "economy", "tech", "society", "calendar"}) {
        if (kind == QLatin1String(k)) {
            return QStringLiteral("世界观条目");
        }
    }
    return QStringLiteral("设定");
}

} // namespace

InitChainView::InitChainView(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(theme::space::kSteps[3], theme::space::kSteps[2],
                              theme::space::kSteps[3], theme::space::kSteps[2]);
    outer->setSpacing(theme::space::kSteps[2]);

    // 头部动作行（UI.md §3：空态给下一步 —— 下一步就是这两个按钮）
    auto* head = new QWidget(this);
    auto* hr = new QHBoxLayout(head);
    hr->setContentsMargins(0, 0, 0, 0);
    hr->setSpacing(theme::space::kSteps[1]);
    runSkeletonBtn_ = new widgets::Button(QStringLiteral("▶ 一键跑骨架（I1–I16 结构步）"),
                                          widgets::Button::Variant::Primary,
                                          widgets::Button::Size::Sm, head);
    runGatesBtn_ = new widgets::Button(QStringLiteral("▶ 跑门禁 N1–N14"), widgets::Button::Variant::Secondary,
                                       widgets::Button::Size::Sm, head);
    state_ = new QLabel(QStringLiteral("初始化链还没开跑 —— 下一步：点「一键跑骨架」建结构（书名/文风/"
                                       "卷/主线 + 种子），再点「跑门禁 N1–N14」看还缺什么。"),
                        head);
    state_->setWordWrap(true);
    hr->addWidget(runSkeletonBtn_);
    hr->addWidget(runGatesBtn_);
    hr->addWidget(state_, 1);
    outer->addWidget(head);

    auto* split = new QSplitter(Qt::Vertical, this);
    split->addWidget(BuildStagesArea());
    split->addWidget(BuildGatesArea());
    split->addWidget(BuildImportArea());
    split->setSizes({260, 300, 220});
    outer->addWidget(split, 1);

    connect(runSkeletonBtn_, &QPushButton::clicked, this, [this] {
        QString err;
        if (!RunSkeleton(&err)) {
            state_->setText(err);
            widgets::SetTextColor(state_, theme::Current().statusDanger);
        }
    });
    connect(runGatesBtn_, &QPushButton::clicked, this, [this] {
        QString report;
        (void)RunGates(&report);
        state_->setText(report.split(QLatin1Char('\n')).value(0));
        widgets::SetTextColor(state_, theme::Current().textSecondary);
    });

    RebuildStages();
    RebuildGates();
}

InitChainView::~InitChainView() {
    CloseDb();
}

// ————————————————————————————————————————————— 布局三块

QWidget* InitChainView::BuildStagesArea() {
    auto* w = new QWidget(this);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(theme::space::kSteps[1]);
    v->addWidget(SectionLabel(w, QStringLiteral("初始化流水线 I1–I16（`10` §2.2 · 可断点续跑）")));

    stageTable_ = new data::DataTable(QStringLiteral("init-chain-stages"), w);
    stageTable_->SetColumns({{QStringLiteral("stage"), QStringLiteral("阶段"), 64},
                             {QStringLiteral("name"), QStringLiteral("名称"), 96},
                             {QStringLiteral("code"), QStringLiteral("代码"), 210},
                             {QStringLiteral("artifact"), QStringLiteral("产物"), 176},
                             {QStringLiteral("status"), QStringLiteral("状态"), 168}});
    stageTable_->SetActionColumn(QStringLiteral("看产物"), [this](int row) {
        const auto specs = novelcore::InitStageCatalog();
        if (row >= 0 && row < static_cast<int>(specs.size())) {
            ShowArtifact(QString::fromStdString(std::string{specs[static_cast<std::size_t>(row)].artifact}));
        }
    });
    v->addWidget(stageTable_, 3);

    auto* artRow = new QWidget(w);
    auto* ar = new QHBoxLayout(artRow);
    ar->setContentsMargins(0, 0, 0, 0);
    ar->setSpacing(theme::space::kSteps[1]);
    ar->addWidget(SectionLabel(artRow, QStringLiteral("阶段产物（init/ · JsonTree）")));
    artifactSel_ = new widgets::Select(false, false, artRow);
    std::vector<widgets::Select::Item> items;
    for (const novelcore::InitStageSpec& s : novelcore::InitStageCatalog()) {
        items.push_back({QString::fromStdString(std::string{s.artifact}), QString{}, false});
    }
    artifactSel_->SetItems(std::move(items));
    artifactSel_->SetPlaceholder(QStringLiteral("选产物文件（I1–I16）"));
    artifactSel_->SetOnChanged([this] {
        const std::vector<QString> got = artifactSel_->Checked();
        if (!got.empty()) {
            ShowArtifact(got.front());
        }
    });
    ar->addWidget(artifactSel_, 1);
    v->addWidget(artRow);

    artifactStack_ = new QStackedWidget(w);
    artifactEmpty_ = new widgets::EmptyState(QStringLiteral("📄"), QStringLiteral("还没看产物"),
                                             QStringLiteral("从阶段表点「看产物」，或在上面选 init/ 下的产物文件。"
                                                             "I15_gate.json 是门禁报告、I16_commit.json 是提交账。"),
                                             QStringLiteral("看 I15 门禁报告"), artifactStack_);
    artifactEmpty_->SetOnAction([this] { ShowArtifact(QStringLiteral("I15_gate.json")); });
    artifactTree_ = new data::JsonTree(artifactStack_);
    artifactStack_->addWidget(artifactEmpty_);
    artifactStack_->addWidget(artifactTree_);
    artifactStack_->setCurrentWidget(artifactEmpty_);
    v->addWidget(artifactStack_, 2);

    stageNote_ = new QLabel(QStringLiteral("可断点续跑：骨架幂等（重跑不重做、已有产物保留）；"
                                           "I1–I14 由路径 C（AI 分域生成）/ 路径 A（导入）落盘（`10` §2.8 当前落盘范围），"
                                           "I15/I16 由「一键跑骨架」落盘。"),
                            w);
    stageNote_->setWordWrap(true);
    widgets::SetTextColor(stageNote_, theme::Current().textMuted);
    v->addWidget(stageNote_);
    return w;
}

QWidget* InitChainView::BuildGatesArea() {
    auto* w = new QWidget(this);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(theme::space::kSteps[1]);
    v->addWidget(SectionLabel(w, QStringLiteral("门禁 N1–N14（`10` §2.3 · 全部满足才可开写第 1 章，"
                                                "失败不允许警告后放行）")));
    gateState_ = new QLabel(QStringLiteral("门禁还没跑 —— 下一步：点「跑门禁 N1–N14」逐条判定。"), w);
    gateState_->setWordWrap(true);
    v->addWidget(gateState_);

    auto* scroll = new QScrollArea(w);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    gateHost_ = new QWidget(scroll);
    gateCol_ = new QVBoxLayout(gateHost_);
    gateCol_->setContentsMargins(0, 0, 0, 0);
    gateCol_->setSpacing(theme::space::kSteps[1]);

    for (const GateInfo& g : kGates) {
        GateRowUi row;
        row.nId = QString::fromLatin1(g.id);
        auto* line = new QWidget(gateHost_);
        auto* hl = new QHBoxLayout(line);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(theme::space::kSteps[1]);
        row.name = new QLabel(QString("%1　%2").arg(row.nId, QString::fromUtf8(g.name)), line);
        row.name->setMinimumWidth(260);
        row.mode = new QLabel(QStringLiteral("启用 enforce"), line);
        // 判定文本可能很长（失败详情 + 修法），普通 QLabel 既不省略也不限宽，
        // 会把同一行的按钮顶出容器并在右栏上叠字。改用 ElidedLabel：
        // 单行按可用宽度省略、hover 出全文、点击就地展开成多行。
        row.verdict = new widgets::ElidedLabel(QStringLiteral("未判定"), line);
        row.verdict->setMinimumWidth(200);
        widgets::SetKind(row.verdict, "fieldhelp");
        row.ignoreBtn = new widgets::Button(QStringLiteral("忽略并人工确认"), widgets::Button::Variant::Ghost,
                                            widgets::Button::Size::Sm, line);
        row.approveBtn = new widgets::Button(QStringLiteral("人工审批"), widgets::Button::Variant::Ghost,
                                             widgets::Button::Size::Sm, line);
        row.enforceBtn = new widgets::Button(QStringLiteral("启用"), widgets::Button::Variant::Ghost,
                                             widgets::Button::Size::Sm, line);
        hl->addWidget(row.name, 3);
        hl->addWidget(row.mode);
        hl->addWidget(row.verdict, 4);
        hl->addWidget(row.ignoreBtn);
        hl->addWidget(row.approveBtn);
        hl->addWidget(row.enforceBtn);
        gateCol_->addWidget(line);

        const QString nId = row.nId;
        connect(row.ignoreBtn, &QPushButton::clicked, this, [this, nId] {
            QString err;
            if (!SetGateRule(nId, QStringLiteral("ignore-approved"), &err)) {
                gateState_->setText(err);
                widgets::SetTextColor(gateState_, theme::Current().statusDanger);
            } else {
                gateState_->setText(QStringLiteral("「%1」已申请「忽略并人工确认」—— 还要过「人工审批」才放行"
                                                   "（显式确认 + 记 audit_logs，不允许警告后放行）。")
                                        .arg(nId));
                widgets::SetTextColor(gateState_, theme::Current().statusWarn);
            }
        });
        connect(row.approveBtn, &QPushButton::clicked, this, [this, nId] {
            // 显式确认（人工审批动作）：确认框是「人工」二字的落点；探针走 ApproveGate 同路径
            const auto yes = QMessageBox::question(
                this, QStringLiteral("人工审批：%1").arg(nId),
                QStringLiteral("确认忽略门禁「%1 %2」并放行？\n此动作会记入 audit_logs"
                               "（action=gate_ignore_approved），报告里标「人工确认放行」。")
                    .arg(nId, GateNameOf(nId)),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (yes != QMessageBox::Yes) {
                return;
            }
            QString err;
            if (!ApproveGate(nId, &err)) {
                gateState_->setText(err);
                widgets::SetTextColor(gateState_, theme::Current().statusDanger);
            } else {
                gateState_->setText(QStringLiteral("「%1」人工审批通过：忽略并放行（已记 audit_logs）。").arg(nId));
                widgets::SetTextColor(gateState_, theme::Current().statusWarn);
            }
        });
        connect(row.enforceBtn, &QPushButton::clicked, this, [this, nId] {
            QString err;
            if (!SetGateRule(nId, QStringLiteral("enforce"), &err)) {
                gateState_->setText(err);
                widgets::SetTextColor(gateState_, theme::Current().statusDanger);
            } else {
                gateState_->setText(QStringLiteral("「%1」恢复启用：照 `10` §2.3 判定，不再放行。").arg(nId));
                widgets::SetTextColor(gateState_, theme::Current().textSecondary);
            }
        });
        gateRows_.push_back(std::move(row));
    }
    gateCol_->addStretch(1);
    scroll->setWidget(gateHost_);
    v->addWidget(scroll, 1);
    return w;
}

QWidget* InitChainView::BuildImportArea() {
    auto* w = new QWidget(this);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(theme::space::kSteps[1]);
    v->addWidget(SectionLabel(w, QStringLiteral("前情导入（决策 §7：写时导入为主 · 只读打开前作库）")));

    auto* row = new QWidget(w);
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(theme::space::kSteps[1]);
    priorSel_ = new widgets::Select(false, true, row);
    priorSel_->SetPlaceholder(QStringLiteral("选前作书（当前书除外）"));
    budget_ = new widgets::NumberInput(false, 1, 100000, row);
    budget_->SetValue(2000);
    importBtn_ = new widgets::Button(QStringLiteral("导入前情"), widgets::Button::Variant::Secondary,
                                     widgets::Button::Size::Sm, row);
    hl->addWidget(priorSel_, 2);
    hl->addWidget(new QLabel(QStringLiteral("摘要上限（字）"), row));
    hl->addWidget(budget_);
    hl->addWidget(importBtn_);
    v->addWidget(row);

    importResult_ = new QLabel(QStringLiteral("还没有导入 —— 选前作书后点「导入前情」：抽「人物终态卡 / 世界观 / "
                                              "已完结伏笔 / 每部结案摘要」，摘要 ≤ 上限，超限截断并明说。"),
                               w);
    importResult_->setWordWrap(true);
    v->addWidget(importResult_);

    importStack_ = new QStackedWidget(w);
    importEmpty_ = new widgets::EmptyState(QStringLiteral("📥"), QStringLiteral("还没有前情导入产物"),
                                           QStringLiteral("导入后这里显示 work/init/prior_import.json（JsonTree）；"
                                                           "可追溯条目带出处「(书, 章)」+ canonical 名落本作库。"),
                                           QStringLiteral("导入前情"), importStack_);
    importEmpty_->SetOnAction([this] {
        QString report;
        QStringList books;
        for (const QString& b : priorSel_->Checked()) {
            books.push_back(b);
        }
        (void)ImportPriorBooks(books, static_cast<int>(budget_->Value()), &report);
    });
    importTree_ = new data::JsonTree(importStack_);
    importStack_->addWidget(importEmpty_);
    importStack_->addWidget(importTree_);
    importStack_->setCurrentWidget(importEmpty_);
    v->addWidget(importStack_, 1);

    connect(importBtn_, &QPushButton::clicked, this, [this] {
        QString report;
        QStringList books;
        for (const QString& b : priorSel_->Checked()) {
            books.push_back(b);
        }
        const bool ok = ImportPriorBooks(books, static_cast<int>(budget_->Value()), &report);
        importResult_->setText(report);
        widgets::SetTextColor(importResult_, ok ? theme::Current().textSecondary : theme::Current().statusDanger);
    });
    return w;
}

// ————————————————————————————————————————————— 数据接入

void InitChainView::LoadFromRef(const project::ProjectRef& ref, const std::string& lastNovel) {
    CloseDb();
    refStore_ = std::make_unique<project::ProjectRef>(ref);
    lastNovel_ = lastNovel;
    bookTitle_.clear();
    bookRoot_.clear();
    bookWork_.clear();
    bookDbPath_.clear();
    openError_.clear();
    gatesRan_ = false;
    lastGates_.clear();

    const std::vector<project::BookRef> books = project::ListBooks(ref);
    const project::BookRef* pick = nullptr;
    for (const project::BookRef& b : books) { // 按 ui.lastNovel 选书（一部一库）
        if (!lastNovel.empty() && b.title == lastNovel) {
            pick = &b;
            break;
        }
    }
    if (pick == nullptr && !books.empty()) {
        pick = &books.front(); // 默认书在前；lastNovel 空（或书已不在）→ 默认书
    }
    if (pick == nullptr) {
        openError_ = QStringLiteral("这个项目还没有任何书（db/novel.db 缺失）。下一步：新建项目后先建默认书库，"
                                    "或把书放到 books/<书名>/db/novel.db 再回来。");
        state_->setText(openError_);
        widgets::SetTextColor(state_, theme::Current().statusWarn);
        LoadGateConfig();
        RebuildStages();
        RebuildGates();
        RebuildImportBooks();
        return;
    }
    db_ = std::make_unique<db::sqlite::Database>();
    if (auto r = db_->Open({.path = pick->dbPath}); !r) {
        openError_ = QStringLiteral("书库打不开：%1（%2）。请确认磁盘可写、文件未被其他程序独占，然后重新选书。")
                         .arg(QString::fromStdString(util::PathToUtf8(pick->dbPath)),
                              QString::fromStdString(r.error().message));
        state_->setText(openError_);
        widgets::SetTextColor(state_, theme::Current().statusDanger);
        db_.reset();
    }
    bookTitle_ = QString::fromStdString(pick->title);
    bookRoot_ = pick->rootDir;
    bookWork_ = pick->workDir;
    bookDbPath_ = pick->dbPath;
    LoadGateConfig();
    RebuildStages();
    RebuildGates();
    RebuildImportBooks();

    // 前情导入产物读回（重启读回一致：ImportProbe 与这里同源）
    if (std::optional<std::string> text = util::ReadFileBytes(PriorContextPath())) {
        importTree_->SetJson(QString::fromStdString(*text));
        importStack_->setCurrentWidget(importTree_);
        util::json::OwnedDoc doc = util::json::ParseDoc(*text);
        const int k = static_cast<int>(util::json::GetI64(doc.root(), "summary_chars"));
        const int t = static_cast<int>(util::json::GetI64(doc.root(), "truncated_chars"));
        importResult_->setText(
            QStringLiteral("已读回前情导入产物（work/init/prior_import.json）：摘要 %1 字%2。"
                           "重导入会按 canonical + 出处幂等，不重复落库。")
                .arg(k)
                .arg(t > 0 ? QStringLiteral("（曾截断 %1 字）").arg(t) : QString{}));
        widgets::SetTextColor(importResult_, theme::Current().textSecondary);
    }
    if (openError_.isEmpty()) {
        state_->setText(QStringLiteral("已载入《%1》的初始化链 —— 下一步：一键跑骨架 → 跑门禁 N1–N14。")
                            .arg(bookTitle_));
        widgets::SetTextColor(state_, theme::Current().textSecondary);
    }
}

void InitChainView::CloseDb() noexcept {
    db_.reset(); // 关库（探针「关库重开」：下次 LoadFromRef 开新 Database 实例）
}

// ————————————————————————————————————————————— 公开 API

bool InitChainView::RunSkeleton(QString* err) {
    const auto fail = [err](const QString& m) {
        if (err != nullptr) {
            *err = m;
        }
        return false;
    };
    if (!db_) {
        return fail(QStringLiteral("骨架没跑成：书库没打开 —— 先在小说工作区选书（或检查 db/novel.db 是否存在）。"));
    }
    SetRunning(QStringLiteral("跑中：正在建结构骨架（路径 B/D，不调 LLM）…"), true);
    QElapsedTimer chainTimer; // 阶段行「耗时」列：骨架是单次事务成链，记整链耗时
    chainTimer.start();
    // project_dir 传书的 work/ —— core 的 InitDir(<dir>)=<dir>/init，产物落 work/init/（S4 判定口径）
    const novelcore::InitSkeletonResult sk =
        novelcore::RunInitSkeleton(*db_, bookWork_, bookTitle_.toStdString(), 100);
    const qint64 chainMs = chainTimer.elapsed();
    SetRunning(QString{}, false);
    if (!sk.ok) {
        return fail(QStringLiteral("骨架失败：%1。出路：确认书库可写、schema 可建（ApplyCanonicalSchema），"
                                   "修好后重跑 —— 骨架幂等，已建的部分不会重复。")
                        .arg(QString::fromStdString(sk.error)));
    }
    stageMs_.clear();
    for (int i = 1; i <= 16; ++i) {
        stageMs_.insert(QStringLiteral("I%1").arg(i), chainMs);
    }
    RebuildStages();
    QString made;
    for (const std::string& c : sk.created) {
        made += made.isEmpty() ? QString::fromStdString(c) : QStringLiteral("、") + QString::fromStdString(c);
    }
    state_->setText(QStringLiteral("骨架已建：%1。下一步：点「跑门禁 N1–N14」—— 内容类条件（主角/地点…）"
                                   "骨架不伪造，门禁会如实报缺并给修法。")
                        .arg(ElideText(made, 120)));
    widgets::SetTextColor(state_, theme::Current().statusOk);
    if (err != nullptr) {
        err->clear();
    }
    return true;
}

bool InitChainView::RunGates(QString* report) {
    if (!db_) {
        if (report != nullptr) {
            *report = QStringLiteral("门禁没跑成：书库没打开 —— 先在小说工作区选书，再点「跑门禁 N1–N14」。");
        }
        return false;
    }
    SetRunning(QStringLiteral("跑中：正在逐条判定 N1–N14…"), true);
    const std::vector<GateLine> lines = EvaluateGates();
    SetRunning(QString{}, false);
    lastGates_ = lines;
    gatesRan_ = true;

    int waived = 0;
    int bad = 0;
    for (const GateLine& l : lines) {
        if (l.waived) {
            ++waived;
        } else if (!l.rawPass) {
            ++bad;
        }
    }
    QString text;
    if (bad == 0) {
        text = QStringLiteral("门禁判定：可以开写第 1 章（N1–N14 全部满足%1）")
                   .arg(waived > 0 ? QStringLiteral("，其中 %1 条人工确认放行").arg(waived) : QString{});
    } else {
        text = QStringLiteral("门禁挡住：不能开写第 1 章 —— %1 条不满足%2。逐条明细（含修法）如下：")
                   .arg(bad)
                   .arg(waived > 0 ? QStringLiteral("（另有 %1 条人工确认放行）").arg(waived) : QString{});
    }
    for (const GateLine& l : lines) {
        QString verdictWord;
        if (l.waived) {
            verdictWord = QStringLiteral("人工确认放行");
        } else if (l.rawPass) {
            verdictWord = QStringLiteral("通过");
        } else if (l.pending) {
            verdictWord = QStringLiteral("不通过（已申请「忽略并人工确认」，但还没过人工审批 —— "
                                         "不允许警告后放行）");
        } else {
            verdictWord = QStringLiteral("不通过");
        }
        QString line = QStringLiteral("%1 %2　%3").arg(l.nId, verdictWord, l.name);
        if (!l.rawPass && !l.waived) {
            line += QStringLiteral("　%1").arg(l.detail);
            if (!l.fixHint.isEmpty()) {
                line += QStringLiteral("　修法：%1").arg(l.fixHint);
            }
        } else if (l.waived) {
            line += QStringLiteral("　原判定：%1　审计：audit_logs(action=gate_ignore_approved)")
                        .arg(l.rawPass ? QStringLiteral("满足") : l.detail);
        }
        text += QLatin1Char('\n') + line;
    }
    if (report != nullptr) {
        *report = text;
    }
    state_->setText(text.section(QLatin1Char('\n'), 0, 0));
    widgets::SetTextColor(state_, bad == 0 ? theme::Current().statusOk : theme::Current().statusDanger);
    RebuildGates();
    return bad == 0;
}

bool InitChainView::SetGateRule(const QString& nId, const QString& mode, QString* err) {
    const auto fail = [err](const QString& m) {
        if (err != nullptr) {
            *err = m;
        }
        return false;
    };
    if (!IsGateId(nId)) {
        return fail(QStringLiteral("「%1」不在 N1–N14 门禁清单内（`10` §2.3）—— 请从面板按钮操作具体规则。")
                        .arg(nId));
    }
    GateRuleCfg cfg;
    if (mode == QLatin1String("enforce")) {
        cfg.mode = QStringLiteral("enforce");
        cfg.approved = false;
    } else if (mode == QLatin1String("ignore-approved")) {
        // 只是**申请**忽略并人工确认：必须再走 ApproveGate（显式确认 + audit_logs）才放行
        cfg.mode = QStringLiteral("ignore-approved");
        cfg.approved = false;
    } else {
        return fail(QStringLiteral("mode 只能是 enforce（启用）或 ignore-approved（忽略并人工确认），"
                                   "收到「%1」。忽略**必须**走人工审批，不允许警告后放行。")
                        .arg(mode));
    }
    gateCfg_.insert(nId, cfg);
    SaveGateConfig();
    RebuildGates();
    if (err != nullptr) {
        err->clear();
    }
    return true;
}

bool InitChainView::SetGateRule(const QString& nId, bool ignore, QString* err) {
    // 布尔便捷重载（S4 公开 API 口径）：ignore=true → 申请「忽略并人工确认」，false → 恢复 enforce
    return SetGateRule(nId,
                       ignore ? QStringLiteral("ignore-approved") : QStringLiteral("enforce"), err);
}

bool InitChainView::ApproveGate(const QString& nId, QString* err) {
    const auto fail = [err](const QString& m) {
        if (err != nullptr) {
            *err = m;
        }
        return false;
    };
    if (!IsGateId(nId)) {
        return fail(QStringLiteral("「%1」不在 N1–N14 门禁清单内。").arg(nId));
    }
    GateRuleCfg cfg = gateCfg_.value(nId);
    if (cfg.mode != QLatin1String("ignore-approved")) {
        return fail(QStringLiteral("「%1」当前是「启用」—— 先点「忽略并人工确认」提出申请，"
                                   "再走人工审批（不允许跳过申请直接放行）。")
                        .arg(nId));
    }
    if (!db_) {
        return fail(QStringLiteral("书库没打开，审批无法落 audit_logs —— 审批要求留痕，未留痕不放行。"));
    }
    novelcore::NovelGraph g(*db_);
    const std::string detail =
        QStringLiteral("N%1 人工审批：忽略并放行（%2）").arg(GateNum(nId)).arg(GateNameOf(nId)).toStdString();
    if (auto r = g.LogAudit("author", "gate_ignore_approved", "init_gate", GateNum(nId), detail); !r) {
        return fail(QStringLiteral("记 audit_logs 失败：%1 —— 审批要求留痕，未留痕不放行。")
                        .arg(QString::fromStdString(r.error().message)));
    }
    cfg.approved = true;
    cfg.approvedAt = QString::fromStdString(project::Iso8601UtcNow());
    gateCfg_.insert(nId, cfg);
    SaveGateConfig();
    RebuildGates();
    if (err != nullptr) {
        err->clear();
    }
    return true;
}

bool InitChainView::ImportPriorBooks(const QStringList& bookTitles, int budgetChars, QString* report) {
    const auto setReport = [report](const QString& t) {
        if (report != nullptr) {
            *report = t;
        }
    };
    if (!db_ || !refStore_) {
        setReport(QStringLiteral("前情导入没跑成：书库没打开 —— 先在小说工作区选书。"));
        return false;
    }
    if (bookTitles.isEmpty()) {
        setReport(QStringLiteral("前情导入没跑成：没选前作书 —— 在「前作书」里勾一部或多部（当前书除外）。"));
        return false;
    }
    const int budget = budgetChars > 0 ? budgetChars : 2000;
    const std::vector<project::BookRef> books = project::ListBooks(*refStore_);

    std::vector<ImportEntry> entries;
    QStringList perBook;
    int failedBooks = 0;
    for (const QString& title : bookTitles) {
        const project::BookRef* pick = nullptr;
        for (const project::BookRef& b : books) {
            if (QString::fromStdString(b.title) == title) {
                pick = &b;
                break;
            }
        }
        if (pick == nullptr) {
            perBook << QStringLiteral("·《%1》：不在本书系（project::ListBooks 没有这本书）—— "
                                      "检查 books/%1/db/novel.db 是否存在。")
                           .arg(title);
            ++failedBooks;
            continue;
        }
        if (title == bookTitle_) {
            perBook << QStringLiteral("·《%1》：这是当前书 —— 前情导入只导**前作**，请换一本非当前书。").arg(title);
            ++failedBooks;
            continue;
        }
        // 🔴 只读打开前作库（一部一库，绝不写前作库）
        db::sqlite::Database prior;
        if (auto r = prior.Open({.path = pick->dbPath, .readOnly = true, .create = false}); !r) {
            perBook << QStringLiteral("·《%1》：前作库打不开（只读）：%2 —— 确认文件存在且未损坏。")
                           .arg(title, QString::fromStdString(r.error().message));
            ++failedBooks;
            continue;
        }
        std::size_t persons = 0;
        std::size_t worlds = 0;
        std::size_t foreshadows = 0;
        std::size_t recaps = 0;
        // ① 实体：人物终态卡 / 世界观条目 / 其他设定（出处取 meta_json 的 source_book/source_ch）
        if (auto st = prior.Prepare(
                "SELECT kind,name,summary,meta_json,created_chapter FROM entities ORDER BY id")) {
            for (;;) {
                auto s = st->Step();
                if (!s || *s == db::sqlite::StepResult::Done) {
                    break;
                }
                ImportEntry e;
                e.kind = QString::fromStdString(st->ColumnText(0));
                e.name = QString::fromStdString(st->ColumnText(1));
                const MetaIdentity idt = ParseIdentity(st->ColumnText(3));
                e.canonical = idt.canonical.empty() ? e.name : QString::fromStdString(idt.canonical);
                e.sourceCh = idt.sourceCh > 0 ? static_cast<int>(idt.sourceCh)
                                              : static_cast<int>(st->ColumnInt(4));
                e.sourceBook = title;
                e.source = QStringLiteral("(%1, %2)").arg(title).arg(e.sourceCh);
                e.excerpt = OneLine(QString::fromStdString(st->ColumnText(2)));
                if (e.excerpt.isEmpty()) {
                    e.excerpt = QStringLiteral("（前作未填摘要）");
                }
                const QString cat = CategoryOf(e.kind);
                if (cat == QLatin1String("人物终态卡")) {
                    ++persons;
                } else if (cat == QLatin1String("世界观条目")) {
                    ++worlds;
                }
                entries.push_back(std::move(e));
            }
        }
        // ② 已完结伏笔（决策 §7：只取 RESOLVED 的结案真相）
        if (auto st = prior.Prepare(
                "SELECT title,content,truth,payoff_ch FROM foreshadowings WHERE status='RESOLVED' "
                "ORDER BY id")) {
            for (;;) {
                auto s = st->Step();
                if (!s || *s == db::sqlite::StepResult::Done) {
                    break;
                }
                ImportEntry e;
                e.kind = QStringLiteral("foreshadowing");
                e.name = QString::fromStdString(st->ColumnText(0));
                e.canonical = e.name;
                e.sourceCh = static_cast<int>(st->ColumnInt(3));
                e.sourceBook = title;
                e.source = QStringLiteral("(%1, %2)").arg(title).arg(e.sourceCh);
                const QString content = OneLine(QString::fromStdString(st->ColumnText(1)));
                const QString truth = OneLine(QString::fromStdString(st->ColumnText(2)));
                e.excerpt = truth.isEmpty() ? content
                                            : QStringLiteral("%1（真相：%2）").arg(content, truth);
                ++foreshadows;
                entries.push_back(std::move(e));
            }
        }
        // ③ 每部结案摘要（章节摘要按章序）
        if (auto st = prior.Prepare("SELECT ord,title,summary FROM chapters ORDER BY ord")) {
            for (;;) {
                auto s = st->Step();
                if (!s || *s == db::sqlite::StepResult::Done) {
                    break;
                }
                ImportEntry e;
                e.kind = QStringLiteral("event");
                e.sourceCh = static_cast<int>(st->ColumnInt(0));
                e.sourceBook = title;
                e.source = QStringLiteral("(%1, %2)").arg(title).arg(e.sourceCh);
                e.canonical = QStringLiteral("%1·第 %2 章").arg(title).arg(e.sourceCh);
                e.name = e.canonical;
                const QString sum = OneLine(QString::fromStdString(st->ColumnText(2)));
                e.excerpt = sum.isEmpty() ? OneLine(QString::fromStdString(st->ColumnText(1))) : sum;
                ++recaps;
                entries.push_back(std::move(e));
            }
        }
        perBook << QStringLiteral("·《%1》（只读打开）：条目 %2 条（人物终态卡 %3 · 世界观 %4 · "
                                  "已完结伏笔 %5 · 结案摘要 %6）")
                       .arg(title)
                       .arg(persons + worlds + foreshadows + recaps)
                       .arg(persons)
                       .arg(worlds)
                       .arg(foreshadows)
                       .arg(recaps);
    }

    // —— 摘要组装 + 预算护栏（≤ budget 字；超限截断并明说「已截断 N 字」）——
    std::vector<QString> lines;
    lines.reserve(entries.size());
    for (const ImportEntry& e : entries) {
        lines.push_back(QStringLiteral("[%1] %2：%3｜出处 %4")
                            .arg(CategoryOf(e.kind), e.canonical, e.excerpt, e.source));
    }
    int fullChars = 0;
    for (const QString& l : lines) {
        fullChars += l.size() + 1; // + '\n'
    }
    QString summary;
    std::vector<ImportEntry> kept;
    int keptChars = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const int add = lines[i].size() + (summary.isEmpty() ? 0 : 1);
        if (keptChars + add > budget) {
            break;
        }
        if (!summary.isEmpty()) {
            summary += QLatin1Char('\n');
        }
        summary += lines[i];
        keptChars += add;
        kept.push_back(entries[i]);
    }
    const int truncated = fullChars - keptChars;
    const int dropped = static_cast<int>(entries.size() - kept.size());

    // —— 落盘：work/init/prior_import.json（前情摘要 + 可追溯条目）——
    std::error_code ec;
    std::filesystem::create_directories(bookWork_ / "init", ec);
    std::string json = "{\"schema\":1,\"generated_at\":" +
                       util::json::JsonQuote(project::Iso8601UtcNow()) + ",\"budget_chars\":" +
                       std::to_string(budget) + ",\"summary_chars\":" + std::to_string(keptChars) +
                       ",\"truncated_chars\":" + std::to_string(truncated) + ",\"books\":[";
    for (int i = 0; i < bookTitles.size(); ++i) {
        if (i > 0) {
            json += ',';
        }
        json += util::json::JsonQuote(bookTitles[i].toStdString());
    }
    json += "],\"summary_text\":" + util::json::JsonQuote(summary.toStdString()) + ",\"entries\":[";
    for (std::size_t i = 0; i < kept.size(); ++i) {
        const ImportEntry& e = kept[i];
        if (i > 0) {
            json += ',';
        }
        json += "{\"canonical\":" + util::json::JsonQuote(e.canonical.toStdString()) +
                ",\"kind\":" + util::json::JsonQuote(e.kind.toStdString()) + ",\"source\":" +
                util::json::JsonQuote(e.source.toStdString()) + ",\"name\":" +
                util::json::JsonQuote(e.name.toStdString()) + ",\"excerpt\":" +
                util::json::JsonQuote(e.excerpt.toStdString()) + '}';
    }
    json += "]}";
    const bool fileOk = util::WriteFileBytes(PriorContextPath(), json);

    // —— 可追溯条目落本作库（entities + meta_json 身份；幂等）——
    int written = 0;
    QString persistErr;
    const bool dbOk = PersistEntries(kept, &written, &persistErr);
    if (fileOk) {
        importTree_->SetJson(QString::fromStdString(json));
        importStack_->setCurrentWidget(importTree_);
    }

    QString text = QStringLiteral("前情导入：%1 部 → 摘要 %2 字（上限 %3 字）")
                       .arg(bookTitles.size())
                       .arg(keptChars)
                       .arg(budget);
    for (const QString& l : perBook) {
        text += QLatin1Char('\n') + l;
    }
    if (truncated > 0) {
        text += QLatin1Char('\n') +
                QStringLiteral("预算护栏：原文 %1 字 / 上限 %2 字 → 保留 %3 字，已截断 %4 字（丢弃 %5 条条目）")
                    .arg(fullChars)
                    .arg(budget)
                    .arg(keptChars)
                    .arg(truncated)
                    .arg(dropped);
    }
    text += QLatin1Char('\n') +
            QStringLiteral("摘要落盘 work/init/prior_import.json%1；%2 条可追溯条目落本作库"
                           "（出处「(书, 章)」+ canonical 名，meta_json 身份）%3")
                .arg(fileOk ? QStringLiteral(" 成功") : QStringLiteral(" 失败"))
                .arg(written)
                .arg(dbOk ? QStringLiteral(" 成功") : QStringLiteral(" 失败：") + persistErr);
    text += QLatin1Char('\n') + QStringLiteral("前作库只读打开，零写入（探针核对前作行数不变）");
    setReport(text);
    importResult_->setText(text);
    widgets::SetTextColor(importResult_, (fileOk && dbOk && failedBooks == 0) ? theme::Current().textSecondary
                                                                 : theme::Current().statusDanger);
    RebuildImportBooks();
    return fileOk && dbOk && failedBooks == 0;
}

// ————————————————————————————————————————————— 探针

QString InitChainView::StageProbe() const {
    QString out;
    const std::filesystem::path dir = bookRoot_.empty() ? std::filesystem::path{}
                                                        : novelcore::InitDir(bookWork_);
    std::error_code ec;
    int i = 0;
    for (const novelcore::InitStageSpec& s : novelcore::InitStageCatalog()) {
        ++i;
        const QString code = QString::fromStdString(std::string{s.code});
        const QString artifact = QString::fromStdString(std::string{s.artifact});
        const bool done = !dir.empty() && std::filesystem::exists(dir / std::string{s.artifact}, ec);
        out += QStringLiteral("stage I%1|%2|%3|%4|%5|resumable=1\n")
                   .arg(i)
                   .arg(code, QString::fromUtf8(kStageNames[i - 1]), artifact)
                   .arg(done ? QStringLiteral("done") : QStringLiteral("todo"));
    }
    out += QStringLiteral("stages=%1").arg(i);
    return out;
}

QString InitChainView::GateProbe() const {
    QString out;
    int waived = 0;
    int bad = 0;
    for (const GateLine& l : EvaluateGates()) {
        QString verdict;
        if (l.waived) {
            verdict = QStringLiteral("waived");
            ++waived;
        } else if (l.rawPass) {
            verdict = QStringLiteral("pass");
        } else if (l.pending) {
            verdict = QStringLiteral("fail-pending");
            ++bad;
        } else {
            verdict = QStringLiteral("fail");
            ++bad;
        }
        const GateRuleCfg cfg = gateCfg_.value(l.nId);
        out += QStringLiteral("gate %1|%2|mode=%3|approved=%4|verdict=%5|detail=%6|fix=%7\n")
                   .arg(l.nId, l.name, cfg.mode)
                   .arg(cfg.approved ? 1 : 0)
                   .arg(verdict, l.detail, l.fixHint);
    }
    out += QStringLiteral("gates=14\n");
    out += QStringLiteral("gate-overall=%1").arg(bad == 0 ? QStringLiteral("pass") : QStringLiteral("fail"));
    out += QStringLiteral("\ngate-waived=%1").arg(waived);
    return out;
}

QString InitChainView::ImportProbe() const {
    QString out;
    std::optional<std::string> text = util::ReadFileBytes(PriorContextPath());
    if (!text) {
        out += QStringLiteral("import none=1\n");
    } else {
        util::json::OwnedDoc doc = util::json::ParseDoc(*text);
        yyjson_val* root = doc.root();
        out += QStringLiteral("import file=%1\n")
                   .arg(QString::fromStdString(util::PathToUtf8(PriorContextPath())));
        QStringList books;
        if (yyjson_val* arr = util::json::GetArr(root, "books"); arr != nullptr) {
            const std::size_t n = yyjson_arr_size(arr);
            for (std::size_t i = 0; i < n; ++i) {
                yyjson_val* v = yyjson_arr_get(arr, i);
                if (v != nullptr && yyjson_is_str(v)) {
                    books << QString::fromUtf8(yyjson_get_str(v));
                }
            }
        }
        out += QStringLiteral("import books=%1\n").arg(books.join(QLatin1Char(',')));
        out += QStringLiteral("import budget=%1\n").arg(util::json::GetI64(root, "budget_chars"));
        out += QStringLiteral("import summary-chars=%1\n").arg(util::json::GetI64(root, "summary_chars"));
        out += QStringLiteral("import truncated=%1\n").arg(util::json::GetI64(root, "truncated_chars"));
        if (yyjson_val* arr = util::json::GetArr(root, "entries"); arr != nullptr) {
            const std::size_t n = yyjson_arr_size(arr);
            for (std::size_t i = 0; i < n; ++i) {
                yyjson_val* v = yyjson_arr_get(arr, i);
                if (v == nullptr) {
                    continue;
                }
                out += QStringLiteral("entry canonical=%1|kind=%2|source=%3|excerpt=%4\n")
                           .arg(QString::fromStdString(util::json::GetStrCopy(v, "canonical")),
                                QString::fromStdString(util::json::GetStrCopy(v, "kind")),
                                QString::fromStdString(util::json::GetStrCopy(v, "source")),
                                OneLine(QString::fromStdString(util::json::GetStrCopy(v, "excerpt"))));
            }
        }
    }
    // 落库的可追溯条目（entities.meta_json：source_book 非空 = 前情导入条目）
    int dbEntries = 0;
    if (db_) {
        if (auto st = db_->Prepare("SELECT meta_json FROM entities")) {
            for (;;) {
                auto s = st->Step();
                if (!s || *s == db::sqlite::StepResult::Done) {
                    break;
                }
                const MetaIdentity idt = ParseIdentity(st->ColumnText(0));
                if (!idt.sourceBook.empty()) {
                    ++dbEntries;
                }
            }
        }
    }
    out += QStringLiteral("import db-entries=%1").arg(dbEntries);
    return out;
}

// ————————————————————————————————————————————— 内部

std::vector<InitChainView::GateLine> InitChainView::EvaluateGates() const {
    std::map<std::string, novelcore::InitFailure> failed;
    if (db_) {
        const novelcore::InitReport rep = novelcore::CheckInitGate(*db_);
        for (const novelcore::InitFailure& f : rep.failures) {
            failed[f.n_id] = f;
        }
    }
    std::vector<GateLine> lines;
    lines.reserve(std::size(kGates));
    for (const GateInfo& g : kGates) {
        GateLine l;
        l.nId = QString::fromLatin1(g.id);
        l.name = QString::fromUtf8(g.name);
        const auto it = failed.find(g.id);
        if (it == failed.end()) {
            l.rawPass = true;
            l.detail = QStringLiteral("满足");
        } else {
            l.rawPass = false;
            l.detail = QString::fromStdString(it->second.detail);
            l.fixHint = QString::fromStdString(it->second.fix_hint);
        }
        const GateRuleCfg cfg = gateCfg_.value(l.nId);
        if (cfg.mode == QLatin1String("ignore-approved")) {
            if (cfg.approved) {
                l.waived = true; // 人工确认放行（audit_logs 已留痕）
            } else if (!l.rawPass) {
                l.pending = true; // 申请了但没审批：仍按不通过计（不允许警告后放行）
            }
        }
        lines.push_back(std::move(l));
    }
    return lines;
}

void InitChainView::LoadGateConfig() {
    gateCfg_.clear();
    const std::optional<std::string> text = util::ReadFileBytes(GateConfigPath());
    util::json::OwnedDoc doc = text ? util::json::ParseDoc(*text) : util::json::OwnedDoc{};
    yyjson_val* rules = doc ? util::json::GetObj(doc.root(), "rules") : nullptr;
    for (const GateInfo& g : kGates) {
        GateRuleCfg cfg;
        if (yyjson_val* r = util::json::Get(rules, g.id); r != nullptr) {
            const std::string mode = util::json::GetStrCopy(r, "mode");
            cfg.mode = mode == "ignore-approved" ? QStringLiteral("ignore-approved")
                                                 : QStringLiteral("enforce");
            cfg.approved = util::json::GetBool(r, "approved", false);
            cfg.approvedAt = QString::fromStdString(util::json::GetStrCopy(r, "approved_at"));
        }
        gateCfg_.insert(QString::fromLatin1(g.id), cfg);
    }
}

void InitChainView::SaveGateConfig() const {
    if (bookWork_.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(bookWork_, ec);
    std::string json = "{\"schema\":1,\"rules\":{";
    bool first = true;
    for (const GateInfo& g : kGates) {
        const GateRuleCfg cfg = gateCfg_.value(QString::fromLatin1(g.id));
        if (!first) {
            json += ',';
        }
        first = false;
        json += util::json::JsonQuote(g.id);
        json += ":{\"mode\":" + util::json::JsonQuote(cfg.mode.toStdString()) +
                ",\"approved\":" + (cfg.approved ? "true" : "false") + ",\"approved_at\":" +
                util::json::JsonQuote(cfg.approvedAt.toStdString()) + '}';
    }
    json += "}}";
    (void)util::WriteFileBytes(GateConfigPath(), json);
}

std::filesystem::path InitChainView::GateConfigPath() const {
    return bookWork_ / "init_gates.json";
}

std::filesystem::path InitChainView::PriorContextPath() const {
    return bookWork_ / "init" / "prior_import.json";
}

bool InitChainView::PersistEntries(const std::vector<ImportEntry>& entries, int* written,
                                   QString* report) const {
    if (!db_) {
        if (report != nullptr) {
            *report = QStringLiteral("书库没打开");
        }
        return false;
    }
    novelcore::NovelGraph g(*db_);
    const std::expected<std::vector<EntityRow>, novelcore::DbError> existing =
        g.ListEntities({}, {}, 5000);
    int w = 0;
    for (const ImportEntry& e : entries) {
        // 幂等：同名 + 同 source_book 的前情条目不重复落
        bool dup = false;
        if (existing) {
            for (const EntityRow& row : *existing) {
                if (e.name.toStdString() != row.name) {
                    continue;
                }
                const MetaIdentity idt = ParseIdentity(row.meta_json);
                if (idt.sourceBook == e.sourceBook.toStdString()) {
                    dup = true;
                    break;
                }
            }
        }
        if (dup) {
            continue;
        }
        EntityRow row;
        row.kind = e.kind.toStdString();
        row.name = e.name.toStdString();
        row.summary = QStringLiteral("前情摘要（%1）：%2｜出处 %3")
                          .arg(CategoryOf(e.kind), e.excerpt, e.source)
                          .toStdString();
        row.status = "active";
        row.meta_json = IdentityMetaJson(e.canonical.toStdString(), e.sourceBook.toStdString(), e.sourceCh);
        if (auto r = g.UpsertEntity(row); !r) {
            if (report != nullptr) {
                *report = QStringLiteral("落库失败（%1）：%2")
                              .arg(e.canonical, QString::fromStdString(r.error().message));
            }
            return false;
        }
        ++w;
    }
    if (written != nullptr) {
        *written = w;
    }
    return true;
}

// ————————————————————————————————————————————— 刷新

void InitChainView::RebuildStages() {
    std::vector<std::vector<QString>> rows;
    const std::filesystem::path dir = bookRoot_.empty() ? std::filesystem::path{}
                                                        : novelcore::InitDir(bookWork_);
    std::error_code ec;
    int i = 0;
    for (const novelcore::InitStageSpec& s : novelcore::InitStageCatalog()) {
        ++i;
        const bool done = !dir.empty() && std::filesystem::exists(dir / std::string{s.artifact}, ec);
        const qint64 ms = stageMs_.value(QStringLiteral("I%1").arg(i), 0);
        const QString msView = ms > 0 ? QStringLiteral(" · %1 ms").arg(ms) : QString{};
        rows.push_back({QStringLiteral("I%1").arg(i), QString::fromUtf8(kStageNames[i - 1]),
                        QString::fromStdString(std::string{s.code}),
                        QString::fromStdString(std::string{s.artifact}),
                        (done ? QStringLiteral("已落盘（可断点续跑）")
                              : QStringLiteral("未跑（可断点续跑）")) +
                            msView});
    }
    stageTable_->SetRows(rows);
}

void InitChainView::RebuildGates() {
    const std::vector<GateLine> lines = EvaluateGates();
    for (std::size_t i = 0; i < gateRows_.size(); ++i) {
        GateRowUi& row = gateRows_[i];
        const QString nId = row.nId;
        const GateRuleCfg cfg = gateCfg_.value(nId);
        QString modeView;
        if (cfg.mode == QLatin1String("ignore-approved")) {
            modeView = cfg.approved ? QStringLiteral("忽略并人工确认（已审批）")
                                    : QStringLiteral("忽略并人工确认（待审批）");
        } else {
            modeView = QStringLiteral("启用 enforce");
        }
        row.mode->setText(modeView);

        QString verdictText;
        std::uint32_t token = theme::Current().textMuted;
        { // 门禁判定随时可看（EvaluateGates 是纯查询，不依赖是否点过「跑门禁」）
            const GateLine& l = lines[i];
            if (l.waived) {
                verdictText = QStringLiteral("人工确认放行");
                token = theme::Current().statusWarn;
            } else if (l.rawPass) {
                verdictText = QStringLiteral("通过");
                token = theme::Current().statusOk;
            } else if (l.pending) {
                verdictText = QStringLiteral("不通过 · 待人工审批");
                token = theme::Current().statusWarn;
            } else {
                verdictText = QStringLiteral("不通过：%1｜修法：%2").arg(l.detail, l.fixHint);
                token = theme::Current().statusDanger;
            }
            row.name->setToolTip(QStringLiteral("%1 %2　%3").arg(nId, l.name, l.detail));
        }
        row.verdict->SetFullText(verdictText);
        widgets::SetTextColor(row.verdict, token);
        row.ignoreBtn->setEnabled(cfg.mode == QLatin1String("enforce"));
        row.approveBtn->setEnabled(cfg.mode == QLatin1String("ignore-approved") && !cfg.approved);
        row.enforceBtn->setEnabled(cfg.mode != QLatin1String("enforce"));
    }

    // 门禁状态条（UI.md §3「门禁挡住」：列出具体哪条与修复建议；行内就是明细）
    const theme::ColorToken& t = theme::Current();
    if (!db_) {
        gateState_->setText(openError_.isEmpty() ? QStringLiteral("书库没打开 —— 门禁无从判定。下一步：选书后点"
                                                                 "「跑门禁 N1–N14」。")
                                                 : openError_);
        widgets::SetTextColor(gateState_, t.statusWarn);
        return;
    }
    int waived = 0;
    QStringList badIds;
    for (const GateLine& l : lines) {
        if (l.waived) {
            ++waived;
        } else if (!l.rawPass) {
            badIds << l.nId;
        }
    }
    if (badIds.isEmpty()) {
        gateState_->setText(QStringLiteral("门禁通过：可以开写第 1 章（N1–N14 全部满足%1）。")
                                .arg(waived > 0 ? QStringLiteral("，其中 %1 条人工确认放行").arg(waived)
                                                : QString{}));
        widgets::SetTextColor(gateState_, t.statusOk);
    } else {
        gateState_->setText(QStringLiteral("门禁挡住：不能开写第 1 章 —— %1 不满足（每条的实测差额与修法见下行）。"
                                           "修完再点「跑门禁 N1–N14」复判。")
                                .arg(badIds.join(QStringLiteral("、"))));
        widgets::SetTextColor(gateState_, t.statusDanger);
    }
}

void InitChainView::RebuildImportBooks() {
    std::vector<widgets::Select::Item> items;
    if (refStore_) {
        for (const project::BookRef& b : project::ListBooks(*refStore_)) {
            const QString title = QString::fromStdString(b.title);
            if (title == bookTitle_) {
                continue; // 前情导入只列**前作**（非当前书）
            }
            items.push_back({title, QString{}, false});
        }
    }
    priorSel_->SetItems(std::move(items));
}

void InitChainView::ShowArtifact(const QString& fileName) {
    const std::filesystem::path path =
        (bookRoot_.empty() ? std::filesystem::path{} : novelcore::InitDir(bookWork_)) /
        fileName.toStdString();
    const std::optional<std::string> text = util::ReadFileBytes(path);
    if (!text) {
        artifactTree_->SetJson(
            QStringLiteral("{\"状态\":\"该阶段还没落产物\",\"产物\":\"%1\","
                           "\"下一步\":\"I1–I14 由路径 C（AI 分域生成）/ 路径 A（导入）落盘（`10` §2.8 当前落盘范围）；"
                           "I15_gate.json / I16_commit.json 由「一键跑骨架」落盘。\"}")
                .arg(fileName));
        artifactStack_->setCurrentWidget(artifactTree_);
        return;
    }
    artifactTree_->SetJson(QString::fromStdString(*text));
    artifactStack_->setCurrentWidget(artifactTree_);
}

void InitChainView::SetRunning(const QString& step, bool on) {
    runSkeletonBtn_->SetLoading(on);
    runGatesBtn_->SetLoading(on);
    importBtn_->setEnabled(!on);
    if (on) {
        state_->setText(step);
        widgets::SetTextColor(state_, theme::Current().statusBusy);
    }
    QApplication::processEvents();
}

} // namespace shine::app
