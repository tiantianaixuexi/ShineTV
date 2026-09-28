#include "ui/pages/novel/WorldBoardView.h"

#include "ui/pages/novel/WorldBoardShared.h"

#include "db/sqlite/SqliteDb.h"
#include "ui/kit/data/Panels.h"
#include "ui/kit/data/Table.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/layout/QtLayout.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/layout/QtLayout.h"
#include "ui/kit/controls/Inputs.h"
#include "ui/layout/QtLayout.h"
#include "ui/kit/controls/Navigation.h"
#include "ui/layout/QtLayout.h"
#include "ui/kit/controls/Surfaces.h"
#include "ui/layout/QtLayout.h"
#include "novel/NovelDb.h"
#include "novel/NovelFields.h"
#include "novel/NovelGraph.h"
#include "novel/NovelTypes.h"
#include "project/Project.h"
#include "util/Encoding.h"
#include "util/Json.h"

#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSet>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace shine::app {
namespace {

using novelcore::EntityFieldRow;
using novelcore::EntityRow;
using novelcore::FieldDefRow;

// kind / 实体状态标签（31 种元类别 + active|dead|destroyed|archived）上收到
// pages/novel/WorldBoardShared.h（S2/S3 共用），此处只保留字段页专属的小工具。

[[nodiscard]] QString ValueTypeLabelOf(std::string_view vt) {
    if (vt == "number") return QStringLiteral("数字 number");
    if (vt == "json") return QStringLiteral("JSON json");
    if (vt == "enum") return QStringLiteral("枚举 enum");
    return QStringLiteral("文本 text");
}

[[nodiscard]] std::string ValueTypeKeyOfLabel(const QString& label) {
    if (label.contains(QStringLiteral("number"))) return "number";
    if (label.contains(QStringLiteral("json"))) return "json";
    if (label.contains(QStringLiteral("enum"))) return "enum";
    return "text";
}

[[nodiscard]] QString ScopeLabelOf(std::string_view scope) {
    if (scope == "world") return QStringLiteral("世界");
    if (scope == "chapter") return QStringLiteral("章节");
    if (scope == "agent") return QStringLiteral("Agent");
    if (scope == "custom") return QStringLiteral("自定义");
    return QStringLiteral("实体");
}

[[nodiscard]] std::string ScopeKeyOfLabel(const QString& label) {
    if (label == QStringLiteral("世界")) return "world";
    if (label == QStringLiteral("章节")) return "chapter";
    if (label == QStringLiteral("Agent")) return "agent";
    if (label == QStringLiteral("自定义")) return "custom";
    return "entity";
}

// 布局小工具（CssRgb / SectionLabel / ClearLayout / FirstChecked）同在
// pages/novel/WorldBoardShared.h。

// 列表文本 → 选项数组：逗号 / 顿号 / 分号 / 换行分隔（中文输入习惯）
[[nodiscard]] QStringList SplitList(const QString& raw) {
    QStringList out;
    QString buf;
    const auto flush = [&out, &buf] {
        const QString t = buf.trimmed();
        if (!t.isEmpty()) {
            out.push_back(t);
        }
        buf.clear();
    };
    const QString seps = QStringLiteral(",，、;；\n");
    for (const QChar c : raw) {
        if (seps.contains(c)) {
            flush();
        } else {
            buf.push_back(c);
        }
    }
    flush();
    return out;
}

[[nodiscard]] std::string JoinJsonArray(const QStringList& items) {
    std::string out = "[";
    for (int i = 0; i < items.size(); ++i) {
        if (i > 0) {
            out += ',';
        }
        out += util::json::JsonQuote(items[static_cast<qsizetype>(i)].toStdString());
    }
    out += ']';
    return out;
}

[[nodiscard]] bool IsJsonStringArray(std::string_view json, bool requireNonEmpty) {
    util::json::OwnedDoc doc = util::json::ParseDoc(json);
    if (!doc) {
        return false;
    }
    yyjson_val* root = doc.root();
    if (!yyjson_is_arr(root)) {
        return false;
    }
    const std::size_t n = yyjson_arr_size(root);
    if (requireNonEmpty && n == 0) {
        return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
        yyjson_val* v = yyjson_arr_get(root, i);
        if (v == nullptr || !yyjson_is_str(v)) {
            return false;
        }
    }
    return true;
}

// 跨库稳定身份（决策 §7）：meta_json 约定键序固定 {"canonical","aliases","source_book","source_ch"}
[[nodiscard]] std::string BuildMetaJson(std::string_view canonical,
                                        const std::vector<std::string>& aliases,
                                        std::string_view sourceBook, int sourceCh) {
    std::string out = "{\"canonical\":" + util::json::JsonQuote(canonical) + ",\"aliases\":[";
    for (std::size_t i = 0; i < aliases.size(); ++i) {
        if (i > 0) {
            out += ',';
        }
        out += util::json::JsonQuote(aliases[i]);
    }
    out += "],\"source_book\":" + util::json::JsonQuote(sourceBook);
    out += ",\"source_ch\":" + std::to_string(sourceCh) + '}';
    return out;
}

[[nodiscard]] bool MetaGet(yyjson_val* root, std::string_view key, std::string& out) {
    yyjson_val* v = util::json::Get(root, key);
    if (v == nullptr || !yyjson_is_str(v)) {
        return false;
    }
    out = std::string{yyjson_get_str(v), yyjson_get_len(v)};
    return true;
}

} // namespace

WorldBoardView::WorldBoardView(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    // 页面级留白 / 分区间距统一走 layout helper（对齐 webui .vw：20 24 26 / gap 16）
    util::PageMargins(outer);
    util::PageSpacing(outer);

    // 页签（UI.md §2.2）：[实体][关系][剧情线][伏笔][谜][字段]
    tabs_ = new widgets::Tabs({QStringLiteral("实体"), QStringLiteral("关系"), QStringLiteral("剧情线"),
                               QStringLiteral("伏笔"), QStringLiteral("谜"), QStringLiteral("字段")},
                              this);
    pages_ = new QStackedWidget(this);
    pages_->addWidget(BuildEntitiesPage());   // 0 实体（S2）
    pages_->addWidget(BuildRelationsPage());  // 1 关系（S3）
    pages_->addWidget(BuildPlotsPage());      // 2 剧情线（S3）
    pages_->addWidget(BuildForeshadowPage()); // 3 伏笔（S3）
    pages_->addWidget(BuildMysteryPage());    // 4 谜 + 知情边界矩阵（S3）
    pages_->addWidget(BuildFieldsPage());     // 5 字段（S2）
    tabs_->SetOnChanged([this](int idx) { pages_->setCurrentIndex(idx); });

    outer->addWidget(tabs_);
    outer->addWidget(pages_, 1);
}

WorldBoardView::~WorldBoardView() {
    if (drawer_ != nullptr) {
        drawer_->close();
    }
    CloseDb();
}
void WorldBoardView::ShowPage(int index) {
    if (pages_ != nullptr && index >= 0 && index < pages_->count()) {
        pages_->setCurrentIndex(index);
    }
}

// ————————————————————————————————————————————— 数据接入

