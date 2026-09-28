// P04-S3 设定台续页（PLAN §5 S3 / UI.md §2.2）：关系 / 剧情线 / 伏笔 / 谜 + 知情边界矩阵。
//
//   · 关系页：时间化关系边（`01` §2.4.1 受检 14 类）登记 + 列表；点行 → Drawer 看两端实体卡摘要。
//   · 剧情线页：Plot 列表（kind/title/target_ch）+ PlotBeat 行内列表 + 新建。
//   · 伏笔页：每条伏笔一条五段 Timeline（PLANNED→PLANTED→DEVELOPING→REVEALED→RESOLVED，
//     `01` §2.3.3 不变式 I8）；推进按钮只给合法跃迁，高级「指定目标状态」对非法跃迁给中文原因。
//     core 的 UpsertForeshadow 不强制状态机（只写串），合法性在 app 层 WorldBoardShared.h::fstate 收口，
//     表单与探针走同一路径（TransitionForeshadow / AdvanceForeshadow）。
//   · 谜页：Mystery 列表 + MysteryBeat 节拍；下半 = 知情边界矩阵（角色 × 秘密）：
//     勾「知道/不知道」双落 character_knowledge + secret_knowledge（`01` §2.4.5），
//     「查询章」按 `01` §2.3.2 规范查询切片「第 N 章时谁知道什么」。
// 颜色零内联（check-layers rule 3）：一律 theme token + widgets::TokenQColor() 运行时拼串。
#include "ui/pages/novel/WorldBoardView.h"

#include "ui/pages/novel/WorldBoardShared.h"
#include "db/sqlite/SqliteDb.h"
#include "ui/kit/data/Flow.h"
#include "ui/kit/data/Panels.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/Inputs.h"
#include "ui/kit/controls/Surfaces.h"
#include "novel/NovelGraph.h"
#include "novel/NovelTypes.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shine::app {
namespace {

using novelcore::ForeshadowRow;
using novelcore::MysteryBeatRow;
using novelcore::MysteryRow;
using novelcore::PlotBeatRow;
using novelcore::PlotRow;
using novelcore::RelationRow;
using novelcore::SecretRow;

// character_knowledge.fact_kind：秘密类事实（fact_id = secrets.id，`01` §2.4.5）
constexpr std::string_view kFactSecret = "secret";

// 剧情线 kind（`01` §2.5：main | sub | romance | revenge …）
[[nodiscard]] QString PlotKindLabelOf(std::string_view key) {
    if (key == "main") return QStringLiteral("主线 main");
    if (key == "sub") return QStringLiteral("支线 sub");
    if (key == "romance") return QStringLiteral("感情线 romance");
    if (key == "revenge") return QStringLiteral("复仇线 revenge");
    return QString::fromStdString(std::string{key});
}

[[nodiscard]] std::string PlotKindKeyOfLabel(const QString& label) {
    if (label == QStringLiteral("主线 main")) return "main";
    if (label == QStringLiteral("支线 sub")) return "sub";
    if (label == QStringLiteral("感情线 romance")) return "romance";
    if (label == QStringLiteral("复仇线 revenge")) return "revenge";
    return label.toStdString();
}

// 伏笔状态机目标下拉：标签 ↔ key（fstate::kLabels）
[[nodiscard]] std::string FStateKeyOfLabel(const QString& label) {
    for (std::size_t i = 0; i < fstate::kLabels.size(); ++i) {
        if (label == QString::fromUtf8(fstate::kLabels[i].data(),
                                       static_cast<qsizetype>(fstate::kLabels[i].size()))) {
            return std::string{fstate::kStates[i]};
        }
    }
    return label.toStdString();
}

[[nodiscard]] QString FStatePickLabel(std::size_t i) {
    return QString::fromUtf8(fstate::kLabels[i].data(),
                             static_cast<qsizetype>(fstate::kLabels[i].size()));
}

// 伏笔状态 → Tag tone（状态文字本身承载语义；色只做轻重提示）
[[nodiscard]] const char* ForeshadowToneOf(std::string_view status) {
    const int seg = fstate::SegOf(status);
    if (seg == 2) return "accent";
    if (seg == 3) return "info";
    if (seg >= 4) return "ok";
    return "";
}

// 关系：生效区间中英混合展示（to_chapter=0 = 至今有效，`01` §2.4.1）
[[nodiscard]] QString RelSpanView(qint64 fromCh, qint64 toCh) {
    const QString from = fromCh > 0 ? QStringLiteral("第 %1 章起").arg(fromCh)
                                    : QStringLiteral("书前既有");
    const QString to = toCh > 0 ? QStringLiteral("至第 %1 章").arg(toCh) : QStringLiteral("至今有效");
    return QStringLiteral("%1 · %2").arg(from, to);
}

// 章序展示（0 = 未排定/书前）
[[nodiscard]] QString ChapterView(qint64 ch, std::string_view zeroText) {
    return ch > 0 ? QStringLiteral("第 %1 章").arg(ch)
                  : QString::fromUtf8(zeroText.data(), static_cast<qsizetype>(zeroText.size()));
}

} // namespace

// ————————————————————————————————————————————— 整表只读（core 无 ListAll* 的三张表）

