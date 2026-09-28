#pragma once
// shine::images —— 联系表（P02-S7，UI.md §2.3）：SheetGrid + PNG 导出。
#include <QImage>
#include <QSize>
#include <QString>

#include <filesystem>
#include <vector>

namespace shine::images {

// SheetGrid —— 多格联系表（缩略图 + 图注），Compose() 出整版 QImage，ExportPng 存盘
class SheetGrid {
  public:
    explicit SheetGrid(int columns = 4, QSize cell = {200, 160});

    void Add(const QImage& img, const QString& caption);
    void Clear();
    [[nodiscard]] int Count() const { return static_cast<int>(items_.size()); }

    [[nodiscard]] QImage Compose() const; // 整版渲染
    bool ExportPng(const std::filesystem::path& path) const; // PNG 导出

  private:
    struct Item {
        QImage img;
        QString caption;
    };

    int columns_;
    QSize cell_;
    std::vector<Item> items_;
};

} // namespace shine::images
