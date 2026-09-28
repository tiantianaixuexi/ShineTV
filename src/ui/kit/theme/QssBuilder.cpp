#include "ui/kit/theme/QssBuilder.h"

#include "ui/kit/theme/Theme.h"
#include "util/File.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <regex>
#include <string>
#include <vector>

namespace shine::theme {
namespace {

// QSS 颜色写法：不透明色 #rrggbb；带 alpha 的用 rgba(r, g, b, 0.xxx)（QSS 对 #RRGGBBAA 不友好）
std::string EmitColor(std::uint32_t rgba) {
    const int r = static_cast<int>((rgba >> 24) & 0xFF);
    const int g = static_cast<int>((rgba >> 16) & 0xFF);
    const int b = static_cast<int>((rgba >> 8) & 0xFF);
    const int a = static_cast<int>(rgba & 0xFF);
    char buf[64];
    if (a == 0xFF) {
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
    } else {
        std::snprintf(buf, sizeof(buf), "rgba(%d, %d, %d, %.3f)", r, g, b, a / 255.0);
    }
    return buf;
}

std::string Trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

// 整张 QSS 模板：**零字面颜色**（设计原则）——所有颜色是 %1..%28 占位符，
// 位置与 kColorTokenNames（Token.h）一一对应，由 Build() 用当前主题的 Token 值填充。
// 按 UI.md §2 的控件清单铺满常用 Qt 控件的每一种视觉状态。
constexpr std::string_view kTemplate = R"QSS(
/* 本样式表由 shine::theme::QssBuilder 从 Token 生成（P01-S8 验收：禁止散落字面色） */

QWidget { color: %9; background-color: %2; font-size: 13px; }
QWidget:disabled { color: %11; }

QMainWindow { background-color: %1; }
QMenuBar { background-color: %2; color: %9; border-bottom: 1px solid %6; }
QMenuBar::item:selected { background-color: %25; color: %9; }
QMenu { background-color: %3; color: %9; border: 1px solid %7; }
QMenu::item:selected { background-color: %25; }

QPushButton {
  background-color: %3; color: %9;
  border: 1px solid %7; border-radius: 5px; padding: 5px 12px;
}
QPushButton:hover { background-color: %24; border-color: %8; }
QPushButton:pressed { background-color: %2; }
QPushButton:disabled { background-color: %2; color: %11; border-color: %6; }
QPushButton:checked { background-color: %25; color: %13; }

QToolButton {
  background-color: transparent; color: %9;
  border: 1px solid transparent; border-radius: 5px; padding: 4px 8px;
}
QToolButton:hover { background-color: %24; border-color: %7; }
QToolButton:pressed { background-color: %2; }
QToolButton:checked { background-color: %25; color: %13; }

QLineEdit, QPlainTextEdit, QTextEdit {
  background-color: %26; color: %9;
  border: 1px solid %7; border-radius: 6px; padding: 3px 10px;
  selection-background-color: %25; selection-color: %9;
  placeholder-text-color: %11;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: %27; }
QLineEdit:disabled, QPlainTextEdit:disabled, QTextEdit:disabled {
  background-color: %2; color: %11;
}
QLineEdit[readOnly="true"] { background-color: %26; color: %10; }

QSpinBox, QDoubleSpinBox {
  background-color: %3; color: %9; border: 1px solid %7; border-radius: 5px; padding: 2px 4px;
}
QSpinBox::up-button, QDoubleSpinBox::up-button { width: 16px; background-color: %4; }
QSpinBox::down-button, QDoubleSpinBox::down-button { width: 16px; background-color: %4; }
QSpinBox::up-arrow { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-bottom: 5px solid %10; }
QSpinBox::down-arrow { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 5px solid %10; }

QComboBox {
  background-color: %26; color: %9; border: 1px solid %7; border-radius: 6px; padding: 3px 10px;
}
QComboBox:hover { border-color: %8; }
QComboBox::drop-down { border: none; width: 20px; }
QComboBox::down-arrow { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 5px solid %10; margin-right: 6px; }
QComboBox QAbstractItemView {
  background-color: %3; color: %9;
  border: 1px solid %7; selection-background-color: %25; selection-color: %9;
}

QCheckBox, QRadioButton { color: %9; spacing: 6px; }
QCheckBox::indicator, QRadioButton::indicator { width: 14px; height: 14px; }
QCheckBox::indicator:unchecked { background-color: %3; border: 1px solid %8; border-radius: 3px; }
QCheckBox::indicator:checked { background-color: %13; border: 1px solid %13; border-radius: 3px; }
QRadioButton::indicator:unchecked { background-color: %3; border: 1px solid %8; border-radius: 7px; }
QRadioButton::indicator:checked { background-color: %13; border: 1px solid %13; border-radius: 7px; }
QCheckBox:disabled, QRadioButton:disabled { color: %11; }

QSlider::groove:horizontal { height: 4px; background-color: %1; border-radius: 2px; }
QSlider::sub-page:horizontal { background-color: %13; border-radius: 2px; }
QSlider::handle:horizontal { width: 14px; margin: -5px 0; border-radius: 7px; background-color: %13; }
QSlider::handle:horizontal:hover { background-color: %14; }
QSlider::groove:vertical { width: 4px; background-color: %1; border-radius: 2px; }
QSlider::sub-page:vertical { background-color: %13; border-radius: 2px; }
QSlider::handle:vertical { height: 14px; margin: 0 -5px; border-radius: 7px; background-color: %13; }

QProgressBar {
  background-color: %26; border: none; border-radius: 999px; text-align: center; color: %10;
}
QProgressBar::chunk { background-color: %13; border-radius: 999px; }

QGroupBox { border: 1px solid %6; border-radius: 10px; margin-top: 8px; padding-top: 4px; color: %10; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; color: %10; }

QTabWidget::pane { border: none; border-radius: 10px; background-color: transparent; }
QTabBar { background: transparent; }
/* webui .tabs > button：p8 12 / f13 / w600 / text-muted；选中 accent + 2px 下划线 */
QTabBar::tab {
  background-color: transparent; color: %10;
  padding: 8px 12px; min-height: 30px; border: none;
  font-size: 13px; font-weight: 600;
}
QTabBar::tab:hover { color: %9; }
QTabBar::tab:selected { color: %13; border-bottom: 2px solid %13; }
QTabBar::close-button { subcontrol-position: right; }

QListView, QTreeView, QTableView {
  background-color: %2; color: %9;
  border: 1px solid %6; border-radius: 10px;
  alternate-background-color: %26;
  selection-background-color: %25; selection-color: %9;
}
/* webui .table：th f11.5 w600 muted p8 12 + 底部 line-normal；td p9 12 text-secondary */
QHeaderView::section {
  background-color: %3; color: %10;
  border: none; border-bottom: 1px solid %7; border-right: 1px solid %6;
  padding: 6px 12px;
  font-size: 11px; font-weight: 600;
}
QHeaderView::section:hover { background-color: %24; color: %9; }
QTableView::item { padding: 5px 12px; color: %10; border-bottom: 1px solid %6; }
QTableView::item:selected { background-color: %25; color: %9; }
QTreeView::item { padding: 3px 8px; border-radius: 6px; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background-color: %7; border-radius: 5px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background-color: %8; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:horizontal { background-color: %7; border-radius: 5px; min-width: 24px; }
QScrollBar::handle:horizontal:hover { background-color: %8; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QToolTip {
  background-color: %5; color: %9;
  border: 1px solid %8; padding: 4px 8px;
}

QStatusBar { background-color: %2; color: %10; border-top: 1px solid %6; }

QSplitter::handle { background-color: %6; }
QSplitter::handle:hover { background-color: %8; }

QToolBar { background-color: %3; border-bottom: 1px solid %6; spacing: 4px; padding: 2px; }
QToolBar::separator { background-color: %6; width: 1px; margin: 4px 2px; }
)QSS";

// kit/widgets 样式段（P02-S5）—— 同样**零字面颜色**；选择器挂动态属性：
//   shineKind=控件名  shineVariant=变体  shineSize=sm/md/lg  shineState=截图态强制
// 五态（UI.md §1）：normal / hover / pressed / disabled / focus
// focus = 2px accent 环 + 1px 间隙（border 2px + padding 各减 1px，无布局跳动）
// 注意（P02-S5 探针实证）：**选择器分组会吞掉后续选择器导致规则整条失效**——
//   本段全部写成单选择器规则：伪态（:hover 等）与 shineState 强制态各占一条。
constexpr std::string_view kKitTemplate = R"QSS(

/* —— kit/widgets：通用 —— */
*[shineWeight="semibold"] { font-weight: 600; }
*[shineWeight="bold"] { font-weight: 700; }
/* mono / 小号：webui .mono(12px) / .tiny(12px) / .small(12.5px) */
*[shineWeight="mono"] { font-family: "Cascadia Mono", "Consolas", monospace; font-size: 12px; }
*[shineSize="tiny"] { font-size: 12px; }
*[shineSize="small"] { font-size: 12px; }

/* —— Button（变体 primary/secondary/ghost/danger × sm/md/lg） ——
   webui ui.css .btn：h30 / p0 14 / r6 / f13 / w600；sm h24 p0 10 f12；lg h36 p0 20 f14 r10 */
*[shineKind="button"] {
  border: 1px solid %7; border-radius: 6px; padding: 0 14px; min-height: 30px;
  background-color: %3; color: %9; font-size: 13px; font-weight: 600;
}
*[shineKind="button"][shineSize="sm"] { padding: 0 10px; min-height: 24px; font-size: 12px; }
*[shineKind="button"][shineSize="lg"] { padding: 0 20px; min-height: 36px; border-radius: 10px; font-size: 14px; }
*[shineKind="button"][shineVariant="primary"] { background-color: %13; color: %15; border-color: %13; }
*[shineKind="button"][shineVariant="secondary"] { background-color: %4; color: %9; border-color: %7; }
*[shineKind="button"][shineVariant="ghost"] { background-color: transparent; color: %13; border-color: %7; }
*[shineKind="button"][shineVariant="danger"] { background-color: %20; color: %12; border-color: %20; }
*[shineKind="button"][shineVariant="primary"]:hover { background-color: %14; border-color: %14; }
*[shineKind="button"][shineVariant="primary"][shineState="hover"] { background-color: %14; border-color: %14; }
*[shineKind="button"][shineVariant="ghost"]:hover { background-color: %24; color: %14; }
*[shineKind="button"][shineVariant="ghost"][shineState="hover"] { background-color: %24; color: %14; }
*[shineKind="button"]:hover { background-color: %24; border-color: %8; }
*[shineKind="button"][shineState="hover"] { background-color: %24; border-color: %8; }
*[shineKind="button"][shineVariant="primary"]:pressed { background-color: %2; color: %13; border-color: %13; }
*[shineKind="button"][shineVariant="primary"][shineState="pressed"] { background-color: %2; color: %13; border-color: %13; }
*[shineKind="button"]:pressed { background-color: %2; border-color: %8; }
*[shineKind="button"][shineState="pressed"] { background-color: %2; border-color: %8; }
*[shineKind="button"]:disabled { background-color: %2; color: %11; border-color: %6; }
*[shineKind="button"][shineState="disabled"] { background-color: %2; color: %11; border-color: %6; }
/* focus = 2px accent 环 + 内距各减 1px（无布局跳动） */
*[shineKind="button"]:focus { border: 2px solid %27; padding: 0 13px; }
*[shineKind="button"][shineState="focus"] { border: 2px solid %27; padding: 0 13px; }
*[shineKind="button"][shineSize="sm"]:focus { padding: 0 9px; }
*[shineKind="button"][shineSize="sm"][shineState="focus"] { padding: 0 9px; }
*[shineKind="button"][shineSize="lg"]:focus { padding: 0 19px; }
*[shineKind="button"][shineSize="lg"][shineState="focus"] { padding: 0 19px; }

/* —— IconButton（active 高亮给活动栏） —— */
*[shineKind="iconbutton"] {
  border: 1px solid transparent; border-radius: 5px; padding: 5px;
  background-color: transparent; color: %10;
}
*[shineKind="iconbutton"][shineSize="sm"] { padding: 3px; }
*[shineKind="iconbutton"]:hover { background-color: %24; color: %9; }
*[shineKind="iconbutton"][shineState="hover"] { background-color: %24; color: %9; }
*[shineKind="iconbutton"]:pressed { background-color: %2; color: %9; }
*[shineKind="iconbutton"][shineState="pressed"] { background-color: %2; color: %9; }
*[shineKind="iconbutton"][active="true"] { background-color: %25; color: %13; border-color: %6; }
*[shineKind="iconbutton"]:disabled { color: %11; }
*[shineKind="iconbutton"][shineState="disabled"] { color: %11; }
*[shineKind="iconbutton"]:focus { border: 2px solid %27; padding: 4px; }
*[shineKind="iconbutton"][shineState="focus"] { border: 2px solid %27; padding: 4px; }

/* —— Card（flat/outlined/elevated + accent 左条；hover 抬升由 Tween 负责） ——
   webui .card：bg-panel + line-subtle 1px + r-md(10) —— */
*[shineKind="card"] { background-color: %3; border: 1px solid %6; border-radius: 10px; }
*[shineKind="card"][shineVariant="flat"] { border-color: transparent; }
*[shineKind="card"][shineVariant="outlined"] { border-color: %7; }
*[shineKind="card"][shineVariant="elevated"] { background-color: %4; border-color: %7; }
*[shineKind="card"]:hover { border-color: %8; }
*[shineKind="card"][shineState="hover"] { border-color: %8; }
*[shineKind="card"]:disabled { color: %11; border-color: %6; }
*[shineKind="card"][shineState="disabled"] { color: %11; border-color: %6; }
*[shineKind="cardaccent"] { background-color: %13; border-radius: 2px; }

/* —— SectionCard（分区卡片：标题栏 + 内容；页面单列滚动时的分组件）——
   标题栏对齐 webui .card-h：p12 16 + 底部发丝线 + 13.5px w600 —— */
*[shineKind="sectioncard"] { background-color: %3; border: 1px solid %6; border-radius: 10px; }
*[shineKind="sectionhead"] { background: transparent; border: none; text-align: left; }
*[shineKind="sectionhead"]:hover { background-color: %24; border-radius: 6px; }
*[shineKind="sectionhead"][shineState="hover"] { background-color: %24; border-radius: 6px; }
*[shineKind="sectionhead"]:focus { border: none; }
*[shineKind="sectionhead"][shineState="focus"] { border: none; }
*[shineKind="sectiontitle"] { background: transparent; color: %9; }
*[shineKind="sectionsub"] { background: transparent; color: %11; }
*[shineKind="sectionchevron"] { background: transparent; color: %10; }

/* —— Tag：webui .tag h20 p0 8 r-pill f11.5 w600（色来自 status.* / accent.*） —— */
*[shineKind="tag"] {
  border: 1px solid %7; border-radius: 999px; padding: 0 8px; min-height: 20px;
  background-color: %26; color: %10; font-size: 11px; font-weight: 600;
}
*[shineKind="tag"][tone="accent"] { background-color: %13; color: %15; border-color: %13; }
*[shineKind="tag"][tone="info"] { background-color: %17; color: %12; border-color: %17; }
*[shineKind="tag"][tone="ok"] { background-color: %18; color: %12; border-color: %18; }
*[shineKind="tag"][tone="warn"] { background-color: %19; color: %12; border-color: %19; }
*[shineKind="tag"][tone="danger"] { background-color: %20; color: %12; border-color: %20; }
*[shineKind="tag"][tone="busy"] { background-color: %21; color: %12; border-color: %21; }
*[shineKind="tag"][tone="pending"] { background-color: %23; color: %12; border-color: %23; }
*[shineKind="tag"][tone="idle"] { background-color: %22; color: %12; border-color: %22; }
*[shineKind="tag"]:hover { border-color: %8; }
*[shineKind="tag"][shineState="hover"] { border-color: %8; }
*[shineKind="tag"]:disabled { color: %11; border-color: %6; }
*[shineKind="tag"][shineState="disabled"] { color: %11; border-color: %6; }
*[shineKind="tag"]:focus { border: 2px solid %27; padding: 0 7px; }
*[shineKind="tag"][shineState="focus"] { border: 2px solid %27; padding: 0 7px; }

/* —— Badge（数字/点） —— */
*[shineKind="badge"] {
  border-radius: 999px; padding: 1px 6px; background-color: %20; color: %12;
}
*[shineKind="badge"][tone="accent"] { background-color: %13; color: %15; }
*[shineKind="badge"][dot="true"] { padding: 3px; }

/* —— Kbd（快捷键标注） —— */
*[shineKind="kbd"] {
  border: 1px solid %7; border-bottom-width: 2px; border-radius: 4px;
  padding: 1px 6px; background-color: %4; color: %10;
}

/* —— Segmented：webui .seg p3 gap2 fill-muted + line-subtle + r-sm(6) —— */
*[shineKind="segmented"] { background-color: %26; border: 1px solid %6; border-radius: 6px; padding: 3px; }
/* .seg > button：h26 / p0 13 / r4 / f12.5 / w600 / text-muted；选中态抬到 bg-elevated */
*[shineKind="segment"] {
  border: 1px solid transparent; border-radius: 4px; padding: 0 13px; min-height: 26px;
  background-color: transparent; color: %10; font-size: 12px; font-weight: 600;
}
*[shineKind="segment"]:hover { color: %9; }
*[shineKind="segment"][shineState="hover"] { color: %9; }
*[shineKind="segment"][selected="true"] { background-color: %4; color: %9; border-color: %7; }
*[shineKind="segment"]:disabled { color: %11; }
*[shineKind="segment"][shineState="disabled"] { color: %11; }
*[shineKind="segment"]:focus { border: 2px solid %27; padding: 0 12px; }
*[shineKind="segment"][shineState="focus"] { border: 2px solid %27; padding: 0 12px; }

/* —— Spinner（转圈自绘，颜色走 accent） —— */
*[shineKind="spinner"] { background: transparent; }
*[shineKind="spinner"][variant="muted"] { color: %11; }

/* —— Field（标签左/上 + help + error） —— */
*[shineKind="field"] { background: transparent; }
*[shineKind="fieldlabel"] { color: %10; background: transparent; }
*[shineKind="fieldhelp"] { color: %11; background: transparent; }
*[shineKind="fielderror"] { color: %20; background: transparent; }

/* —— 输入族：webui .input h30 / p0 10 / r6 / fill-muted + line-normal —— */
*[shineKind="input"] {
  background-color: %26; color: %9; border: 1px solid %7; border-radius: 6px;
  padding: 0 10px; min-height: 30px; font-size: 13px;
  placeholder-text-color: %11;
}
*[shineKind="input"]:hover { border-color: %8; }
*[shineKind="input"][shineState="hover"] { border-color: %8; }
*[shineKind="input"]:focus { border: 2px solid %27; padding: 0 9px; background-color: %2; }
*[shineKind="input"][shineState="focus"] { border: 2px solid %27; padding: 0 9px; background-color: %2; }
*[shineKind="input"][error="true"] { border-color: %20; }
*[shineKind="input"][error="true"][shineState="focus"] { border-color: %20; border-width: 1px; padding: 0 10px; }
*[shineKind="input"][error="true"]:focus { border-color: %20; border-width: 1px; padding: 0 10px; }
*[shineKind="input"]:disabled { background-color: %2; color: %11; border-color: %6; }
*[shineKind="input"][shineState="disabled"] { background-color: %2; color: %11; border-color: %6; }
*[shineKind="input"][readOnly="true"] { background-color: %26; color: %10; }
/* .textarea：p8 10 + r6 + line-height 1.6（高度随行数自适应，不设 min-height） */
*[shineKind="textarea"] {
  background-color: %26; color: %9; border: 1px solid %7; border-radius: 6px; padding: 8px 10px;
  font-size: 13px;
  placeholder-text-color: %11;
}
*[shineKind="textarea"]:hover { border-color: %8; }
*[shineKind="textarea"]:focus { border: 2px solid %27; padding: 7px 9px; background-color: %2; }
*[shineKind="textarea"][shineState="focus"] { border: 2px solid %27; padding: 7px 9px; background-color: %2; }
*[shineKind="textarea"][error="true"] { border-color: %20; }
*[shineKind="textarea"]:disabled { background-color: %2; color: %11; }
*[shineKind="textarea"][shineState="disabled"] { background-color: %2; color: %11; }
*[shineKind="clearbutton"] { background: transparent; border: none; color: %11; padding: 0px; }
*[shineKind="clearbutton"]:hover { color: %9; }
*[shineKind="clearbutton"][shineState="hover"] { color: %9; }
*[shineKind="searchicon"] { background: transparent; color: %11; border: none; }
*[shineKind="counter"] { background: transparent; color: %11; }
*[shineKind="counter"][error="true"] { color: %20; }

/* —— Select（弹层列表） —— */
/* 摘要按钮：容器自己画描边，按钮只负责左对齐文字 + 交互反馈。
   颜色必须落在这里而不是内联 palette(...)：QSS 的 palette(text) 取 QPalette，
   与 Token 不同源；深色主题下会渲染成系统黑字。 */
*[shineKind="selectbutton"] {
  background: transparent; border: none; border-radius: 5px;
  text-align: left; color: %9; padding: 4px 8px;
}
*[shineKind="selectbutton"]:hover { background-color: %24; }
*[shineKind="selectbutton"][shineState="hover"] { background-color: %24; }
*[shineKind="selectbutton"]:pressed { background-color: %2; }
*[shineKind="selectbutton"][shineState="pressed"] { background-color: %2; }
*[shineKind="selectbutton"]:focus { border: 2px solid %27; padding: 3px 7px; }
*[shineKind="selectbutton"][shineState="focus"] { border: 2px solid %27; padding: 3px 7px; }
*[shineKind="selectbutton"]:disabled { color: %11; }
*[shineKind="selectbutton"][shineState="disabled"] { color: %11; }
*[shineKind="select"] {
  background-color: %26; color: %9; border: 1px solid %7; border-radius: 6px;
  padding: 0 10px; min-height: 30px; font-size: 13px;
}
*[shineKind="select"]:hover { border-color: %8; }
*[shineKind="select"][shineState="hover"] { border-color: %8; }
*[shineKind="select"]:focus { border: 2px solid %27; padding: 0 9px; background-color: %2; }
*[shineKind="select"][shineState="focus"] { border: 2px solid %27; padding: 0 9px; background-color: %2; }
*[shineKind="select"]:disabled { color: %11; border-color: %6; }
*[shineKind="select"][shineState="disabled"] { color: %11; border-color: %6; }
*[shineKind="selectpopup"] {
  background-color: %5; color: %9; border: 1px solid %7; border-radius: 10px; padding: 4px;
}
*[shineKind="selectgroup"] { color: %10; background: transparent; padding: 4px 8px 2px 8px; }
*[shineKind="selectitem"] { background: transparent; color: %9; padding: 4px 8px; border-radius: 5px; }
*[shineKind="selectitem"]:hover { background-color: %24; }
*[shineKind="selectitem"][shineState="hover"] { background-color: %24; }
*[shineKind="selectitem"][checked="true"] { color: %13; }
*[shineKind="selectitem"]:disabled { color: %11; }
*[shineKind="selectitem"][shineState="disabled"] { color: %11; }

/* —— Slider（值 + 单位标签；handle 态挂在控件 shineState 上） —— */
*[shineKind="slidervalue"] { background: transparent; color: %10; }
QSlider::groove:horizontal[shineKind="slider"] { height: 4px; border-radius: 2px; background-color: %1; }
QSlider::handle:horizontal[shineKind="slider"] { width: 14px; margin: -5px 0; border-radius: 7px; background-color: %13; }
QSlider::handle:horizontal[shineKind="slider"][shineState="hover"] { background-color: %14; }
QSlider::handle:horizontal[shineKind="slider"][shineState="pressed"] { background-color: %16; }
QSlider::handle:horizontal[shineKind="slider"][shineState="disabled"] { background-color: %11; }
QSlider::handle:horizontal[shineKind="slider"][shineState="focus"] { background-color: %14; }
QSlider::sub-page:horizontal[shineKind="slider"] { background-color: %13; border-radius: 2px; }
QSlider::groove:vertical[shineKind="slider"] { width: 4px; border-radius: 2px; background-color: %1; }
QSlider::handle:vertical[shineKind="slider"] { height: 14px; margin: 0 -5px; border-radius: 7px; background-color: %13; }
QSlider::sub-page:vertical[shineKind="slider"] { background-color: %13; border-radius: 2px; }

/* —— Toggle / Checkbox / Radio（自绘；QSS 只管文字色） —— */
*[shineKind="toggle"] { background: transparent; }
*[shineKind="check"] { background: transparent; color: %9; }
*[shineKind="radio"] { background: transparent; color: %9; }
*[shineKind="check"]:disabled { color: %11; }
*[shineKind="radio"]:disabled { color: %11; }
*[shineKind="check"][shineState="disabled"] { color: %11; }
*[shineKind="radio"][shineState="disabled"] { color: %11; }

/* —— ProgressBar：webui .prog h6 r-pill fill-muted；chunk 用 grad-accent（取 accent） —— */
*[shineKind="progressbar"] {
  background-color: %26; border: none; border-radius: 999px;
  min-height: 6px; max-height: 6px;
}
*[shineKind="progressbar"]::chunk { background-color: %13; border-radius: 999px; }
*[shineKind="progresslabel"] { background: transparent; color: %10; }
*[shineKind="progressbar"][state="error"]::chunk { background-color: %20; }
*[shineKind="progressbar"][state="ok"]::chunk { background-color: %18; }
*[shineKind="progressbar"][state="idle"]::chunk { background-color: %22; }

/* —— EmptyState / ErrorState（容器三态中的两态）—— */
/* webui .empty：p40 20 居中；.glyph 52×52 r-lg + 虚线边 + fill-muted */
*[shineKind="emptystate"] { background: transparent; }
*[shineKind="errorstate"] { background: transparent; }
*[shineKind="statetitle"] { background: transparent; color: %9; font-weight: 600; }
*[shineKind="statesub"] { background: transparent; color: %10; font-size: 12px; }
*[shineKind="stateicon"] { background: transparent; color: %11; }
/* .statesub 系列的补充字号档：页内小字统一 12px muted */
*[shineKind="statemeta"] { background: transparent; color: %11; font-size: 11px; }
/* 状态行：与 .tag 同族但不做胶囊底，只给文字色 */
*[shineKind="statestatus"] { background: transparent; color: %10; font-size: 12px; font-weight: 600; }
/* webui .kpi .k-delta：涨绿 / 跌红 / 持平灰 */
*[shineKind="statemeta"][shineVariant="up"] { color: %18; font-weight: 700; }
*[shineKind="statemeta"][shineVariant="down"] { color: %20; font-weight: 700; }
*[shineKind="statemeta"][shineVariant="flat"] { color: %11; }
*[shineKind="errorstate"] *[shineKind="stateicon"] { color: %20; }
*[shineKind="statedetail"] {
  background-color: %26; color: %10; border: 1px solid %6; border-radius: 6px;
  padding: 4px 8px; font-size: 12px;
}

/* —— Art 占位画 / 胶片格 / 输入外框 ——
   webui .art：r-sm(6) + fill-muted；.film-cell r-sm + line-normal + fthumb h84 —— */
*[shineKind="art"] { background-color: %26; border: 1px solid %6; border-radius: 6px; }
*[shineKind="filmcell"] {
  background-color: %26; border: 1px solid %7; border-radius: 6px;
}
*[shineKind="filmcell"][selected="true"] { border-color: %13; }
*[shineKind="inputframe"] {
  background-color: %26; border: 1px solid %6; border-radius: 6px;
}

/* —— Toast：webui .toast r-md(10) / p10 14 / f12.5 / 左侧 3px 状态条 —— */
*[shineKind="toast"] {
  background-color: %5; color: %9; border: 1px solid %7; border-radius: 10px; padding: 10px 14px;
  font-size: 12px;
}
*[shineKind="toast"][tone="info"] { border-left: 3px solid %17; }
*[shineKind="toast"][tone="success"] { border-left: 3px solid %18; }
*[shineKind="toast"][tone="warning"] { border-left: 3px solid %19; }
*[shineKind="toast"][tone="error"] { border-left: 3px solid %20; }
*[shineKind="toast"][tone="busy"] { border-left: 3px solid %21; }
*[shineKind="toast"][tone="pending"] { border-left: 3px solid %23; }
*[shineKind="toasticon"][tone="info"] { color: %17; background: transparent; }
*[shineKind="toasticon"][tone="success"] { color: %18; background: transparent; }
*[shineKind="toasticon"][tone="warning"] { color: %19; background: transparent; }
*[shineKind="toasticon"][tone="error"] { color: %20; background: transparent; }
*[shineKind="toasticon"][tone="busy"] { color: %21; background: transparent; }
*[shineKind="toasticon"][tone="pending"] { color: %23; background: transparent; }

/* —— Dialog / Drawer / Tooltip：webui .modal r-lg(14)；tooltip r-sm(6) —— */
*[shineKind="dialog"] { background-color: %5; border: 1px solid %7; border-radius: 14px; }
*[shineKind="dialogtitle"] { background: transparent; color: %9; }
*[shineKind="dialogbody"] { background: transparent; color: %10; }
*[shineKind="drawer"] { background-color: %5; border-left: 1px solid %7; }
*[shineKind="tooltip"] {
  background-color: %5; color: %9; border: 1px solid %8; border-radius: 6px; padding: 4px 9px;
  font-size: 11px;
}
*[shineKind="tooltip"] QLabel { background: transparent; color: %9; }
*[shineKind="tooltipkbd"] { background: transparent; color: %10; }

/* -- Scrim: floating-layer mask (bg.overlay is now surface-only, never a mask). -- */
*[shineKind="scrim"] { background-color: %28; }

/* —— Tabs：webui .tabs > button p8 12 / f13 / w600 / text-muted；选中 accent + 2px 指示条 —— */
*[shineKind="tabs"] { background: transparent; border-bottom: 1px solid %6; }
*[shineKind="tab"] {
  background: transparent; color: %10; padding: 8px 12px; min-height: 30px;
  font-size: 13px; font-weight: 600;
}
*[shineKind="tab"]:hover { color: %9; }
*[shineKind="tab"][shineState="hover"] { color: %9; }
*[shineKind="tab"][selected="true"] { color: %13; border-bottom: 2px solid %13; }
*[shineKind="tab"]:disabled { color: %11; }
*[shineKind="tab"][shineState="disabled"] { color: %11; }
*[shineKind="tab"]:focus { border: 2px solid %27; padding: 7px 11px; }
*[shineKind="tab"][shineState="focus"] { border: 2px solid %27; padding: 7px 11px; }
*[shineKind="tabindicator"] { background-color: %13; }
*[shineKind="toolbar"] { background-color: %3; border-bottom: 1px solid %6; }
*[shineKind="toolseparator"] { background-color: %6; }
*[shineKind="splitter"]::handle { background-color: %6; }
*[shineKind="splitter"]::handle:hover { background-color: %8; }

/* —— 外壳七区：数值逐条取自 webui shell.css ——
   .topbar h46 glass + line-subtle；.rail w56 bg-surface；.crumbs h34；
   .inspector w280 bg-surface + 左边线；.statusbar h26 f11.5；
   .float-panel r14 + line-subtle + shadow；.float-toolbar r10 —— */
QWidget#topBar {
  background-color: %2; border-bottom: 1px solid %6;
  font-size: 13px;
}
QWidget#breadcrumb {
  background-color: %2; border-bottom: 1px solid %6;
  font-size: 12px; color: %10;
}
QLabel#crumbSep { color: %11; background: transparent; }
QLabel#crumbHere { color: %9; background: transparent; font-weight: 600; }
QWidget#activityRail {
  background-color: %2; border-right: 1px solid %6;
}
QWidget#rightPanel {
  background-color: %2; border-left: 1px solid %6;
}
QWidget#sidePanel {
  background-color: %2; border-right: 1px solid %6;
}
QWidget#bottomDock {
  background-color: %2; border-top: 1px solid %6;
}
QWidget#statusBar {
  background-color: %2; border-top: 1px solid %6;
  font-size: 11px; color: %10;
}
QPushButton#statusItem {
  background: transparent; border: none; border-radius: 4px;
  padding: 1px 8px; min-height: 20px; color: %10; font-size: 11px;
}
QPushButton#statusItem:hover { background-color: %24; color: %9; }
/* 浮动面板（出图 / 出片）：glass 底在 Qt 里退化为 bg-surface + 1px 边 + 14px 圆角 */
QWidget#floatPanel {
  background-color: %3; border: 1px solid %6; border-radius: 14px;
}
QWidget#floatPanel QTabWidget::pane { border: none; background: transparent; }
/* 浮动工具栏：r10 + 细边 + 面板底色，避免与画布同色糊在一起 */
QWidget#floatToolbar {
  background-color: %4; border: 1px solid %6; border-radius: 10px;
}
QWidget#floatSep { background-color: %7; }
/* 浮动面板与工具栏的外缩容器：必须完全透明，否则会在画布上留出一条底色带 */
QWidget#floatHost { background: transparent; border: none; }
/* 节点画布：webui .flow-canvas 的底是 fill-muted（不是 bg.void），配 22px 点阵 */
QGraphicsView { background-color: %26; border: none; }

