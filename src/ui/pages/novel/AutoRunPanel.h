#pragma once
// P04-S10 `AutoRunPanel`（PLAN §5 S9 行 / 用户口述序第 10 步）：
//   * manual / semi / auto 三种运行模式；运行请求统一交给 novelcore::NovelRunLoop
//   * 单章与全书预算、S1–S12 停止条件、检查点、stop_report.md / cost_report.json 可见
//   * auto 启动前展示六条前置校验；不满足时保留可读中文拒绝原因
//   * 生产运行走 worker + UI mailbox，Stop 只写取消标记，不阻塞 UI
#include "ui/kit/controls/WidgetCommon.h"
#include "novel/NovelDirector.h"
#include "novel/NovelRunLoop.h"

#include <QString>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>

class QLabel;
class QPlainTextEdit;

namespace shine::db::sqlite {
class Database;
}
namespace shine::widgets {
class Button;
class NumberInput;
class ProgressBar;
class Segmented;
} // namespace shine::widgets

namespace shine::app {

class AutoRunPanel : public QWidget {
  public:
    explicit AutoRunPanel(QWidget* parent = nullptr);
    ~AutoRunPanel() override;
    AutoRunPanel(const AutoRunPanel&) = delete;
    AutoRunPanel& operator=(const AutoRunPanel&) = delete;

    // 一部一库：dbPath + 书根（work/、snapshots/、报告均相对书根）。
    bool OpenBook(const std::filesystem::path& dbPath, const std::filesystem::path& projectDir,
                  QString* err = nullptr);
    void CloseBook() noexcept;

    // 探针/测试注入 mock；产品路径为空时由 NovelPipeline::RunOnce 构造真实回调。
    void SetLlm(agent::LlmCallFn call, agent::LlmStreamFn stream = nullptr);
    void SetMode(novelcore::RunMode mode);
    void SetStartChapter(int ord);
    void SetMaxChapters(int count);
    void SetCheckpointEvery(int count);
    void SetRunLimits(const novelcore::RunLimits& limits);
    void SetMaxTotalLlmCalls(std::int64_t calls);

    void Start();
    void Stop();
    [[nodiscard]] bool IsRunning() const { return running_; }

    // —— 自动化探针（S10 判据；产品代码不用）——
    [[nodiscard]] QString ProbeText() const;
    // 用离线 mock runner 走真实 NovelRunLoop：manual / semi / auto、S1 停止、
    // cost_report、stop_report 与检查点均实际落盘后返回账。
    [[nodiscard]] QString RunProbe();

  private:
    struct RunState {
        std::atomic<bool> cancel{false};
    };

    void BuildUi();
    void Refresh();
    void RefreshLimits();
    [[nodiscard]] novelcore::RunRequest MakeRequest() const;
    [[nodiscard]] bool LlmReady() const;
    [[nodiscard]] QString PreconditionText() const;
    void AppendLog(const QString& text);
    void SetRunning(bool running);
    void Finish(const std::shared_ptr<RunState>& state, novelcore::RunOutcome outcome);
    void ProgressToUi(const std::shared_ptr<RunState>& state,
                      const novelcore::RunProgress& progress);

    std::shared_ptr<db::sqlite::Database> db_;
    std::filesystem::path db_path_;
    std::filesystem::path project_dir_;
    agent::LlmCallFn call_;
    agent::LlmStreamFn stream_;

    novelcore::RunMode run_mode_ = novelcore::RunMode::Manual;
    int start_ord_ = 0;
    int max_chapters_ = 0;
    int checkpoint_every_ = 10;
    std::int64_t max_total_calls_ = 0;
    novelcore::RunLimits limits_;

    std::shared_ptr<RunState> state_;
    std::uint64_t run_id_ = 0;
    bool running_ = false;
    QString last_result_;
    novelcore::RunOutcome last_outcome_;

    widgets::Segmented* mode_control_ = nullptr;
    widgets::NumberInput* llm_calls_input_ = nullptr;
    widgets::NumberInput* max_chapters_input_ = nullptr;
    widgets::NumberInput* high_tier_input_ = nullptr;
    widgets::NumberInput* images_input_ = nullptr;
    widgets::NumberInput* total_calls_input_ = nullptr;
    widgets::NumberInput* checkpoint_input_ = nullptr;
    widgets::Button* start_btn_ = nullptr;
    widgets::Button* stop_btn_ = nullptr;
    // 以下三处都是长判定/多行文本：ElidedLabel（单行省略 + hover 全文 + 点击展开）
    widgets::ElidedLabel* precondition_ = nullptr;
    QLabel* status_ = nullptr;
    widgets::ElidedLabel* progress_text_ = nullptr;
    widgets::ElidedLabel* stop_text_ = nullptr;
    widgets::ProgressBar* progress_ = nullptr;
    QPlainTextEdit* timeline_ = nullptr;
};

} // namespace shine::app
