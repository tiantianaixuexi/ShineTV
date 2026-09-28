#pragma once
// P06-S7 NarrativeShot → GenShot / shots.json 预览与导出。
#include "visual/NovelShotBridge.h"
#include "novel/NovelVisual.h"

#include <QWidget>

#include <filesystem>
#include <string>

class QLabel;
class QPlainTextEdit;

namespace shine::app {

class GenShotBridgeView : public QWidget {
  public:
    explicit GenShotBridgeView(QWidget* parent = nullptr);

    void SetContext(std::filesystem::path db_path, std::filesystem::path project_dir,
                    shine::novelcore::RowId chapter_id);
    bool RunBridge();
    [[nodiscard]] QString BridgeProbe() const;

 private:
    void Rebuild();

    std::filesystem::path db_path_;
    std::filesystem::path project_dir_;
    shine::novelcore::RowId chapter_id_ = 0;
    shine::video::ToGenShotResult result_;
    std::filesystem::path export_path_;
    bool has_result_ = false;
    QLabel* status_ = nullptr;
    QPlainTextEdit* preview_ = nullptr;

};

} // namespace shine::app