/* —— Chip：webui views.css .chip h26 / p0 11 / r-pill / line-normal / f12 / w600 ——
   选中态 webui 用 accent-dim 底 + accent-glow 边；Qt 无同源半透明 token，
   用 fill.selected（带强调色倾向的暗底）+ accent.primary 边等效。 */
*[shineKind="chip"] {
  border: 1px solid %7; border-radius: 999px; padding: 0 11px; min-height: 26px;
  background-color: transparent; color: %10; font-size: 12px; font-weight: 600;
}
*[shineKind="chip"]:hover { border-color: %8; color: %9; }
*[shineKind="chip"][shineState="hover"] { border-color: %8; color: %9; }
*[shineKind="chip"]:pressed { background-color: %24; }
*[shineKind="chip"][on="true"] { background-color: %25; border-color: %13; color: %13; }
*[shineKind="chip"]:disabled { color: %11; border-color: %6; }
*[shineKind="chip"][shineState="disabled"] { color: %11; border-color: %6; }
*[shineKind="chip"]:focus { border: 2px solid %27; padding: 0 10px; }
*[shineKind="chip"][shineState="focus"] { border: 2px solid %27; padding: 0 10px; }
/* 连续性 / 校验清单语义色：webui 用 ok / danger 描边加同色文字 */
*[shineKind="chip"][tone="ok"] { color: %18; border-color: %18; }
*[shineKind="chip"][tone="danger"] { color: %20; border-color: %20; }
*[shineKind="chip"][tone="warn"] { color: %19; border-color: %19; }
*[shineKind="chip"][tone="idle"] { color: %11; border-color: %6; }
*[shineKind="chip"][tone="ok"]:hover, *[shineKind="chip"][tone="danger"]:hover,
*[shineKind="chip"][tone="warn"]:hover, *[shineKind="chip"][tone="idle"]:hover {
  border-color: %8; color: %9;
}
/* .chip .cnt：计数小胶囊（f10.5 → 11px） */
*[shineKind="chipcount"] { border-radius: 999px; padding: 0 6px; background-color: %26; color: %11; font-size: 11px; }
*[shineKind="chip"][on="true"] *[shineKind="chipcount"] { background-color: %25; }