void WorldBoardView::LoadFromRef(const project::ProjectRef& ref, const std::string& lastNovel) {
    CloseDb();
    refStore_ = std::make_unique<project::ProjectRef>(ref);
    lastNovel_ = lastNovel;
    bookTitle_.clear();
    bookDbPath_.clear();
    openError_.clear();

    const std::vector<project::BookRef> books = project::ListBooks(ref);
    const project::BookRef* pick = nullptr;
    for (const project::BookRef& b : books) { // 按 ui.lastNovel 选书（书 = 一部一库）
        if (!lastNovel.empty() && b.title == lastNovel) {
            pick = &b;
            break;
        }
    }
    if (pick == nullptr && !books.empty()) {
        pick = &books.front(); // 默认书在前；lastNovel 空（或书已不在）→ 默认书
    }
    if (pick == nullptr) {
        RefreshEntities();
        RefreshFields();
        RefreshRelations();
        RefreshPlots();
        RefreshForeshadows();
        RefreshMysteries();
        return;
    }
    db_ = std::make_unique<db::sqlite::Database>();
    if (auto r = db_->Open({.path = pick->dbPath}); !r) {
        openError_ = QStringLiteral("库文件打不开：%1（%2）。请确认磁盘可写、文件未被其他程序独占，然后点「重试」。")
                         .arg(QString::fromStdString(util::PathToUtf8(pick->dbPath)),
                              QString::fromStdString(r.error().message));
        db_.reset();
    }
    bookTitle_ = QString::fromStdString(pick->title);
    bookDbPath_ = pick->dbPath;
    RefreshEntities();
    RefreshFields();
    RefreshRelations();
    RefreshPlots();
    RefreshForeshadows();
    RefreshMysteries();
}

void WorldBoardView::CloseDb() noexcept {
    db_.reset(); // 关库（探针的「关库重开」：下次 LoadFromRef 开新 Database 实例）
}

// ————————————————————————————————————————————— 公开 API

bool WorldBoardView::CreateEntity(const QString& name, const QString& kind, const QString& summary,
                                  const QString& canonical, const QStringList& aliases,
                                  const QString& sourceBook, int sourceCh, QString* err,
                                  const QString& status) {
    const auto fail = [err](const QString& why) {
        if (err != nullptr) {
            *err = why;
        }
        return false;
    };
    if (!db_) {
        return fail(QStringLiteral("库未打开：当前书没有可写的 db/novel.db（先建书库再建实体）。"));
    }
    const QString name2 = name.trimmed();
    if (name2.isEmpty()) {
        return fail(QStringLiteral("名称：不能为空——实体卡、名称过滤与跨库引用都以名称为准。"));
    }
    const std::string kindStd = kind.trimmed().toStdString();
    if (!IsKnownKind(kindStd)) {
        return fail(QStringLiteral("kind：'%1' 不在 31 种元类别内（person / location / item …，见 `01` §2.1）；"
                                   "设定细节用动态字段表达，不新增 kind。")
                        .arg(kind));
    }
    const std::string statusStd = status.isEmpty() ? "active" : status.trimmed().toStdString();
    if (statusStd != "active" && statusStd != "dead" && statusStd != "destroyed" &&
        statusStd != "archived") {
        return fail(QStringLiteral("状态：'%1' 不是 active|dead|destroyed|archived。").arg(status));
    }
    if (sourceCh < 0) {
        return fail(QStringLiteral("出处章序：不能为负（0 = 未指定章）。"));
    }
    std::vector<std::string> aliasStd;
    for (const QString& a : aliases) {
        const QString t = a.trimmed();
        if (!t.isEmpty()) {
            aliasStd.push_back(t.toStdString());
        }
    }

    EntityRow row;
    row.kind = kindStd;
    row.name = name2.toStdString();
    row.summary = summary.trimmed().toStdString();
    row.status = statusStd;
    // 决策 §7：跨库稳定身份全走 meta_json（不改 entities 表结构）
    row.meta_json = BuildMetaJson(canonical.trimmed().toStdString(), aliasStd,
                                  sourceBook.trimmed().toStdString(), sourceCh);

    novelcore::NovelGraph g(*db_);
    auto id = g.UpsertEntity(row);
    if (!id) {
        return fail(QStringLiteral("落库失败：%1（修正后可重试）")
                        .arg(QString::fromStdString(id.error().message)));
    }
    RefreshEntities();
    return true;
}

bool WorldBoardView::RefreshEntities() {
    RebuildKindBadges();
    RebuildCards();
    return db_ != nullptr;
}

bool WorldBoardView::RegisterField(const QString& fieldKey, const QString& scope,
                                   const QString& entityKind, const QString& title,
                                   const QString& valueType, const QString& enumJson,
                                   const QString& description, QString* err) {
    const auto fail = [err](const QString& why) {
        if (err != nullptr) {
            *err = why;
        }
        return false;
    };
    if (!db_) {
        return fail(QStringLiteral("库未打开：当前书没有可写的 db/novel.db。"));
    }
    novelcore::NovelFields fields(*db_);

    // —— 三步校验 ① 别名解析（`08` §2.2 ① / §2.4）——
    const std::string raw = fieldKey.trimmed().toStdString();
    const std::string key = novelcore::NovelFields::NormalizeKey(raw);
    if (!novelcore::NovelFields::IsValidKey(key)) {
        return fail(QStringLiteral("键名不合法：'%1' 归一后为 '%2'，须满足 ^[a-z][a-z0-9_]{{1,39}}$"
                                   "（小写字母开头、2–40 位小写/数字/下划线）。")
                        .arg(fieldKey, QString::fromStdString(key)));
    }
    auto resolved = fields.ResolveAlias(key);
    if (!resolved) {
        return fail(QStringLiteral("别名无法解析：读别名表失败（%1）。")
                        .arg(QString::fromStdString(resolved.error().message)));
    }
    auto defs = fields.ListFieldDefs();
    if (!defs) {
        return fail(QStringLiteral("落库失败：读 field_defs 失败（%1）")
                        .arg(QString::fromStdString(defs.error().message)));
    }
    const auto findDef = [&defs](const std::string& k) -> const FieldDefRow* {
        const auto it = std::ranges::find_if(*defs, [&k](const FieldDefRow& d) {
            return d.field_key == k;
        });
        return it == defs->end() ? nullptr : &*it;
    };
    if (*resolved != key) {
        // 命中别名表：规范键必须已登记 —— 悬空别名 = 语义漂移，登记前先拦（`08` §2.4）
        const FieldDefRow* canonicalDef = findDef(*resolved);
        if (canonicalDef == nullptr) {
            return fail(QStringLiteral("别名无法解析：'%1' 是 '%2' 的别名，但规范键 '%2' 未在 field_defs 登记"
                                       "（`08` §2.4）。请先登记规范键 '%2'，或换一个不与别名表冲突的键名。")
                            .arg(fieldKey, QString::fromStdString(*resolved)));
        }
        return fail(QStringLiteral("重复键：'%1' 经别名解析为已登记字段 '%2'（id=%3），新建登记不覆盖既有键。")
                        .arg(fieldKey, QString::fromStdString(*resolved))
                        .arg(canonicalDef->id));
    }

    // —— 三步校验 ② 类型合法（`08` §2.2 ③）——
    const std::string vt = valueType.trimmed().toStdString();
    if (vt != "text" && vt != "number" && vt != "json" && vt != "enum") {
        return fail(QStringLiteral("类型非法：'%1' 不在 text|number|json|enum 内（`08` §2.2 ③）。"
                                   "请选择四种受检类型之一——字段定义是契约，值才自由。")
                        .arg(valueType));
    }
    std::string enumStd = enumJson.trimmed().toStdString();
    if (vt == "enum") {
        if (!IsJsonStringArray(enumStd, true)) {
            return fail(QStringLiteral("枚举值非法：value_type=enum 须给出非空 JSON 字符串数组"
                                       "（如 [\"a\",\"b\"]）；请检查枚举值填写。"));
        }
    } else {
        enumStd = "[]";
    }

    // —— 三步校验 ③ 新建语义 + 键数上限（`08` §2.3）——
    const std::string scopeStd = scope.isEmpty() ? "entity" : scope.trimmed().toStdString();
    const std::string kindStd = entityKind.trimmed().toStdString();
    const FieldDefRow* existing = findDef(key);
    if (existing != nullptr && existing->scope == scopeStd && existing->entity_kind == kindStd) {
        return fail(QStringLiteral("重复键：'%1'（scope=%2）已登记 id=%3。新建登记不覆盖既有键；"
                                   "要改定义请在下方列表核对后处理。")
                        .arg(fieldKey, scope)
                        .arg(existing->id));
    }
    if (static_cast<int>(defs->size()) >= novelcore::NovelFields::kMaxFieldDefs) {
        return fail(QStringLiteral("键数上限：field_defs 已有 %1 条，达到单工程上限 %2（`08` §2.3）。"
                                   "请先复核并合并低频键（进人工复核清单）再登记新键。")
                        .arg(static_cast<int>(defs->size()))
                        .arg(novelcore::NovelFields::kMaxFieldDefs));
    }

    FieldDefRow row;
    row.scope = scopeStd;
    row.entity_kind = kindStd;
    row.field_key = key;
    row.title = title.trimmed().isEmpty() ? key : title.trimmed().toStdString();
    row.value_type = vt;
    row.enum_json = enumStd;
    row.description = description.trimmed().toStdString();
    row.created_by = "human:ui";
    row.is_system = 0;
    row.status = "CANON"; // `08` §2.3：人工可显式传 CANON（AI 提案才是 PROPOSED）
    auto id = fields.UpsertFieldDef(row);
    if (!id) {
        return fail(QStringLiteral("落库失败：%1（修正后可重试）")
                        .arg(QString::fromStdString(id.error().message)));
    }
    RefreshFields();
    return true;
}

