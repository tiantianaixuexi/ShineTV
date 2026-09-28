#pragma once
// WidgetGalleryView —— 控件画廊（P02-S5）：UI.md §2.1 全部控件逐个演示；
// 交互控件带五态并排行（normal / hover / pressed / disabled / focus），
// 容器带三态行（有数据 / 空 / 错误）。SHINE_GALLERY_SHOTS=<目录> 自动逐页截图。
#include <QWidget>

#include <string>

class QStackedWidget;
class QListWidget;

namespace shine::gallery {

class WidgetGalleryView : public QWidget {
  public:
    WidgetGalleryView();

    void GotoPage(int index);
    [[nodiscard]] int PageCount() const;
    // 逐页截图存 outDirUtf8（<控件名>.png），返回张数；<0 失败
    int GrabAllPages(const std::string& outDirUtf8);

  private:
    QListWidget* nav_ = nullptr;
    QStackedWidget* pages_ = nullptr;
};

// P02-S9 多模态评审截图包（UI.md §4）：一键产出 build/_shots/P02/ 全部 10 张
void SaveP02Review(const std::string& dirUtf8);

} // namespace shine::gallery