/* —— .vsec 详情分区：无卡片框，靠发丝线分隔（p14 2 + border-bottom line-subtle） —— */
*[shineKind="vsec"] { background: transparent; border: none; border-bottom: 1px solid %6; padding: 14px 2px; }
*[shineKind="vsec"][last="true"] { border-bottom: none; }
*[shineKind="vsechead"] { background: transparent; color: %9; font-size: 13px; font-weight: 700; }
*[shineKind="vsecicon"] { background: transparent; border: none; color: %13; font-size: 13px; }

/* —— .chap-summary 章节摘要：fill-muted 底 + 3px accent 左条 + r6 p10 14 f12 —— */
*[shineKind="chapsummary"] {
  background-color: %26; color: %10; border: 1px solid %6; border-left: 3px solid %13;
  border-radius: 6px; padding: 10px 14px; font-size: 12px;
}

/* —— .draft 正文容器：f14；行高 1.9 / 段距 14px / 首行缩进 2em 由 QTextBlockFormat 设 —— */
*[shineKind="draftbody"] { background-color: %2; border: none; padding: 0; font-size: 14px; }

/* —— 故事板 .tl-card：w128 / 圆角 10 / fill-muted 底 + line-normal 边 —— */
*[shineKind="tlcard"] { background-color: %26; border: 1px solid %7; border-radius: 10px; }
*[shineKind="tlcard"][selected="true"] { border-color: %13; }
*[shineKind="tlthumb"] { background: transparent; border: none; border-radius: 0px; color: %11; }
*[shineKind="tlcode"] {
  background: transparent; border: none; color: %13;
  font-family: "Cascadia Mono", "Consolas", monospace; font-size: 11px; font-weight: 700;
}
*[shineKind="tldur"] { background: transparent; border: none; color: %11; font-size: 11px; }