bool WorldBoardView::SaveEntityField(qint64 entityId, const QString& fieldKey,
                                     const QString& valueText, const QString& valueJson,
                                     const QString& layer, const QString& note, QString* err) {
    const auto fail = [err](const QString& why) {
        if (err != nullptr) {
            *err = why;
        }
        return false;
    };
    if (!db_) {
        return fail(QStringLiteral("库未打开：当前书没有可写的 db/novel.db。"));
    }
    novelcore::NovelFields fields(*db_);
    EntityFieldRow row;
    row.entity_id = entityId;
    row.field_key = fieldKey.toStdString();
    row.value_text = valueText.toStdString();
    row.value_json = valueJson.toStdString();
    row.layer = layer.isEmpty() ? "global" : layer.toStdString();
    row.note = note.toStdString();
    row.created_by = "human:ui";
    auto id = fields.UpsertEntityField(row);
    if (!id) {
        return fail(QStringLiteral("字段值落库失败：%1（未登记的键先到「字段」页登记）")
                        .arg(QString::fromStdString(id.error().message)));
    }
    return true;
}

bool WorldBoardView::RefreshFields() {
    if (fieldTable_ == nullptr) {
        return false;
    }
    if (!db_) {
        fieldTable_->SetRows({});
        fieldCount_->SetFullText(QStringLiteral("未打开小说库"));
        return false;
    }
    novelcore::NovelFields fields(*db_);
    auto defs = fields.ListFieldDefs();
    if (!defs) {
        fieldCount_->SetFullText(QStringLiteral("读取失败：%1")
                                 .arg(QString::fromStdString(defs.error().message)));
        return false;
    }
    std::vector<std::vector<QString>> rows;
    rows.reserve(defs->size());
    for (const FieldDefRow& d : *defs) {
        rows.push_back({QString::fromStdString(d.field_key), QString::fromStdString(d.title),
                        ValueTypeLabelOf(d.value_type), ScopeLabelOf(d.scope),
                        d.entity_kind.empty() ? QStringLiteral("不限")
                                              : KindLabelOf(d.entity_kind),
                        d.status == "CANON" ? QStringLiteral("已定 CANON")
                                            : QStringLiteral("待审 PROPOSED")});
    }
    fieldTable_->SetRows(rows);
    fieldCount_->SetFullText(
        QStringLiteral("已登记 %1 / %2 条（上限 `08` §2.3；PROPOSED 由检查点升格）")
            .arg(static_cast<int>(defs->size()))
            .arg(novelcore::NovelFields::kMaxFieldDefs));
    return true;
}

QString WorldBoardView::BookProbe() const {
    return QStringLiteral("book=%1|db=%2")
        .arg(bookTitle_, QString::fromStdString(util::PathToUtf8(bookDbPath_)));
}

int WorldBoardView::EntityCount(const QString& kind) const {
    if (!db_) {
        return 0;
    }
    std::string sql = "SELECT COUNT(*) FROM entities";
    if (!kind.isEmpty()) {
        sql += " WHERE kind=?1";
    }
    auto st = db_->Prepare(sql);
    if (!st) {
        return 0;
    }
    if (!kind.isEmpty()) {
        (void)st->BindText(1, kind.toStdString());
    }
    if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
        return static_cast<int>(st->ColumnInt(0));
    }
    return 0;
}

// ————————————————————————————————————————————— 页面搭建

