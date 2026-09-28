#include "ui/kit/images/Sheet.h"

#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "util/Encoding.h"

#include <QPainter>

#include <cmath>

namespace shine::images {

SheetGrid::SheetGrid(int columns, QSize cell) : columns_(std::max(columns, 1)), cell_(cell) {}

void SheetGrid::Add(const QImage& img, const QString& caption) {
    items_.push_back({img, caption});
}

void SheetGrid::Clear() { items_.clear(); }

QImage SheetGrid::Compose() const {
    const theme::ColorToken& t = theme::Current();
    const int margin = 16;
    const int capH = 22;
    const int rows = static_cast<int>(std::ceil(items_.size() / static_cast<double>(columns_)));
    const int w = margin * 2 + columns_ * (cell_.width() + margin);
    const int h = margin * 2 + std::max(rows, 1) * (cell_.height() + capH + margin);

    QImage sheet(w, h, QImage::Format_RGB32);
    sheet.fill(widgets::TokenQColor(t.bgSurface).rgb());
    QPainter p(&sheet);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setPen(widgets::TokenQColor(t.textSecondary));

    for (std::size_t i = 0; i < items_.size(); ++i) {
        const int col = static_cast<int>(i) % columns_;
        const int row = static_cast<int>(i) / columns_;
        const int x = margin + col * (cell_.width() + margin);
        const int y = margin + row * (cell_.height() + capH + margin);
        const QRect tile(x, y, cell_.width(), cell_.height());
        p.setPen(widgets::TokenQColor(t.lineSubtle));
        p.drawRect(tile);
        if (!items_[i].img.isNull()) {
            p.drawImage(tile.adjusted(2, 2, -2, -2), items_[i].img);
        }
        p.setPen(widgets::TokenQColor(t.textSecondary));
        p.drawText(QRect(x, tile.bottom() + 2, cell_.width(), capH), Qt::AlignLeft | Qt::AlignVCenter,
                   items_[i].caption);
    }
    return sheet;
}

bool SheetGrid::ExportPng(const std::filesystem::path& path) const {
    const QImage sheet = Compose();
    const QString qpath = QString::fromStdString(shine::util::PathToUtf8(path));
    return sheet.save(qpath, "PNG");
}

} // namespace shine::images
