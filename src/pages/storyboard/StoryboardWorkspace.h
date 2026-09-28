#pragma once
// P06-S1/S2 分镜工作区：已提交 Scene 列表 + V1–V8 阶段流水线。
#include "novel/NovelDirector.h"
#include "novel/NovelVisual.h"
#include "novel/NovelTypes.h"

#include <QStringList>
#include <QWidget>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class QLabel;
class QPlainTextEdit;

namespace shine::db::sqlite {
class Database;
}
namespace shine::data {
class DataTree;
class StageFlow;
}
namespace shine::widgets {
class Button;
}

namespace shine::app {
class StoryboardTimeline;
class ShotTableView;
class ShotDetailView;
class ContinuityView;
class GenShotBridgeView;

class StoryboardWorkspace : public QWidget {
  public:
    explicit StoryboardWorkspace(QWidget* parent = nullptr);
    ~StoryboardWorkspace() override;

    bool OpenBook(const std::filesystem::path& dbPath, const std::filesystem::path& projectDir,
                  QString* error = nullptr);
    void CloseBook() noexcept;
    bool ReorderTimeline(const std::vector<novelcore::RowId>& orderedIds);
    [[nodiscard]] QString TimelineProbe() const;
    bool UpdateShot(std::int64_t id, std::string action, std::string mood, std::string duration_note);
    bool ApplyMoodBatch(const std::vector<novelcore::RowId>& ids, const QString& mood);
    [[nodiscard]] QString ShotTableProbe() const;
    bool SaveSelectedTimeline(const std::string& timeline_json);
    [[nodiscard]] QString ShotDetailProbe() const;
    [[nodiscard]] QString ContinuityProbe() const;
    bool RunContinuityCheck();
    bool RunGenShotBridge();
    [[nodiscard]] QString GenShotProbe() const;
    bool SelectScene(std::int64_t id);
    void SetLlm(agent::LlmCallFn call) { llm_ = std::move(call); }
    bool RunStagePipeline();
    bool RunStoryboardPersistence();
    bool RunPromptPersistence();
    [[nodiscard]] QString PersistenceProbe() const;
    [[nodiscard]] QString SceneProbe() const;
    [[nodiscard]] QString StageProbe() const;

  private:
    struct ChapterScenes {
        novelcore::ChapterRow chapter;
        std::vector<novelcore::SceneRow> scenes;
    };
    struct StageState {
        QString code;
        QString status;
        QString detail;
        QString artifact;
    };

    void BuildUi();
    bool ReloadScenes();
    void LoadTimeline();
    StoryboardTimeline* timeline_ = nullptr;
    std::vector<novelcore::ShotRow> current_shots_;
    std::int64_t selected_chapter_ = 0;
    bool timeline_busy_ = false;
    void RebuildTree();
    ShotTableView* shot_table_ = nullptr;
    void UpdateSelection();
    ShotDetailView* shot_detail_ = nullptr;
    ContinuityView* continuity_ = nullptr;
    GenShotBridgeView* gen_shot_ = nullptr;
    bool persistence_running_ = false;
    bool persistence_ran_ = false;
    bool persistence_v9_ok_ = false;
    bool persistence_v10_ok_ = false;
    int persistence_shots_ = 0;
    int persistence_prompts_ = 0;
    QString persistence_status_;
    void UpdateStageGraph();
    void ShowLatestArtifact();
    [[nodiscard]] std::int64_t SelectedChapterId() const;

    std::unique_ptr<shine::db::sqlite::Database> db_;
    std::filesystem::path dbPath_;
    std::filesystem::path projectDir_;
    std::vector<ChapterScenes> chapters_;
    std::int64_t selected_scene_ = 0;
    QString error_;
    agent::LlmCallFn llm_;
    std::vector<StageState> stage_states_;
    bool stage_running_ = false;

    QLabel* status_ = nullptr;
    shine::data::DataTree* tree_ = nullptr;
    QWidget* detail_ = nullptr;
    shine::data::StageFlow* stage_flow_ = nullptr;
    shine::widgets::Button* run_stages_ = nullptr;
    QPlainTextEdit* artifact_view_ = nullptr;
    QLabel* persistence_status_label_ = nullptr;
};

} // namespace shine::app
