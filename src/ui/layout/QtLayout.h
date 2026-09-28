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
inline void ClearLayout(QLayout* lay) {
    if (lay == nullptr) {
        return;
    }
    while (QLayoutItem* it = lay->takeAt(0)) {
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

} // namespace shine::util
