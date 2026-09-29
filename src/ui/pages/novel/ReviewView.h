#pragma once
// P04-S7 `ReviewView`（PLAN §5 S7 判据 / UI.md §2.4）：
//   * 语义评审 rubric 8 维（`06` §2.4）条形 + 阈值刻度线，低于阈值/存在 high issue → FAIL
//     —— 阈值表与总判定公式在此**首次落地**（此前 NovelDirector 判 `passed` 靠子串匹配，
//     `NovelDirector.cpp:402` 的 ⚠️ 注释即指此处）。
//   * 机器校验 K01–K29 清单（`novelcore::RunChapterChecks` 单一权威；low 级可记但放行）。
//   * FAIL 项 [修复] → 进入修复轮（累计 ≤ `max_revisions=3`），轮数可见（UI.md §2.4
//     「[进入修复轮 2/3]」）；修复走同一 `GenerateChapter` 真实管线（T12 FAIL → T13）。
// 颜色零内联（check-layers rule 3）：条形/文字一律 theme token 派生。
#include "ui/kit/controls/WidgetCommon.h" // QWidget
#include "novel/NovelChecks.h"         // CheckResult / CheckOutcome
#include "novel/NovelDirector.h"       // LlmCallFn / LlmStreamFn

#include <QString>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class QLabel;
class QScrollArea;
class QStackedWidget;

namespace shine::data {
class DataTable;
}
namespace shine::db::sqlite {
class Database;
}
namespace shine::widgets {
class Button;
class ProgressBar;
class Tag;
} // namespace shine::widgets

namespace shine::app {

// rubric 维度 + 阈值（`06` §2.4 的表，顺序即 UI 条形顺序）
struct RubricDim {
    const char* key; // plot / character / …（critic JSON 的字段名）
    const char* name; // 中文名
    int threshold;
};
[[nodiscard]] const std::vector<RubricDim>& RubricDims();

// 一条 rubric 评分
struct RubricScore {
    std::string key;
    QString name;
    int score = 0;
    int threshold = 0;
    bool below = false; // 低于阈值
};

// critic JSON → 8 维评分（缺维按 0 计 = FAIL，不伪造通过；`06` §2.4「不许静默放过」）
[[nodiscard]] std::vector<RubricScore> ParseRubric(std::string_view criticJson);
// 总判定：任一维 < 阈值 或 issues 含 high → FAIL（`06` §2.4 公式）
[[nodiscard]] bool RubricVerdictFail(const std::vector<RubricScore>& scores,
                                     std::string_view criticJson, QString* why);

// —— 汇总区的三态判定 ——
// 词表取自设计稿 webui/src/data/mock.js:520 的 `review_003.json`
// （`verdict: '有条件通过'`）：rubric 均值 84.3 ≥ 阈值 75、机器 25/26
// （mock.js:526 注明缺的那条是「提示级，不阻塞」），但 issues 里留着一条
// medium（mock.js:528）。即：没到 FAIL，但也还没到干净通过 —— 这就是原来
// 二态判定漏掉的中态。FAIL / Pass 的判据仍由 RubricVerdictFail 给出。
enum class ReviewVerdict { Pass, Conditional, Fail };
// 三态词表（判定区直接显示的中文结论）
[[nodiscard]] const char* ReviewVerdictText(ReviewVerdict v);

class ReviewView : public QWidget {
  public:
    explicit ReviewView(QWidget* parent = nullptr);
    ~ReviewView() override;
    ReviewView(const ReviewView&) = delete;
    ReviewView& operator=(const ReviewView&) = delete;

    void SetLlm(agent::LlmCallFn call, agent::LlmStreamFn stream = nullptr);

    // 一部一库：开书库（机器校验与修复轮都要查库）
    bool OpenBook(const std::filesystem::path& dbPath, const std::filesystem::path& projectDir,
                  QString* err = nullptr);
    void CloseDb() noexcept;
    // 选章：读 `work/chNNN/10_review.json`（rubric 源）+ 即时跑 K01–K29
    void SelectChapter(qint64 chapterId, int chapterOrd);
    [[nodiscard]] qint64 CurrentChapterId() const { return chapter_id_; }

