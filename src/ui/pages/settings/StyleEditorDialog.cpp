#include "ui/pages/settings/StyleEditorDialog.h"

#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/ThemeService.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Inputs.h"

#include <QColorDialog>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <cstdio>

namespace shine::app {
namespace {

QString HexOf(std::uint32_t rgba) {
    char buf[16];
    if ((rgba & 0xFF) == 0xFF) {
        std::snprintf(buf, sizeof(buf), "#%06X", (rgba >> 8) & 0xFFFFFF);
    } else {
        std::snprintf(buf, sizeof(buf), "#%08X", rgba);
    }
    return QString::fromLatin1(buf);
}

bool ParseHex(const QString& text, std::uint32_t& out) {
    std::string s = text.trimmed().toStdString();
    if (!s.empty() && s.front() == '#') {
        s.erase(s.begin());
    }
    if (s.size() != 6 && s.size() != 8) {
        return false;
    }
    std::uint32_t v = 0;
    for (const char ch : s) {
        v <<= 4;
        if (ch >= '0' && ch <= '9') {
            v |= static_cast<std::uint32_t>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            v |= static_cast<std::uint32_t>(ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            v |= static_cast<std::uint32_t>(ch - 'A' + 10);
        } else {
            return false;
        }
    }
    out = s.size() == 6 ? (v << 8) | 0xFFu : v;
    return true;
}

} // namespace

StyleEditorDialog::StyleEditorDialog(QWidget* parent)
    : widgets::Dialog(QStringLiteral("样式编辑器 · 改 Token → 实时预览 → 另存为主题"),
                      widgets::Dialog::Size::Lg, parent) {
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);

    const std::array<std::uint32_t, theme::kColorTokenCount> start = theme::TokenValues(theme::Current());
    values_ = start;

    // Token 行（行数 = kColorTokenCount；改 token 数这里自动跟着走）
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* host = new QWidget();
    auto* grid = new QGridLayout(host);
    grid->setColumnStretch(2, 1);
    scroll->setWidget(host);
    BodyLayout()->addWidget(scroll);

    for (int i = 0; i < static_cast<int>(theme::kColorTokenCount); ++i) {
        auto* name = new QLabel(QString::fromLatin1(theme::kColorTokenNames[static_cast<std::size_t>(i)]),
                                host);
        widgets::SetKind(name, "fieldlabel");
        grid->addWidget(name, i, 0);

        auto* sw = new QPushButton(host);
        sw->setFixedSize(40, 20);
        swatches_.push_back(sw);
        grid->addWidget(sw, i, 1);

        auto* hex = new QLineEdit(HexOf(values_[static_cast<std::size_t>(i)]), host);
        hex->setFixedWidth(110);
        hexes_.push_back(hex);
        grid->addWidget(hex, i, 2);

        // 色板点击 → 取色器 → 改值 + 实时预览
        connect(sw, &QPushButton::clicked, this, [this, i] {
            const std::uint32_t cur = values_[static_cast<std::size_t>(i)];
            const QColor init((cur >> 24) & 0xFF, (cur >> 16) & 0xFF, (cur >> 8) & 0xFF, cur & 0xFF);
            const QColor got = QColorDialog::getColor(init, this, QStringLiteral("选择颜色"));
            if (got.isValid()) {
                SetTokenValue(i, (static_cast<std::uint32_t>(got.red()) << 24) |
                                     (static_cast<std::uint32_t>(got.green()) << 16) |
                                     (static_cast<std::uint32_t>(got.blue()) << 8) |
                                     static_cast<std::uint32_t>(got.alpha()));
                Preview();
            }
        });
        // hex 回车 → 改值 + 实时预览
        connect(hex, &QLineEdit::editingFinished, this, [this, i, hex] {
            std::uint32_t v = 0;
            if (ParseHex(hex->text(), v)) {
                values_[static_cast<std::size_t>(i)] = v;
                hexes_[static_cast<std::size_t>(i)]->setText(HexOf(v));
                swatches_[static_cast<std::size_t>(i)]->setStyleSheet(
                    QStringLiteral("background-color: %1; border: 1px solid #808080;")
                        .arg(HexOf(v)));
                Preview();
            } else {
                hex->setText(HexOf(values_[static_cast<std::size_t>(i)])); // 非法输入回滚
            }
        });
    }

    // 名字输入 + 动作按钮
    auto* nameRow = new QHBoxLayout();
    auto* nameLabel = new QLabel(QStringLiteral("另存为主题名："), this);
    widgets::SetKind(nameLabel, "fieldlabel");
    name_ = new QLineEdit(this);
    name_->setPlaceholderText(QStringLiteral("例如：我的暖色"));
    nameRow->addWidget(nameLabel);
    nameRow->addWidget(name_, 1);
    BodyLayout()->addLayout(nameRow);

    SetActions(QStringLiteral("另存为自定义主题"), QStringLiteral("取消"),
               [this] {
                   const std::string n = name_->text().trimmed().toStdString();
                   if (!n.empty()) {
                       (void)SaveAs(n);
                       hide();
                   }
               },
               [this] {
                   theme::ThemeService::RevertPreview(); // 取消 = 撤销实时预览
                   hide();
               });

    // 初始色板着色
    for (int i = 0; i < static_cast<int>(theme::kColorTokenCount); ++i) {
        swatches_[static_cast<std::size_t>(i)]->setStyleSheet(
            QStringLiteral("background-color: %1; border: 1px solid #808080;")
                .arg(HexOf(values_[static_cast<std::size_t>(i)])));
    }
}

void StyleEditorDialog::SetTokenValue(int index, std::uint32_t rgba) {
    values_[static_cast<std::size_t>(index)] = rgba;
    hexes_[static_cast<std::size_t>(index)]->setText(HexOf(rgba));
    swatches_[static_cast<std::size_t>(index)]->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid #808080;").arg(HexOf(rgba)));
}

std::uint32_t StyleEditorDialog::TokenValue(int index) const {
    return values_[static_cast<std::size_t>(index)];
}

theme::ColorToken StyleEditorDialog::Compose() const {
    // 值数组 → JSON → ColorToken（复用反射解析，不在应用层重反射）
    std::string json = "{";
    for (std::size_t i = 0; i < values_.size(); ++i) {
        if (i > 0) {
            json += ", ";
        }
        json += std::string{"\""} + std::string{theme::kColorTokenNames[i]} + "\": \"" +
                HexOf(values_[i]).toStdString() + "\"";
    }
    json += "}";
    theme::ColorToken c{};
    (void)theme::ColorTokenFromJson(json, c);
    return c;
}

void StyleEditorDialog::Preview() {
    theme::ThemeService::Preview(Compose()); // 改 Token → 实时预览
}

bool StyleEditorDialog::SaveAs(const std::string& name) {
    const theme::ColorToken c = Compose();
    if (!theme::ThemeService::SaveCustom(name, c)) {
        return false;
    }
    theme::ThemeService::SwitchCustom(name); // 另存即激活 + 持久化
    return true;
}

} // namespace shine::app
