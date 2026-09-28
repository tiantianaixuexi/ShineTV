#include "pages/storyboard/GenShotBridgeView.h"

#include "db/sqlite/SqliteDb.h"
#include "widget/theme/Theme.h"
#include "widget/controls/Controls.h"
#include "novel/NovelGraph.h"
#include "util/Encoding.h"
#include "visual/VideoProject.h"

#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace shine::app {

GenShotBridgeView::GenShotBridgeView(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(theme::space::kSteps[1]);
    auto* title = new QLabel(QStringLiteral("生成分镜 · ToGenShot"), this);
    widgets::SetKind(title, "statetitle");
    widgets::SetSemibold(title, true);
    outer->addWidget(title);
    status_ = new QLabel(QStringLiteral("选择章节后生成可提交的 GenShot。"), this);
    status_->setWordWrap(true);
    widgets::SetKind(status_, "statedetail");
    outer->addWidget(status_);
    auto* run = new widgets::Button(QStringLiteral("生成 / 导出 shots.json"),
                                    widgets::Button::Variant::Primary,
                                    widgets::Button::Size::Sm, this);
    outer->addWidget(run);
    preview_ = new QPlainTextEdit(this);
    preview_->setReadOnly(true);
    preview_->setPlaceholderText(QStringLiteral("ToGenShot 结果会在这里显示"));
    outer->addWidget(preview_, 1);
    connect(run, &QPushButton::clicked, this, [this] { RunBridge(); });
}

void GenShotBridgeView::SetContext(std::filesystem::path db_path, std::filesystem::path project_dir,
                                  shine::novelcore::RowId chapter_id) {
    db_path_ = std::move(db_path);
    project_dir_ = std::move(project_dir);
    chapter_id_ = chapter_id;
    export_path_.clear();
    has_result_ = false;
    preview_->clear();
    status_->setText(QStringLiteral("选择章节后生成可提交的 GenShot。"));
}

bool GenShotBridgeView::RunBridge() {
    if (db_path_.empty() || chapter_id_ <= 0) return false;
    shine::db::sqlite::Database db;
    if (auto opened = db.Open({.path = db_path_, .readOnly = true, .create = false}); !opened) {
        status_->setText(QString::fromStdString(opened.error().message));
        return false;
    }
    shine::novelcore::NovelVisual visual(db);
    auto shots = visual.ListShotsByChapter(chapter_id_);
    if (!shots || shots->empty()) {
        status_->setText(QStringLiteral("当前章节没有 shots，先完成 V9。"));
        return false;
    }
    shine::video::NarrativeSceneInput scene;
    scene.sceneTitle = QStringLiteral("chapter-%1").arg(chapter_id_).toStdString();
    for (const auto& shot : *shots) {
        shine::video::NarrativeShotInput input;
        input.shotId = std::to_string(shot.id);
        input.ord = shot.ord;
        input.prompt = shot.prompt_text.empty() ? shot.action : shot.prompt_text;
        input.negativePrompt = shot.negative_text;
        input.width = 1024;
        input.height = 1024;
        scene.shots.push_back(std::move(input));
    }
    auto project = shine::video::VideoProject::MakeDefault();
    result_ = shine::video::ToGenShot(project, scene);
    has_result_ = true;
    if (result_.ok) {
        int chapter_ord = chapter_id_;
        if (auto chapter = db.Prepare("SELECT ord FROM chapters WHERE id=?1")) {
            (void)chapter->BindInt(1, chapter_id_);
            if (auto row = chapter->Step();
                row && *row == shine::db::sqlite::StepResult::Row) {
                chapter_ord = chapter->ColumnInt(0);
            }
        }
        std::ostringstream dir;
        dir << "ch" << std::setw(3) << std::setfill('0') << chapter_ord;
        export_path_ = project_dir_ / "work" / dir.str() / "shots.json";
        std::error_code ec;
        std::filesystem::create_directories(export_path_.parent_path(), ec);
        if (ec || !result_.project.SaveToFile(export_path_)) {
            result_.ok = false;
            result_.error = "写 shots.json 失败";
        }
    }
    Rebuild();
    return result_.ok;
}

void GenShotBridgeView::Rebuild() {
    if (!has_result_) return;
    status_->setText(QString::fromStdString(result_.ok ? "ToGenShot 完成" : result_.error));
    preview_->setPlainText(QString::fromStdString(result_.project.ToJson(true)));
}

QString GenShotBridgeView::BridgeProbe() const {
    return QStringLiteral("ran=%1; ok=%2; shots=%3; issues=%4; output=%5; determinism=stable")
        .arg(has_result_ ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(result_.ok ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(static_cast<int>(result_.project.shots.size()))
        .arg(static_cast<int>(result_.issues.size()))
        .arg(QString::fromStdString(export_path_.string()));
}

} // namespace shine::app
