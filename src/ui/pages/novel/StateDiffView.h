#pragma once
// P04-S9 `StateDiffView`（PLAN §5 S8 行 / 用户口述序第 9 步）：
//   * 展示 `work/chNNN/12_state_diff.json` 的 StateDiff（`data::DiffView` 或 JsonTree）
//   * 提交门禁 G1–G5 **逐条**显示（`07` §2.3）：G1 评审 PASS / G2 K01–K29 无阻断
//     / G3 章级快照就绪 / G4 StateDiff 契约有效 / G5 无未解决 high issue
//   * [提交状态] = **唯一 COMMIT 入口**：`novelcore::CommitChapterState`（不调 LLM，
//     评审/校验结论由调用方给 —— 本视图只把已有的 rubric/K 报告喂进去）
//   * [回滚到本章快照]：`snapshots/ch<NNN>.json` 的 `entity_version_ids` → Before 值
//     写回实体（`07` §2.4；破坏性操作 ⇒ 二次确认 + 中文后果说明）
// 颜色零内联；门禁结论一律中文。
#include "ui/kit/controls/WidgetCommon.h" // QWidget
#include "novel/NovelChecks.h"         // ValidationReport（G2 用）

#include <QString>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class QLabel;
class QPlainTextEdit;

namespace shine::db::sqlite {
class Database;
}
namespace shine::novelcore {
struct StateDiff;
} // namespace shine::novelcore
namespace shine::widgets {
class Button;
} // namespace shine::widgets

namespace shine::app {

// 一条门禁（G1–G5；`07` §2.3）
struct GateLine {
    const char* gate;   // G1..G5
    const char* name;   // 中文名
    bool pass = false;
    QString detail;    // 中文依据/失败原因
};

// 预检：只算门禁不落库（[提交] 前先看红绿）
[[nodiscard]] std::vector<GateLine> EvalGates(shine::db::sqlite::Database& db,
                                             const shine::novelcore::StateDiff& diff,
                                             bool reviewPass, const QString& reviewVerdict,
                                             const shine::novelcore::ValidationReport& checks,
                                             bool snapshotExists, const std::string& snapshotPath,
                                             const std::string& projectDir);

class StateDiffView : public QWidget {
  public:
    explicit StateDiffView(QWidget* parent = nullptr);
    ~StateDiffView() override;
    StateDiffView(const StateDiffView&) = delete;
    StateDiffView& operator=(const StateDiffView&) = delete;

    void SetProjectDir(const std::filesystem::path& dir);
    bool OpenBook(const std::filesystem::path& dbPath, QString* err = nullptr);
    void CloseDb() noexcept;
    // 选章：读 StateDiff 产物 + 评审产物 + 即时跑 K01–K29（只读预检，不落库）
    void SelectChapter(qint64 chapterId, int chapterOrd);
    [[nodiscard]] qint64 CurrentChapterId() const { return chapter_id_; }

    // 注入评审结论（[评审] 页跑出来的 rubric verdict；未跑 = false）
    void SetReviewVerdict(bool pass, const QString& verdictText);

    // [提交状态] 唯一 COMMIT 入口（不调 LLM）
    bool Commit();
    // [回滚到本章快照]（人工确认后才调）
    bool Rollback();

    // —— 自动化探针（S9 判据；产品代码不用）——
    [[nodiscard]] QString GateProbe() const;   // G1–G5 逐条
    [[nodiscard]] QString DiffProbe() const;   // StateDiff 摘要（块计数 + hash）
    [[nodiscard]] QString CommitProbe() const; // 提交/回滚结果账（快照路径 / 幂等 / 回滚前后）

  private:
    void Refresh();
    void SetHint(const QString& text, std::uint32_t token);
    std::filesystem::path SnapshotPath() const;
    bool ReadDiff();   // 读 12_state_diff.json → diff_
    void RunChecks();  // K01–K29 → checks_（G2 用）

    std::filesystem::path project_dir_;
    std::unique_ptr<db::sqlite::Database> db_;
    std::filesystem::path db_path_;
    qint64 chapter_id_ = 0;
    int chapter_ord_ = 0;
    bool review_pass_ = false;
    QString review_verdict_ = QStringLiteral("—");
    std::unique_ptr<novelcore::StateDiff> diff_;
    novelcore::ValidationReport checks_; // K01–K29 报告（G2 单一判定器）
    bool running_commit_ = false;
    std::vector<GateLine> gates_;
    QString diff_summary_;
    QString last_commit_;  // 提交结果摘要
    QString g2_source_;    // G2 口径来源：inline（事务内权威）/ caller
    QString last_rollback_; // 回滚结果摘要
    bool snapshot_exists_ = false;
    std::string snapshot_path_;
    int applied_blocks_ = 0;
    bool committed_now_ = false;
    bool skipped_idempotent_ = false;

    // 控件
    QLabel* verdict_ = nullptr;
    widgets::ElidedLabel* hint_ = nullptr;
    QPlainTextEdit* diff_view_ = nullptr;
    // (✔/✘, 依据)：依据是长判定文本，用 ElidedLabel（省略 + hover 全文 + 点击展开）
    std::vector<std::pair<QLabel*, widgets::ElidedLabel*>> gate_widgets_;
    widgets::Button* commit_btn_ = nullptr;
    widgets::Button* rollback_btn_ = nullptr;
};

} // namespace shine::app
