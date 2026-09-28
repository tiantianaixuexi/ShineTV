#pragma once
// shine::app —— 命令面板（P03-S9，Ctrl+K）：命令 / 页面 / 项目内容 三类结果，模糊匹配。
// 输入即搜（防抖 150ms，见 P03 风险表）；↑↓ 选择、Enter 执行、Esc 关。
// 命中字符以 accent.primary 高亮（运行时取 Token，源码零字面色）。
#include <QString>
#include <QWidget>
#include <functional>
#include <vector>

class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QTimer;

namespace shine::app {

struct CommandItem {
    enum class Kind { Command, Page, Entity };

    Kind kind = Kind::Command;
    QString title;
    QString subtitle; // 右侧补充（快捷键等）
    QString detail;   // 第二行（实体副标题，如「章节 12 · 雨夜的灯语」）
    std::function<void()> action;
};

class CommandPalette : public QWidget {
  public:
    explicit CommandPalette(QWidget* host);

    void SetCommands(std::vector<CommandItem> items); // 命令 + 页面（静态注册）
    void SetEntityProvider(std::function<std::vector<CommandItem>(const QString& query)> provider);

    void OpenPalette();
    void ClosePalette();
    [[nodiscard]] bool IsOpen() const;

    // —— 自检 / 截图辅助（同步查询，不经防抖）——
    [[nodiscard]] int QueryCount(const QString& query) const;
    [[nodiscard]] QString GroupSummary(const QString& query) const; // "命令 n / 页面 n / 项目内容 n"
    [[nodiscard]] QString FirstTitle(const QString& query) const;
    void ExecuteCurrent();

  protected:
    bool eventFilter(QObject* watched, QEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;

  private:
    struct Row {
        CommandItem item;
        bool selectable = true;
    };

    [[nodiscard]] std::vector<Row> Match(const QString& query) const;
    void Rebuild(const QString& query);
    void MoveSelection(int delta);
    void Relayout();
    void AdjustHeight(); // 面板高度随结果行数走，封顶 60vh

    QWidget* host_ = nullptr;
    QLineEdit* edit_ = nullptr;
    QListWidget* list_ = nullptr;
    QLabel* empty_label_ = nullptr; // 无结果时的空态（.cmdk-empty）
    QTimer* debounce_ = nullptr;
    // 上下两块固定区的容器，供 AdjustHeight 实测高度（不写死常数）
    QFrame* input_row_ = nullptr;
    QFrame* foot_ = nullptr;
    QFrame* list_box_ = nullptr;
    std::vector<CommandItem> commands_;
    std::vector<Row> rows_; // 当前渲染行（与 list 行序对齐）
    std::function<std::vector<CommandItem>(const QString&)> entity_provider_;
};

} // namespace shine::app