std::vector<RelationRow> WorldBoardView::ReadRelations() const {
    std::vector<RelationRow> out;
    if (!db_) {
        return out;
    }
    auto st = db_->Prepare(
        "SELECT id,from_id,to_id,rel_type,strength,from_chapter,to_chapter,reason,status"
        " FROM relations ORDER BY id");
    if (!st) {
        return out;
    }
    for (;;) {
        auto s = st->Step();
        if (!s || *s != db::sqlite::StepResult::Row) {
            break;
        }
        RelationRow r;
        r.id = st->ColumnInt(0);
        r.from_id = st->ColumnInt(1);
        r.to_id = st->ColumnInt(2);
        r.rel_type = st->ColumnText(3);
        r.strength = static_cast<int>(st->ColumnInt(4));
        r.from_chapter = st->ColumnInt(5);
        r.to_chapter = st->ColumnInt(6);
        r.reason = st->ColumnText(7);
        r.status = st->ColumnText(8);
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<ForeshadowRow> WorldBoardView::ReadForeshadows() const {
    std::vector<ForeshadowRow> out;
    if (!db_) {
        return out;
    }
    auto st = db_->Prepare(
        "SELECT id,title,content,status,setup_ch,payoff_ch,importance,truth,entity_ids_json"
        " FROM foreshadowings ORDER BY id");
    if (!st) {
        return out;
    }
    for (;;) {
        auto s = st->Step();
        if (!s || *s != db::sqlite::StepResult::Row) {
            break;
        }
        ForeshadowRow f;
        f.id = st->ColumnInt(0);
        f.title = st->ColumnText(1);
        f.content = st->ColumnText(2);
        f.status = st->ColumnText(3);
        f.setup_ch = st->ColumnInt(4);
        f.payoff_ch = st->ColumnInt(5);
        f.importance = static_cast<int>(st->ColumnInt(6));
        f.truth = st->ColumnText(7);
        f.entity_ids_json = st->ColumnText(8);
        out.push_back(std::move(f));
    }
    return out;
}

std::vector<SecretRow> WorldBoardView::ReadSecrets() const {
    std::vector<SecretRow> out;
    if (!db_) {
        return out;
    }
    auto st = db_->Prepare(
        "SELECT id,content,truth,reveal_ch,reveal_condition,entity_id,scope FROM secrets ORDER BY id");
    if (!st) {
        return out;
    }
    for (;;) {
        auto s = st->Step();
        if (!s || *s != db::sqlite::StepResult::Row) {
            break;
        }
        SecretRow r;
        r.id = st->ColumnInt(0);
        r.content = st->ColumnText(1);
        r.truth = st->ColumnText(2);
        r.reveal_ch = st->ColumnInt(3);
        r.reveal_condition = st->ColumnText(4);
        r.entity_id = st->ColumnInt(5);
        r.scope = st->ColumnText(6);
        out.push_back(std::move(r));
    }
    return out;
}

// ————————————————————————————————————————————— 公开 API：关系

bool WorldBoardView::CreateRelation(qint64 fromId, qint64 toId, const QString& relType, int strength,
                                    qint64 fromChapter, qint64 toChapter, const QString& reason,
                                    QString* err) {
    const auto fail = [err](const QString& why) {
        if (err != nullptr) {
            *err = why;
        }
        return false;
    };
    if (!db_) {
        return fail(QStringLiteral("库未打开：当前书没有可写的 db/novel.db。"));
    }
    if (fromId <= 0 || toId <= 0) {
        return fail(QStringLiteral("两端：必须各选一个实体（起点 / 终点）——关系是实体间的时间化边（`01` §2.4.1）。"));
    }
    if (fromId == toId) {
        return fail(QStringLiteral("两端：不能是同一实体（#%1）——关系边连接两个不同实体。").arg(fromId));
    }
    novelcore::NovelGraph g(*db_);
    if (!g.GetEntity(fromId)) {
        return fail(QStringLiteral("起点实体：id=%1 不存在——先在「实体」页建立该实体再连线。").arg(fromId));
    }
    if (!g.GetEntity(toId)) {
        return fail(QStringLiteral("终点实体：id=%1 不存在——先在「实体」页建立该实体再连线。").arg(toId));
    }
    const std::string relStd = relType.trimmed().toStdString();
    if (!IsKnownRelType(relStd)) {
        return fail(QStringLiteral("关系类型：'%1' 不在受检 14 类内（`01` §2.4.1：family/love/friend/ally/"
                                   "enemy/rival/mentor/student/subordinate/superior/member_of/owns/"
                                   "counter/knows_secret）；扩展需登记，不放行自由串。")
                        .arg(relType));
    }
    if (strength < 0 || strength > 100) {
        return fail(QStringLiteral("强度：%1 超出 0–100（`01` §2.4.1 strength 语义）。").arg(strength));
    }
    if (fromChapter < 0) {
        return fail(QStringLiteral("起始章：不能为负（0 = 书前既有）。"));
    }
    if (toChapter < 0) {
        return fail(QStringLiteral("结束章：不能为负（0 = 至今仍有效）。"));
    }
    if (toChapter > 0 && toChapter < fromChapter) {
        return fail(QStringLiteral("生效区间：结束章（%1）早于起始章（%2）——区间须向前；"
                                   "「至今有效」请把结束章填 0。")
                        .arg(toChapter)
                        .arg(fromChapter));
    }

    RelationRow row;
    row.from_id = fromId;
    row.to_id = toId;
    row.rel_type = relStd;
    row.strength = strength;
    row.from_chapter = fromChapter;
    row.to_chapter = toChapter;
    row.reason = reason.trimmed().toStdString();
    auto id = g.UpsertRelation(row);
    if (!id) {
        return fail(QStringLiteral("落库失败：%1（修正后可重试）")
                        .arg(QString::fromStdString(id.error().message)));
    }
    RefreshRelations();
    return true;
}

bool WorldBoardView::RefreshRelations() {
    RebuildRelations();
    return db_ != nullptr;
}

// ————————————————————————————————————————————— 公开 API：剧情线

bool WorldBoardView::CreatePlot(const QString& kind, const QString& title, qint64 introCh,
                                qint64 targetCh, const QString& note, QString* err) {
    const auto fail = [err](const QString& why) {
        if (err != nullptr) {
            *err = why;
        }
        return false;
    };
    if (!db_) {
        return fail(QStringLiteral("库未打开：当前书没有可写的 db/novel.db。"));
    }
    const QString title2 = title.trimmed();
    if (title2.isEmpty()) {
        return fail(QStringLiteral("标题：不能为空——剧情线按标题追踪推进（`01` §2.5）。"));
    }
    const std::string kindStd = kind.trimmed().isEmpty() ? "main" : kind.trimmed().toStdString();
    if (kindStd.size() > 32) {
        return fail(QStringLiteral("类型：'%1' 过长（≤32 字符）——常用 main/sub/romance/revenge（`01` §2.5）。")
                        .arg(kind));
    }
    if (introCh < 0 || targetCh < 0) {
        return fail(QStringLiteral("章序：引入章 / 目标章不能为负（0 = 未排定）。"));
    }
    if (targetCh > 0 && introCh > 0 && targetCh <= introCh) {
        return fail(QStringLiteral("章序：目标章（%1）须晚于引入章（%2）——剧情线要有推进空间。")
                        .arg(targetCh)
                        .arg(introCh));
    }

    PlotRow row;
    row.kind = kindStd;
    row.title = title2.toStdString();
    row.status = "active";
    row.intro_ch = introCh;
    row.target_ch = targetCh;
    row.note = note.trimmed().toStdString();
    novelcore::NovelGraph g(*db_);
    auto id = g.UpsertPlot(row);
    if (!id) {
        return fail(QStringLiteral("落库失败：%1（修正后可重试）")
                        .arg(QString::fromStdString(id.error().message)));
    }
    RefreshPlots();
    return true;
}

bool WorldBoardView::CreatePlotBeat(qint64 plotId, qint64 chapterId, int ord,
                                    const QString& beatType, const QString& title,
                                    const QString& summary, QString* err) {
    const auto fail = [err](const QString& why) {
        if (err != nullptr) {
            *err = why;
        }
        return false;
    };
    if (!db_) {
        return fail(QStringLiteral("库未打开：当前书没有可写的 db/novel.db。"));
    }
    novelcore::NovelGraph g(*db_);
    if (!g.GetPlot(plotId)) {
        return fail(QStringLiteral("剧情线：id=%1 不存在——先建剧情线再排节拍。").arg(plotId));
    }
    const std::string typeStd = beatType.trimmed().toStdString();
    if (!IsKnownPlotBeat(typeStd)) {
        return fail(QStringLiteral("节拍类型：'%1' 不在 7 值内（`01` §2.5：setup|rising|turning_point|"
                                   "climax|falling|resolution|revelation）。")
                        .arg(beatType));
    }
    if (chapterId < 0 || ord < 0) {
        return fail(QStringLiteral("章序/排序：不能为负（章序 0 = 未排定章）。"));
    }
    const QString title2 = title.trimmed();
    if (title2.isEmpty()) {
        return fail(QStringLiteral("标题：不能为空——节拍按标题排叙事序（`01` §2.5 plot_beats.ord）。"));
    }

    PlotBeatRow row;
    row.plot_id = plotId;
    row.chapter_id = chapterId;
    row.ord = ord;
    row.beat_type = typeStd;
    row.title = title2.toStdString();
    row.summary = summary.trimmed().toStdString();
    auto id = g.UpsertPlotBeat(row);
    if (!id) {
        return fail(QStringLiteral("落库失败：%1（修正后可重试）")
                        .arg(QString::fromStdString(id.error().message)));
    }
    RefreshPlots();
    return true;
}

bool WorldBoardView::RefreshPlots() {
    RebuildPlots();
    return db_ != nullptr;
}

// ————————————————————————————————————————————— 公开 API：伏笔（`01` §2.3.3 状态机）

bool WorldBoardView::CreateForeshadow(const QString& title, const QString& content, qint64 setupCh,
                                      qint64 payoffCh, int importance, const QString& truth,
                                      QString* err) {
    const auto fail = [err](const QString& why) {
        if (err != nullptr) {
            *err = why;
        }
        return false;
    };
    if (!db_) {
        return fail(QStringLiteral("库未打开：当前书没有可写的 db/novel.db。"));
    }
    const QString title2 = title.trimmed();
    if (title2.isEmpty()) {
        return fail(QStringLiteral("标题：不能为空——伏笔账本按标题追踪埋点与回收（`01` §2.3.4）。"));
    }
    if (importance < 0 || importance > 100) {
        return fail(QStringLiteral("重要度：%1 超出 0–100（`01` §2.3.3 importance 语义）——用于 Context 排序与超期告警。")
                        .arg(importance));
    }
    if (setupCh < 0) {
        return fail(QStringLiteral("埋设章：不能为负（0 = 未排定）。"));
    }
    if (payoffCh < 0) {
        return fail(QStringLiteral("回收章：不能为负（0 = 未排定）。"));
    }
    if (payoffCh > 0 && setupCh > 0 && payoffCh <= setupCh) {
        return fail(QStringLiteral("埋设/回收章：须满足 埋设章 < 回收章（`10` §2.3 N11），当前 %1 ≥ %2。")
                        .arg(setupCh)
                        .arg(payoffCh));
    }

    ForeshadowRow row;
    row.title = title2.toStdString();
    row.content = content.trimmed().toStdString();
    row.status = "PLANNED"; // 新建即「计划」，之后只能沿状态机推进
    row.setup_ch = setupCh;
    row.payoff_ch = payoffCh;
    row.importance = importance;
    row.truth = truth.trimmed().toStdString();
    novelcore::NovelGraph g(*db_);
    auto id = g.UpsertForeshadow(row);
    if (!id) {
        return fail(QStringLiteral("落库失败：%1（修正后可重试）")
                        .arg(QString::fromStdString(id.error().message)));
    }
    RefreshForeshadows();
    return true;
}

bool WorldBoardView::TransitionForeshadow(qint64 id, const QString& toStatus, QString* err) {
    const auto fail = [err](const QString& why) {
        if (err != nullptr) {
            *err = why;
        }
        return false;
    };
    if (!db_) {
        return fail(QStringLiteral("库未打开：当前书没有可写的 db/novel.db。"));
    }
    const std::vector<ForeshadowRow> rows = ReadForeshadows();
    const auto it = std::ranges::find_if(rows, [id](const ForeshadowRow& f) { return f.id == id; });
    if (it == rows.end()) {
        return fail(QStringLiteral("伏笔：id=%1 不存在（先在伏笔页新建或刷新列表）。").arg(id));
    }
    const std::string to = toStatus.trimmed().toStdString();
    // 状态机判定（`01` §2.3.3 不变式 I8）：非法跃迁在 app 层拦下并给中文原因
    if (QString why = fstate::TransitionError(it->status, to); !why.isEmpty()) {
        return fail(why);
    }
    ForeshadowRow row = *it;
    row.status = to;
    novelcore::NovelGraph g(*db_);
    auto saved = g.UpsertForeshadow(row);
    if (!saved) {
        return fail(QStringLiteral("落库失败：%1（修正后可重试）")
                        .arg(QString::fromStdString(saved.error().message)));
    }
    RefreshForeshadows();
    return true;
}

bool WorldBoardView::AdvanceForeshadow(qint64 id, QString* err) {
    const auto fail = [err](const QString& why) {
        if (err != nullptr) {
            *err = why;
        }
        return false;
    };
    if (!db_) {
        return fail(QStringLiteral("库未打开：当前书没有可写的 db/novel.db。"));
    }
    const std::vector<ForeshadowRow> rows = ReadForeshadows();
    const auto it = std::ranges::find_if(rows, [id](const ForeshadowRow& f) { return f.id == id; });
    if (it == rows.end()) {
        return fail(QStringLiteral("伏笔：id=%1 不存在（先在伏笔页新建或刷新列表）。").arg(id));
    }
    const std::string_view next = fstate::NextOf(it->status);
    if (next.empty()) {
        return fail(QStringLiteral("已收束 %1：状态机终点，这条伏笔已完结，无可推进的下一状态。")
                        .arg(fstate::LabelOf(it->status)));
    }
    return TransitionForeshadow(id, QString::fromStdString(std::string{next}), err);
}

bool WorldBoardView::RefreshForeshadows() {
    RebuildForeshadows();
    return db_ != nullptr;
}

QString WorldBoardView::ForeshadowProbe() const {
    QString out;
    for (const ForeshadowRow& f : ReadForeshadows()) {
        const int seg = fstate::SegOf(f.status);
        out += QStringLiteral("伏笔#%1「%2」：%3 · 第 %4/5 段 · %5\n")
                   .arg(f.id)
                   .arg(QString::fromStdString(f.title), QString::fromStdString(f.status))
                   .arg(seg)
                   .arg(fstate::PhraseOf(f.status));
    }
    return out;
}

// ————————————————————————————————————————————— 公开 API：谜 + 知情

bool WorldBoardView::CreateMystery(const QString& question, const QString& answer, qint64 askCh,
                                   qint64 answerCh, int importance, QString* err) {
    const auto fail = [err](const QString& why) {
        if (err != nullptr) {
            *err = why;
        }
        return false;
    };
    if (!db_) {
        return fail(QStringLiteral("库未打开：当前书没有可写的 db/novel.db。"));
    }
    const QString q = question.trimmed();
    if (q.isEmpty()) {
        return fail(QStringLiteral("问题：不能为空——谜团是读者侧节奏的提问（`01` §2.3.4）。"));
    }
    if (importance < 0 || importance > 100) {
        return fail(QStringLiteral("重要度：%1 超出 0–100。").arg(importance));
    }
    if (askCh < 0 || answerCh < 0) {
        return fail(QStringLiteral("章序：提问章 / 答案章不能为负（0 = 未排定）。"));
    }

    MysteryRow row;
    row.question = q.toStdString();
    row.answer = answer.trimmed().toStdString();
    row.status = "open";
    row.ask_ch = askCh;
    row.answer_ch = answerCh;
    row.importance = importance;
    novelcore::NovelGraph g(*db_);
    auto id = g.UpsertMystery(row);
    if (!id) {
        return fail(QStringLiteral("落库失败：%1（修正后可重试）")
                        .arg(QString::fromStdString(id.error().message)));
    }
    RefreshMysteries();
    return true;
}

bool WorldBoardView::CreateMysteryBeat(qint64 mysteryId, const QString& beatType, qint64 chapterId,
                                       const QString& content, QString* err) {
    const auto fail = [err](const QString& why) {
        if (err != nullptr) {
            *err = why;
        }
        return false;
    };
    if (!db_) {
        return fail(QStringLiteral("库未打开：当前书没有可写的 db/novel.db。"));
    }
    novelcore::NovelGraph g(*db_);
    if (!g.GetMystery(mysteryId)) {
        return fail(QStringLiteral("谜团：id=%1 不存在——先建谜团再排节拍。").arg(mysteryId));
    }
    const std::string typeStd = beatType.trimmed().toStdString();
    if (!IsKnownMysteryBeat(typeStd)) {
        return fail(QStringLiteral("节拍类型：'%1' 不在 5 值内（`01` §2.3.4：question|hint|reveal|"
                                   "answer|red_herring）。")
                        .arg(beatType));
    }
    if (chapterId < 0) {
        return fail(QStringLiteral("章序：不能为负（0 = 未排定）。"));
    }
    const QString c = content.trimmed();
    if (c.isEmpty()) {
        return fail(QStringLiteral("内容：节拍内容不能为空——线索 / 揭示要有具体内容才排得出节奏。"));
    }

    MysteryBeatRow row;
    row.mystery_id = mysteryId;
    row.beat_type = typeStd;
    row.chapter_id = chapterId;
    row.content = c.toStdString();
    row.ord = 0;
    if (auto beats = g.ListMysteryBeats(mysteryId); beats) {
        row.ord = static_cast<int>(beats->size()); // 追加排尾（ord 参与叙事序）
    }
    auto id = g.UpsertMysteryBeat(row);
    if (!id) {
        return fail(QStringLiteral("落库失败：%1（修正后可重试）")
                        .arg(QString::fromStdString(id.error().message)));
    }
    RefreshMysteries();
    return true;
}

bool WorldBoardView::CreateSecret(const QString& content, const QString& truth, qint64 revealCh,
                                  const QString& revealCondition, qint64 entityId,
                                  const QString& scope, QString* err) {
    const auto fail = [err](const QString& why) {
        if (err != nullptr) {
            *err = why;
        }
        return false;
    };
    if (!db_) {
        return fail(QStringLiteral("库未打开：当前书没有可写的 db/novel.db。"));
    }
    const QString c = content.trimmed();
    if (c.isEmpty()) {
        return fail(QStringLiteral("内容：不能为空——秘密是知情边界矩阵的列（`01` §2.4.5）。"));
    }
    const std::string scopeStd = scope.trimmed().isEmpty() ? "character" : scope.trimmed().toStdString();
    if (!IsKnownSecretScope(scopeStd)) {
        return fail(QStringLiteral("范围：'%1' 不在 world|character|faction 内（`01` §1.1.6 SecretRow.scope）。")
                        .arg(scope));
    }
    if (revealCh < 0) {
        return fail(QStringLiteral("揭示章：不能为负（0 = 未排定）。"));
    }

    SecretRow row;
    row.content = c.toStdString();
    row.truth = truth.trimmed().toStdString();
    row.reveal_ch = revealCh;
    row.reveal_condition = revealCondition.trimmed().toStdString();
    row.entity_id = entityId;
    row.scope = scopeStd;
    novelcore::NovelGraph g(*db_);
    auto id = g.UpsertSecret(row);
    if (!id) {
        return fail(QStringLiteral("落库失败：%1（修正后可重试）")
                        .arg(QString::fromStdString(id.error().message)));
    }
    RefreshMysteries();
    return true;
}

bool WorldBoardView::SetKnows(qint64 entityId, qint64 secretId, bool knows, qint64 chapter,
                              QString* err) {
    const auto fail = [err](const QString& why) {
        if (err != nullptr) {
            *err = why;
        }
        return false;
    };
    if (!db_) {
        return fail(QStringLiteral("库未打开：当前书没有可写的 db/novel.db。"));
    }
    if (chapter < 0) {
        return fail(QStringLiteral("生效章：不能为负（0 = 书前即知）。"));
    }
    novelcore::NovelGraph g(*db_);
    if (!g.GetEntity(entityId)) {
        return fail(QStringLiteral("角色：id=%1 不存在——矩阵行取自「人物」实体。").arg(entityId));
    }
    const std::vector<SecretRow> secrets = ReadSecrets();
    const auto sit = std::ranges::find_if(secrets, [secretId](const SecretRow& s) {
        return s.id == secretId;
    });
    if (sit == secrets.end()) {
        return fail(QStringLiteral("秘密：id=%1 不存在——矩阵列取自 secrets 表（先「新建秘密」）。").arg(secretId));
    }

    // 同一 (entity, secret) 更新既有行，不重复插行 —— 表单反复勾选也保持一行真相
    novelcore::CharacterKnowledgeRow row;
    if (auto rows = g.ListKnowledge(entityId, 0); rows) {
        const auto it = std::ranges::find_if(*rows, [secretId](const novelcore::CharacterKnowledgeRow& k) {
            return k.fact_kind == kFactSecret && k.fact_id == secretId;
        });
        if (it != rows->end()) {
            row.id = it->id;
        }
    }
    row.entity_id = entityId;
    row.fact_kind = std::string{kFactSecret};
    row.fact_id = secretId;
    row.fact_text = sit->content; // 便于「谁知道什么」直接读回事实文本
    row.knows = knows ? 1 : 0;
    row.chapter_known = chapter;
    if (auto saved = g.UpsertKnowledge(row); !saved) {
        return fail(QStringLiteral("知情落库失败：%1（character_knowledge）")
                        .arg(QString::fromStdString(saved.error().message)));
    }
    // 双落 secret_knowledge（`01` §2.4.5）：GetSecretsFor 与 CharacterKnows 两条规范查询一致
    if (auto mirrored = g.SetSecretKnowledge(secretId, entityId, knows ? 1 : 0, chapter); !mirrored) {
        return fail(QStringLiteral("知情落库失败：%1（secret_knowledge 镜像）")
                        .arg(QString::fromStdString(mirrored.error().message)));
    }
    return true;
}

bool WorldBoardView::WhoKnowsWhat(int chapter, QString* probe) const {
    const auto write = [probe](const QString& s) {
        if (probe != nullptr) {
            *probe = s;
        }
    };
    if (!db_) {
        write(QStringLiteral("库未打开：无法查询知情边界。"));
        return false;
    }
    novelcore::NovelGraph g(*db_);
    auto persons = g.ListEntities(novelcore::kind::person, {}, 200);
    if (!persons) {
        write(QStringLiteral("读取人物失败：%1").arg(QString::fromStdString(persons.error().message)));
        return false;
    }
    const std::vector<SecretRow> secrets = ReadSecrets();
    QString out;
    out += (chapter > 0
                ? QStringLiteral("第 %1 章时谁知道什么（角色 × 秘密，`01` §2.3.2 规范查询）：").arg(chapter)
                : QStringLiteral("谁知道什么（不限章时点 · 当前态）："));
    out += '\n';
    for (const novelcore::EntityRow& p : *persons) {
        std::vector<novelcore::CharacterKnowledgeRow> known;
        if (auto rows = g.ListKnowledge(p.id, 0); rows) {
            known = std::move(*rows);
        }
        for (const SecretRow& s : secrets) {
            const auto it = std::ranges::find_if(known, [&s](const novelcore::CharacterKnowledgeRow& k) {
                return k.fact_kind == kFactSecret && k.fact_id == s.id;
            });
            QString verdict;
            const bool knowsNow =
                it != known.end() && it->knows == 1 &&
                (chapter <= 0 || it->chapter_known == 0 || it->chapter_known <= chapter);
            if (knowsNow) {
                verdict = it->chapter_known == 0 ? QStringLiteral("知道（书前即知）")
                                                 : QStringLiteral("知道（第 %1 章起）").arg(it->chapter_known);
            } else if (it != known.end() && it->knows == 1 && chapter > 0 && it->chapter_known > chapter) {
                verdict = QStringLiteral("不知道（第 %1 章起才知）").arg(it->chapter_known);
            } else if (it != known.end() && it->knows == 0) {
                verdict = QStringLiteral("不知道（已显式标记不知）");
            } else {
                verdict = QStringLiteral("不知道（无知情记录）");
            }
            out += QStringLiteral("%1 → 秘密#%2「%3」：%4\n")
                       .arg(QString::fromStdString(p.name))
                       .arg(s.id)
                       .arg(ElideText(QString::fromStdString(s.content), 24), verdict);
        }
    }
    write(out);
    return true;
}

bool WorldBoardView::RefreshMysteries() {
    RebuildMysteries();
    RebuildMatrix();
    return db_ != nullptr;
}

// ————————————————————————————————————————————— 页面搭建

QWidget* WorldBoardView::BuildRelationsPage() {
    auto* page = new QWidget(this);
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(theme::space::kSteps[2]);

    auto* top = new QWidget(page);
    auto* tr = new QHBoxLayout(top);
    tr->setContentsMargins(0, 0, 0, 0);
    tr->setSpacing(theme::space::kSteps[2]);
    auto* newBtn = new widgets::Button(QStringLiteral("+ 新建关系"), widgets::Button::Variant::Primary,
                                       widgets::Button::Size::Sm, top);
    connect(newBtn, &widgets::Button::clicked, this, [this, newBtn] {
        const bool show = rel_.form != nullptr && !rel_.form->isVisible();
        rel_.form->setVisible(show);
        newBtn->setText(show ? QStringLiteral("收起表单") : QStringLiteral("+ 新建关系"));
    });
    rel_.count = new QLabel(QStringLiteral("关系 0 条"), top);
    auto* hint = new QLabel(
        QStringLiteral("rel_type 受检 14 类（`01` §2.4.1）；结束章 0 = 至今有效。点行看两端实体卡摘要。"), top);
    tr->addWidget(newBtn);
    tr->addWidget(rel_.count, 0, Qt::AlignVCenter);
    tr->addStretch(1);
    tr->addWidget(hint, 0, Qt::AlignVCenter);
    col->addWidget(top);

    // 新建表单（from / to / rel_type / 强度 / 章区间 / 成因）
    rel_.form = new QWidget(page);
    auto* form = new QVBoxLayout(rel_.form);
    form->setContentsMargins(theme::space::kSteps[2], theme::space::kSteps[2],
                             theme::space::kSteps[2], theme::space::kSteps[2]);
    form->setSpacing(theme::space::kSteps[2]);
    form->addWidget(SectionLabel(rel_.form, QStringLiteral("新建关系（时间化边）")));

    rel_.fromSel = new widgets::Select(true, false, rel_.form);
    rel_.fromSel->SetPlaceholder(QStringLiteral("选择起点实体（可搜索）"));
    rel_.wFrom = new widgets::Field(QStringLiteral("起点 from"), widgets::Field::LabelPos::Left,
                                    rel_.fromSel, rel_.form);
    form->addWidget(rel_.wFrom);

    rel_.toSel = new widgets::Select(true, false, rel_.form);
    rel_.toSel->SetPlaceholder(QStringLiteral("选择终点实体（可搜索）"));
    rel_.wTo = new widgets::Field(QStringLiteral("终点 to"), widgets::Field::LabelPos::Left,
                                  rel_.toSel, rel_.form);
    form->addWidget(rel_.wTo);

    rel_.typeSel = new widgets::Select(false, false, rel_.form);
    {
        std::vector<widgets::Select::Item> items;
        for (std::size_t i = 0; i < std::size(kRelTypes); ++i) {
            items.push_back({RelLabelOf(kRelTypes[i].key), QStringLiteral("关系类型"), i == 2});
        }
        rel_.typeSel->SetItems(std::move(items));
    }
    rel_.wType = new widgets::Field(QStringLiteral("关系类型"), widgets::Field::LabelPos::Left,
                                    rel_.typeSel, rel_.form);
    rel_.wType->SetHelp(QStringLiteral("受检 14 类（`01` §2.4.1）；扩展需登记"));
    form->addWidget(rel_.wType);

    rel_.strength = new widgets::NumberInput(false, 0, 100, rel_.form);
    rel_.strength->SetValue(50);
    form->addWidget(new widgets::Field(QStringLiteral("强度"), widgets::Field::LabelPos::Left,
                                       rel_.strength, rel_.form));
    rel_.fromCh = new widgets::NumberInput(false, 0, 999999, rel_.form);
    rel_.fromCh->SetValue(0);
    form->addWidget(new widgets::Field(QStringLiteral("起始章"), widgets::Field::LabelPos::Left,
                                       rel_.fromCh, rel_.form));
    rel_.toCh = new widgets::NumberInput(false, 0, 999999, rel_.form);
    rel_.toCh->SetValue(0);
    form->addWidget(new widgets::Field(QStringLiteral("结束章"), widgets::Field::LabelPos::Left,
                                       rel_.toCh, rel_.form));
    rel_.reason = new widgets::TextInput(rel_.form);
    rel_.reason->SetPlaceholder(QStringLiteral("成因：这条关系为什么成立 / 何时改变"));
    form->addWidget(new widgets::Field(QStringLiteral("成因"), widgets::Field::LabelPos::Left,
                                       rel_.reason, rel_.form));

    auto* formBtns = new QWidget(rel_.form);
    auto* fbr = new QHBoxLayout(formBtns);
    fbr->setContentsMargins(0, 0, 0, 0);
    fbr->addStretch(1);
    auto* cancel = new widgets::Button(QStringLiteral("取消"), widgets::Button::Variant::Ghost,
                                       widgets::Button::Size::Md, formBtns);
    auto* submit = new widgets::Button(QStringLiteral("创建并落库"), widgets::Button::Variant::Primary,
                                       widgets::Button::Size::Md, formBtns);
    connect(cancel, &widgets::Button::clicked, this, [this, newBtn] {
        rel_.form->setVisible(false);
        newBtn->setText(QStringLiteral("+ 新建关系"));
    });
    connect(submit, &widgets::Button::clicked, this, &WorldBoardView::SubmitCreateRelation);
    fbr->addWidget(cancel);
    fbr->addWidget(submit);
    form->addWidget(formBtns);
    rel_.form->setVisible(false);
    col->addWidget(rel_.form);

    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    rel_.listHost = new QWidget(scroll);
    rel_.listCol = new QVBoxLayout(rel_.listHost);
    rel_.listCol->setContentsMargins(0, 0, 0, 0);
    rel_.listCol->setSpacing(theme::space::kSteps[2]);
    rel_.listCol->addStretch(1);
    scroll->setWidget(rel_.listHost);
    col->addWidget(scroll, 1);
    return page;
}

QWidget* WorldBoardView::BuildPlotsPage() {
    auto* page = new QWidget(this);
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(theme::space::kSteps[2]);

    auto* top = new QWidget(page);
    auto* tr = new QHBoxLayout(top);
    tr->setContentsMargins(0, 0, 0, 0);
    tr->setSpacing(theme::space::kSteps[2]);
    auto* newBtn = new widgets::Button(QStringLiteral("+ 新建剧情线"), widgets::Button::Variant::Primary,
                                       widgets::Button::Size::Sm, top);
    connect(newBtn, &widgets::Button::clicked, this, [this, newBtn] {
        const bool show = plot_.form != nullptr && !plot_.form->isVisible();
        plot_.form->setVisible(show);
        newBtn->setText(show ? QStringLiteral("收起表单") : QStringLiteral("+ 新建剧情线"));
    });
    plot_.count = new QLabel(QStringLiteral("剧情线 0 条"), top);
    auto* hint = new QLabel(QStringLiteral("节拍类型 7 值（`01` §2.5）：setup→rising→turning_point→climax→…"),
                            top);
    tr->addWidget(newBtn);
    tr->addWidget(plot_.count, 0, Qt::AlignVCenter);
    tr->addStretch(1);
    tr->addWidget(hint, 0, Qt::AlignVCenter);
    col->addWidget(top);

    plot_.form = new QWidget(page);
    auto* form = new QVBoxLayout(plot_.form);
    form->setContentsMargins(theme::space::kSteps[2], theme::space::kSteps[2],
                             theme::space::kSteps[2], theme::space::kSteps[2]);
    form->setSpacing(theme::space::kSteps[2]);
    form->addWidget(SectionLabel(plot_.form, QStringLiteral("新建剧情线")));

    plot_.kindSel = new widgets::Select(false, false, plot_.form);
    plot_.kindSel->SetItems({{QStringLiteral("主线 main"), QStringLiteral("类型"), true},
                             {QStringLiteral("支线 sub"), QStringLiteral("类型"), false},
                             {QStringLiteral("感情线 romance"), QStringLiteral("类型"), false},
                             {QStringLiteral("复仇线 revenge"), QStringLiteral("类型"), false}});
    plot_.wKind = new widgets::Field(QStringLiteral("类型 kind"), widgets::Field::LabelPos::Left,
                                     plot_.kindSel, plot_.form);
    form->addWidget(plot_.wKind);

    plot_.title = new widgets::TextInput(plot_.form);
    plot_.title->SetPlaceholder(QStringLiteral("如：灯语的回声（必填）"));
    plot_.wTitle = new widgets::Field(QStringLiteral("标题"), widgets::Field::LabelPos::Left,
                                      plot_.title, plot_.form);
    form->addWidget(plot_.wTitle);

    plot_.introCh = new widgets::NumberInput(false, 0, 999999, plot_.form);
    plot_.introCh->SetValue(0);
    form->addWidget(new widgets::Field(QStringLiteral("引入章"), widgets::Field::LabelPos::Left,
                                       plot_.introCh, plot_.form));
    plot_.targetCh = new widgets::NumberInput(false, 0, 999999, plot_.form);
    plot_.targetCh->SetValue(0);
    form->addWidget(new widgets::Field(QStringLiteral("目标章"), widgets::Field::LabelPos::Left,
                                       plot_.targetCh, plot_.form));
    plot_.note = new widgets::TextInput(plot_.form);
    plot_.note->SetPlaceholder(QStringLiteral("一句话说明这条线要走到哪"));
    form->addWidget(new widgets::Field(QStringLiteral("备注"), widgets::Field::LabelPos::Left,
                                       plot_.note, plot_.form));

    auto* formBtns = new QWidget(plot_.form);
    auto* fbr = new QHBoxLayout(formBtns);
    fbr->setContentsMargins(0, 0, 0, 0);
    fbr->addStretch(1);
    auto* cancel = new widgets::Button(QStringLiteral("取消"), widgets::Button::Variant::Ghost,
                                       widgets::Button::Size::Md, formBtns);
    auto* submit = new widgets::Button(QStringLiteral("创建并落库"), widgets::Button::Variant::Primary,
                                       widgets::Button::Size::Md, formBtns);
    connect(cancel, &widgets::Button::clicked, this, [this, newBtn] {
        plot_.form->setVisible(false);
        newBtn->setText(QStringLiteral("+ 新建剧情线"));
    });
    connect(submit, &widgets::Button::clicked, this, &WorldBoardView::SubmitCreatePlot);
    fbr->addWidget(cancel);
    fbr->addWidget(submit);
    form->addWidget(formBtns);
    plot_.form->setVisible(false);
    col->addWidget(plot_.form);

    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    plot_.listHost = new QWidget(scroll);
    plot_.listCol = new QVBoxLayout(plot_.listHost);
    plot_.listCol->setContentsMargins(0, 0, 0, 0);
    plot_.listCol->setSpacing(theme::space::kSteps[2]);
    plot_.listCol->addStretch(1);
    scroll->setWidget(plot_.listHost);
    col->addWidget(scroll, 1);
    return page;
}

QWidget* WorldBoardView::BuildForeshadowPage() {
    auto* page = new QWidget(this);
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(theme::space::kSteps[2]);

    auto* top = new QWidget(page);
    auto* tr = new QHBoxLayout(top);
    tr->setContentsMargins(0, 0, 0, 0);
    tr->setSpacing(theme::space::kSteps[2]);
    auto* newBtn = new widgets::Button(QStringLiteral("+ 新建伏笔"), widgets::Button::Variant::Primary,
                                       widgets::Button::Size::Sm, top);
    connect(newBtn, &widgets::Button::clicked, this, [this, newBtn] {
        const bool show = fs_.form != nullptr && !fs_.form->isVisible();
        fs_.form->setVisible(show);
        newBtn->setText(show ? QStringLiteral("收起表单") : QStringLiteral("+ 新建伏笔"));
    });
    fs_.count = new QLabel(QStringLiteral("伏笔 0 条"), top);
    auto* hint = new QLabel(
        QStringLiteral("状态机 PLANNED→PLANTED→DEVELOPING→REVEALED→RESOLVED（`01` §2.3.3）：逐段推进，"
                       "短伏笔可 PLANNED 直跳 REVEALED；不得回退。"),
        top);
    tr->addWidget(newBtn);
    tr->addWidget(fs_.count, 0, Qt::AlignVCenter);
    tr->addStretch(1);
    tr->addWidget(hint, 0, Qt::AlignVCenter);
    col->addWidget(top);

    fs_.form = new QWidget(page);
    auto* form = new QVBoxLayout(fs_.form);
    form->setContentsMargins(theme::space::kSteps[2], theme::space::kSteps[2],
                             theme::space::kSteps[2], theme::space::kSteps[2]);
    form->setSpacing(theme::space::kSteps[2]);
    form->addWidget(SectionLabel(fs_.form, QStringLiteral("新建伏笔（新建即 PLANNED）")));

    fs_.title = new widgets::TextInput(fs_.form);
    fs_.title->SetPlaceholder(QStringLiteral("如：铜钥匙的来历（必填）"));
    fs_.wTitle = new widgets::Field(QStringLiteral("标题"), widgets::Field::LabelPos::Left,
                                    fs_.title, fs_.form);
    form->addWidget(fs_.wTitle);

    fs_.content = new widgets::TextArea(500, fs_.form);
    fs_.content->Edit()->setPlaceholderText(
        QStringLiteral("埋点内容：读者会看到什么（作者侧账本，`01` §2.3.4）"));
    form->addWidget(new widgets::Field(QStringLiteral("埋点内容"), widgets::Field::LabelPos::Left,
                                       fs_.content, fs_.form));

    fs_.truth = new widgets::TextInput(fs_.form);
    fs_.truth->SetPlaceholder(QStringLiteral("真相：回收时揭示什么（不进读者侧 Context）"));
    form->addWidget(new widgets::Field(QStringLiteral("真相"), widgets::Field::LabelPos::Left,
                                       fs_.truth, fs_.form));

    fs_.setupCh = new widgets::NumberInput(false, 0, 999999, fs_.form);
    fs_.setupCh->SetValue(1);
    fs_.wSetupCh = new widgets::Field(QStringLiteral("埋设章"), widgets::Field::LabelPos::Left,
                                      fs_.setupCh, fs_.form);
    fs_.wSetupCh->SetHelp(QStringLiteral("须 < 回收章（`10` §2.3 N11）"));
    form->addWidget(fs_.wSetupCh);

    fs_.payoffCh = new widgets::NumberInput(false, 0, 999999, fs_.form);
    fs_.payoffCh->SetValue(0);
    form->addWidget(new widgets::Field(QStringLiteral("回收章"), widgets::Field::LabelPos::Left,
                                       fs_.payoffCh, fs_.form));

    fs_.importance = new widgets::NumberInput(false, 0, 100, fs_.form);
    fs_.importance->SetValue(50);
    form->addWidget(new widgets::Field(QStringLiteral("重要度"), widgets::Field::LabelPos::Left,
                                       fs_.importance, fs_.form));

    auto* formBtns = new QWidget(fs_.form);
    auto* fbr = new QHBoxLayout(formBtns);
    fbr->setContentsMargins(0, 0, 0, 0);
    fbr->addStretch(1);
    auto* cancel = new widgets::Button(QStringLiteral("取消"), widgets::Button::Variant::Ghost,
                                       widgets::Button::Size::Md, formBtns);
    auto* submit = new widgets::Button(QStringLiteral("创建并落库"), widgets::Button::Variant::Primary,
                                       widgets::Button::Size::Md, formBtns);
    connect(cancel, &widgets::Button::clicked, this, [this, newBtn] {
        fs_.form->setVisible(false);
        newBtn->setText(QStringLiteral("+ 新建伏笔"));
    });
    connect(submit, &widgets::Button::clicked, this, &WorldBoardView::SubmitCreateForeshadow);
    fbr->addWidget(cancel);
    fbr->addWidget(submit);
    form->addWidget(formBtns);
    fs_.form->setVisible(false);
    col->addWidget(fs_.form);

    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    fs_.listHost = new QWidget(scroll);
    fs_.listCol = new QVBoxLayout(fs_.listHost);
    fs_.listCol->setContentsMargins(0, 0, 0, 0);
    fs_.listCol->setSpacing(theme::space::kSteps[2]);
    fs_.listCol->addStretch(1);
    scroll->setWidget(fs_.listHost);
    col->addWidget(scroll, 1);
    return page;
}

QWidget* WorldBoardView::BuildMysteryPage() {
    auto* page = new QWidget(this);
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(theme::space::kSteps[2]);

    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* host = new QWidget(scroll);
    auto* v = new QVBoxLayout(host);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(theme::space::kSteps[3]);

    // —— 上半：谜团列表（读者侧节奏，`01` §2.3.4）——
    auto* top = new QWidget(host);
    auto* tr = new QHBoxLayout(top);
    tr->setContentsMargins(0, 0, 0, 0);
    tr->setSpacing(theme::space::kSteps[2]);
    auto* newBtn = new widgets::Button(QStringLiteral("+ 新建谜团"), widgets::Button::Variant::Primary,
                                       widgets::Button::Size::Sm, top);
    connect(newBtn, &widgets::Button::clicked, this, [this, newBtn] {
        const bool show = ms_.form != nullptr && !ms_.form->isVisible();
        ms_.form->setVisible(show);
        newBtn->setText(show ? QStringLiteral("收起表单") : QStringLiteral("+ 新建谜团"));
    });
    ms_.count = new QLabel(QStringLiteral("谜团 0 条"), top);
    auto* hint = new QLabel(
        QStringLiteral("谜团 = 读者侧节奏（何时提问 / 何时给答案）；伏笔 = 作者侧账本（`01` §2.3.4）。"), top);
    tr->addWidget(newBtn);
    tr->addWidget(ms_.count, 0, Qt::AlignVCenter);
    tr->addStretch(1);
    tr->addWidget(hint, 0, Qt::AlignVCenter);
    v->addWidget(top);

    ms_.form = new QWidget(host);
    auto* form = new QVBoxLayout(ms_.form);
    form->setContentsMargins(theme::space::kSteps[2], theme::space::kSteps[2],
                             theme::space::kSteps[2], theme::space::kSteps[2]);
    form->setSpacing(theme::space::kSteps[2]);
    form->addWidget(SectionLabel(ms_.form, QStringLiteral("新建谜团")));

    ms_.question = new widgets::TextInput(ms_.form);
    ms_.question->SetPlaceholder(QStringLiteral("读者被引导去问的问题（必填），如：灯塔下的尸骨是谁"));
    ms_.wQuestion = new widgets::Field(QStringLiteral("问题"), widgets::Field::LabelPos::Left,
                                       ms_.question, ms_.form);
    form->addWidget(ms_.wQuestion);

    ms_.answer = new widgets::TextInput(ms_.form);
    ms_.answer->SetPlaceholder(QStringLiteral("预设答案（可空——答案也可由节拍 answer 再给）"));
    form->addWidget(new widgets::Field(QStringLiteral("答案"), widgets::Field::LabelPos::Left,
                                       ms_.answer, ms_.form));

    ms_.askCh = new widgets::NumberInput(false, 0, 999999, ms_.form);
    ms_.askCh->SetValue(0);
    form->addWidget(new widgets::Field(QStringLiteral("提问章"), widgets::Field::LabelPos::Left,
                                       ms_.askCh, ms_.form));
    ms_.answerCh = new widgets::NumberInput(false, 0, 999999, ms_.form);
    ms_.answerCh->SetValue(0);
    form->addWidget(new widgets::Field(QStringLiteral("答案章"), widgets::Field::LabelPos::Left,
                                       ms_.answerCh, ms_.form));
    ms_.importance = new widgets::NumberInput(false, 0, 100, ms_.form);
    ms_.importance->SetValue(50);
    form->addWidget(new widgets::Field(QStringLiteral("重要度"), widgets::Field::LabelPos::Left,
                                       ms_.importance, ms_.form));

    auto* mBtns = new QWidget(ms_.form);
    auto* mbr = new QHBoxLayout(mBtns);
    mbr->setContentsMargins(0, 0, 0, 0);
    mbr->addStretch(1);
    auto* mCancel = new widgets::Button(QStringLiteral("取消"), widgets::Button::Variant::Ghost,
                                        widgets::Button::Size::Md, mBtns);
    auto* mSubmit = new widgets::Button(QStringLiteral("创建并落库"), widgets::Button::Variant::Primary,
                                        widgets::Button::Size::Md, mBtns);
    connect(mCancel, &widgets::Button::clicked, this, [this, newBtn] {
        ms_.form->setVisible(false);
        newBtn->setText(QStringLiteral("+ 新建谜团"));
    });
    connect(mSubmit, &widgets::Button::clicked, this, &WorldBoardView::SubmitCreateMystery);
    mbr->addWidget(mCancel);
    mbr->addWidget(mSubmit);
    form->addWidget(mBtns);
    ms_.form->setVisible(false);
    v->addWidget(ms_.form);

    ms_.listHost = new QWidget(host);
    ms_.listCol = new QVBoxLayout(ms_.listHost);
    ms_.listCol->setContentsMargins(0, 0, 0, 0);
    ms_.listCol->setSpacing(theme::space::kSteps[2]);
    ms_.listCol->addStretch(1);
    v->addWidget(ms_.listHost);

    // —— 下半：知情边界矩阵（角色 × 秘密，`01` §2.4.5 / §2.3.2）——
    v->addWidget(SectionLabel(host, QStringLiteral("知情边界矩阵（角色 × 秘密）")));
    v->addWidget(new QLabel(
        QStringLiteral("知情 = 角色侧边界（`01` §2.3.4）：任何「角色知道某事」都落 character_knowledge / "
                       "secret_knowledge。勾 = 知道，取消 = 不知道；「查询章」切「第 N 章时谁知道什么」。"),
        host));

    auto* ctl = new QWidget(host);
    auto* cr = new QHBoxLayout(ctl);
    cr->setContentsMargins(0, 0, 0, 0);
    cr->setSpacing(theme::space::kSteps[2]);
    ms_.queryCh = new widgets::NumberInput(false, 0, 999999, ctl);
    ms_.queryCh->SetValue(0);
    cr->addWidget(new widgets::Field(QStringLiteral("查询章"), widgets::Field::LabelPos::Left,
                                     ms_.queryCh, ctl));
    ms_.effectiveCh = new widgets::NumberInput(false, 0, 999999, ctl);
    ms_.effectiveCh->SetValue(0);
    cr->addWidget(new widgets::Field(QStringLiteral("生效章"), widgets::Field::LabelPos::Left,
                                     ms_.effectiveCh, ctl));
    cr->addStretch(1);
    v->addWidget(ctl);
    v->addWidget(new QLabel(
        QStringLiteral("查询章 0 = 不限时点（当前态）；生效章 = 勾选记为「第 N 章起知道」，0 = 书前即知。"),
        host));

    auto* sTop = new QWidget(host);
    auto* sr = new QHBoxLayout(sTop);
    sr->setContentsMargins(0, 0, 0, 0);
    sr->setSpacing(theme::space::kSteps[2]);
    auto* sBtn = new widgets::Button(QStringLiteral("+ 新建秘密"), widgets::Button::Variant::Secondary,
                                     widgets::Button::Size::Sm, sTop);
    connect(sBtn, &widgets::Button::clicked, this, [this, sBtn] {
        const bool show = ms_.secretForm != nullptr && !ms_.secretForm->isVisible();
        ms_.secretForm->setVisible(show);
        sBtn->setText(show ? QStringLiteral("收起表单") : QStringLiteral("+ 新建秘密"));
    });
    ms_.matrixNote = new QLabel(QStringLiteral("矩阵 –"), sTop);
    sr->addWidget(sBtn);
    sr->addStretch(1);
    sr->addWidget(ms_.matrixNote, 0, Qt::AlignVCenter);
    v->addWidget(sTop);

    ms_.secretForm = new QWidget(host);
    auto* sform = new QVBoxLayout(ms_.secretForm);
    sform->setContentsMargins(theme::space::kSteps[2], theme::space::kSteps[2],
                              theme::space::kSteps[2], theme::space::kSteps[2]);
    sform->setSpacing(theme::space::kSteps[2]);
    sform->addWidget(SectionLabel(ms_.secretForm, QStringLiteral("新建秘密（矩阵的列）")));
    ms_.sContent = new widgets::TextInput(ms_.secretForm);
    ms_.sContent->SetPlaceholder(QStringLiteral("秘密内容（必填），如：铜钥匙的来历"));
    ms_.wSContent = new widgets::Field(QStringLiteral("内容"), widgets::Field::LabelPos::Left,
                                       ms_.sContent, ms_.secretForm);
    sform->addWidget(ms_.wSContent);
    ms_.sTruth = new widgets::TextInput(ms_.secretForm);
    ms_.sTruth->SetPlaceholder(QStringLiteral("真相：这个秘密实际是什么"));
    sform->addWidget(new widgets::Field(QStringLiteral("真相"), widgets::Field::LabelPos::Left,
                                        ms_.sTruth, ms_.secretForm));
    ms_.sRevealCh = new widgets::NumberInput(false, 0, 999999, ms_.secretForm);
    ms_.sRevealCh->SetValue(0);
    sform->addWidget(new widgets::Field(QStringLiteral("揭示章"), widgets::Field::LabelPos::Left,
                                        ms_.sRevealCh, ms_.secretForm));
    ms_.sRevealCond = new widgets::TextInput(ms_.secretForm);
    ms_.sRevealCond->SetPlaceholder(QStringLiteral("揭示条件（可空）：满足什么才公开"));
    sform->addWidget(new widgets::Field(QStringLiteral("揭示条件"), widgets::Field::LabelPos::Left,
                                        ms_.sRevealCond, ms_.secretForm));
    ms_.sScope = new widgets::Select(false, false, ms_.secretForm);
    ms_.sScope->SetItems({{QStringLiteral("角色 character"), QStringLiteral("范围"), true},
                          {QStringLiteral("世界 world"), QStringLiteral("范围"), false},
                          {QStringLiteral("势力 faction"), QStringLiteral("范围"), false}});
    ms_.wSScope = new widgets::Field(QStringLiteral("范围"), widgets::Field::LabelPos::Left,
                                     ms_.sScope, ms_.secretForm);
    sform->addWidget(ms_.wSScope);
    auto* sBtns = new QWidget(ms_.secretForm);
    auto* sbr = new QHBoxLayout(sBtns);
    sbr->setContentsMargins(0, 0, 0, 0);
    sbr->addStretch(1);
    auto* sCancel = new widgets::Button(QStringLiteral("取消"), widgets::Button::Variant::Ghost,
                                        widgets::Button::Size::Md, sBtns);
    auto* sSubmit = new widgets::Button(QStringLiteral("创建并落库"), widgets::Button::Variant::Primary,
                                        widgets::Button::Size::Md, sBtns);
    connect(sCancel, &widgets::Button::clicked, this, [this, sBtn] {
        ms_.secretForm->setVisible(false);
        sBtn->setText(QStringLiteral("+ 新建秘密"));
    });
    connect(sSubmit, &widgets::Button::clicked, this, &WorldBoardView::SubmitCreateSecret);
    sbr->addWidget(sCancel);
    sbr->addWidget(sSubmit);
    sform->addWidget(sBtns);
    ms_.secretForm->setVisible(false);
    v->addWidget(ms_.secretForm);

    ms_.matrixHost = new QWidget(host);
    ms_.matrixGrid = new QGridLayout(ms_.matrixHost);
    ms_.matrixGrid->setContentsMargins(0, 0, 0, 0);
    ms_.matrixGrid->setHorizontalSpacing(theme::space::kSteps[3]);
    ms_.matrixGrid->setVerticalSpacing(theme::space::kSteps[1]);
    v->addWidget(ms_.matrixHost);
    v->addStretch(1);

    scroll->setWidget(host);
    col->addWidget(scroll, 1);

    connect(ms_.queryCh, &widgets::NumberInput::SetOnChanged ? nullptr : nullptr, this, nullptr);
    return page;
}

// ————————————————————————————————————————————— 刷新

void WorldBoardView::RebuildRelations() {
    // 表单两个实体下拉与「文本 → id」映射随实体表同步（Select 只回文本，名称可能重复 → 文本带 #id）
    rel_.entityIds.clear();
    std::vector<widgets::Select::Item> opts;
    if (db_ && rel_.fromSel != nullptr) {
        novelcore::NovelGraph g(*db_);
        if (auto list = g.ListEntities({}, {}, 200); list) {
            for (const novelcore::EntityRow& e : *list) {
                const QString text =
                    QStringLiteral("%1（#%2）").arg(QString::fromStdString(e.name)).arg(e.id);
                rel_.entityIds.insert(text, e.id);
                opts.push_back({text, KindLabelOf(e.kind), false});
            }
        }
    }
    if (rel_.fromSel != nullptr) {
        rel_.fromSel->SetItems(opts);
    }
    if (rel_.toSel != nullptr) {
        rel_.toSel->SetItems(opts);
    }

    ClearLayout(rel_.listCol);
    if (rel_.listCol != nullptr) {
        rel_.listCol->addStretch(1);
    }
    if (!db_) {
        rel_.count->setText(QStringLiteral("关系 – 条"));
        auto* es = new widgets::EmptyState(
            QStringLiteral("🕸"), QStringLiteral("还没有打开小说库"),
            QStringLiteral("关系存在 <书>/db/novel.db 的 relations 表；先打开或创建书库。"),
            QStringLiteral("重新扫描"), rel_.listHost);
        es->SetOnAction([this] {
            if (refStore_) {
                LoadFromRef(*refStore_, lastNovel_);
            }
        });
        rel_.listCol->insertWidget(0, es);
        return;
    }
    const std::vector<RelationRow> rows = ReadRelations();
    rel_.count->setText(QStringLiteral("关系 %1 条（%2）").arg(static_cast<int>(rows.size())).arg(bookTitle_));
    if (rows.empty()) {
        auto* es = new widgets::EmptyState(
            QStringLiteral("🔗"), QStringLiteral("还没有关系"),
            QStringLiteral("关系是时间化边：from → to + rel_type + 生效章区间（`01` §2.4.1）。"
                           "先建两个实体，再点「+ 新建关系」连起来。"),
            QStringLiteral("新建关系"), rel_.listHost);
        es->SetOnAction([this] { rel_.form->setVisible(true); });
        rel_.listCol->insertWidget(0, es);
        return;
    }
    novelcore::NovelGraph g(*db_);
    int insertAt = 0;
    for (const RelationRow& r : rows) {
        QString fromName = QStringLiteral("#%1").arg(r.from_id);
        QString toName = QStringLiteral("#%1").arg(r.to_id);
        if (auto e = g.GetEntity(r.from_id); e) {
            fromName = QString::fromStdString(e->name);
        }
        if (auto e = g.GetEntity(r.to_id); e) {
            toName = QString::fromStdString(e->name);
        }
        auto* card = new widgets::Card(widgets::Card::Variant::Outlined, rel_.listHost);
        auto* body = card->BodyLayout();
        auto* row1 = new QWidget(card);
        auto* rr = new QHBoxLayout(row1);
        rr->setContentsMargins(0, 0, 0, 0);
        rr->setSpacing(theme::space::kSteps[1]);
        auto* from = new QLabel(fromName, row1);
        widgets::SetSemibold(from, true);
        auto* type = new widgets::Tag(
            QStringLiteral("%1 · %2").arg(RelLabelOf(r.rel_type)).arg(r.strength), "accent", false, row1);
        auto* to = new QLabel(toName, row1);
        widgets::SetSemibold(to, true);
        rr->addWidget(from, 0);
        rr->addWidget(new QLabel(QStringLiteral("→"), row1), 0);
        rr->addWidget(type, 0);
        rr->addWidget(to, 0);
        rr->addStretch(1);
        rr->addWidget(new widgets::Tag(r.status == "ended" ? QStringLiteral("已结束 ended")
                                                           : QStringLiteral("生效中 active"),
                                       r.status == "ended" ? "" : "ok", false, row1), 0);
        body->addWidget(row1);
        auto* row2 = new QLabel(
            QStringLiteral("%1 · %2%3")
                .arg(RelSpanView(r.from_chapter, r.to_chapter),
                     r.reason.empty() ? QStringLiteral("（未记成因）") : QString::fromStdString(r.reason),
                     QStringLiteral("　#%1").arg(r.id)),
            card);
        body->addWidget(row2);
        card->SetOnClick([this, id = r.id] { OpenRelationDrawer(id); });
        rel_.listCol->insertWidget(insertAt++, card);
    }
}

void WorldBoardView::RebuildPlots() {
    ClearLayout(plot_.listCol);
    if (plot_.listCol != nullptr) {
        plot_.listCol->addStretch(1);
    }
    if (!db_) {
        plot_.count->setText(QStringLiteral("剧情线 – 条"));
        return;
    }
    novelcore::NovelGraph g(*db_);
    auto plots = g.ListPlots({}, 100);
    if (!plots) {
        plot_.count->setText(QStringLiteral("读取失败：%1")
                                 .arg(QString::fromStdString(plots.error().message)));
        return;
    }
    plot_.count->setText(
        QStringLiteral("剧情线 %1 条（%2）").arg(static_cast<int>(plots->size())).arg(bookTitle_));
    if (plots->empty()) {
        auto* es = new widgets::EmptyState(
            QStringLiteral("🧭"), QStringLiteral("还没有剧情线"),
            QStringLiteral("剧情线是「这条故事往哪走」的账本（`01` §2.5）：kind + 标题 + 目标章，"
                           "再按 setup→rising→… 排节拍。"),
            QStringLiteral("新建剧情线"), plot_.listHost);
        es->SetOnAction([this] { plot_.form->setVisible(true); });
        plot_.listCol->insertWidget(0, es);
        return;
    }
    int insertAt = 0;
    for (const PlotRow& p : *plots) {
        auto* card = new widgets::Card(widgets::Card::Variant::Outlined, plot_.listHost);
        auto* body = card->BodyLayout();
        auto* top = new QWidget(card);
        auto* tr = new QHBoxLayout(top);
        tr->setContentsMargins(0, 0, 0, 0);
        tr->setSpacing(theme::space::kSteps[1]);
        auto* title = new QLabel(QString::fromStdString(p.title), top);
        widgets::SetSemibold(title, true);
        tr->addWidget(new widgets::Tag(PlotKindLabelOf(p.kind), "accent", false, top), 0);
        tr->addWidget(title, 1);
        tr->addWidget(new QLabel(QStringLiteral("目标 %1 · 引入 %2")
                                     .arg(ChapterView(p.target_ch, "未排定"),
                                          ChapterView(p.intro_ch, "未排定")),
                                 top), 0);
        body->addWidget(top);
        if (!p.note.empty()) {
            body->addWidget(new QLabel(QString::fromStdString(p.note), card));
        }

        // 节拍行内列表 + 行内新建
        auto beats = g.ListPlotBeats(p.id);
        body->addWidget(SectionLabel(card, QStringLiteral("节拍（%1）")
                                               .arg(beats ? static_cast<int>(beats->size()) : 0)));
        if (beats && !beats->empty()) {
            for (const PlotBeatRow& b : *beats) {
                body->addWidget(new QLabel(
                    QStringLiteral("#%1 %2 · %3 · %4%5")
                        .arg(b.ord)
                        .arg(ChapterView(b.chapter_id, "未排定章"), PlotBeatLabelOf(b.beat_type),
                             QString::fromStdString(b.title),
                             b.summary.empty() ? QString{} : QStringLiteral(" — %1")
                                                         .arg(QString::fromStdString(b.summary))),
                    card));
            }
        } else {
            body->addWidget(new QLabel(
                QStringLiteral("（还没有节拍——点「+ 节拍」排第一拍，如 setup「起雾的灯塔」）"), card));
        }

        auto* beatBtn = new widgets::Button(QStringLiteral("+ 节拍"), widgets::Button::Variant::Ghost,
                                            widgets::Button::Size::Sm, card);
        auto* beatForm = new QWidget(card);
        auto* bv = new QVBoxLayout(beatForm);
        bv->setContentsMargins(theme::space::kSteps[2], 0, 0, 0);
        bv->setSpacing(theme::space::kSteps[1]);
        auto* bType = new widgets::Select(false, false, beatForm);
        {
            std::vector<widgets::Select::Item> items;
            for (std::size_t i = 0; i < kPlotBeatTypes.size(); ++i) {
                items.push_back({QString::fromUtf8(kPlotBeatLabels[i].data(),
                                                   static_cast<qsizetype>(kPlotBeatLabels[i].size())),
                                 QStringLiteral("节拍类型"), i == 0});
            }
            bType->SetItems(std::move(items));
        }
        auto* bTypeW = new widgets::Field(QStringLiteral("节拍类型"), widgets::Field::LabelPos::Left,
                                          bType, beatForm);
        bv->addWidget(bTypeW);
        auto* bCh = new widgets::NumberInput(false, 0, 999999, beatForm);
        bCh->SetValue(0);
        bv->addWidget(new widgets::Field(QStringLiteral("章序"), widgets::Field::LabelPos::Left,
                                         bCh, beatForm));
        auto* bOrd = new widgets::NumberInput(false, 0, 9999, beatForm);
        bOrd->SetValue(beats ? static_cast<double>(beats->size()) : 0.0);
        bv->addWidget(new widgets::Field(QStringLiteral("排序 ord"), widgets::Field::LabelPos::Left,
                                         bOrd, beatForm));
        auto* bTitle = new widgets::TextInput(beatForm);
        bTitle->SetPlaceholder(QStringLiteral("节拍标题（必填），如：起雾的灯塔"));
        bv->addWidget(new widgets::Field(QStringLiteral("标题"), widgets::Field::LabelPos::Left,
                                         bTitle, beatForm));
        auto* bSum = new widgets::TextInput(beatForm);
        bSum->SetPlaceholder(QStringLiteral("一句话摘要（可空）"));
        bv->addWidget(new widgets::Field(QStringLiteral("摘要"), widgets::Field::LabelPos::Left,
                                         bSum, beatForm));
        auto* bSave = new widgets::Button(QStringLiteral("保存节拍"), widgets::Button::Variant::Primary,
                                          widgets::Button::Size::Sm, beatForm);
        connect(bSave, &widgets::Button::clicked, this,
                [this, plotId = p.id, bType, bTypeW, bCh, bOrd, bTitle, bSum] {
                    bTypeW->SetError(QString{});
                    QString err;
                    const bool ok =
                        CreatePlotBeat(plotId, static_cast<qint64>(bCh->Value()),
                                       static_cast<int>(bOrd->Value()),
                                       QString::fromStdString(PlotBeatKeyOfLabel(FirstChecked(*bType))),
                                       bTitle->Text(), bSum->Text(), &err);
                    if (ok) {
                        widgets::Toast::Show(QStringLiteral("节拍已落库"), widgets::Toast::Tone::Success);
                        return;
                    }
                    bTypeW->SetError(err);
                    widgets::Toast::Show(QStringLiteral("节拍被拦下：见红字原因"),
                                         widgets::Toast::Tone::Error);
                });
        bv->addWidget(bSave, 0, Qt::AlignRight);
        beatForm->setVisible(false);
        connect(beatBtn, &widgets::Button::clicked, beatForm, [beatForm] {
            beatForm->setVisible(!beatForm->isVisible());
        });
        auto* btnRow = new QWidget(card);
        auto* br = new QHBoxLayout(btnRow);
        br->setContentsMargins(0, 0, 0, 0);
        br->addWidget(beatBtn);
        br->addStretch(1);
        body->addWidget(btnRow);
        body->addWidget(beatForm);

        plot_.listCol->insertWidget(insertAt++, card);
    }
}

void WorldBoardView::RebuildForeshadows() {
    ClearLayout(fs_.listCol);
    if (fs_.listCol != nullptr) {
        fs_.listCol->addStretch(1);
    }
    if (!db_) {
        fs_.count->setText(QStringLiteral("伏笔 – 条"));
        return;
    }
    const std::vector<ForeshadowRow> rows = ReadForeshadows();
    fs_.count->setText(
        QStringLiteral("伏笔 %1 条（%2）").arg(static_cast<int>(rows.size())).arg(bookTitle_));
    if (rows.empty()) {
        auto* es = new widgets::EmptyState(
            QStringLiteral("🪡"), QStringLiteral("还没有伏笔"),
            QStringLiteral("伏笔是作者侧账本（`01` §2.3.4）：打算何时埋、何时回收哪个埋点。"
                           "新建即 PLANNED，之后沿状态机推进。"),
            QStringLiteral("新建伏笔"), fs_.listHost);
        es->SetOnAction([this] { fs_.form->setVisible(true); });
        fs_.listCol->insertWidget(0, es);
        return;
    }
    int insertAt = 0;
    for (const ForeshadowRow& f : rows) {
        const int seg = fstate::SegOf(f.status);
        auto* card = new widgets::Card(widgets::Card::Variant::Outlined, fs_.listHost);
        auto* body = card->BodyLayout();

        auto* top = new QWidget(card);
        auto* tr = new QHBoxLayout(top);
        tr->setContentsMargins(0, 0, 0, 0);
        tr->setSpacing(theme::space::kSteps[1]);
        auto* title = new QLabel(QString::fromStdString(f.title), top);
        widgets::SetSemibold(title, true);
        tr->addWidget(title, 1);
        tr->addWidget(new widgets::Tag(fstate::LabelOf(f.status), ForeshadowToneOf(f.status), false, top), 0);
        tr->addWidget(new QLabel(QStringLiteral("重要度 %1").arg(f.importance), top), 0);
        tr->addWidget(new QLabel(QStringLiteral("埋 %1 · 回收 %2")
                                     .arg(ChapterView(f.setup_ch, "未排定"),
                                          ChapterView(f.payoff_ch, "未排定")),
                                 top), 0);
        body->addWidget(top);

        body->addWidget(new QLabel(
            f.content.empty() ? QStringLiteral("（未记埋点内容）")
                              : ElideText(QString::fromStdString(f.content), 120),
            card));
        if (!f.truth.empty()) {
            auto* truth = new QLabel(
                QStringLiteral("真相（作者侧）：%1").arg(ElideText(QString::fromStdString(f.truth), 100)),
                card);
            truth->setStyleSheet(QStringLiteral("color:%1;").arg(CssRgb(theme::Current().textMuted)));
            body->addWidget(truth);
        }

        // 五段状态 Timeline（UI.md §2.2：PLANNED→…→RESOLVED 五段进度；高亮段 = 当前状态）
        auto* tl = new data::Timeline(Qt::Horizontal, card);
        std::vector<data::Timeline::Event> evs;
        evs.reserve(fstate::kStates.size());
        for (std::size_t i = 0; i < fstate::kStates.size(); ++i) {
            evs.push_back({QStringLiteral("第 %1 段").arg(static_cast<int>(i) + 1),
                           FStatePickLabel(i),
                           QString::fromUtf8(fstate::kPhrases[i].data(),
                                             static_cast<qsizetype>(fstate::kPhrases[i].size()))});
        }
        tl->SetEvents(std::move(evs));
        tl->SetCurrent(seg > 0 ? seg - 1 : -1);
        tl->setMinimumHeight(104);
        tl->setToolTip(QStringLiteral("伏笔状态机五段（`01` §2.3.3）：当前第 %1/5 段").arg(seg));
        body->addWidget(tl);

        // 推进按钮：只给状态机允许的下一状态（非法的不给）
        auto* btnRow = new QWidget(card);
        auto* br = new QHBoxLayout(btnRow);
        br->setContentsMargins(0, 0, 0, 0);
        br->setSpacing(theme::space::kSteps[1]);
        const auto addTarget = [this, &br, btnRow, id = f.id](std::string_view to,
                                                              const QString& text) {
            auto* btn = new widgets::Button(text, widgets::Button::Variant::Secondary,
                                            widgets::Button::Size::Sm, btnRow);
            connect(btn, &widgets::Button::clicked, this, [this, id, to] {
                QString err;
                if (TransitionForeshadow(id, QString::fromStdString(std::string{to}), &err)) {
                    widgets::Toast::Show(QStringLiteral("伏笔已推进到 %1")
                                             .arg(fstate::LabelOf(to)),
                                         widgets::Toast::Tone::Success);
                } else {
                    widgets::Toast::Show(err, widgets::Toast::Tone::Error);
                }
            });
            br->addWidget(btn);
        };
        if (seg == 1) {
            addTarget("PLANTED", QStringLiteral("埋下 → PLANTED"));
            addTarget("REVEALED", QStringLiteral("短伏笔直跳 → REVEALED"));
        } else if (seg == 2) {
            addTarget("DEVELOPING", QStringLiteral("推进 → DEVELOPING"));
        } else if (seg == 3) {
            addTarget("REVEALED", QStringLiteral("揭示 → REVEALED"));
        } else if (seg == 4) {
            addTarget("RESOLVED", QStringLiteral("收束 → RESOLVED"));
        } else {
            br->addWidget(new QLabel(QStringLiteral("已收束 RESOLVED：状态机终点，无可推进"), btnRow));
        }
        br->addStretch(1);
        body->addWidget(btnRow);

        // 高级：指定目标状态（任意跃迁入口；非法跃迁在此给中文原因）
        auto* advBtn = new widgets::Button(QStringLiteral("高级：指定目标状态"),
                                           widgets::Button::Variant::Ghost,
                                           widgets::Button::Size::Sm, card);
        advBtn->setCheckable(true);
        auto* advRow = new QWidget(card);
        auto* ar = new QHBoxLayout(advRow);
        ar->setContentsMargins(0, 0, 0, 0);
        ar->setSpacing(theme::space::kSteps[1]);
        auto* jumpSel = new widgets::Select(false, false, advRow);
        {
            std::vector<widgets::Select::Item> items;
            for (std::size_t i = 0; i < fstate::kStates.size(); ++i) {
                items.push_back({FStatePickLabel(i), QStringLiteral("目标状态"),
                                 static_cast<int>(i) + 1 == seg});
            }
            jumpSel->SetItems(std::move(items));
        }
        auto* jumpW = new widgets::Field(QStringLiteral("指定目标状态"), widgets::Field::LabelPos::Top,
                                         jumpSel, advRow);
        jumpW->SetHelp(QStringLiteral("按 `01` §2.3.3 校验：逐段推进，或短伏笔 PLANNED→REVEALED；"
                                      "非法跃迁在红字给出原因"));
        auto* jumpBtn = new widgets::Button(QStringLiteral("跃迁"), widgets::Button::Variant::Secondary,
                                            widgets::Button::Size::Sm, advRow);
        connect(jumpBtn, &widgets::Button::clicked, this, [this, id = f.id, jumpSel, jumpW] {
            jumpW->SetError(QString{});
            QString err;
            const QString to = QString::fromStdString(FStateKeyOfLabel(FirstChecked(*jumpSel)));
            if (TransitionForeshadow(id, to, &err)) {
                widgets::Toast::Show(QStringLiteral("伏笔已跃迁到 %1").arg(fstate::LabelOf(
                                         FStateKeyOfLabel(FirstChecked(*jumpSel)))),
                                     widgets::Toast::Tone::Success);
            } else {
                jumpW->SetError(err); // 非法跃迁：中文原因留在表单错误态
                widgets::Toast::Show(QStringLiteral("跃迁被拦下：见红字原因"),
                                     widgets::Toast::Tone::Error);
            }
        });
        ar->addWidget(jumpW, 1);
        ar->addWidget(jumpBtn, 0, Qt::AlignBottom);
        advRow->setVisible(false);
        connect(advBtn, &widgets::Button::toggled, advRow, &QWidget::setVisible);
        body->addWidget(advBtn);
        body->addWidget(advRow);

        fs_.listCol->insertWidget(insertAt++, card);
    }
}

void WorldBoardView::RebuildMysteries() {
    ClearLayout(ms_.listCol);
    if (ms_.listCol != nullptr) {
        ms_.listCol->addStretch(1);
    }
    if (!db_) {
        ms_.count->setText(QStringLiteral("谜团 – 条"));
        return;
    }
    novelcore::NovelGraph g(*db_);
    auto mysteries = g.ListMysteries(0, 100);
    if (!mysteries) {
        ms_.count->setText(QStringLiteral("读取失败：%1")
                               .arg(QString::fromStdString(mysteries.error().message)));
        return;
    }
    ms_.count->setText(
        QStringLiteral("谜团 %1 条（%2）").arg(static_cast<int>(mysteries->size())).arg(bookTitle_));
    if (mysteries->empty()) {
        auto* es = new widgets::EmptyState(
            QStringLiteral("❓"), QStringLiteral("还没有谜团"),
            QStringLiteral("谜团记「读者被引导成什么样、何时给答案」（`01` §2.3.4）：问题 + 提问/答案章，"
                           "再按 question→hint→reveal→answer 排节拍。"),
            QStringLiteral("新建谜团"), ms_.listHost);
        es->SetOnAction([this] { ms_.form->setVisible(true); });
        ms_.listCol->insertWidget(0, es);
        return;
    }
    int insertAt = 0;
    for (const MysteryRow& m : *mysteries) {
        auto* card = new widgets::Card(widgets::Card::Variant::Outlined, ms_.listHost);
        auto* body = card->BodyLayout();
        auto* top = new QWidget(card);
        auto* tr = new QHBoxLayout(top);
        tr->setContentsMargins(0, 0, 0, 0);
        tr->setSpacing(theme::space::kSteps[1]);
        auto* q = new QLabel(QString::fromStdString(m.question), top);
        widgets::SetSemibold(q, true);
        tr->addWidget(q, 1);
        tr->addWidget(new widgets::Tag(MysteryStatusLabelOf(m.status), "info", false, top), 0);
        tr->addWidget(new QLabel(QStringLiteral("重要度 %1").arg(m.importance), top), 0);
        body->addWidget(top);
        body->addWidget(new QLabel(
            QStringLiteral("提问 %1 · 答案 %2 · 答案（预设）：%3")
                .arg(ChapterView(m.ask_ch, "未排定"), ChapterView(m.answer_ch, "未排定"),
                     m.answer.empty() ? QStringLiteral("（未设）") : QString::fromStdString(m.answer)),
            card));

        auto beats = g.ListMysteryBeats(m.id);
        body->addWidget(SectionLabel(card, QStringLiteral("节拍（%1）")
                                               .arg(beats ? static_cast<int>(beats->size()) : 0)));
        if (beats && !beats->empty()) {
            for (const MysteryBeatRow& b : *beats) {
                body->addWidget(new QLabel(
                    QStringLiteral("#%1 %2 · %3%4")
                        .arg(b.ord)
                        .arg(ChapterView(b.chapter_id, "未排定章"), MysteryBeatLabelOf(b.beat_type),
                             b.content.empty() ? QString{}
                                               : QStringLiteral(" — %1")
                                                     .arg(ElideText(QString::fromStdString(b.content), 60))),
                    card));
            }
        } else {
            body->addWidget(new QLabel(
                QStringLiteral("（还没有节拍——点「+ 节拍」放第一条线索 hint）"), card));
        }

        auto* beatBtn = new widgets::Button(QStringLiteral("+ 节拍"), widgets::Button::Variant::Ghost,
                                            widgets::Button::Size::Sm, card);
        auto* beatForm = new QWidget(card);
        auto* bv = new QVBoxLayout(beatForm);
        bv->setContentsMargins(theme::space::kSteps[2], 0, 0, 0);
        bv->setSpacing(theme::space::kSteps[1]);
        auto* bType = new widgets::Select(false, false, beatForm);
        {
            std::vector<widgets::Select::Item> items;
            for (std::size_t i = 0; i < kMysteryBeatTypes.size(); ++i) {
                items.push_back({QString::fromUtf8(kMysteryBeatLabels[i].data(),
                                                   static_cast<qsizetype>(kMysteryBeatLabels[i].size())),
                                 QStringLiteral("节拍类型"), i == 1});
            }
            bType->SetItems(std::move(items));
        }
        auto* bTypeW = new widgets::Field(QStringLiteral("节拍类型"), widgets::Field::LabelPos::Left,
                                          bType, beatForm);
        bv->addWidget(bTypeW);
        auto* bCh = new widgets::NumberInput(false, 0, 999999, beatForm);
        bCh->SetValue(0);
        bv->addWidget(new widgets::Field(QStringLiteral("章序"), widgets::Field::LabelPos::Left,
                                         bCh, beatForm));
        auto* bContent = new widgets::TextInput(beatForm);
        bContent->SetPlaceholder(QStringLiteral("节拍内容（必填），如：雨夜里多出一串脚印"));
        bv->addWidget(new widgets::Field(QStringLiteral("内容"), widgets::Field::LabelPos::Left,
                                         bContent, beatForm));
        auto* bSave = new widgets::Button(QStringLiteral("保存节拍"), widgets::Button::Variant::Primary,
                                          widgets::Button::Size::Sm, beatForm);
        connect(bSave, &widgets::Button::clicked, this,
                [this, mysteryId = m.id, bType, bTypeW, bCh, bContent] {
                    bTypeW->SetError(QString{});
                    QString err;
                    const bool ok = CreateMysteryBeat(
                        mysteryId,
                        QString::fromStdString(MysteryBeatKeyOfLabel(FirstChecked(*bType))),
                        static_cast<qint64>(bCh->Value()), bContent->Text(), &err);
                    if (ok) {
                        widgets::Toast::Show(QStringLiteral("节拍已落库"), widgets::Toast::Tone::Success);
                        return;
                    }
                    bTypeW->SetError(err);
                    widgets::Toast::Show(QStringLiteral("节拍被拦下：见红字原因"),
                                         widgets::Toast::Tone::Error);
                });
        bv->addWidget(bSave, 0, Qt::AlignRight);
        beatForm->setVisible(false);
        connect(beatBtn, &widgets::Button::clicked, beatForm, [beatForm] {
            beatForm->setVisible(!beatForm->isVisible());
        });
        auto* btnRow = new QWidget(card);
        auto* br = new QHBoxLayout(btnRow);
        br->setContentsMargins(0, 0, 0, 0);
        br->addWidget(beatBtn);
        br->addStretch(1);
        body->addWidget(btnRow);
        body->addWidget(beatForm);

        ms_.listCol->insertWidget(insertAt++, card);
    }
}

void WorldBoardView::RebuildMatrix() {
    if (ms_.matrixGrid == nullptr) {
        return;
    }
    ClearLayout(ms_.matrixGrid);
    if (!db_) {
        ms_.matrixNote->setText(QStringLiteral("未打开小说库，矩阵不可用"));
        return;
    }
    novelcore::NovelGraph g(*db_);
    auto persons = g.ListEntities(novelcore::kind::person, {}, 200);
    const std::vector<SecretRow> secrets = ReadSecrets();
    const int queryCh = ms_.queryCh != nullptr ? static_cast<int>(ms_.queryCh->Value()) : 0;
    const qint64 effectiveCh =
        ms_.effectiveCh != nullptr ? static_cast<qint64>(ms_.effectiveCh->Value()) : 0;

    if (!persons || persons->empty() || secrets.empty()) {
        ms_.matrixNote->setText(QStringLiteral("矩阵空：需要至少 1 位人物（「实体」页）与 1 条秘密（「新建秘密」）"));
        auto* es = new widgets::EmptyState(
            QStringLiteral("🕵️"), QStringLiteral("知情边界还查不了"),
            QStringLiteral("矩阵 = 角色 × 秘密（`01` §2.4.5）：先在「实体」页建人物，"
                           "再点「+ 新建秘密」建第一条秘密，然后勾「知道 / 不知道」。"),
            QStringLiteral("新建秘密"), ms_.matrixHost);
        es->SetOnAction([this] { ms_.secretForm->setVisible(true); });
        ms_.matrixGrid->addWidget(es, 0, 0, 1, 3);
        return;
    }

    int knownNow = 0;
    int totalCells = 0;
    ms_.matrixGrid->addWidget(new QLabel(QStringLiteral("角色 ＼ 秘密"), ms_.matrixHost), 0, 0);
    const int maxCols = 8;
    const int maxRows = 20;
    const int colN = static_cast<int>(std::min<std::size_t>(secrets.size(), maxCols));
    for (int c = 0; c < colN; ++c) {
        const SecretRow& s = secrets[static_cast<std::size_t>(c)];
        auto* head = new QLabel(
            QStringLiteral("秘密#%1\n%2").arg(s.id).arg(ElideText(QString::fromStdString(s.content), 10)),
            ms_.matrixHost);
        widgets::SetSemibold(head, true);
        head->setToolTip(QStringLiteral("秘密#%1「%2」\n真相：%3\n揭示：%4（%5，范围 %6）")
                             .arg(s.id)
                             .arg(QString::fromStdString(s.content),
                                  s.truth.empty() ? QStringLiteral("（未设）")
                                                  : QString::fromStdString(s.truth),
                                  ChapterView(s.reveal_ch, "未排定"),
                                  s.reveal_condition.empty() ? QStringLiteral("无条件")
                                                             : QString::fromStdString(s.reveal_condition),
                                  SecretScopeLabelOf(s.scope)));
        ms_.matrixGrid->addWidget(head, 0, c + 1);
    }
    const int rowN = static_cast<int>(std::min<std::size_t>(persons->size(), maxRows));
    for (int r = 0; r < rowN; ++r) {
        const novelcore::EntityRow& p = (*persons)[static_cast<std::size_t>(r)];
        ms_.matrixGrid->addWidget(new QLabel(QString::fromStdString(p.name), ms_.matrixHost), r + 1, 0);
        for (int c = 0; c < colN; ++c) {
            const SecretRow& s = secrets[static_cast<std::size_t>(c)];
            const auto knows = g.CharacterKnows(p.id, std::string{kFactSecret}, s.id, queryCh);
            const bool on = knows && *knows;
            ++totalCells;
            if (on) {
                ++knownNow;
            }
            auto* cb = new widgets::Checkbox(QString{}, ms_.matrixHost);
            cb->setToolTip(
                QStringLiteral("%1 × 秘密#%2「%3」：%4。勾 = 知道（按「生效章」记录），取消 = 不知道")
                    .arg(QString::fromStdString(p.name))
                    .arg(s.id)
                    .arg(ElideText(QString::fromStdString(s.content), 16))
                    .arg(on ? QStringLiteral("知道") : QStringLiteral("不知道")));
            cb->SetChecked(on); // SetChecked 会触发回调：先置值、后挂回调，避免误写
            cb->SetOnToggled([this, cb, pId = p.id, sId = s.id, effectiveCh](bool checked) {
                QString err;
                if (SetKnows(pId, sId, checked, effectiveCh, &err)) {
                    RebuildMatrix();
                    return;
                }
                cb->SetChecked(!checked); // 写失败回滚勾选（回调内不再触发：SetChecked 之后立即重挂空回调）
                cb->SetOnToggled({});
                cb->SetChecked(!checked);
                widgets::Toast::Show(err, widgets::Toast::Tone::Error);
            });
            ms_.matrixGrid->addWidget(cb, r + 1, c + 1);
        }
    }
    QString note = QStringLiteral("查询章 %1：%2 格知道 / 共 %3 格（%4 角色 × %5 秘密）")
                       .arg(queryCh)
                       .arg(knownNow)
                       .arg(totalCells)
                       .arg(rowN)
                       .arg(colN);
    if (static_cast<int>(persons->size()) > maxRows || static_cast<int>(secrets.size()) > maxCols) {
        note += QStringLiteral("；显示截断 %1 人物 / %2 秘密").arg(maxRows).arg(maxCols);
    }
    ms_.matrixNote->setText(note);
}

// ————————————————————————————————————————————— 提交（表单与探针同路径）

void WorldBoardView::SubmitCreateRelation() {
    rel_.wFrom->SetError(QString{});
    rel_.wTo->SetError(QString{});
    rel_.wType->SetError(QString{});
    QString err;
    const qint64 fromId = rel_.entityIds.value(FirstChecked(*rel_.fromSel), 0);
    const qint64 toId = rel_.entityIds.value(FirstChecked(*rel_.toSel), 0);
    const bool ok =
        CreateRelation(fromId, toId,
                       QString::fromStdString(RelKeyOfLabel(FirstChecked(*rel_.typeSel))),
                       static_cast<int>(rel_.strength->Value()), static_cast<qint64>(rel_.fromCh->Value()),
                       static_cast<qint64>(rel_.toCh->Value()), rel_.reason->Text(), &err);
    if (ok) {
        widgets::Toast::Show(QStringLiteral("关系已落库"), widgets::Toast::Tone::Success);
        rel_.reason->SetText(QString{});
        rel_.form->setVisible(false);
        return;
    }
    if (err.startsWith(QStringLiteral("终点实体"))) {
        rel_.wTo->SetError(err);
    } else if (err.startsWith(QStringLiteral("关系类型")) || err.startsWith(QStringLiteral("强度"))) {
        rel_.wType->SetError(err);
    } else {
        rel_.wFrom->SetError(err);
    }
    widgets::Toast::Show(QStringLiteral("新建关系被拦下：见表单红字原因"), widgets::Toast::Tone::Error);
}

void WorldBoardView::SubmitCreatePlot() {
    plot_.wKind->SetError(QString{});
    plot_.wTitle->SetError(QString{});
    QString err;
    const bool ok =
        CreatePlot(QString::fromStdString(PlotKindKeyOfLabel(FirstChecked(*plot_.kindSel))),
                   plot_.title->Text(), static_cast<qint64>(plot_.introCh->Value()),
                   static_cast<qint64>(plot_.targetCh->Value()), plot_.note->Text(), &err);
    if (ok) {
        widgets::Toast::Show(QStringLiteral("剧情线已落库"), widgets::Toast::Tone::Success);
        plot_.title->SetText(QString{});
        plot_.note->SetText(QString{});
        plot_.form->setVisible(false);
        return;
    }
    if (err.startsWith(QStringLiteral("标题"))) {
        plot_.wTitle->SetError(err);
    } else if (err.startsWith(QStringLiteral("类型"))) {
        plot_.wKind->SetError(err);
    } else {
        plot_.wTitle->SetError(err);
    }
    widgets::Toast::Show(QStringLiteral("新建剧情线被拦下：见表单红字原因"), widgets::Toast::Tone::Error);
}

void WorldBoardView::SubmitCreateForeshadow() {
    fs_.wTitle->SetError(QString{});
    fs_.wSetupCh->SetError(QString{});
    QString err;
    const bool ok = CreateForeshadow(fs_.title->Text(), fs_.content->Text(),
                                     static_cast<qint64>(fs_.setupCh->Value()),
                                     static_cast<qint64>(fs_.payoffCh->Value()),
                                     static_cast<int>(fs_.importance->Value()), fs_.truth->Text(), &err);
    if (ok) {
        widgets::Toast::Show(QStringLiteral("伏笔已落库（PLANNED）"), widgets::Toast::Tone::Success);
        fs_.title->SetText(QString{});
        fs_.content->SetText(QString{});
        fs_.truth->SetText(QString{});
        fs_.form->setVisible(false);
        return;
    }
    if (err.startsWith(QStringLiteral("埋设"))) {
        fs_.wSetupCh->SetError(err);
    } else {
        fs_.wTitle->SetError(err);
    }
    widgets::Toast::Show(QStringLiteral("新建伏笔被拦下：见表单红字原因"), widgets::Toast::Tone::Error);
}

void WorldBoardView::SubmitCreateMystery() {
    ms_.wQuestion->SetError(QString{});
    QString err;
    const bool ok = CreateMystery(ms_.question->Text(), ms_.answer->Text(),
                                  static_cast<qint64>(ms_.askCh->Value()),
                                  static_cast<qint64>(ms_.answerCh->Value()),
                                  static_cast<int>(ms_.importance->Value()), &err);
    if (ok) {
        widgets::Toast::Show(QStringLiteral("谜团已落库（open）"), widgets::Toast::Tone::Success);
        ms_.question->SetText(QString{});
        ms_.answer->SetText(QString{});
        ms_.form->setVisible(false);
        return;
    }
    ms_.wQuestion->SetError(err);
    widgets::Toast::Show(QStringLiteral("新建谜团被拦下：见表单红字原因"), widgets::Toast::Tone::Error);
}

void WorldBoardView::SubmitCreateSecret() {
    ms_.wSContent->SetError(QString{});
    ms_.wSScope->SetError(QString{});
    QString err;
    const bool ok = CreateSecret(ms_.sContent->Text(), ms_.sTruth->Text(),
                                 static_cast<qint64>(ms_.sRevealCh->Value()), ms_.sRevealCond->Text(),
                                 0, QString::fromStdString(SecretScopeKeyOfLabel(FirstChecked(*ms_.sScope))),
                                 &err);
    if (ok) {
        widgets::Toast::Show(QStringLiteral("秘密已落库（矩阵新列）"), widgets::Toast::Tone::Success);
        ms_.sContent->SetText(QString{});
        ms_.sTruth->SetText(QString{});
        ms_.sRevealCond->SetText(QString{});
        ms_.secretForm->setVisible(false);
        return;
    }
    if (err.startsWith(QStringLiteral("范围"))) {
        ms_.wSScope->SetError(err);
    } else {
        ms_.wSContent->SetError(err);
    }
    widgets::Toast::Show(QStringLiteral("新建秘密被拦下：见表单红字原因"), widgets::Toast::Tone::Error);
}

// ————————————————————————————————————————————— Drawer：关系两端实体卡摘要

void WorldBoardView::OpenRelationDrawer(qint64 relationId) {
    if (drawer_ != nullptr) {
        drawer_->close(); // WA_DeleteOnClose：旧抽屉让位（QPointer 自动清空）
    }
    auto* drawer = new widgets::Drawer(QStringLiteral("关系详情"), this);
    drawer_ = drawer;
    QVBoxLayout* body = drawer->BodyLayout();

    if (!db_) {
        body->addWidget(new QLabel(QStringLiteral("库未打开，无法查看详情。"), drawer));
        drawer->Open();
        return;
    }
    const std::vector<RelationRow> rows = ReadRelations();
    const auto it = std::ranges::find_if(rows, [relationId](const RelationRow& r) {
        return r.id == relationId;
    });
    if (it == rows.end()) {
        body->addWidget(new QLabel(QStringLiteral("关系 #%1 不存在（列表可能已刷新，请重新点行）。").arg(relationId),
                                   drawer));
        drawer->Open();
        return;
    }
    const RelationRow& r = *it;
    novelcore::NovelGraph g(*db_);

    auto* kv = new data::KeyValue(drawer);
    kv->SetPairs({{QStringLiteral("关系类型"),
                   QStringLiteral("%1（%2）").arg(RelLabelOf(r.rel_type),
                                                 QString::fromStdString(r.rel_type))},
                  {QStringLiteral("强度"), QStringLiteral("%1 / 100").arg(r.strength)},
                  {QStringLiteral("生效区间"), RelSpanView(r.from_chapter, r.to_chapter)},
                  {QStringLiteral("成因"),
                   r.reason.empty() ? QStringLiteral("（未记成因）") : QString::fromStdString(r.reason)},
                  {QStringLiteral("状态"),
                   r.status == "ended" ? QStringLiteral("已结束（ended）")
                                       : QStringLiteral("生效中（active）")},
                  {QStringLiteral("行 id"), QString::number(r.id)}});
    body->addWidget(kv);

    const auto addEndpoint = [&](qint64 entityId, const QString& role) {
        body->addWidget(SectionLabel(drawer, role));
        auto got = g.GetEntity(entityId);
        if (!got) {
            body->addWidget(new QLabel(
                QStringLiteral("实体 #%1 读不到：%2（可能已被清理；关系仍保留占位）")
                    .arg(entityId)
                    .arg(QString::fromStdString(got.error().message)),
                drawer));
            return;
        }
        const novelcore::EntityRow& e = *got;
        auto* card = new widgets::Card(widgets::Card::Variant::Outlined, drawer);
        auto* cb = card->BodyLayout();
        auto* row1 = new QWidget(card);
        auto* rr = new QHBoxLayout(row1);
        rr->setContentsMargins(0, 0, 0, 0);
        rr->setSpacing(theme::space::kSteps[1]);
        auto* name = new QLabel(QString::fromStdString(e.name), row1);
        widgets::SetSemibold(name, true);
        rr->addWidget(name, 1);
        rr->addWidget(new widgets::Tag(KindLabelOf(e.kind), "accent", false, row1), 0);
        rr->addWidget(new widgets::Tag(StatusLabelOf(e.status), StatusToneOf(e.status), false, row1), 0);
        cb->addWidget(row1);
        cb->addWidget(new QLabel(
            e.summary.empty() ? QStringLiteral("（暂无摘要）") : QString::fromStdString(e.summary), card));
        cb->addWidget(new QLabel(QStringLiteral("#%1 · 出场与字段见「实体」页").arg(e.id), card));
    };
    addEndpoint(r.from_id, QStringLiteral("起点实体卡摘要"));
    addEndpoint(r.to_id, QStringLiteral("终点实体卡摘要"));
    body->addWidget(new QLabel(QStringLiteral("关系变化不覆盖旧行：结束旧行（to_chapter）再开新行（`01` §2.4.1）。"),
                               drawer));
    drawer->Open();
}

} // namespace shine::app