QWidget* WorldBoardView::BuildEntitiesPage() {
    auto* page = new QWidget(this);
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(theme::space::kSteps[2]);

    // 顶部行：名称过滤 + 计数 + [+ 新建]
    auto* top = new QWidget(page);
    auto* tr = new QHBoxLayout(top);
    tr->setContentsMargins(0, 0, 0, 0);
    tr->setSpacing(theme::space::kSteps[2]);
    search_ = new widgets::SearchBox(top);
    search_->setMinimumWidth(240);
    search_->SetPlaceholder(QStringLiteral("按名称过滤实体（防抖 200ms，Esc 清空）"));
    search_->SetOnSearch([this](const QString& text) {
        nameFilter_ = text.trimmed();
        RefreshEntities();
    });
    entityCount_ = new QLabel(QStringLiteral("实体 0 条"), top);
    auto* newBtn = new widgets::Button(QStringLiteral("+ 新建"), widgets::Button::Variant::Primary,
                                       widgets::Button::Size::Sm, top);
    connect(newBtn, &widgets::Button::clicked, this, [this, newBtn] {
        const bool show = createForm_ != nullptr && !createForm_->isVisible();
        ToggleCreateForm(show);
        newBtn->setText(show ? QStringLiteral("收起表单") : QStringLiteral("+ 新建"));
    });
    tr->addWidget(search_, 1);
    tr->addStretch(1);
    tr->addWidget(newBtn, 0);
    col->addWidget(top);

    // kind 计数徽标行：全部 + 31 kind（点选过滤，再点取消）
    auto* badgeHost = new QWidget(page);
    auto* badges = new QGridLayout(badgeHost);
    badges->setContentsMargins(0, 0, 0, 0);
    badges->setHorizontalSpacing(theme::space::kSteps[1]);
    badges->setVerticalSpacing(theme::space::kSteps[1]);
    kindBtns_.clear();
    kindBtns_.reserve(1 + std::size(kKinds));
    for (int i = 0; i <= static_cast<int>(std::size(kKinds)); ++i) {
        auto* btn = new widgets::Button(i == 0 ? QStringLiteral("全部 0") : QStringLiteral("…"),
                                        widgets::Button::Variant::Ghost,
                                        widgets::Button::Size::Sm, badgeHost);
        btn->setCheckable(true);
        connect(btn, &widgets::Button::clicked, this, [this, i] {
            if (i == 0) {
                kindFilter_.clear();
            } else {
                const QString key = QString::fromStdString(std::string{kKinds[i - 1].key});
                kindFilter_ = (kindFilter_ == key) ? QString{} : key; // 再点一次 = 取消过滤
            }
            RefreshEntities();
        });
        badges->addWidget(btn, i / 8, i % 8);
        kindBtns_.push_back(btn);
    }
    badges->setRowStretch((static_cast<int>(std::size(kKinds)) + 1) / 8 + 1, 1);
    col->addWidget(badgeHost);

    // [+ 新建] 表单（名称 / kind / 摘要 / 状态 / canonical / 别名 / 出处书名 / 出处章序）
    // 卡片壳：表单本身是一大摞裸控件，套一层 SectionCard 才有「哪块是哪块」的边界
    auto* create_card = new widgets::SectionCard(QStringLiteral("新建实体"), page);
    create_card->SetSubtitle(QStringLiteral("名称 / kind / 摘要 / 跨库身份"));
    create_card->SetContentMinWidth(640);
    createForm_ = create_card; // 变量类型不变（QWidget*），显隐路径照旧
    auto* form = create_card->BodyLayout();

    fName_ = new widgets::TextInput(createForm_);
    fName_->SetPlaceholder(QStringLiteral("如：苏黎（必填）"));
    fwName_ = new widgets::Field(QStringLiteral("名称"), widgets::Field::LabelPos::Left, fName_,
                                 createForm_);
    fwName_->SetHelp(QStringLiteral("实体卡与名称过滤的主键显示名"));
    form->addWidget(fwName_);

    fKind_ = new widgets::Select(false, false, createForm_);
    {
        std::vector<widgets::Select::Item> items;
        for (const KindInfo& k : kKinds) {
            items.push_back({QString::fromUtf8(k.label), QString::fromUtf8(k.group),
                             std::string{k.key} == "person"});
        }
        fKind_->SetItems(std::move(items));
        fKind_->SetPlaceholder(QStringLiteral("选择 kind（31 种元类别）"));
    }
    fwKind_ = new widgets::Field(QStringLiteral("kind"), widgets::Field::LabelPos::Left, fKind_,
                                 createForm_);
    fwKind_->SetHelp(QStringLiteral("元类别（人物/地点/物品…）；设定细节用动态字段，不新增 kind"));
    form->addWidget(fwKind_);

    fSummary_ = new widgets::TextArea(500, createForm_);
    fSummary_->Edit()->setPlaceholderText(
        QStringLiteral("一句话摘要：这个设定在故事里是谁 / 在哪 / 干什么"));
    fwSummary_ = new widgets::Field(QStringLiteral("摘要"), widgets::Field::LabelPos::Left,
                                    fSummary_, createForm_);
    form->addWidget(fwSummary_);

    fStatus_ = new widgets::Select(false, false, createForm_);
    fStatus_->SetItems({{QStringLiteral("活跃"), QStringLiteral("状态"), true},
                        {QStringLiteral("已故"), QStringLiteral("状态"), false},
                        {QStringLiteral("已毁"), QStringLiteral("状态"), false},
                        {QStringLiteral("归档"), QStringLiteral("状态"), false}});
    fwStatus_ = new widgets::Field(QStringLiteral("状态"), widgets::Field::LabelPos::Left, fStatus_,
                                   createForm_);
    fwStatus_->SetHelp(QStringLiteral("active / dead / destroyed / archived"));
    form->addWidget(fwStatus_);

    fCanonical_ = new widgets::TextInput(createForm_);
    fCanonical_->SetPlaceholder(QStringLiteral("可空 = 用名称；系列级稳定名（跨库引用用）"));
    auto* fwCanonical = new widgets::Field(QStringLiteral("canonical 名"),
                                           widgets::Field::LabelPos::Left, fCanonical_, createForm_);
    fwCanonical->SetHelp(QStringLiteral("跨库稳定身份（决策 §7）：RowId 跨书撞号，引用一律认 canonical + 出处"));
    form->addWidget(fwCanonical);

    fAliases_ = new widgets::TextInput(createForm_);
    fAliases_->SetPlaceholder(QStringLiteral("逗号 / 顿号分隔，如：小黎，苏家丫头"));
    auto* fwAliases = new widgets::Field(QStringLiteral("别名"), widgets::Field::LabelPos::Left,
                                         fAliases_, createForm_);
    form->addWidget(fwAliases);

    fSourceBook_ = new widgets::TextInput(createForm_);
    fSourceBook_->SetPlaceholder(QStringLiteral("出处书名，如：灯语回声"));
    auto* fwSourceBook = new widgets::Field(QStringLiteral("出处书名"),
                                            widgets::Field::LabelPos::Left, fSourceBook_, createForm_);
    form->addWidget(fwSourceBook);

    fSourceCh_ = new widgets::NumberInput(false, 0, 999999, createForm_);
    fSourceCh_->SetValue(0);
    fwSourceCh_ = new widgets::Field(QStringLiteral("出处章序"), widgets::Field::LabelPos::Left,
                                     fSourceCh_, createForm_);
    fwSourceCh_->SetHelp(QStringLiteral("出处「(书, 章)」里的章序；0 = 未指定"));
    form->addWidget(fwSourceCh_);

    auto* formBtns = new QWidget(createForm_);
    auto* fbr = new QHBoxLayout(formBtns);
    fbr->setContentsMargins(0, 0, 0, 0);
    fbr->addStretch(1);
    auto* cancel = new widgets::Button(QStringLiteral("取消"), widgets::Button::Variant::Ghost,
                                       widgets::Button::Size::Md, formBtns);
    auto* submit = new widgets::Button(QStringLiteral("创建并落库"), widgets::Button::Variant::Primary,
                                       widgets::Button::Size::Md, formBtns);
    connect(cancel, &widgets::Button::clicked, this, [this, newBtn] {
        ToggleCreateForm(false);
        newBtn->setText(QStringLiteral("+ 新建"));
    });
    connect(submit, &widgets::Button::clicked, this, &WorldBoardView::SubmitCreateEntity);
    fbr->addWidget(cancel);
    fbr->addWidget(submit);
    form->addWidget(formBtns);
    createForm_->setVisible(false);
    col->addWidget(createForm_);

    // 实体卡网格
    auto* cards_card = new widgets::SectionCard(QStringLiteral("实体卡"), page);
    cards_card->SetSubtitle(QStringLiteral("点卡看详情；空态给下一步"));
    cards_card->SetContentMinWidth(560);
    entityCount_->setParent(cards_card); // 计数挪进标题栏右侧
    cards_card->BodyLayout()->addWidget(entityCount_);
    auto* scroll = new QScrollArea(cards_card);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumHeight(280);
    cardsHost_ = new QWidget(scroll);
    cardsGrid_ = new QGridLayout(cardsHost_);
    cardsGrid_->setContentsMargins(0, 0, 0, 0);
    cardsGrid_->setSpacing(theme::space::kSteps[3]);
    scroll->setWidget(cardsHost_);
    cards_card->BodyLayout()->addWidget(scroll, 1);
    col->addWidget(cards_card, 1);
    return page;
}