/* —— .derive 派生链节点 / .tl 关联时间线 —— */
*[shineKind="derivenode"] { background-color: %26; border: 1px solid %7; border-radius: 10px; }
*[shineKind="derivenode"][selected="true"] { border-color: %13; }
*[shineKind="derivelink"] { background-color: %7; border: none; }
*[shineKind="derivelink"][fill="true"] { background-color: %13; }
*[shineKind="tlaxis"] { background-color: %7; border: none; border-radius: 2px; }
*[shineKind="tltick"] { background-color: %8; border: none; }
*[shineKind="tlpin"] { border: 2px solid %2; border-radius: 999px; background-color: %11; }
*[shineKind="tlpin"][hot="true"] { background-color: %13; }
*[shineKind="tlcap"] { background: transparent; border: none; color: %10; font-size: 11px; }
*[shineKind="tlcap"][hot="true"] { color: %13; font-weight: 700; }

/* —— 小说 .ntab 模式页签：p8 12 / f13 / w600 / text-muted；选中 accent + 2px 下划线 —— */
*[shineKind="ntabbar"] { background: transparent; border: none; border-bottom: 1px solid %6; }
*[shineKind="ntab"] {
  background: transparent; border: none; border-bottom: 2px solid transparent;
  border-radius: 0px; padding: 8px 12px; min-height: 44px;
  color: %10; font-size: 13px; font-weight: 600;
}
*[shineKind="ntab"]:hover { color: %9; background-color: %26; }
*[shineKind="ntab"][shineState="hover"] { color: %9; background-color: %26; }
*[shineKind="ntab"]:pressed { background-color: %24; }
*[shineKind="ntab"][selected="true"] { color: %13; border-bottom: 2px solid %13; background-color: transparent; }
*[shineKind="ntab"]:disabled { color: %11; }
*[shineKind="ntab"]:focus { border-bottom: 2px solid transparent; }
*[shineKind="ntab"][selected="true"]:focus { border-bottom: 2px solid %13; }
)QSS";

