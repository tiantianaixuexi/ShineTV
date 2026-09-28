// P04-S6 DraftView 实现：正文流式预览（逐 token 只追加末段）+ 中断落盘 + 失败段末重试 +
// 手改后重算哈希。颜色零内联（check-layers rule 3）：一律 theme token 派生。
#include "ui/pages/novel/DraftView.h"
#include "ui/kit/theme/CssColor.h"

#include "core/Async.h"
#include "db/sqlite/SqliteDb.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "novel/NovelChecks.h"
#include "novel/NovelGraph.h"
#include "novel/NovelTypes.h"

#include <QAbstractAnimation>
#include <QEventLoop>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <cmath>
#include <utility>

namespace shine::app {

DraftView::DraftView(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(theme::space::kSteps[1]);

    // —— 工具行：流式 / 中断 / 重试 / 保存 + 状态 ——
    auto* head = new QWidget(this);
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(theme::space::kSteps[1]);
    stream_btn_ = new widgets::Button(QStringLiteral("▶ 流式续写"),
                                      widgets::Button::Variant::Primary,
                                      widgets::Button::Size::Sm, head);
    stream_btn_->setToolTip(QStringLiteral("writer 流式生成：worker 跑线程，逐 token 只追加末段"));
    connect(stream_btn_, &widgets::Button::clicked, this, [this] { StartStream({}); });

    stop_btn_ = new widgets::Button(QStringLiteral("■ 中断"),
                                    widgets::Button::Variant::Secondary,
                                    widgets::Button::Size::Sm, head);
    stop_btn_->setToolTip(QStringLiteral("中断后已收正文即刻落盘（chapters.body），随时续写"));
    stop_btn_->setEnabled(false);
    connect(stop_btn_, &widgets::Button::clicked, this, [this] { StopStream(); });

    retry_btn_ = new widgets::Button(QStringLiteral("↻ 重试本段"),
                                     widgets::Button::Variant::Secondary,
                                     widgets::Button::Size::Sm, head);
    retry_btn_->setToolTip(QStringLiteral("生成失败后从失败段末接着写"));
    retry_btn_->setEnabled(false);
    connect(retry_btn_, &widgets::Button::clicked, this, [this] { RetryLastSegment(); });

    save_btn_ = new widgets::Button(QStringLiteral("保存"),
                                    widgets::Button::Variant::Ghost,
                                    widgets::Button::Size::Sm, head);
    save_btn_->setToolTip(QStringLiteral("手改落盘（chapters.body + words），哈希已实时重算"));
    connect(save_btn_, &widgets::Button::clicked, this, [this] { SaveManualEdit(); });

    state_ = new QLabel(QStringLiteral("按「▶ 流式续写」开始，或直接手写正文"), head);
    state_->setWordWrap(true);
    widgets::SetTextColor(state_, theme::Current().textSecondary);

    hl->addWidget(stream_btn_);
    hl->addWidget(stop_btn_);
    hl->addWidget(retry_btn_);
    hl->addWidget(save_btn_);
    hl->addWidget(state_, 1);

    // —— 正文区（可编辑；流式只追加末段 + 新增高亮 + 呼吸光边框）——
    // 用 QTextEdit 而非 QPlainTextEdit：后者不消费 textIndent，设计稿的
    // `.draft p { text-indent: 2em }`（views.css:576）渲染不出来。
    edit_ = new QTextEdit(this);
    // 保持「纯文本」语义：QTextEdit 默认 acceptRichText=true，粘贴 HTML 会把
    // 富文本格式灌进文档，toPlainText() 的结果就不再等于 chapters.body，
    // 会连带影响手改哈希与落盘一致性（P04-S6 edit-hash 判据）。
    edit_->setAcceptRichText(false);
    widgets::SetKind(edit_, "draftbody");
    // webui .draft：f14 / line-height 1.9 / 段距 14px / text-indent 2em / max-width 720px
    edit_->setMaximumWidth(kDraftMaxW);
    ApplyEditSheet(QString{});
    ApplyDraftTypography();
    connect(edit_->document(), &QTextDocument::contentsChange, this,
            [this](int, int, int) { ApplyDraftTypography(); });
    edit_->setPlaceholderText(
        QStringLiteral("正文（P04-S6 DraftView）：流式生成时只追加末段并高亮新增；"
                       "手改后哈希实时重算，中断/失败的已收内容都会落盘。"));
    connect(edit_, &QTextEdit::textChanged, this, [this] {
        if (applying_ || streaming_) {
            return;
        }
        // 手改 → 重算哈希（`03` §2.7 P4：正文变更影响复用判定的对账口径）
        RecalcHash(edit_->toPlainText());
        SetHint(QStringLiteral("正文已手改（哈希已重算 %1…）—— 点「保存」落盘")
                    .arg(body_hash_.left(12)),
                theme::Current().statusWarn);
        save_btn_->setEnabled(true);
    });

    // UI 心跳（50ms）：流式期间计数增长 = 事件循环活着（「流式不卡 UI」的证据）
    heartbeat_ = new QTimer(this);
    heartbeat_->setInterval(50);
    connect(heartbeat_, &QTimer::timeout, this, [this] { ++heartbeats_; });
    heartbeat_->start();

    outer->addWidget(head);
    outer->addWidget(edit_, 1);
}

DraftView::~DraftView() {
    CloseDb();
}

void DraftView::SetLlm(agent::LlmStreamFn stream, agent::LlmCallFn call) {
    stream_ = std::move(stream);
    call_ = std::move(call);
}

bool DraftView::OpenBook(const std::filesystem::path& dbPath, QString* err) {
    if (db_ && db_path_ == dbPath) {
        return true; // 同一本书已开：重开 SQLite 纯属浪费（S1 千章选章判据）
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

void DraftView::CloseDb() noexcept {
    db_.reset();
}

void DraftView::SelectChapter(qint64 chapterId, const QString& title, const QString& body) {
    chapter_id_ = chapterId;
    chapter_title_ = title;
    seg_index_ = 0;
    seg_start_ = 0;
    fail_pos_ = -1;
    failed_reason_.clear();
    appends_.clear();
    applying_ = true;
    edit_->setPlainText(body);
    edit_->setExtraSelections({});
    ApplyDraftTypography();
    applying_ = false;
    baseline_hash_ = QString::fromStdString(novelcore::Sha1Hex(body.toStdString()));
    body_hash_ = baseline_hash_;
    retry_btn_->setEnabled(false);
    save_btn_->setEnabled(false);
    SetHint(chapterId > 0 ? QStringLiteral("按「▶ 流式续写」开始，或直接手写正文（哈希 %1…）")
                                .arg(body_hash_.left(12))
                          : QStringLiteral("还没选章节 —— 在左侧「书 → 卷 → 章」选一章"),
            theme::Current().textSecondary);
}

// ————————————————————————————————————————————— 流式

void DraftView::StartStream(const QString& hint) {
    if (streaming_) {
        return;
    }
    if (!stream_) {
        SetHint(QStringLiteral("跑不成：还没接 writer 流式。出路：S8「模型与 Prompt」配置模型，"
                               "或用自动化探针注入 mock。"),
                theme::Current().statusWarn);
        return;
    }
    if (chapter_id_ <= 0) {
        SetHint(QStringLiteral("还没选章节 —— 先在左侧选一章再写。"), theme::Current().statusWarn);
        return;
    }
    ClearFailureMarker();
    ++seg_index_;
    // 段起点 = 当前文档长度（此后只追加末段；StreamProbe 逐条留 append-only 证据）
    applying_ = true;
    {
        QTextCursor c = edit_->textCursor();
        c.movePosition(QTextCursor::End);
        if (!edit_->toPlainText().isEmpty()) {
            c.insertText(QStringLiteral("\n\n"));
        }
        seg_start_ = static_cast<int>(edit_->toPlainText().size());
    }
    applying_ = false;
    streaming_ = true;
    const int rid = ++run_id_; // 运行代号：此后只有本代回调可动状态
    stream_btn_->setEnabled(false);
    stop_btn_->setEnabled(true);
    retry_btn_->setEnabled(false);
    SetHint(QStringLiteral("流式生成中（worker 线程跑 writer，逐 token 只追加末段）…"),
            theme::Current().statusBusy);
    StartBreathing();

    const std::string user =
        hint.isEmpty()
            ? QStringLiteral("为《%1》续写正文（已有 %2 字；从上文自然续写，不要重复）。")
                  .arg(chapter_title_)
                  .arg(edit_->toPlainText().size())
                  .toStdString()
            : hint.toStdString();

    auto onDelta = [this, rid](std::string_view d) {
        const QString s = QString::fromUtf8(d.data(), static_cast<qsizetype>(d.size()));
        async::PostToUi([this, s, rid] {
            if (rid == run_id_ && streaming_) {
                AppendDelta(s);
            }
        });
    };
    async::RunOnWorker([this, user, onDelta, rid] {
        auto r = stream_(agent::LlmRole::Writer, agent::DefaultPrompt("writer"), user, onDelta);
        async::PostToUi([this, r = std::move(r), rid] {
            if (rid == run_id_) { // 旧代（已被中断/重试作废）的迟到收尾一律丢弃
                FinishStream(r);
            }
        });
    });
}

void DraftView::StopStream() {
    if (!streaming_) {
        return;
    }
    streaming_ = false; // 迟到的 delta 一律丢弃（onDelta 只在 streaming_ 时追加）
    ++run_id_;          // 作废在飞的旧 worker：其迟到回调不得再动状态
    StopBreathing();
    stream_btn_->setEnabled(true);
    stop_btn_->setEnabled(false);
    const QString body = edit_->toPlainText();
    SaveToDb(body); // 中断后状态正确落盘：已收正文 → chapters.body + words
    SetHint(QStringLiteral("已中断 —— 已收 %1 字落盘（chapters.body），点「▶ 流式续写」接着写。")
                .arg(body.size()),
            theme::Current().statusWarn);
}

void DraftView::RetryLastSegment() {
    if (failed_reason_.isEmpty() || streaming_) {
        return;
    }
    ClearFailureMarker();
    StartStream({}); // 从当前正文续写（失败段末重试）
}

void DraftView::AppendDelta(const QString& s) {
    if (s.isEmpty()) {
        return;
    }
    const QString before = edit_->toPlainText();
    AppendRec rec;
    rec.seq = static_cast<int>(appends_.size()) + 1;
    rec.delta_chars = static_cast<int>(s.size());
    rec.seg = seg_index_;
    {
        applying_ = true;
        QTextCursor c = edit_->textCursor();
        c.movePosition(QTextCursor::End);
        c.insertText(s);
        applying_ = false;
    }
    const QString after = edit_->toPlainText();
    rec.total_chars = static_cast<int>(after.size());
    // append-only 证据：旧前缀逐字不变、长度恰增 delta（「只追加末段」的机器证明）
    rec.prefix_ok = after.size() == before.size() + s.size() && after.left(before.size()) == before;
    appends_.push_back(rec);

    // 新增高亮（accent.primary 背景 20%）：只亮当前段
    QList<QTextEdit::ExtraSelection> sels;
    if (seg_start_ < after.size()) {
        QTextEdit::ExtraSelection sel;
        QColor bg = shine::widgets::TokenQColor(theme::Current().accentPrimary);
        bg.setAlphaF(0.2);
        sel.format.setBackground(bg);
        sel.cursor = edit_->textCursor();
        sel.cursor.setPosition(seg_start_);
        sel.cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        sels.append(sel);
    }
    edit_->setExtraSelections(sels);
    ApplyDraftTypography();
    auto* sb = edit_->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void DraftView::FinishStream(const std::expected<std::string, agent::AgentError>& r) {
    if (!streaming_) {
        return; // 已被 StopStream 收尾（落盘已完成）
    }
    streaming_ = false;
    StopBreathing();
    stream_btn_->setEnabled(true);
    stop_btn_->setEnabled(false);
    const QString body = edit_->toPlainText();
    SaveToDb(body); // 成功 / 失败都落已收内容（中断/失败后状态正确落盘）
    if (r) {
        SetHint(QStringLiteral("本段写完（%1 字）—— 已落盘 chapters.body。").arg(body.size()),
                theme::Current().statusOk);
    } else {
        failed_reason_ =
            QString::fromStdString(r.error().code + ": " + r.error().message);
        MarkFailure(failed_reason_);
        retry_btn_->setEnabled(true);
        SetHint(QStringLiteral("生成失败：%1 —— 出路：点「↻ 重试本段」从段末接着写。")
                    .arg(failed_reason_),
                theme::Current().statusDanger);
    }
}

void DraftView::StartBreathing() {
    if (breath_ == nullptr) {
        breath_ = new QVariantAnimation(this);
        breath_->setDuration(1400);
        breath_->setLoopCount(-1);
        breath_->setStartValue(0.0);
        breath_->setEndValue(1.0);
        connect(breath_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            const double a = 0.3 + 0.5 * std::fabs(std::sin(v.toDouble() * 3.14159265358979));
            const QColor c = shine::widgets::TokenQColor(theme::Current().accentPrimary);
            ApplyEditSheet(QStringLiteral("border: 2px solid rgba(%1,%2,%3,%4);")
                               .arg(c.red())
                               .arg(c.green())
                               .arg(c.blue())
                               .arg(a, 0, 'f', 2));
        });
    }
    breath_->start();
}

// `.draft` 的字色（text.primary）QssBuilder 的 draftbody 段已经落了（views.css:571），
// 这里仍自写一条：呼吸光每帧要重设 border，若拆成两条 setStyleSheet 会互相把属性清掉。
// 合成一条后，字色随 theme::Current() 实时取（切主题不必等 QSS 重建），
// 同时满足 check-layers rule 3：颜色只从 token 派生，不写死字面量。
void DraftView::ApplyEditSheet(const QString& borderRule) {
    if (edit_ == nullptr) {
        return;
    }
    const QColor fg = shine::widgets::TokenQColor(theme::Current().textPrimary);
    edit_->setStyleSheet(QStringLiteral("QTextEdit { color: rgb(%1,%2,%3); %4 }")
                             .arg(fg.red())
                             .arg(fg.green())
                             .arg(fg.blue())
                             .arg(borderRule));
}

// webui .draft：正文 14px、行高 1.9、段间距 14px、首行缩进 2em。
// QSS 管不到 line-height / text-indent，这三项落在 QTextBlockFormat 上；
// 流式追加与手改都会改块集合，所以统一由 contentsChange → 全文重刷（章节级文本，代价可忽略）。
//
// 已解决（2026-09-29）：此前用 QPlainTextEdit，其块格式虽正确写进文档
// （firstBlock().blockFormat().textIndent() == 28），但 QPlainTextDocumentLayout
// 不消费 textIndent，2em 首行缩进渲染不出来。改为 QTextEdit 后四项全部生效；
// 连带把 acceptRichText 关掉，保证粘贴仍是纯文本（否则手改哈希会与落盘正文不一致）。
void DraftView::ApplyDraftTypography() {
    if (edit_ == nullptr || typing_) {
        return; // setFormat 自身也会发 contentsChange —— 用 typing_ 掐断递归
    }
    typing_ = true;
    for (QTextBlock block = edit_->document()->firstBlock(); block.isValid();
         block = block.next()) {
        QTextBlockFormat fmt = block.blockFormat();
        fmt.setLineHeight(190.0, QTextBlockFormat::ProportionalHeight);
        fmt.setTextIndent(kIndentPx);
        fmt.setBottomMargin(14.0);
        QTextCursor cursor(block);
        cursor.setBlockFormat(fmt);
    }
    typing_ = false;
}

void DraftView::StopBreathing() {
    if (breath_ != nullptr) {
        breath_->stop();
    }
    ApplyEditSheet(QString{}); // 只撤边框，字色保留（否则正文退回调色板灰）
}

[[nodiscard]] bool DraftView::Breathing() const {
    return breath_ != nullptr && breath_->state() == QAbstractAnimation::Running;
}

void DraftView::MarkFailure(const QString& reason) {
    applying_ = true;
    QTextCursor c = edit_->textCursor();
    c.movePosition(QTextCursor::End);
    fail_pos_ = static_cast<int>(edit_->toPlainText().size());
    c.insertText(QStringLiteral("\n⛔ 生成失败：%1（点「↻ 重试本段」继续）").arg(reason));
    applying_ = false;
    QList<QTextEdit::ExtraSelection> sels = edit_->extraSelections();
    QTextEdit::ExtraSelection sel;
    sel.format.setForeground(shine::widgets::TokenQColor(theme::Current().statusDanger));
    sel.cursor = edit_->textCursor();
    sel.cursor.setPosition(fail_pos_);
    sel.cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    sels.append(sel);
    edit_->setExtraSelections(sels);
}

void DraftView::ClearFailureMarker() {
    if (fail_pos_ >= 0) {
        applying_ = true;
        QTextCursor c = edit_->textCursor();
        c.setPosition(fail_pos_);
        c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        applying_ = false;
        fail_pos_ = -1;
        edit_->setExtraSelections({});
    }
    failed_reason_.clear();
}

void DraftView::SaveManualEdit() {
    SaveToDb(edit_->toPlainText());
    SetHint(QStringLiteral("已保存（chapters.body + words）—— 哈希 %1…").arg(body_hash_.left(12)),
            theme::Current().statusOk);
    save_btn_->setEnabled(false);
}

void DraftView::SaveToDb(const QString& body) {
    RecalcHash(body);
    if (!db_ || chapter_id_ <= 0) {
        return;
    }
    novelcore::NovelGraph g(*db_);
    auto ch = g.GetChapter(static_cast<novelcore::RowId>(chapter_id_));
    if (!ch) {
        SetHint(QStringLiteral("落盘失败：章节读不到（%1）—— 出路：重开书库后重试。")
                    .arg(QString::fromStdString(ch.error().message)),
                theme::Current().statusDanger);
        return;
    }
    novelcore::ChapterRow row = *ch;
    row.body = body.toStdString();
    row.words = static_cast<int>(body.size());
    (void)g.UpsertChapter(row);
}

void DraftView::RecalcHash(const QString& body) {
    body_hash_ = QString::fromStdString(novelcore::Sha1Hex(body.toStdString()));
}

void DraftView::SetHint(const QString& text, std::uint32_t token) {
    state_->setText(text);
    widgets::SetTextColor(state_, token);
}

bool DraftView::WaitStream(int timeoutMs) {
    if (!streaming_) {
        return true;
    }
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(15);
    connect(&poll, &QTimer::timeout, &loop, [this, &loop] {
        if (!streaming_) {
            loop.quit();
        }
    });
    QTimer guard;
    guard.setSingleShot(true);
    connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start();
    guard.start(timeoutMs);
    loop.exec();
    return !streaming_;
}

// ————————————————————————————————————————————— 探针

QString DraftView::StreamProbe() const {
    QString out = QStringLiteral("stream appends=%1 seg=%2 heartbeats=%3 streaming=%4\n")
                      .arg(appends_.size())
                      .arg(seg_index_)
                      .arg(heartbeats_)
                      .arg(streaming_ ? 1 : 0);
    for (const AppendRec& r : appends_) {
        out += QStringLiteral("append #%1 seg=%2 delta=%3 total=%4 prefix_ok=%5\n")
                   .arg(r.seq)
                   .arg(r.seg)
                   .arg(r.delta_chars)
                   .arg(r.total_chars)
                   .arg(r.prefix_ok ? 1 : 0);
    }
    return out;
}

QString DraftView::DraftProbe() const {
    QString dbBody;
    int words = -1;
    if (db_ && chapter_id_ > 0) {
        novelcore::NovelGraph g(*db_);
        if (auto ch = g.GetChapter(static_cast<novelcore::RowId>(chapter_id_)); ch) {
            dbBody = QString::fromStdString(ch->body);
            words = ch->words;
        }
    }
    const QString body = edit_->toPlainText();
    // 单次多参 arg（一次替换、插入文本不二次扫描 —— 失败原因可能含 %）
    return QStringLiteral(
               "draft chapter=%1 streaming=%2 breathing=%3 dirty=%4\n"
               "hash=%5 baseline=%6\n"
               "saved-words=%7 body-match=%8\n"
               "failure=%9\n")
        .arg(chapter_id_)
        .arg(streaming_ ? 1 : 0)
        .arg(Breathing() ? 1 : 0)
        .arg(body_hash_ == baseline_hash_ ? 0 : 1)
        .arg(body_hash_, baseline_hash_)
        .arg(words)
        .arg(dbBody == body ? 1 : 0)
        .arg(failed_reason_.isEmpty() ? QStringLiteral("-") : failed_reason_);
}

} // namespace shine::app
