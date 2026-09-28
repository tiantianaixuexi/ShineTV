#pragma once
// shine::widgets —— 容器/浮层（P02-S5，UI.md §2.1）：Dialog / Drawer / Toast / Tooltip。
#include "ui/kit/controls/WidgetCommon.h"

#include <functional>
#include <memory>
#include <vector>

#include <QFrame>
#include <QLabel>
#include <QString>
#include <QWidget>

class QHBoxLayout;
class QVBoxLayout;
class QPushButton;

namespace shine::widgets {

// Dialog —— sm 420 / md 560 / lg 800；标题/内容/主次按钮；Esc 关；主按钮 Enter 触发
class Dialog : public QWidget {
  public:
    enum class Size { Sm, Md, Lg };

    Dialog(const QString& title, Size s = Size::Md, QWidget* parent = nullptr);

    [[nodiscard]] QVBoxLayout* BodyLayout();
    void SetActions(const QString& primaryText, const QString& secondaryText,
                    std::function<void()> onPrimary, std::function<void()> onSecondary);

  protected:
    void keyPressEvent(QKeyEvent* ev) override;

  private:
    std::function<void()> on_primary_;
    class Button* primary_ = nullptr;
    QVBoxLayout* body_ = nullptr;
    QHBoxLayout* actions_row_ = nullptr;
};

// Drawer —— 右侧 380px；可叠加；滑入 motion.base
class Drawer : public QWidget {
  public:
    explicit Drawer(const QString& title, QWidget* parent = nullptr);

    [[nodiscard]] QVBoxLayout* BodyLayout();
    void Open();  // 滑入（减少动效 → 直接就位）
    void CloseDrawer();

  private:
    QVBoxLayout* body_ = nullptr;
};

// Toast —— info/success/warning/error；右下角；4s 自动消退（error 8s）；可堆叠 3 条
class Toast {
  public:
    enum class Tone { Info, Success, Warning, Error };
    static void Show(const QString& text, Tone tone = Tone::Info);
};

// Tooltip —— 文本 / 富文本 / 带快捷键；200ms 延迟出现
class Tooltip {
  public:
    // hover 200ms 后显示；shortcutText 非空则右侧带 Kbd 标注
    static void Attach(QWidget* host, const QString& richText, const QString& shortcutText = QString{});
};

// SectionCard —— 分区卡片：标题栏 + 内容 + 可折叠。
//
// 页面内部「单列滚动 + 若干分区卡片」的标准件。页面只提供标题与 body，
// 卡片负责统一的标题栏样式、内边距与间距（theme::space::kSteps），
// 于是同一条竖列上的各块有同一套分组视觉，读者一眼能分清「哪块是哪块」。
//
// 用法：
//   auto* card = new widgets::SectionCard(QStringLiteral("门禁 N1–N14"), this);
//   card->SetContentMinWidth(880);                       // 内容区最小宽，防长行被压扁
//   card->BodyLayout()->addWidget(...);
//   column->addWidget(card);
//
// 标题栏整行可点（折叠/展开）；副标题用 ElidedLabel（长文本省略 + hover 全文 + 点击展开）。
class SectionCard : public QFrame {
  public:
    explicit SectionCard(const QString& title, QWidget* parent = nullptr);

    // 内容区布局（页面往里放自己的控件）
    [[nodiscard]] QVBoxLayout* BodyLayout() const;

    // 标题右侧的补充说明（空串 = 不显示；长文本自动省略 + hover 全文）
    void SetSubtitle(const QString& text);
    void SetTitle(const QString& text);

    // 内容区最小宽：门禁行 / 宽表格这类「一行放不下必须看全」的内容用它兜底，
    // 窄窗口时由外层滚动出横向滚动条，而不是把控件互相压扁。
    void SetContentMinWidth(int px);

    // 可折叠（默认不折叠；折叠时只留标题栏，内容区隐藏）
    void SetCollapsible(bool on);
    void SetExpanded(bool on);
    [[nodiscard]] bool IsExpanded() const { return expanded_; }

  private:
    void ApplyExpanded();

    QPushButton* head_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* chevron_ = nullptr;
    ElidedLabel* subtitle_ = nullptr;
    QWidget* body_ = nullptr;
    QVBoxLayout* body_lay_ = nullptr;
    bool collapsible_ = false;
    bool expanded_ = true;
};

} // namespace shine::widgets
