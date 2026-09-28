#pragma once
// 章节流水线视图（当前实现契约见 src/novel/NovelPipeline.h、src/pipeline/StageMachine.h 与 docs/10-modules/novel.md）：
// * 全链跑完：run_btn → T1…T17 无人工干预跑完（UI.md §2.3；T12 FAIL → 回 T13 修复 ≤3 轮）
// * 产物落盘：work/chNN/（StageFileCatalog 的 01–13 文件）+ _manifest.json（ManifestJson）；
//   点节点 → 右栏 JsonTree 看产物原文
// * 断点续跑：resume=true 从上次阶段接着跑（P1 哈希一致跳过 / P2 不一致重跑 / P4 正文复用）
//   —— 断点账走 `novelcore::FindResumeIndex`（NovelStageLedger 的单一权威）。
// 节点状态机：todo 灰 / running 呼吸光 / done 绿 / failed 红 / skipped 虚线（UI.md §3）——
// 颜色一律 theme token（check-layers rule 3）；StageFlow 的 NodeState 与文件证据双向收束。
//
// ⚠️ 撞车留痕（2026-09-24 15:54–16:10）：曾有并行写手把本文件换成另一套 API（RunChain /
//   ChapterFlowCanvas 离线骨架）。按「磁盘现状 + 全链一致性 + 交接 §7（GenerateChapter 驱动 +
//   mock LLM 注入）收敛回本套 API；行为验证见 docs/40-operations/verification.md。
#include "ui/kit/controls/WidgetCommon.h" // QWidget
#include "novel/NovelDirector.h"      // LlmCallFn / GenerateChapterRequest（全链驱动）

#include <QHash>
#include <QString>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

class QLabel;
class QScrollArea;
class QStackedWidget;

namespace shine::db::sqlite {
class Database;
}
namespace shine::project {
struct ProjectRef;
}
namespace shine::data {
class DataTable;
class JsonTree;
class StageFlow;
}
namespace shine::widgets {
class Button;
class EmptyState;
class Select;
} // namespace shine::widgets

namespace shine::app {

class ChapterFlowView : public QWidget {
  public:
    explicit ChapterFlowView(QWidget* parent = nullptr);
    ~ChapterFlowView() override;
    ChapterFlowView(const ChapterFlowView&) = delete;
    ChapterFlowView& operator=(const ChapterFlowView&) = delete;

    void LoadFromRef(const project::ProjectRef& ref, const std::string& lastNovel);
    void CloseDb() noexcept;

    // LLM 注入（探针传 mock；真跑接 provider）
    void SetLlm(agent::LlmCallFn call, agent::LlmStreamFn stream = nullptr);

    // 全链跑完 T1–T17（GenerateChapter；resume=true = 断点续跑）；report = 逐阶段账
    bool RunChapter(qint64 chapterId, bool resume, QString* report);
    bool RunCurrent(bool resume, QString* report); // 便捷入口（选中章）
    void GenerateCurrent(bool resume);             // 异步入口：worker 跑链 / UI 收账

    void SelectChapter(qint64 chapterId);
    [[nodiscard]] qint64 CurrentChapterId() const { return chapter_id_; }

    // —— 自动化探针（S5 判据；产品代码不用）——
    [[nodiscard]] QString StageProbe() const;  // T1–T17 状态行
    [[nodiscard]] QString LiveProbe() const;  // 跑中状态迁移日志
    [[nodiscard]] QString ArtifactProbe(const QString& stageCode) const; // 产物 JSON 文本
    [[nodiscard]] QString ManifestProbe() const; // _manifest.json 原文
    [[nodiscard]] QString ResumeProbe() const;   // 断点位置（FindResumeIndex 水位线）

  private:
    struct StageRow {
        QString code;  // "T1"
        QString stage; // "CHAPTER_INIT"
        QString name;  // 中文名
        bool merged = false; // 03 §2.2：T2–T4/T6–T9 合并执行 → 并入 09_chapter_plan.json
    };
    struct RunSummary {
        bool ok = false;
        bool committed = false;
        bool skipped = false;
        int revisions = 0;
        int llm_calls = 0;
        std::string plan_json;
        std::string body;
        std::string critic_json;
        std::string commit_note;
        std::string error;
    };

    QWidget* BuildHead();
    QWidget* BuildStageArea();
    QWidget* BuildArtifactArea();
    void RebuildChapters();
    void RebuildStageTable();
    void RefreshFlowStates(); // 盘上证据收束 17 节点状态（重开读回一致）
    void ShowStage(const QString& stageCode);
    [[nodiscard]] QString ArtifactTextFor(const QString& stageCode) const;
    [[nodiscard]] QString StateNameOf(const QString& s) const;
    void SetNodeState(const QString& code, const QString& s, bool live = true);
    void SetRunning(const QString& step, bool on);
    [[nodiscard]] std::filesystem::path ManifestPath() const;
    [[nodiscard]] std::filesystem::path SnapshotPath() const;
    bool WriteManifest(const std::string& status);
    [[nodiscard]] bool HasArtifact(const QString& stageCode) const;
    void Ui(const std::function<void()>& fn); // 控件更新统一回 UI 线程

    // —— 数据 ——
    std::unique_ptr<db::sqlite::Database> db_;
    std::unique_ptr<project::ProjectRef> ref_store_;
    std::filesystem::path book_root_; // 书根：work/chNN/ 与 snapshots/ 按它落
    std::filesystem::path book_work_;
    std::filesystem::path book_db_path_;
    std::string last_novel_; // ui.lastNovel（空 = 默认书）
    QString book_title_;     // 书标题（报告/tooltip）
    QString open_error_;     // 开库失败原因（空态给出路）
    agent::LlmCallFn call_;
    agent::LlmStreamFn stream_;

    std::vector<StageRow> stages_;       // T1–T17
    std::vector<qint64> chapter_ids_;    // 章下拉（与 Select 项同序）
    std::vector<QString> chapter_labels_;
    std::vector<QString> active_stages_; // 跑中当前 Phase 的 T 节点（UI 线程内更新）
    qint64 chapter_id_ = 0;
    int chapter_ord_ = 0;
    QString chapter_title_;
    bool has_run_ = false;
    RunSummary last_run_;
    QHash<QString, QString> state_;   // code → todo/running/done/failed/skipped
    QHash<QString, qint64> stage_ms_; // code → 耗时（ms）
    std::vector<QString> live_log_;   // 「T5:running」逐次迁移（跑中实时记）
    mutable std::mutex live_mutex_;

    // —— 控件 ——
    widgets::Select* chapter_sel_ = nullptr;
    QLabel* state_label_ = nullptr;
    widgets::Button* run_btn_ = nullptr;
    widgets::Button* resume_btn_ = nullptr;
    widgets::Button* gen_btn_ = nullptr;
    data::StageFlow* flow_ = nullptr;
    QScrollArea* flow_scroll_ = nullptr;
    data::DataTable* stage_table_ = nullptr;
    QStackedWidget* artifact_stack_ = nullptr;
    widgets::EmptyState* artifact_empty_ = nullptr;
    data::JsonTree* artifact_tree_ = nullptr;
    widgets::Select* artifact_sel_ = nullptr;
    QLabel* artifact_note_ = nullptr;
};

} // namespace shine::app
