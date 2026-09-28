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

// repolish：改动态属性后刷新 QSS
void Repolish(QWidget* w);

} // namespace shine::widgets