    void Repair();    // [进入修复轮] → 同一 GenerateChapter 真实管线（T12→T13），轮数 ≤3
    void IgnoreAndContinue(); // [忽略并继续] → 记录人工确认（只改 UI 态，不动产物）
    bool Running() const { return running_; }

    // —— 自动化探针（S7 判据；产品代码不用）——
    [[nodiscard]] QString RubricProbe() const; // 8 维分 + 阈值 + 总判定 + issues
    [[nodiscard]] QString CheckProbe() const; // K01–K29 逐条 + 放行判定
    [[nodiscard]] QString RepairProbe() const; // 修复轮账（轮数/上限/回执/verdict 变化）
    // 供探针注入：把某章的「评审产物」直接喂进来（不跑 LLM 的离线路径）
    void InjectReviewArtifact(std::string_view criticJson, int revisionsUsed);

  private:
    struct CheckLine {
        QString id;
        QString name;
        QString outcome; // pass / fail / missing / 跳过（NotApplicable）
        QString severity;
        QString detail;
    };

    void Refresh();
    void RunMachineChecks();
    QWidget* BuildHead();
    void BuildRubric();
    void BuildChecks();
    void ShowRepairRunning();
    void SetHint(const QString& text, std::uint32_t token);

    std::unique_ptr<db::sqlite::Database> db_;
    std::filesystem::path db_path_;  // 已开的书库（同库不重开）
    std::filesystem::path project_dir_;
    agent::LlmCallFn call_;
    agent::LlmStreamFn stream_;
    qint64 chapter_id_ = 0;
    int chapter_ord_ = 0;
    bool running_ = false;
    bool ignored_ = false;
    bool injected_ = false; // 探针直喂评审产物（不重读盘）
    std::string critic_json_;
    int revisions_used_ = 0;
    int repairs_run_ = 0; // 本页累计发起的修复轮
    int max_revisions_ = 3;
    std::vector<RubricScore> scores_;
    std::vector<CheckLine> checks_;
    int fail_count_ = 0;
    int ran_count_ = 0;
    QString verdict_text_ = QStringLiteral("—");
    QString verdict_why_;
    bool verdict_fail_ = false;

    // 控件
    QLabel* verdict_ = nullptr;
    QLabel* verdict_word_ = nullptr; // 「结论：」右侧那个按 tone 上色的状态词
    QLabel* hint_ = nullptr;
    QLabel* round_ = nullptr;
    widgets::Button* repair_btn_ = nullptr;
    widgets::Button* ignore_btn_ = nullptr;
    // 汇总区三 Tag（webui Novel.jsx:211-213：rubric / 机器 / issue）
    widgets::Tag* rubric_tag_ = nullptr;
    widgets::Tag* machine_tag_ = nullptr;
    widgets::Tag* issue_tag_ = nullptr;
    widgets::ProgressBar* summary_bar_ = nullptr; // 汇总进度条（webui Novel.jsx:217）
    QScrollArea* rubric_scroll_ = nullptr;
    QStackedWidget* rubric_stack_ = nullptr;
    QWidget* rubric_rows_ = nullptr;
    QScrollArea* check_scroll_ = nullptr;
    data::DataTable* check_table_ = nullptr;
    int machine_high_fail_ = 0; // 机器校验 high 失败数（并入总判定）
    // 汇总区数据（三态判定 + 三个 Tag / 进度条的取值来源）
    ReviewVerdict verdict_state_ = ReviewVerdict::Fail;
    double rubric_avg_ = 0.0;  // rubric 8 维均值（设计稿 rubric.average）
    QString issue_level_;      // 遗留 issue 的最高级别（high/medium/low，空 = 无）
};

} // namespace shine::app
