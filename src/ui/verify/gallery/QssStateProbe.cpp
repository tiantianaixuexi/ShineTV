#include "ui/verify/gallery/QssStateProbe.h"

#include "ui/kit/data/Table.h"
#include "ui/kit/theme/QssBuilder.h"
#include "ui/kit/theme/Theme.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QHBoxLayout>
#include <QImage>
#include <QPushButton>
#include <QStyle>
#include <QWidget>

#include <array>
#include <cstdio>
#include <random>
#include <string>

namespace shine::gallery {
namespace {

struct Specimen {
    QPushButton* btn = nullptr;
    QWidget* cell = nullptr;
};

std::string FaceColor(QWidget* w, const std::string& tag) {
    QImage img = w->grab().toImage();
    const QRgb px = img.pixel(img.width() / 2, img.height() / 2);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s face #%02X%02X%02X", tag.c_str(), qRed(px), qGreen(px), qBlue(px));
    return buf;
}

} // namespace

std::string QssStateProbe() {
    const std::string qss = shine::theme::QssBuilder::Build(shine::theme::Current());
    std::string out;

    // 每组 3 个：normal / hover / pressed —— 同 kind+variant，只有 shineState 不同
    const std::array<const char*, 3> states{"", "hover", "pressed"};

    // A) 构造即设属性 + unpolish/polish（= 当前 StateRow 的做法）
    {
        QWidget host;
        host.setStyleSheet(QString::fromStdString(qss));
        auto* row = new QHBoxLayout(&host);
        for (const char* s : states) {
            auto* b = new QPushButton(QStringLiteral("样"), &host);
            b->setProperty("shineKind", QStringLiteral("button"));
            b->setProperty("shineVariant", QStringLiteral("primary"));
            b->setProperty("shineState", QString::fromLatin1(s));
            b->style()->unpolish(b);
            b->style()->polish(b);
            row->addWidget(b);
        }
        host.show();
        QApplication::processEvents();
        const auto& kids = row->parentWidget()->findChildren<QPushButton*>();
        for (int i = 0; i < 3 && i < static_cast<int>(kids.size()); ++i) {
            out += "A(pre-polish) " + FaceColor(kids[static_cast<std::size_t>(i)], states[i]) + "\n";
        }
    }

    // B) 先 show 再设属性 + unpolish/polish
    {
        QWidget host;
        host.setStyleSheet(QString::fromStdString(qss));
        auto* row = new QHBoxLayout(&host);
        std::array<QPushButton*, 3> bs{};
        for (auto& b : bs) {
            b = new QPushButton(QStringLiteral("样"), &host);
            b->setProperty("shineKind", QStringLiteral("button"));
            b->setProperty("shineVariant", QStringLiteral("primary"));
            row->addWidget(b);
        }
        host.show();
        QApplication::processEvents();
        for (int i = 0; i < 3; ++i) {
            bs[i]->setProperty("shineState", QString::fromLatin1(states[i]));
            bs[i]->style()->unpolish(bs[i]);
            bs[i]->style()->polish(bs[i]);
            bs[i]->update();
            QApplication::processEvents();
            out += "B(post-show) " + FaceColor(bs[i], states[i]) + "\n";
        }
    }

    // C) 先 show 再设属性 + 发 StyleChange 事件
    {
        QWidget host;
        host.setStyleSheet(QString::fromStdString(qss));
        auto* row = new QHBoxLayout(&host);
        std::array<QPushButton*, 3> bs{};
        for (auto& b : bs) {
            b = new QPushButton(QStringLiteral("样"), &host);
            b->setProperty("shineKind", QStringLiteral("button"));
            b->setProperty("shineVariant", QStringLiteral("primary"));
            row->addWidget(b);
        }
        host.show();
        QApplication::processEvents();
        for (int i = 0; i < 3; ++i) {
            bs[i]->setProperty("shineState", QString::fromLatin1(states[i]));
            QEvent ev{QEvent::StyleChange};
            QApplication::sendEvent(bs[i], &ev);
            bs[i]->update();
            QApplication::processEvents();
            out += "C(StyleChange) " + FaceColor(bs[i], states[i]) + "\n";
        }
    }

    // D) 对照实验：内联新属性名 zzzState 红色规则 + 属性回读
    {
        QWidget host;
        host.setStyleSheet(QString::fromStdString(qss) +
                           QStringLiteral("\n*[zzzState=\"hover\"] { background-color: #FF0000; }\n"
                                          "*[shineKind=\"button\"][shineState=\"hover\"] { background-color: #FF00FF; }\n"
                                          "*[zzzState=\"pressed\"], QPushButton:hover { background-color: #00FF00; }\n"
                                          "QPushButton[zzz2=\"pressed\"] { background-color: #0000FF; }\n"));
        auto* row = new QHBoxLayout(&host);
        std::array<QPushButton*, 3> bs{};
        for (auto& b : bs) {
            b = new QPushButton(QStringLiteral("样"), &host);
            b->setProperty("shineKind", QStringLiteral("button"));
            b->setProperty("shineVariant", QStringLiteral("primary"));
            row->addWidget(b);
        }
        host.show();
        QApplication::processEvents();
        for (int i = 0; i < 3; ++i) {
            bs[i]->setProperty("zzzState", QString::fromLatin1(states[i]));
            bs[i]->setProperty("shineState", QString::fromLatin1(states[i]));
            bs[i]->setProperty("zzz2", QStringLiteral("pressed"));
            bs[i]->style()->unpolish(bs[i]);
            bs[i]->style()->polish(bs[i]);
            bs[i]->update();
            QApplication::processEvents();
            const QString readback = bs[i]->property("shineState").toString();
            out += "D(inline) " + FaceColor(bs[i], states[i]) +
                   "  readback=" + readback.toStdString() + "\n";
        }
    }

    return out;
}

std::string TableBench() {
    QWidget host;
    host.resize(900, 560);
    auto* lay = new QHBoxLayout(&host);
    auto* table = new shine::data::DataTable(QStringLiteral("bench"), &host);
    table->SetColumns({{"name", QStringLiteral("章"), 180},
                       {"stage", QStringLiteral("阶段"), 120},
                       {"words", QStringLiteral("字数"), 90},
                       {"updated", QStringLiteral("更新"), 120}});
    std::vector<std::vector<QString>> rows;
    rows.reserve(10000);
    for (int i = 0; i < 10000; ++i) {
        rows.push_back({QStringLiteral("第 %1 章").arg(i + 1), QStringLiteral("生成中"),
                        QString::number(800 + (i * 37) % 4200), QStringLiteral("09:41")});
    }
    table->SetRows(rows);
    table->SetFixedColumns(1);
    table->SetSelectable(shine::data::DataTable::Select::Rubber);
    lay->addWidget(table);
    host.show();
    QApplication::processEvents();

    QElapsedTimer clock;
    std::mt19937 rng{42};
    clock.start();
    for (int i = 0; i < 200; ++i) {
        table->scrollTo(table->model()->index(static_cast<int>(rng() % 10000), 0));
        QApplication::processEvents();
    }
    const qint64 ms = clock.elapsed();

    char buf[128];
    std::snprintf(buf, sizeof(buf), "rows=10000 scroll-200-frames=%lldms %s",
                  static_cast<long long>(ms), ms < 2000 ? "PASS" : "FAIL");
    return buf;
}

} // namespace shine::gallery