// 全部替换 %28..%1（降序，避免 %1 误伤 %10 之类）
void FillTokens(std::string& out, const std::array<std::uint32_t, kColorTokenCount>& values) {
    for (int n = static_cast<int>(values.size()); n >= 1; --n) {
        const std::string key = "%" + std::to_string(n);
        const std::string val = EmitColor(values[static_cast<std::size_t>(n - 1)]);
        std::size_t pos = 0;
        while ((pos = out.find(key, pos)) != std::string::npos) {
            out.replace(pos, key.size(), val);
            pos += val.size();
        }
    }
}

} // namespace

std::string QssBuilder::Build(const ColorToken& c) {
    const std::array<std::uint32_t, kColorTokenCount> values = TokenValues(c);
    std::string out{kTemplate};
    out += kKitTemplate; // kit/widgets 样式段（P02-S5）
    FillTokens(out, values);
    return out;
}

bool QssBuilder::SelfCheck(const ColorToken& c, std::string* detail) {
    const std::string qss = Build(c);
    const std::array<std::uint32_t, kColorTokenCount> values = TokenValues(c);

    // 所有字面颜色必须是某个 Token 的 EmitColor（禁止散落色值）
    static const std::regex kColorRe{R"((#[0-9A-Fa-f]{3,8}|rgba?\([^)]*\)))"};
    std::vector<std::string> allowed;
    allowed.reserve(values.size());
    for (std::uint32_t v : values) {
        allowed.push_back(EmitColor(v));
    }

    bool ok = true;
    int literalCount = 0;
    for (std::sregex_iterator it(qss.begin(), qss.end(), kColorRe), end; it != end; ++it) {
        ++literalCount;
        std::string token = it->str();
        for (char& ch : token) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        bool found = false;
        for (const std::string& a : allowed) {
            std::string al = a;
            for (char& ch : al) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            if (al == token) {
                found = true;
                break;
            }
        }
        if (!found) {
            ok = false;
            if (detail != nullptr) {
                *detail += "不可回溯的颜色字面: " + it->str() + "\n";
            }
        }
    }

    // 模板占位符必须全部被替换（无残留 %N）；扫描整个 1..N 区间而不是只看 %1/%2
    std::string leftover;
    for (int n = 1; n <= static_cast<int>(values.size()); ++n) {
        if (qss.find("%" + std::to_string(n)) != std::string::npos) {
            leftover += "%" + std::to_string(n) + " ";
        }
    }

    // 覆盖率自检：每个 token 都必须在模板里被真正消费（方案 01 判据 5：不允许占位符空占）
    std::string unused;
    {
        const std::string all{kTemplate};
        const std::string kit{kKitTemplate};
        for (int n = 1; n <= static_cast<int>(values.size()); ++n) {
            const std::string key = "%" + std::to_string(n);
            if (all.find(key) == std::string::npos && kit.find(key) == std::string::npos) {
                if (!unused.empty()) {
                    unused += " ";
                }
                unused += kColorTokenNames[static_cast<std::size_t>(n - 1)];
            }
        }
    }
    ok = ok && unused.empty();

    // 覆盖度自检：控件清单里的每一类 QStyle 基类都要有样式规则
    const std::array<const char*, 21> kRequiredClasses = {
        "QWidget",    "QPushButton", "QToolButton",  "QLineEdit",   "QTextEdit",
        "QPlainTextEdit", "QSpinBox", "QComboBox",   "QCheckBox",   "QRadioButton",
        "QSlider",    "QProgressBar", "QGroupBox",   "QTabWidget",  "QTabBar",
        "QListView",  "QTreeView",   "QTableView",   "QHeaderView", "QScrollBar",
        "QMenu",
    };
    std::string missing;
    for (const char* cls : kRequiredClasses) {
        if (qss.find(cls) == std::string::npos) {
            missing += std::string{cls} + " ";
        }
    }

    if (detail != nullptr) {
        *detail += "字面颜色数: " + std::to_string(literalCount) +
                   "（应=Token 数 " + std::to_string(values.size()) + " 的倍数级出现，全部可回溯）\n";
        *detail += "占位符残留: " + (leftover.empty() ? std::string{"none"} : leftover) + "\n";
        *detail += "未被消费的 token: " + (unused.empty() ? std::string{"none"} : unused) + "\n";
        *detail += missing.empty() ? "missing selectors: none\n"
                                   : ("missing selectors: " + missing + "\n");
        const std::size_t n = qss.find("/* —— Button");
        *detail += "kit 段: " + std::string{n != std::string::npos ? "present" : "MISSING"} + "\n";
    }
    return ok && leftover.empty() && missing.empty();
}

bool QssBuilder::DumpToFile(const ColorToken& c, const std::filesystem::path& path) {
    const std::string qss = Build(c);
    return shine::util::WriteFileBytes(path, qss);
}

} // namespace shine::theme