QWidget* WorldBoardView::BuildFieldsPage() {
    auto* page = new QWidget(this);
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(theme::space::kSteps[2]);

    auto* top = new QWidget(page);
    auto* tr = new QHBoxLayout(top);
    tr->setContentsMargins(0, 0, 0, 0);
    tr->setSpacing(theme::space::kSteps[2]);
    auto* newBtn = new widgets::Button(QStringLiteral("+ 新建登记"), widgets::Button::Variant::Primary,
                                       widgets::Button::Size::Sm, top);
    connect(newBtn, &widgets::Button::clicked, this, [this, newBtn] {
        const bool show = registerForm_ != nullptr && !registerForm_->isVisible();
        ToggleRegisterForm(show);
        newBtn->setText(show ? QStringLiteral("收起表单") : QStringLiteral("+ 新建登记"));
    });
    fieldCount_ = new widgets::ElidedLabel(QStringLiteral("已登记 0 条"), top);
    fieldCount_->SetExpandable(false);
    widgets::SetKind(fieldCount_, "fieldhelp");
    tr->addWidget(newBtn);
    tr->addStretch(1);
    tr->addWidget(fieldCount_, 1);
    col->addWidget(top);

    // 新建登记表单（三步校验拦下时中文原因进 Field 错误态）
    auto* register_card = new widgets::SectionCard(QStringLiteral("新建字段登记"), page);
    register_card->SetSubtitle(QStringLiteral("三步校验：键名 → 类型 → 作用域"));
    register_card->SetContentMinWidth(620);
    registerForm_ = register_card; // 变量类型不变，显隐路径照旧
    auto* form = register_card->BodyLayout();

    gKey_ = new widgets::TextInput(registerForm_);
    gKey_->SetPlaceholder(QStringLiteral("如：true_identity"));
    gwKey_ = new widgets::Field(QStringLiteral("键名"), widgets::Field::LabelPos::Left, gKey_,
                                registerForm_);
    gwKey_->SetHelp(QStringLiteral("归一后须满足 ^[a-z][a-z0-9_]{1,39}$；与别名表冲突会被拦（`08` §2.4）"));
    form->addWidget(gwKey_);

    gScope_ = new widgets::Select(false, false, registerForm_);
    gScope_->SetItems({{QStringLiteral("实体"), QStringLiteral("作用域"), true},
                       {QStringLiteral("世界"), QStringLiteral("作用域"), false},
                       {QStringLiteral("章节"), QStringLiteral("作用域"), false},
                       {QStringLiteral("Agent"), QStringLiteral("作用域"), false},
                       {QStringLiteral("自定义"), QStringLiteral("作用域"), false}});
    gwScope_ = new widgets::Field(QStringLiteral("作用域"), widgets::Field::LabelPos::Left, gScope_,
                                  registerForm_);
    form->addWidget(gwScope_);

    gEntityKind_ = new widgets::Select(false, false, registerForm_);
    {
        std::vector<widgets::Select::Item> items;
        items.push_back({QStringLiteral("不限（全部 kind）"), QStringLiteral("限定"), true});
        for (const KindInfo& k : kKinds) {
            items.push_back({QString::fromUtf8(k.label), QString::fromUtf8(k.group), false});
        }
        gEntityKind_->SetItems(std::move(items));
    }
    gwEntityKind_ = new widgets::Field(QStringLiteral("限定 kind"),
                                       widgets::Field::LabelPos::Left, gEntityKind_, registerForm_);
    gwEntityKind_->SetHelp(QStringLiteral("只对某类实体生效；不限 = 全部 kind 可用"));
    form->addWidget(gwEntityKind_);

    gTitle_ = new widgets::TextInput(registerForm_);
    gTitle_->SetPlaceholder(QStringLiteral("展示名，如：真实身份（可空 = 用键名）"));
    form->addWidget(new widgets::Field(QStringLiteral("展示名"), widgets::Field::LabelPos::Left,
                                       gTitle_, registerForm_));

    gType_ = new widgets::Select(false, false, registerForm_);
    gType_->SetItems({{QStringLiteral("文本 text"), QStringLiteral("类型"), true},
                      {QStringLiteral("数字 number"), QStringLiteral("类型"), false},
                      {QStringLiteral("JSON json"), QStringLiteral("类型"), false},
                      {QStringLiteral("枚举 enum"), QStringLiteral("类型"), false}});
    gwType_ = new widgets::Field(QStringLiteral("类型"), widgets::Field::LabelPos::Left, gType_,
                                 registerForm_);
    gwType_->SetHelp(QStringLiteral("四种受检类型之一（`08` §2.2 ③）；值自由、定义收口"));
    form->addWidget(gwType_);

    gEnum_ = new widgets::TextInput(registerForm_);
    gEnum_->SetPlaceholder(QStringLiteral("类型=枚举时必填：逗号分隔选项，如 真身，化身"));
    gwEnum_ = new widgets::Field(QStringLiteral("枚举值"), widgets::Field::LabelPos::Left, gEnum_,
                                 registerForm_);
    form->addWidget(gwEnum_);

    gDesc_ = new widgets::TextInput(registerForm_);
    gDesc_->SetPlaceholder(QStringLiteral("一句话说明这个字段记什么"));
    form->addWidget(new widgets::Field(QStringLiteral("说明"), widgets::Field::LabelPos::Left,
                                       gDesc_, registerForm_));

    auto* formBtns = new QWidget(registerForm_);
    auto* fbr = new QHBoxLayout(formBtns);
    fbr->setContentsMargins(0, 0, 0, 0);
    fbr->addStretch(1);
    auto* cancel = new widgets::Button(QStringLiteral("取消"), widgets::Button::Variant::Ghost,
                                       widgets::Button::Size::Md, formBtns);
    auto* submit = new widgets::Button(QStringLiteral("登记"), widgets::Button::Variant::Primary,
                                       widgets::Button::Size::Md, formBtns);
    connect(cancel, &widgets::Button::clicked, this, [this, newBtn] {
        ToggleRegisterForm(false);
        newBtn->setText(QStringLiteral("+ 新建登记"));
    });
    connect(submit, &widgets::Button::clicked, this, &WorldBoardView::SubmitRegisterField);
    fbr->addWidget(cancel);
    fbr->addWidget(submit);
    form->addWidget(formBtns);
    registerForm_->setVisible(false);
    col->addWidget(registerForm_);

    // field_defs 列表（key / 类型 / 状态 …）
    auto* table_card = new widgets::SectionCard(QStringLiteral("已登记字段"), page);
    table_card->SetSubtitle(QStringLiteral("`08` §2.4 登记表"));
    table_card->SetContentMinWidth(730); // 6 列合计约 730
    fieldTable_ = new data::DataTable(QStringLiteral("worldboard.field_defs"), table_card);
    fieldTable_->SetColumns({{QStringLiteral("key"), QStringLiteral("字段键"), 180},
                             {QStringLiteral("title"), QStringLiteral("展示名"), 140},
                             {QStringLiteral("type"), QStringLiteral("类型"), 110},
                             {QStringLiteral("scope"), QStringLiteral("作用域"), 80},
                             {QStringLiteral("kind"), QStringLiteral("限定 kind"), 100},
                             {QStringLiteral("status"), QStringLiteral("状态"), 120}});
    fieldTable_->SetSelectable(data::DataTable::Select::Single);
    table_card->BodyLayout()->addWidget(fieldTable_, 1);
    col->addWidget(table_card, 1);
    return page;
}

// ————————————————————————————————————————————— 交互

