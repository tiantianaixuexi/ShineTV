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

} // namespace shine::widgets
