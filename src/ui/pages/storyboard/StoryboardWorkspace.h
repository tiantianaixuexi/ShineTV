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
class Tag;
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
    // 借给外壳左侧栏的导航（章节/场景树）。所有权仍在本页；外壳换工作区时收回。
    [[nodiscard]] QWidget* NavWidget() const { return nav_host_; }
    // 导航在页面里的原宿主（分栏容器）：外壳归还导航时挂回这里的第一格
    [[nodiscard]] QWidget* NavHostBox() const { return nav_box_; }

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
    // 当前选中镜头（webui 的 selShot）：时间线卡片选中态 + 镜头详情的数据源。
    // 0 = 未选，回落到列表首个镜头（与旧行为一致）。
    novelcore::RowId selected_shot_ = 0;
    [[nodiscard]] const novelcore::ShotRow* SelectedShot() const;
    // views.css:140 .vw-head 的标题行与副标题行
    void UpdateHead();
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
    QLabel* head_title_ = nullptr;
    shine::widgets::Tag* shot_status_tag_ = nullptr;
    QLabel* stage_status_ = nullptr;
    shine::widgets::Tag* stage_tag_ = nullptr;
    int committed_scenes_ = 0;
    shine::data::DataTree* tree_ = nullptr;
    QWidget* detail_ = nullptr;
    // 借给外壳左侧栏的导航（章节/场景树）；外壳收回时重新挂回 nav_box_
    QWidget* nav_host_ = nullptr;
    QWidget* nav_box_ = nullptr;
    shine::data::StageFlow* stage_flow_ = nullptr;
    shine::widgets::Button* run_stages_ = nullptr;
    QPlainTextEdit* artifact_view_ = nullptr;
    QLabel* persistence_status_label_ = nullptr;
};

} // namespace shine::app
