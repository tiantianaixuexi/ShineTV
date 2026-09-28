#pragma once
// shine::widgets —— 控件共享约定（P02-S5，UI.md §2.1）
//
// 统一约定：
//  * 样式全部来自 QssBuilder 的全局 QSS，选择器挂在动态属性上
//    （shineKind / shineVariant / shineSize / shineState），源码零内联色值
//    （tools/check-layers.ps1 rule 3）；主题切换 = 换全局 QSS，控件自动跟随；
//  * 五态 = normal / hover / pressed / disabled / focus（UI.md §1，缺一态 = 未完成）；
//    截图 / 演示用 SetForcedState 强制 hover / pressed / focus（shineState 属性 + repolish）。
#include <QColor>
#include <QLabel>
#include <QString>
#include <QStringView>
#include <QWidget>

#include <cstdint>

namespace shine::widgets {

enum class State { Normal, Hover, Pressed, Disabled, Focus };

// Token 存储布局 RRGGBBAA → QColor（自绘控件用）
[[nodiscard]] QColor TokenQColor(std::uint32_t rgba);

// 强制控件进入指定态（Normal 清除强制；Disabled 走 setEnabled(false)）
void SetForcedState(QWidget* w, State s);

// 样式挂钩（控件构造时调用一次）
void SetKind(QWidget* w, const char* kind);
void SetVariant(QWidget* w, const char* variant);
void SetSizeAttr(QWidget* w, const char* size); // "sm" / "md" / "lg"

// 两档字重之一：Semibold(600)（总纲 §2.3）；默认 Regular(400)
void SetSemibold(QWidget* w, bool on);

// 把控件前景色设为某个 Token（运行时拼串；零字面色值，check-layers rule 3 口径）。
// 页面此前各写一份同名 SetColor 副本，现统一到这里。
void SetTextColor(QWidget* w, std::uint32_t token);

// 段落标题标签 = QLabel + statetitle + Semibold。
// 页面里「new QLabel + SetKind(statetitle) + SetSemibold」三行块重复出现二十余处，
// 收敛成一个工厂；调用点仍可继续 setWordWrap / setFixedHeight 等。
[[nodiscard]] QLabel* SectionTitle(const QString& text, QWidget* parent = nullptr,
                                   bool semibold = true);

// 单行省略标签：QLabel 不会自动省略，长文本会把所在布局顶出容器。
// 这里是「按可用宽度省略 + 全文进 tooltip + 点击就地展开成多行」的行为，
// 给列表行、判定结论这类「一行放不下但必须看全」的位置用。
class ElidedLabel : public QLabel {
    Q_OBJECT

  public:
    explicit ElidedLabel(const QString& text = QString{}, QWidget* parent = nullptr);

    // 全文（画出来的可能只是它的省略版；tooltip / 展开态都用它）
    void SetFullText(const QString& full);
    [[nodiscard]] QString FullText() const { return full_; }

    // 不可展开时退化为纯省略（没有点击行为），用于纯展示位
    void SetExpandable(bool on) { expandable_ = on; }
    void SetExpanded(bool on);
    [[nodiscard]] bool IsExpanded() const { return expanded_; }

    // 需要在 setTextColor 之后重画（前景色变化不影响省略宽度，这里只为语义完整）
    void Refresh();

  protected:
    void resizeEvent(QResizeEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;

  private:
    void Recompute();

    QString full_;
    QString shown_;
    bool expanded_ = false;
    bool expandable_ = true;
};

// repolish：改动态属性后刷新 QSS
void Repolish(QWidget* w);

} // namespace shine::widgets