void WorldBoardView::ToggleCreateForm(bool show) {
    if (createForm_ != nullptr) {
        createForm_->setVisible(show);
        if (show) {
            ClearFormErrors();
            fName_->Edit()->setFocus();
        }
    }
}

void WorldBoardView::ToggleRegisterForm(bool show) {
    if (registerForm_ != nullptr) {
        registerForm_->setVisible(show);
        if (show) {
            ClearFormErrors();
            gKey_->Edit()->setFocus();
        }
    }
}

void WorldBoardView::ClearFormErrors() {
    for (widgets::Field* f : {fwName_, fwKind_, fwStatus_, fwSummary_, fwSourceCh_, gwKey_,
                              gwScope_, gwEntityKind_, gwType_, gwEnum_}) {
        if (f != nullptr) {
            f->SetError(QString{});
        }
    }
}

void WorldBoardView::SubmitCreateEntity() {
    ClearFormErrors();
    QString err;
    const bool ok =
        CreateEntity(fName_->Text(), QString::fromStdString(KindKeyOfLabel(FirstChecked(*fKind_))),
                     fSummary_->Text(), fCanonical_->Text(), SplitList(fAliases_->Text()),
                     fSourceBook_->Text(), static_cast<int>(fSourceCh_->Value()), &err,
                     QString::fromStdString(StatusKeyOfLabel(FirstChecked(*fStatus_))));
    if (ok) {
        widgets::Toast::Show(QStringLiteral("实体「%1」已落库").arg(fName_->Text().trimmed()),
                             widgets::Toast::Tone::Success);
        fName_->SetText(QString{});
        fSummary_->SetText(QString{});
        ToggleCreateForm(false);
        return;
    }
    // 中文原因进 Field 错误态（按消息前缀路由到对应表单行）
    if (err.startsWith(QStringLiteral("kind"))) {
        fwKind_->SetError(err);
    } else if (err.startsWith(QStringLiteral("状态"))) {
        fwStatus_->SetError(err);
    } else if (err.startsWith(QStringLiteral("摘要"))) {
        fwSummary_->SetError(err);
    } else if (err.startsWith(QStringLiteral("出处章序"))) {
        fwSourceCh_->SetError(err);
    } else {
        fwName_->SetError(err);
    }
    widgets::Toast::Show(QStringLiteral("新建实体被拦下：见表单红字原因"),
                         widgets::Toast::Tone::Error);
}

void WorldBoardView::SubmitRegisterField() {
    ClearFormErrors();
    const std::string vt = ValueTypeKeyOfLabel(FirstChecked(*gType_));
    QString enumJson;
    if (vt == "enum") {
        enumJson = QString::fromStdString(JoinJsonArray(SplitList(gEnum_->Text())));
    }
    QString err;
    const bool ok =
        RegisterField(gKey_->Text(),
                      QString::fromStdString(ScopeKeyOfLabel(FirstChecked(*gScope_))),
                      QString::fromStdString(KindKeyOfLabel(FirstChecked(*gEntityKind_))),
                      gTitle_->Text(), QString::fromStdString(vt), enumJson, gDesc_->Text(), &err);
    if (ok) {
        widgets::Toast::Show(
            QStringLiteral("字段键 '%1' 已登记（CANON）")
                .arg(QString::fromStdString(novelcore::NovelFields::NormalizeKey(
                    gKey_->Text().trimmed().toStdString()))),
            widgets::Toast::Tone::Success);
        gKey_->SetText(QString{});
        gTitle_->SetText(QString{});
        gEnum_->SetText(QString{});
        gDesc_->SetText(QString{});
        ToggleRegisterForm(false);
        return;
    }
    if (err.startsWith(QStringLiteral("类型"))) {
        gwType_->SetError(err);
    } else if (err.startsWith(QStringLiteral("枚举值"))) {
        gwEnum_->SetError(err);
    } else {
        gwKey_->SetError(err);
    }
    widgets::Toast::Show(QStringLiteral("登记被拦下：见表单红字原因（三步校验）"),
                         widgets::Toast::Tone::Error);
}

// ————————————————————————————————————————————— 刷新

void WorldBoardView::RebuildKindBadges() {
    // 计数徽标：一次 GROUP BY 拿全 31 kind（名称过滤生效时计数同步过滤）
    QHash<QString, int> counts;
    if (db_) {
        const std::string filter = nameFilter_.toStdString();
        std::string sql = "SELECT kind, COUNT(*) FROM entities";
        if (!filter.empty()) {
            sql += " WHERE name LIKE ?1";
        }
        sql += " GROUP BY kind";
        if (auto st = db_->Prepare(sql)) {
            if (!filter.empty()) {
                (void)st->BindText(1, "%" + filter + "%");
            }
            for (;;) {
                auto s = st->Step();
                if (!s || *s != db::sqlite::StepResult::Row) {
                    break;
                }
                counts.insert(QString::fromStdString(st->ColumnText(0)),
                              static_cast<int>(st->ColumnInt(1)));
            }
        }
    }
    int total = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        total += it.value();
    }
    const auto styleBtn = [](QPushButton* b, bool on) {
        b->setChecked(on);
        b->setStyleSheet(on ? QStringLiteral("color:%1;").arg(CssRgb(theme::Current().accentPrimary))
                            : QString{});
        widgets::SetSemibold(b, on);
    };
    styleBtn(kindBtns_[0], kindFilter_.isEmpty());
    kindBtns_[0]->setText(QStringLiteral("全部 %1").arg(total));
    for (std::size_t i = 0; i < std::size(kKinds); ++i) {
        const QString key = QString::fromStdString(std::string{kKinds[i].key});
        QPushButton* btn = kindBtns_[i + 1];
        btn->setText(QStringLiteral("%1 %2").arg(KindLabelOf(kKinds[i].key)).arg(counts.value(key)));
        btn->setToolTip(QStringLiteral("kind=%1（点选过滤，再点取消）")
                            .arg(QString::fromStdString(std::string{kKinds[i].key})));
        styleBtn(btn, kindFilter_ == key);
    }
}

