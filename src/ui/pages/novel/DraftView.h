#pragma once
// P04-S6 DraftView：正文流式预览（PLAN §5 S6 判据 + UI.md §2.1/§3）：
// * 流式逐 token 只追加**末段**（append-only 证据进 StreamProbe），新增段高亮
//   （accent.primary 背景 20%）+ 生成中呼吸光边框 —— 颜色零内联（check-layers rule 3）。
// * 不卡 UI：worker 跑 `LlmStreamFn`（writer 角色），on_delta 回主线程追加；
//   流式期间 UI 心跳（QTimer）持续走，探针以心跳计数为证。
// * 中断（StopStream）→ 已收正文**即刻落盘**（chapters.body + words）；状态不破坏。
// * 生成失败 → 段末 status.danger 标原因 +「重试本段」；重试成功清除失败标记。
// * 手改后重算哈希（`novelcore::Sha1Hex`）—— 探针可读 BodyHash()，保存落 chapters.body。
#include "ui/kit/controls/WidgetCommon.h" // QWidget
#include "novel/NovelDirector.h"     // LlmStreamFn / LlmCallFn / AgentError

#include <QString>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class QLabel;
class QPlainTextEdit;
class QTimer;
class QVariantAnimation;

namespace shine::db::sqlite {
class Database;
}
namespace shine::widgets {
class Button;
} // namespace shine::widgets

namespace shine::app {

class DraftView : public QWidget {
  public:
    explicit DraftView(QWidget* parent = nullptr);
    ~DraftView() override;
    DraftView(const DraftView&) = delete;
    DraftView& operator=(const DraftView&) = delete;

    void SetLlm(agent::LlmStreamFn stream, agent::LlmCallFn call = nullptr);

    // 一部一库：开「当前书」db/novel.db 可写句柄（保存正文用）
    bool OpenBook(const std::filesystem::path& dbPath, QString* err = nullptr);
    void CloseDb() noexcept;
    void SelectChapter(qint64 chapterId, const QString& title, const QString& body);

    // —— 流式写作（T11 正文的流式预览路径）——
    void StartStream(const QString& hint = {}); // worker 流式续写（append-only 末段）
    void StopStream();                          // 中断：已收正文即刻落盘
    void RetryLastSegment();                    // 失败段末重试
    void SaveManualEdit();                      // 手改落盘（chapters.body + words）

    [[nodiscard]] bool Streaming() const { return streaming_; }
    [[nodiscard]] bool Breathing() const;
    [[nodiscard]] QString BodyHash() const { return body_hash_; }
    [[nodiscard]] qint64 CurrentChapterId() const { return chapter_id_; }
    [[nodiscard]] QPlainTextEdit* Edit() const { return edit_; }

    // 阻塞式等流式结束（跑事件循环 → UI 心跳照走；超时返回 false）
    bool WaitStream(int timeoutMs);

    // —— 自动化探针（S6 判据；产品代码不用）——
    [[nodiscard]] QString StreamProbe() const; // 逐次追加账（append-only / 段高亮 / 心跳）
    [[nodiscard]] QString DraftProbe() const;  // 状态账（hash / 流式 / 呼吸光 / 失败段 / 落盘比对）

  private:
    void AppendDelta(const QString& s);
    void FinishStream(const std::expected<std::string, agent::AgentError>& r);
    void StartBreathing();
    void StopBreathing();
    // webui .draft 排版：14px / 行高 1.9 / 段距 14px / 首行缩进 2em（2em = 2 × 14px）
    void ApplyDraftTypography();
    static constexpr qreal kIndentPx = 28.0;
    void MarkFailure(const QString& reason);
    void ClearFailureMarker();
    void SaveToDb(const QString& body);
    void RecalcHash(const QString& body);
    void SetHint(const QString& text, std::uint32_t token);

    // —— 数据 ——
    std::unique_ptr<db::sqlite::Database> db_;
    std::filesystem::path db_path_; // 已开的书库（同库不重开）
    qint64 chapter_id_ = 0;
    QString chapter_title_;
    QString baseline_hash_; // 载入时的正文哈希（手改后与其对比）
    QString body_hash_;     // 实时重算（Sha1Hex）
    bool streaming_ = false;
    bool applying_ = false; // 程序性改文本（载入/流式追加）≠ 手改
    bool typing_ = false;   // 正在刷 .draft 块格式（防 contentsChange 递归）
    int run_id_ = 0;        // 运行代号：迟到的旧 worker 回调（中断/重试后）一律作废
    QString failed_reason_; // 非空 = 有失败段待重试
    int seg_start_ = 0;     // 当前流式段起点（只追加末段的锚）
    int fail_pos_ = -1;     // 失败标记插入点
    int heartbeats_ = 0;    // UI 心跳计数（流式期间走 = 不卡 UI 的证据）
    struct AppendRec {
        int seq = 0;
        int delta_chars = 0;
        int total_chars = 0;
        int seg = 0; // 第几段（重试后 +1）
        bool prefix_ok = true;
    };
    std::vector<AppendRec> appends_;
    int seg_index_ = 0;

    agent::LlmStreamFn stream_;
    agent::LlmCallFn call_;

    // —— 控件 ——
    QPlainTextEdit* edit_ = nullptr;
    QLabel* state_ = nullptr;
    widgets::Button* stream_btn_ = nullptr;
    widgets::Button* stop_btn_ = nullptr;
    widgets::Button* retry_btn_ = nullptr;
    widgets::Button* save_btn_ = nullptr;
    QTimer* heartbeat_ = nullptr;
    QVariantAnimation* breath_ = nullptr;
};

} // namespace shine::app
