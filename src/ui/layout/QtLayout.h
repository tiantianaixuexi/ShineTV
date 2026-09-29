#pragma once
// shine::util — Qt 布局小工具（可被 widget/pages 共用）。
// 仅依赖 Qt Widgets，不依赖业务类型；颜色请走 theme token，勿在此写死色值。

#include <QFont>
#include <QLabel>
#include <QLayout>
#include <QString>
#include <QWidget>

namespace shine::util {

// 清空布局项并 deleteLater 子控件，保留布局本身。
//
// keep_tail：末尾保留 N 个布局项不动（默认 0 = 全清）。
// 列表布局常在末尾常驻一个 addStretch(1) 把内容顶到上面，重建列表时不能连它一起清，
// 那种地方以前各自手写 `while (lay->count() > 1) { takeAt(0) … }`（6 个页面一模一样），
// 现在统一走这里。
//
// ⚠️ 不要拿它覆盖别的意图：保留**首**项的（FilmStrip）、取出并归还控件的
// （SidePanel::TakeCurrentNav）、拆壳但保留 body 的（RightPanel::ClearSections）、
// 保留某个特定控件的（Breadcrumb）都不适用。
inline void ClearLayout(QLayout* lay, int keep_tail = 0) {
    if (lay == nullptr || keep_tail < 0) {
        return;
    }
    while (lay->count() > keep_tail) {
        QLayoutItem* it = lay->takeAt(0);
        if (it == nullptr) {
            break;
        }
        if (QWidget* w = it->widget(); w != nullptr) {
            w->deleteLater();
        }
        delete it;
    }
}

// 分区标题：Semibold 小标签。
[[nodiscard]] inline QLabel* SectionLabel(QWidget* host, const QString& text) {
    auto* lb = new QLabel(text, host);
    QFont f = lb->font();
    f.setWeight(QFont::DemiBold);
    lb->setFont(f);
    return lb;
}

// 中文安全截断（超长加省略号）。
[[nodiscard]] inline QString ElideText(const QString& s, int maxChars) {
    return s.size() <= maxChars ? s : s.left(maxChars) + QStringLiteral("…");
}

// ── 页面级留白与分区节奏 ──────────────────────────────────────────
// 与 webui src/styles/views.css 的 .vw 逐值对齐：
//   padding: 20px 24px 26px;  gap: 16px;
// 各 Workspace 的最外层布局统一调 PageMargins() + PageSpacing()，
// 避免每个页面各写一套 kSteps 下标（8/4 与设计稿的 20/24/26 差了一个量级，
// 这正是上一轮「结构对了但不像」的直接原因）。
namespace page {
inline constexpr int kPadTop = 20;
inline constexpr int kPadX = 24;
inline constexpr int kPadBottom = 26;
inline constexpr int kGap = 16;
} // namespace page

inline void PageMargins(QLayout* lay) {
    if (lay != nullptr) {
        lay->setContentsMargins(page::kPadX, page::kPadTop, page::kPadX, page::kPadBottom);
    }
}

inline void PageSpacing(QLayout* lay) {
    if (lay != nullptr) {
        lay->setSpacing(page::kGap);
    }
}

} // namespace shine::util