void WorldBoardView::RebuildCards() {
    ClearLayout(cardsGrid_);
    if (!db_) {
        entityCount_->setText(QStringLiteral("实体 – 条"));
        if (!openError_.isEmpty()) {
            auto* es = new widgets::ErrorState(QStringLiteral("打不开小说库"), openError_, cardsHost_);
            es->SetOnRetry([this] {
                if (refStore_) {
                    LoadFromRef(*refStore_, lastNovel_);
                }
            });
            cardsGrid_->addWidget(es, 0, 0, 1, 3);
        } else {
            auto* es = new widgets::EmptyState(
                QStringLiteral("📚"), QStringLiteral("这个项目还没有小说库"),
                QStringLiteral("实体设定存在 <书>/db/novel.db 里；初始化链（P04-S4）会建库，"
                               "也可以先在书目录建空库。"),
                QStringLiteral("重新扫描"), cardsHost_);
            es->SetOnAction([this] {
                if (refStore_) {
                    LoadFromRef(*refStore_, lastNovel_);
                }
            });
            cardsGrid_->addWidget(es, 0, 0, 1, 3);
        }
        return;
    }
    novelcore::NovelGraph g(*db_);
    auto list = g.ListEntities(kindFilter_.toStdString(), nameFilter_.toStdString(), 200);
    if (!list) {
        auto* es = new widgets::ErrorState(
            QStringLiteral("读取实体失败"), QString::fromStdString(list.error().message), cardsHost_);
        es->SetOnRetry([this] { RefreshEntities(); });
        cardsGrid_->addWidget(es, 0, 0, 1, 3);
        return;
    }
    entityCount_->setText(QStringLiteral("实体 %1 条（%2 · 最多显示 200）")
                              .arg(static_cast<int>(list->size()))
                              .arg(bookTitle_));
    if (list->empty()) {
        if (kindFilter_.isEmpty() && nameFilter_.isEmpty()) {
            auto* es = new widgets::EmptyState(
                QStringLiteral("📖"), QStringLiteral("还没有实体"),
                QStringLiteral("设定台是全书设定的账本：先建第一位人物、第一个地点或第一条世界法则。"),
                QStringLiteral("新建实体"), cardsHost_);
            es->SetOnAction([this] { ToggleCreateForm(true); });
            cardsGrid_->addWidget(es, 0, 0, 1, 3);
        } else {
            auto* es = new widgets::EmptyState(
                QStringLiteral("🔍"), QStringLiteral("没有匹配的实体"),
                QStringLiteral("当前 kind / 名称过滤下没有结果；清除筛选看全部。"),
                QStringLiteral("清除筛选"), cardsHost_);
            es->SetOnAction([this] {
                kindFilter_.clear();
                nameFilter_.clear();
                if (QLineEdit* edit = search_->findChild<QLineEdit*>(); edit != nullptr) {
                    edit->clear(); // SearchBox 无清空 API：直接清内部输入框，保持与过滤态一致
                }
                RefreshEntities();
            });
            cardsGrid_->addWidget(es, 0, 0, 1, 3);
        }
        cardsGrid_->setRowStretch(1, 1);
        return;
    }
    int col = 0;
    int row = 0;
    for (const EntityRow& e : *list) {
        auto* card = new widgets::Card(widgets::Card::Variant::Outlined, cardsHost_);
        auto* body = card->BodyLayout();
        auto* top = new QWidget(card);
        auto* tr = new QHBoxLayout(top);
        tr->setContentsMargins(0, 0, 0, 0);
        tr->setSpacing(theme::space::kSteps[1]);
        auto* name = new QLabel(QString::fromStdString(e.name), top);
        widgets::SetSemibold(name, true);
        auto* status = new widgets::Tag(StatusLabelOf(e.status), StatusToneOf(e.status), false, top);
        tr->addWidget(name, 1);
        tr->addWidget(status, 0);
        body->addWidget(top);
        auto* kindRow = new QWidget(card);
        auto* kr = new QHBoxLayout(kindRow);
        kr->setContentsMargins(0, 0, 0, 0);
        kr->setSpacing(theme::space::kSteps[1]);
        kr->addWidget(new widgets::Tag(KindLabelOf(e.kind), "accent", false, kindRow));
        kr->addWidget(new QLabel(QStringLiteral("#%1").arg(e.id), kindRow));
        kr->addStretch(1);
        body->addWidget(kindRow);
        auto* summary = new QLabel(card);
        const QString sum = e.summary.empty() ? QStringLiteral("（暂无摘要）")
                                              : QString::fromStdString(e.summary);
        summary->setText(QFontMetrics(summary->font()).elidedText(sum, Qt::ElideRight, 260));
        summary->setToolTip(sum);
        body->addWidget(summary);
        card->SetOnClick([this, id = e.id] { OpenEntityDrawer(id); });
        cardsGrid_->addWidget(card, row, col);
        if (++col == 3) {
            col = 0;
            ++row;
        }
    }
    cardsGrid_->setRowStretch(row, 1);
}

// ————————————————————————————————————————————— Drawer（点卡 → 详情）

