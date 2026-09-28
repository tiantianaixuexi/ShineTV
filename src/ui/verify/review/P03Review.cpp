#include "ui/verify/review/P03Review.h"

#include "ui/pages/project/ProjectHubView.h"
#include "ui/pages/project/ProjectWizardDialog.h"
#include "ui/pages/settings/FirstRunWizard.h"
#include "ui/pages/shell/CommandPalette.h"
#include "ui/pages/shell/MainWindow.h"
#include "ui/kit/controls/Surfaces.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/ThemeService.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QPixmap>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <filesystem>
#include <string>
#include <vector>

namespace shine::app {

namespace {

struct ReviewState {
    MainWindow* window = nullptr;
    std::filesystem::path dir;
    std::vector<std::string> manifest;
};

// 抓一张：grab 前强制重绘（P02-S7 教训：grab 前不 repaint 会抓到空帧）
void Grab(QWidget* w, const std::filesystem::path& file, ReviewState* st) {
    w->repaint();
    const QPixmap pm = w->grab();
    const QString qpath = QString::fromStdWString(file.wstring());
    const bool ok = pm.save(qpath);
    st->manifest.push_back(shine::util::FileNameToUtf8(file) + " " +
                           std::to_string(shine::util::ReadFileBytes(file).value_or("").size()) +
                           (ok ? " saved" : " FAILED"));
}

void Shot(ReviewState* st, QWidget* w, const char* name) {
    Grab(w, st->dir / (std::string{name} + ".png"), st);
}

void Finish(ReviewState* st) {
    std::string report = "P03-S11 shots\n";
    for (const std::string& line : st->manifest) {
        report += line + "\n";
    }
    (void)shine::util::WriteFileBytes(st->dir / "shots-manifest.txt", report);
    std::printf("[p03-review]\n%s", report.c_str());
    std::fflush(nullptr);
    std::_Exit(0);
}

} // namespace

void SaveP03Review(const std::string& dirUtf8) {
    const std::filesystem::path dir = shine::util::PathFromUtf8(dirUtf8);
    std::error_code ec;
    // 沙盒必须每次全新：上一轮的 _appdata（索引）/ _proj（项目）残留会让
    // 「hub-empty=空态」失真、演示项目创建撞「目录已存在」（实测踩过）。
    std::filesystem::remove_all(dir / "_appdata", ec);
    std::filesystem::remove_all(dir / "_proj", ec);
shine::util::EnsureDir(dir);

    auto* st = new ReviewState{nullptr, dir, {}};
    auto* window = new MainWindow();
    st->window = window;
    window->resize(1280, 800);
    window->show();

    // t=500：空态首屏（沙盒索引为空 → 空态主行动）
    QTimer::singleShot(500, window, [st] { Shot(st, st->window->Hub(), "hub-empty"); });

    // t=900：造两个中文项目 → 卡片网格
    QTimer::singleShot(900, window, [st] {
        const std::filesystem::path root = st->dir / "_proj";
        std::error_code ec2;
shine::util::EnsureDir(root);
        struct Demo {
            const char* name;
            const char* tpl;
        };
        for (const Demo& d : {Demo{"灯语回声", "film"}, Demo{"雾都纪事", "novel"}}) {
            project::ProjectSpec spec;
            spec.name = d.name;
            spec.rootDir = root / shine::util::PathFromUtf8(d.name);
            spec.templateId = d.tpl;
            spec.premise = "演示项目";
            (void)st->window->Service().Create(spec);
        }
        st->window->Hub()->Refresh();
        Shot(st, st->window->Hub(), "hub-normal");
    });

    // t=1400..2400：向导四步（UI.md §4：step1-模板 / step2-命名与位置（含 error 态）/ step3-一句话创意 / step4-确认）
    QTimer::singleShot(1400, window, [st] {
        auto* wizard = new ProjectWizardDialog(&st->window->Service(), st->window);
        wizard->setAttribute(Qt::WA_DeleteOnClose);
        wizard->setWindowFlag(Qt::Window, true);
        wizard->resize(720, 520);
        wizard->GoToStep(0);
        wizard->show();
        st->window->setProperty("p03wizard", QVariant::fromValue<QWidget*>(wizard));
    });
    QTimer::singleShot(1650, window, [st] {
        auto* wizard = qobject_cast<ProjectWizardDialog*>(
            st->window->property("p03wizard").value<QWidget*>());
        if (wizard != nullptr) {
            wizard->GoToStep(0);
            Shot(st, wizard, "wizard-step1-模板");
        }
    });
    QTimer::singleShot(1900, window, [st] {
        auto* wizard = qobject_cast<ProjectWizardDialog*>(
            st->window->property("p03wizard").value<QWidget*>());
        if (wizard != nullptr) {
            project::ProjectSpec bad;
            bad.name.clear(); // 触发名称校验 error 态（UI.md §4：step2 含 error 态）
            bad.rootDir = st->dir / "_proj";
            bad.templateId = "film";
            wizard->SetSpec(bad);
            wizard->GoToStep(1);
            Shot(st, wizard, "wizard-step2-命名与位置");
        }
    });
    QTimer::singleShot(2150, window, [st] {
        auto* wizard = qobject_cast<ProjectWizardDialog*>(
            st->window->property("p03wizard").value<QWidget*>());
        if (wizard != nullptr) {
            project::ProjectSpec spec;
            spec.name = QStringLiteral("灯语回声·副本").toStdString();
            spec.rootDir = st->dir / "_proj"; // 位置基目录；表单合成 位置/项目名称
            spec.templateId = "film";
            spec.premise = "一盏灯把迷路的人带回家";
            wizard->SetSpec(spec);
            wizard->GoToStep(2);
            Shot(st, wizard, "wizard-step3-一句话创意");
        }
    });
    QTimer::singleShot(2400, window, [st] {
        auto* wizard = qobject_cast<ProjectWizardDialog*>(
            st->window->property("p03wizard").value<QWidget*>());
        if (wizard != nullptr) {
            wizard->GoToStep(3);
            Shot(st, wizard, "wizard-step4-确认");
            wizard->close(); // 程序化 close 不弹确认（向导刻意如此，自动化不断链）
        }
    });

    // t=2900：进工坊（空工作区 + 状态栏）
    QTimer::singleShot(2900, window, [st] {
        const std::filesystem::path root = st->dir / "_proj" / "灯语回声";
        if (!st->window->OpenProjectPath(root)) {
            st->manifest.push_back("open-project FAILED");
        }
        Shot(st, st->window, "workshop-empty");
    });

    // t=3300 / 3700：两种分辨率不破版
    QTimer::singleShot(3300, window, [st] {
        st->window->resize(1280, 720);
        Shot(st, st->window, "workshop-1280x720");
    });
    QTimer::singleShot(3700, window, [st] {
        st->window->resize(2560, 1440);
        Shot(st, st->window, "workshop-2560x1440");
    });

    // t=4100：命令面板（三段结果）+ 量化「Ctrl+K 到结果」时延（判据 <200ms）
    QTimer::singleShot(4100, window, [st] {
        auto* elapsed = new QElapsedTimer();
        st->window->Palette()->OpenPalette();
        elapsed->start();
        auto* poll = new QTimer(st->window);
        poll->setInterval(1);
        QObject::connect(poll, &QTimer::timeout, st->window, [st, elapsed, poll] {
            if (st->window->Palette()->QueryCount(QString{}) > 0 || elapsed->elapsed() > 2000) {
                st->manifest.push_back("palette-results-ms=" + std::to_string(elapsed->elapsed()) +
                                       (elapsed->elapsed() < 200 ? " PASS(<200ms)" : " FAIL(>=200ms)"));
                poll->stop();
                poll->deleteLater();
                delete elapsed;
                // 富文本行渲染断言（取证教训：视觉通道不可靠，用尺寸断言留痕）——
                // 同一 HTML 串 RichText 渲染 ≈ 4 个汉字宽（<150px）；标签被当纯文本则 >300px。
                {
                    QLabel probeLabel(QStringLiteral("<div style=\"font-size:14px;\">新建项目</div>"));
                    probeLabel.setTextFormat(Qt::RichText);
                    const int w = probeLabel.sizeHint().width();
                    st->manifest.push_back("palette-richtext-width=" + std::to_string(w) +
                                           (w < 150 ? " PASS(富文本渲染)" : " FAIL(标签当纯文本)"));
                }
                Shot(st, st->window->Palette(), "palette-open");
                st->window->Palette()->ClosePalette();
            }
        });
        poll->start();
    });

    // t=4500 / 4800：工坊 × 深空 / 纸墨
    QTimer::singleShot(4500, window, [st] {
        theme::ThemeService::Switch(theme::ThemeId::DeepSpace, false);
        Shot(st, st->window, "theme-dark");
    });
    QTimer::singleShot(4800, window, [st] {
        theme::ThemeService::Switch(theme::ThemeId::PaperInk, false);
        Shot(st, st->window, "theme-light");
    });

    // t=5150：附加证据 —— 状态栏点开详情抽屉（S8「可点开详情」）。
    // Drawer 是独立顶层：抓 window 抓不到（实测与主题图逐字节同哈希），必须抓抽屉本体。
    QTimer::singleShot(5100, window, [st] {
        if (auto* drawer = st->window->ShowStatusDetail(StatusItem::Llm); drawer != nullptr) {
            QTimer::singleShot(400, drawer, [st, drawer] {
                Shot(st, drawer, "bonus-status-detail");
            });
        } else {
            st->manifest.push_back("bonus-status-detail FAILED (no drawer)");
        }
    });

    // t=5700：首启引导。切深空再取证——向导第③步是 Select（摘要按钮 / Field 标签 /
    // placeholder 都在这一页），「深色主题黑字」这类回归只有在深色下才暴露；
    // 前面 t=4800 停在纸墨（浅色），深色取证必须显式切回来。
    QTimer::singleShot(5700, window, [st] {
        theme::ThemeService::Switch(theme::ThemeId::DeepSpace, false);
        auto* fw = new FirstRunWizard(nullptr, false);
        fw->setAttribute(Qt::WA_DeleteOnClose);
        fw->setWindowFlag(Qt::Window, true);
        fw->resize(560, 380);
        fw->show();
        QTimer::singleShot(300, fw, [st, fw] {
            Shot(st, fw, "firstrun");
            fw->GoToStep(2); // ③ LLM 供应商与 Key
            QTimer::singleShot(200, fw, [st, fw] {
                Shot(st, fw, "firstrun-step3-llm");
                fw->close();
                QTimer::singleShot(200, st->window, [st] { Finish(st); });
            });
        });
    });
}

} // namespace shine::app
