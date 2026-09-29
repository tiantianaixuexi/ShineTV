#include "ui/verify/review/P03Review.h"

#include "ui/pages/project/ProjectHubView.h"
#include "ui/pages/project/ProjectWizardDialog.h"
#include "ui/pages/settings/FirstRunWizard.h"
#include "ui/pages/shell/CommandPalette.h"
#include "ui/pages/shell/MainWindow.h"
#include "ui/verify/review/ReviewProbe.h"
#include "ui/kit/controls/Surfaces.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/ThemeService.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <filesystem>
#include <string>
#include <vector>

namespace shine::app {

namespace {

// 命令面板两项量化判据的预算。原来它们只是写进 manifest 的一行字，不影响 ok ——
// 面板慢到 2s 也照样 overall=PASS。阈值集中在这里，报告行也引用同一组数字。
constexpr qint64 kPaletteBudgetMs = 200; // Ctrl+K 到出结果
constexpr int kRichTextWidthMax = 150;   // 同一 HTML 串 RichText ≈ 4 个汉字宽

struct ReviewState {
    MainWindow* window = nullptr;
    std::filesystem::path dir;
    std::vector<std::string> manifest;
    // 本轮真会产出的图 = 每个 Shot 登记的图名（不带扩展名）。**唯一来源就是
    // Grab 调用点**，不另抄一份清单：两份清单迟早漂，漂了就等于少拍一张图还报 PASS。
    // 名字在抓图**之前**登记 —— 控件为空 / save 失败时文件不存在 → 判据读出 MISSING
    // → overall=FAIL。条件产物（对话框、抽屉可能没起来）也照样登记，否则
    // 「没拍到」和「没打算拍」在报告里长得一模一样，那才是真正的悄悄少拍一张。
    std::vector<std::string> expected;
    // 命令面板两项断言的测量值。-1 = 根本没测到（poll 没跑完），同样判 FAIL。
    qint64 palette_ms = -1;
    int richtext_width = -1;
};

// 抓一张。走共享的 review::Grab（见 ReviewProbe.h 头注）：它处理空控件，
// 并且按 QQuickWidget 的场景图路径取帧 —— QWidget::grab() 抓到的是「上一次
// 渲染留下的那一帧」，换实体/换主题后可以拍出**逐字节相同**的图而 manifest
// 照样记 saved。
void Shot(ReviewState* st, QWidget* w, const char* name) {
    st->expected.emplace_back(name); // 先登记，后抓 —— 见 ReviewState::expected
    review::Grab(w, st->dir, name, st->manifest);
}

void Finish(ReviewState* st) {
    // 判据走共享的 review::WriteAndExit（见 ReviewProbe.h 的 FinishOptions）：
    // expected 逐个查存在 + 字节数下限 + 逐字节重复检测，退出码跟着 ok 走。
    std::vector<std::string> files;
    files.reserve(st->expected.size());
    for (const std::string& name : st->expected) {
        files.push_back(name + ".png");
    }
    const bool palette_ok = st->palette_ms >= 0 && st->palette_ms < kPaletteBudgetMs;
    const bool richtext_ok = st->richtext_width >= 0 && st->richtext_width < kRichTextWidthMax;
    // 没测到也要在报告里留痕（名字本身带 NOT-MEASURED），不能因为「没量」而不出现。
    const std::string palette_name =
        st->palette_ms >= 0
            ? "palette-results-ms=" + std::to_string(st->palette_ms) + " (<200ms)"
            : std::string{"palette-results-ms=NOT-MEASURED"};
    const std::string richtext_name =
        st->richtext_width >= 0
            ? "palette-richtext-width=" + std::to_string(st->richtext_width) + " (<150px)"
            : std::string{"palette-richtext-width=NOT-MEASURED"};
    const review::FinishOptions opt{
        .dir = st->dir,
        .header = "P03-S11 shots",
        .expected = files,
        .manifest = &st->manifest,
        .min_bytes = 100,
        // 本页每张图承诺的状态都不同（空态/有数据/四步向导/三种分辨率/面板/两套主题/
        // 抽屉/首启两页/六个工作区）。两张逐字节相同 = 有一张没拍到它该拍的状态
        // （切页是死属性时最典型），必须 FAIL，而不是照旧报 saved。
        .fail_on_duplicate = true,
        .extra = {
            {palette_name, palette_ok},
            {richtext_name, richtext_ok},
        },
    };
    std::printf("[p03-review]\n%s", review::EvaluateShots(opt).report.c_str());
    review::WriteAndExit(opt);
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

    auto* st = new ReviewState;
    st->dir = dir;
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
        }
        // 对话框没起来也照样登记 + 抓：空控件由 review::Grab 记 FAILED，文件缺失
        // → MISSING → FAIL。少一张必须是红的，不许因为「没窗口」就从清单里消失。
        Shot(st, wizard, "wizard-step1-模板");
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
        }
        Shot(st, wizard, "wizard-step2-命名与位置");
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
        }
        Shot(st, wizard, "wizard-step3-一句话创意");
    });
    QTimer::singleShot(2400, window, [st] {
        auto* wizard = qobject_cast<ProjectWizardDialog*>(
            st->window->property("p03wizard").value<QWidget*>());
        if (wizard != nullptr) {
            wizard->GoToStep(3);
        }
        Shot(st, wizard, "wizard-step4-确认");
        if (wizard != nullptr) {
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
                // 测量值存进 state，由 Finish 变成 FinishOptions::extra 的判据。
                // 原来只是往 manifest 写一行字：面板 2s 才出结果也照样 overall=PASS。
                st->palette_ms = elapsed->elapsed();
                poll->stop();
                poll->deleteLater();
                delete elapsed;
                // 富文本行渲染断言（取证教训：视觉通道不可靠，用尺寸断言留痕）——
                // 同一 HTML 串 RichText 渲染 ≈ 4 个汉字宽（<150px）；标签被当纯文本则 >300px。
                {
                    QLabel probeLabel(QStringLiteral("<div style=\"font-size:14px;\">新建项目</div>"));
                    probeLabel.setTextFormat(Qt::RichText);
                    st->richtext_width = probeLabel.sizeHint().width();
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
        auto* drawer = st->window->ShowStatusDetail(StatusItem::Llm);
        if (drawer == nullptr) {
            // 没抽屉也照样登记 + 记 FAILED（review::Grab 处理空控件），文件缺失
            // → MISSING → FAIL。少一张必须是红的，不许因为「没抽屉」就从清单里消失。
            Shot(st, nullptr, "bonus-status-detail");
            return;
        }
        // 计时器的 context 必须是 drawer 本身：抽屉若在 400ms 内被关掉，Qt 会取消
        // 这次回调。换成 st->window 就会拿着已析构的指针去抓图。
        QTimer::singleShot(400, drawer, [st, drawer] {
            Shot(st, drawer, "bonus-status-detail");
        });
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
                // 逐工作区抓**整窗**（不是单页）：各页自检抓的是页内控件，
                // 看不到外壳（活动栏/顶栏/底栏/状态栏）与页面留白合起来是什么样。
                // 「像不像设计稿」只能在整窗上看。
                const char* kNames[] = {"shell-ws0-novel", "shell-ws1-asset", "shell-ws2-storyboard",
                                        "shell-ws3-image", "shell-ws4-video", "shell-ws5-pipeline"};
                // 计数器必须共享所有权：外层 lambda 返回后，按引用捕获的局部 int 就悬空了。
                auto done = std::make_shared<int>(0);
                auto* sweep = new QTimer(st->window);
                QObject::connect(sweep, &QTimer::timeout, st->window, [st, sweep, done, kNames] {
                    if (*done >= 6) {
                        sweep->stop();
                        sweep->deleteLater();
                        QTimer::singleShot(100, st->window, [st] { Finish(st); });
                        return;
                    }
                    const int idx = (*done)++;
                    theme::ThemeService::Switch(theme::ThemeId::DeepSpace, false);
                    st->window->SwitchWorkspace(idx);
                    st->window->ToggleSidePanel();  // 设计稿内容区无常驻侧栏
                    review::Pump();
                    review::Pump();
                    st->window->ToggleSidePanel();
                    review::Pump();
                    Shot(st, st->window, kNames[idx]);
                });
                sweep->start(320);
            });
        });
    });
}

} // namespace shine::app