void WorldBoardView::OpenEntityDrawer(qint64 id) {
    if (drawer_ != nullptr) {
        drawer_->close(); // WA_DeleteOnClose：旧抽屉让位（QPointer 自动清空）
    }
    auto* drawer = new widgets::Drawer(QStringLiteral("实体详情"), this);
    drawer_ = drawer;
    QVBoxLayout* body = drawer->BodyLayout();

    if (!db_) {
        body->addWidget(new QLabel(QStringLiteral("库未打开，无法查看详情。"), drawer));
        drawer->Open();
        return;
    }
    novelcore::NovelGraph g(*db_);
    auto got = g.GetEntity(id);
    if (!got) {
        body->addWidget(new QLabel(
            QStringLiteral("读不到实体：%1").arg(QString::fromStdString(got.error().message)), drawer));
        drawer->Open();
        return;
    }
    const EntityRow& e = *got;

    // —— 基础字段 + 跨库稳定身份（决策 §7：canonical / 别名 / 出处「(书, 章)」）——
    body->addWidget(SectionLabel(drawer, QStringLiteral("基础字段")));
    QString canonical;
    QStringList aliases;
    QString sourceBook;
    qint64 sourceCh = 0;
    {
        util::json::OwnedDoc doc = util::json::ParseDoc(e.meta_json);
        std::string s;
        if (MetaGet(doc.root(), "canonical", s)) {
            canonical = QString::fromStdString(s);
        }
        if (MetaGet(doc.root(), "source_book", s)) {
            sourceBook = QString::fromStdString(s);
        }
        sourceCh = static_cast<qint64>(util::json::GetI64(doc.root(), "source_ch"));
        if (yyjson_val* arr = util::json::GetArr(doc.root(), "aliases"); arr != nullptr) {
            const std::size_t n = yyjson_arr_size(arr);
            for (std::size_t i = 0; i < n; ++i) {
                yyjson_val* v = yyjson_arr_get(arr, i);
                if (v != nullptr && yyjson_is_str(v)) {
                    aliases.push_back(QString::fromUtf8(yyjson_get_str(v)));
                }
            }
        }
    }
    const QString sourceView =
        (sourceBook.isEmpty() && sourceCh <= 0)
            ? QStringLiteral("（未标注出处）")
            : QStringLiteral("(%1, 第 %2 章)")
                  .arg(sourceBook.isEmpty() ? QStringLiteral("未记书名") : sourceBook)
                  .arg(sourceCh);
    auto* kv = new data::KeyValue(drawer);
    kv->SetPairs({{QStringLiteral("名称"), QString::fromStdString(e.name)},
                  {QStringLiteral("kind"), QStringLiteral("%1（%2）")
                                               .arg(KindLabelOf(e.kind),
                                                    QString::fromStdString(e.kind))},
                  {QStringLiteral("状态"),
                   QStringLiteral("%1（%2）").arg(StatusLabelOf(e.status),
                                                 QString::fromStdString(e.status))},
                  {QStringLiteral("摘要"),
                   e.summary.empty() ? QStringLiteral("（暂无摘要）")
                                     : QString::fromStdString(e.summary)},
                  {QStringLiteral("canonical"), canonical.isEmpty()
                                                     ? QStringLiteral("（未设，引用时用名称）")
                                                     : canonical},
                  {QStringLiteral("别名"), aliases.isEmpty() ? QStringLiteral("（无）")
                                                             : aliases.join(QStringLiteral("、"))},
                  {QStringLiteral("出处"), sourceView},
                  {QStringLiteral("行 id"), QString::number(e.id)}});
    body->addWidget(kv);

    // —— 动态字段：已登记的可编辑保存；未登记的折叠（PLAN §7 风险对策）——
    body->addWidget(SectionLabel(drawer, QStringLiteral("动态字段（已登记）")));
    novelcore::NovelFields fields(*db_);
    auto defs = fields.ListFieldDefs("entity", e.kind);
    auto vals = fields.ListEntityFields(e.id);
    QHash<QString, EntityFieldRow> valByKey;
    if (vals) {
        for (const EntityFieldRow& v : *vals) {
            valByKey.insert(QString::fromStdString(v.field_key), v);
        }
    }
    QSet<QString> defKeys;
    if (defs) {
        for (const FieldDefRow& d : *defs) {
            defKeys.insert(QString::fromStdString(d.field_key));
        }
    }
    if (!defs || defs->empty()) {
        body->addWidget(new QLabel(
            QStringLiteral("（本 kind 还没有登记动态字段——到「字段」页登记键后即可填写）"), drawer));
    } else {
        for (const FieldDefRow& d : *defs) {
            const QString key = QString::fromStdString(d.field_key);
            auto* row = new QWidget(drawer);
            auto* rr = new QHBoxLayout(row);
            rr->setContentsMargins(0, 0, 0, 0);
            rr->setSpacing(theme::space::kSteps[1]);
            auto* input = new widgets::TextInput(row);
            const EntityFieldRow cur = valByKey.value(key);
            input->SetText(QString::fromStdString(
                d.value_type == "json" ? cur.value_json : cur.value_text));
            if (d.value_type == "json" && cur.value_json == "null") {
                input->SetText(QString{});
            }
            input->SetPlaceholder(QStringLiteral("%1（%2%3）")
                                      .arg(ValueTypeLabelOf(d.value_type),
                                           QString::fromStdString(d.field_key),
                                           d.status == "PROPOSED"
                                               ? QStringLiteral("，待审 PROPOSED")
                                               : QString{}));
            const QString label = QStringLiteral("%1").arg(QString::fromStdString(d.title));
            auto* fw = new widgets::Field(label, widgets::Field::LabelPos::Top, input, row);
            auto* save = new widgets::Button(QStringLiteral("保存"), widgets::Button::Variant::Secondary,
                                             widgets::Button::Size::Sm, row);
            const std::string vt = d.value_type;
            const QString layer = cur.layer.empty() ? QStringLiteral("global")
                                                    : QString::fromStdString(cur.layer);
            connect(save, &widgets::Button::clicked, this,
                    [this, id, key, vt, layer, input, fw] {
                        const QString text = input->Text();
                        QString valueText = text;
                        QString valueJson;
                        if (vt == "json") { // json 类型的值进 value_json（写入侧做合法性校验）
                            valueText.clear();
                            valueJson = text;
                        }
                        QString err;
                        if (SaveEntityField(id, key, valueText, valueJson, layer, QString{}, &err)) {
                            fw->SetError(QString{});
                            widgets::Toast::Show(
                                QStringLiteral("字段「%1」已保存").arg(key),
                                widgets::Toast::Tone::Success);
                        } else {
                            fw->SetError(err);
                        }
                    });
            rr->addWidget(fw, 1);
            rr->addWidget(save, 0, Qt::AlignBottom);
            body->addWidget(row);
        }
    }

    // 未登记字段（折叠）：只读展示，登记后才可编辑（不变式 I12）
    QStringList unregLines;
    if (vals) {
        for (const EntityFieldRow& v : *vals) {
            const QString key = QString::fromStdString(v.field_key);
            if (!defKeys.contains(key)) {
                unregLines.push_back(QStringLiteral("%1 = %2（layer=%3）")
                                         .arg(key, QString::fromStdString(v.value_text),
                                              QString::fromStdString(v.layer)));
            }
        }
    }
    auto* foldBtn = new widgets::Button(
        QStringLiteral("未登记字段（%1）— 已折叠，点开展示").arg(unregLines.size()),
        widgets::Button::Variant::Ghost, widgets::Button::Size::Sm, drawer);
    foldBtn->setCheckable(true);
    auto* foldHost = new QWidget(drawer);
    auto* foldCol = new QVBoxLayout(foldHost);
    foldCol->setContentsMargins(theme::space::kSteps[2], 0, 0, 0);
    foldCol->setSpacing(theme::space::kSteps[1]);
    if (unregLines.isEmpty()) {
        foldCol->addWidget(new QLabel(QStringLiteral("（没有未登记字段）"), foldHost));
    } else {
        auto* kv2 = new data::KeyValue(foldHost);
        std::vector<std::pair<QString, QString>> pairs;
        for (const QString& line : unregLines) {
            pairs.emplace_back(QStringLiteral("未登记"), line);
        }
        kv2->SetPairs(pairs);
        foldCol->addWidget(kv2);
        foldCol->addWidget(new QLabel(
            QStringLiteral("未登记字段不可编辑保存（不变式 I12）：先到「字段」页登记键再改值。"),
            foldHost));
    }
    foldHost->setVisible(false);
    connect(foldBtn, &widgets::Button::toggled, foldHost, &QWidget::setVisible);
    body->addWidget(foldBtn);
    body->addWidget(foldHost);

    // —— 关系 ——
    body->addWidget(SectionLabel(drawer, QStringLiteral("关系")));
    auto rels = g.GetRelations(e.id, true);
    if (!rels || rels->empty()) {
        body->addWidget(new QLabel(
            QStringLiteral("（暂无关系；P04-S3 关系页接入登记与编辑）"), drawer));
    } else {
        auto* kv3 = new data::KeyValue(drawer);
        std::vector<std::pair<QString, QString>> pairs;
        for (const novelcore::RelationRow& r : *rels) {
            const bool outgoing = r.from_id == e.id;
            const qint64 otherId = outgoing ? r.to_id : r.from_id;
            QString other = QStringLiteral("#%1").arg(otherId);
            if (auto o = g.GetEntity(otherId); o) {
                other = QString::fromStdString(o->name);
            }
            pairs.emplace_back(QString::fromStdString(r.rel_type),
                               outgoing ? QStringLiteral("→ %1（强度 %2，%3）")
                                              .arg(other)
                                              .arg(r.strength)
                                              .arg(QString::fromStdString(r.status))
                                        : QStringLiteral("← %1（强度 %2，%3）")
                                              .arg(other)
                                              .arg(r.strength)
                                              .arg(QString::fromStdString(r.status)));
        }
        kv3->SetPairs(pairs);
        body->addWidget(kv3);
    }

    // —— 出场章节（scene_cast 在场 + event_participants 参与 → 章）——
    body->addWidget(SectionLabel(drawer, QStringLiteral("出场章节")));
    QStringList appear;
    if (auto st = db_->Prepare(
            "SELECT c.id, c.ord, c.title FROM chapters c WHERE c.id IN ("
            "  SELECT s.chapter_id FROM scene_cast sc JOIN scenes s ON s.id = sc.scene_id"
            "   WHERE sc.entity_id = ?1"
            "  UNION"
            "  SELECT e.created_chapter FROM event_participants p JOIN entities e ON e.id = p.event_id"
            "   WHERE p.entity_id = ?1 AND e.created_chapter > 0"
            ") ORDER BY c.ord, c.id")) {
        (void)st->BindInt(1, e.id);
        for (;;) {
            auto s = st->Step();
            if (!s || *s != db::sqlite::StepResult::Row) {
                break;
            }
            appear.push_back(QStringLiteral("第 %1 章 · %2")
                                 .arg(st->ColumnInt(1))
                                 .arg(QString::fromStdString(st->ColumnText(2))));
        }
    }
    body->addWidget(new QLabel(
        appear.isEmpty() ? QStringLiteral("（暂无出场记录——场次/事件登记后自动汇总）")
                         : appear.join(QStringLiteral("\n")),
        drawer));

    drawer->Open();
}

} // namespace shine::app
