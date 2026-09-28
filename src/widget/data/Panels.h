#pragma once
// shine::data —— 信息面板（P02-S6，UI.md §2.2）：StatTile / KeyValue / DiffView / JsonTree。
#include "widget/controls/WidgetCommon.h"

#include <vector>

#include <QFrame>
#include <QLabel>
#include <QString>
#include <QTreeWidget>
#include <QWidget>

struct yyjson_val;

namespace shine::data {

// StatTile —— 大数字 + 标签 + 趋势箭头
class StatTile : public QFrame {
  public:
    explicit StatTile(QWidget* parent = nullptr);

    void SetValue(const QString& value);
    void SetLabel(const QString& label);
    void SetTrend(int percent); // >0 ↑ ok / <0 ↓ danger / =0 平
};

// KeyValue —— 两列；值可复制（双击复制到剪贴板）；长值折叠（点开展开）
class KeyValue : public QFrame {
  public:
    explicit KeyValue(QWidget* parent = nullptr);

    void SetPairs(const std::vector<std::pair<QString, QString>>& pairs);
};

// DiffView —— 左右 / 行内；增删改三色（status.*）
class DiffView : public QFrame {
  public:
    enum class Kind { Same, Add, Del, Change };

    struct Row {
        Kind kind = Kind::Same;
        QString left;
        QString right;
    };

    explicit DiffView(QWidget* parent = nullptr);

    void SetRows(std::vector<Row> rows);
    void SetInline(bool on); // true = 行内模式；false = 左右并排
};

// JsonTree —— 可折叠；类型着色；右键复制路径
class JsonTree : public QTreeWidget {
  public:
    explicit JsonTree(QWidget* parent = nullptr);

    void SetJson(const QString& jsonText); // yyjson 解析建树
    [[nodiscard]] QString PathOf(QTreeWidgetItem* item) const; // a.b[2].c 形式

  private:
    void BuildFrom(const yyjson_val* val, QTreeWidgetItem* parent, const QString& key);
};

} // namespace shine::data
