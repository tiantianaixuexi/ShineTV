#include "ui/pages/shell/TopBar.h"

#include "ui/pages/settings/StyleEditorDialog.h"
#include "ui/kit/motion/Easing.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/ThemeService.h"
#include "ui/kit/theme/Token.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Surfaces.h"

#include <QAction>
#include <QHBoxLayout>
#include <QMenu>

namespace shine::app {

TopBar::TopBar(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("topBar"));
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(theme::space::kSteps[3], theme::space::kSteps[2],
                            theme::space::kSteps[3], theme::space::kSteps[2]);
    lay->setSpacing(theme::space::kSteps[2]);

    // 项目名（▾ 菜单：项目列表 / 关闭项目 / 在资源管理器中显示）
    project_btn_ = new shine::widgets::Button(QStringLiteral("未打开项目"),
                                              shine::widgets::Button::Variant::Ghost,
                                              shine::widgets::Button::Size::Md, this);
    connect(project_btn_, &shine::widgets::Button::clicked, this, [this] {
        QMenu menu(this);
        QAction* hub = menu.addAction(QStringLiteral("项目列表…"));
        connect(hub, &QAction::triggered, this, [this] {
            if (on_show_hub_) on_show_hub_();
        });
        QAction* reveal = menu.addAction(QStringLiteral("在资源管理器中显示"));
        reveal->setEnabled(project_btn_->text() != QStringLiteral("未打开项目"));
        connect(reveal, &QAction::triggered, this, [this] {
            if (on_reveal_project_) on_reveal_project_();
        });
        QAction* close = menu.addAction(QStringLiteral("关闭项目"));
        close->setEnabled(project_btn_->text() != QStringLiteral("未打开项目"));
        connect(close, &QAction::triggered, this, [this] {
            if (on_close_project_) on_close_project_();
        });
        menu.exec(project_btn_->mapToGlobal(QPoint(0, project_btn_->height())));
    });
    lay->addWidget(project_btn_);
    lay->addStretch();

    // 🔍 命令面板入口（Ctrl+K）
    auto* palette = new shine::widgets::Button(QStringLiteral("🔍  搜索命令"),
                                               shine::widgets::Button::Variant::Secondary,
                                               shine::widgets::Button::Size::Md, this);
    shine::widgets::Tooltip::Attach(palette, QStringLiteral("命令 / 页面 / 项目内容"),
                                    QStringLiteral("Ctrl+K"));
    connect(palette, &shine::widgets::Button::clicked, this, [this] {
        if (on_open_palette_) on_open_palette_();
    });
    lay->addWidget(palette);

    // 左侧导航开合（Ctrl+B）—— 工作区导航（章节树 / 实体树 / 镜头树）的归属栏
    side_btn_ = new shine::widgets::Button(QStringLiteral("导航"),
                                           shine::widgets::Button::Variant::Ghost,
                                           shine::widgets::Button::Size::Md, this);
    shine::widgets::Tooltip::Attach(side_btn_, QStringLiteral("显示 / 隐藏左侧导航栏"),
                                    QStringLiteral("Ctrl+B"));
    connect(side_btn_, &shine::widgets::Button::clicked, this, [this] {
        if (on_toggle_side_panel_) on_toggle_side_panel_();
    });
    lay->addWidget(side_btn_);

    // 右侧检查器开合（Ctrl+I）—— 检查器默认收起，这里是它的显式入口，
    // 免得用户不知道有这个东西可以打开
    inspector_btn_ = new shine::widgets::Button(QStringLiteral("检查器"),
                                                shine::widgets::Button::Variant::Ghost,
                                                shine::widgets::Button::Size::Md, this);
    shine::widgets::Tooltip::Attach(inspector_btn_, QStringLiteral("显示 / 隐藏右侧检查器"),
                                    QStringLiteral("Ctrl+I"));
    connect(inspector_btn_, &shine::widgets::Button::clicked, this, [this] {
        if (on_toggle_inspector_) on_toggle_inspector_();
    });
    lay->addWidget(inspector_btn_);

    // ▶ 运行 / ⏹ 停止（P09 接入前为占位，点了给中文提示）
    auto* run = new shine::widgets::Button(QStringLiteral("▶ 运行"),
                                           shine::widgets::Button::Variant::Primary,
                                           shine::widgets::Button::Size::Md, this);
    connect(run, &shine::widgets::Button::clicked, this, [this] {
        if (on_run_) on_run_();
    });
    lay->addWidget(run);
    auto* stop = new shine::widgets::Button(QStringLiteral("⏹ 停止"),
                                            shine::widgets::Button::Variant::Ghost,
                                            shine::widgets::Button::Size::Md, this);
    connect(stop, &shine::widgets::Button::clicked, this, [this] {
        if (on_stop_) on_stop_();
    });
    lay->addWidget(stop);

    // 🎨 主题（P02-S8 菜单整体搬入；状态栏点主题名复用同一菜单）
    theme_btn_ = new shine::widgets::Button(QStringLiteral("🎨 主题"),
                                            shine::widgets::Button::Variant::Ghost,
                                            shine::widgets::Button::Size::Md, this);
    connect(theme_btn_, &shine::widgets::Button::clicked, this, [this] {
        PopupThemeMenu();
    });
    lay->addWidget(theme_btn_);

    // ⚙ 设置（首启引导同一套三步：主题 / Comfy 地址 / LLM Key）
    auto* settings = new shine::widgets::Button(QStringLiteral("⚙ 设置"),
                                                shine::widgets::Button::Variant::Ghost,
                                                shine::widgets::Button::Size::Md, this);
    connect(settings, &shine::widgets::Button::clicked, this, [this] {
        if (on_settings_) on_settings_();
    });
    lay->addWidget(settings);
}

void TopBar::SetProjectName(const QString& name) {
    const bool open = !name.isEmpty();
    project_btn_->setText(open ? name + QStringLiteral(" ▾") : QStringLiteral("未打开项目"));
    project_btn_->setEnabled(true); // 菜单里「项目列表」始终可用
}

void TopBar::SetOnShowHub(std::function<void()> cb) { on_show_hub_ = std::move(cb); }
void TopBar::SetOnCloseProject(std::function<void()> cb) { on_close_project_ = std::move(cb); }
void TopBar::SetOnRevealProject(std::function<void()> cb) { on_reveal_project_ = std::move(cb); }
void TopBar::SetOnOpenPalette(std::function<void()> cb) { on_open_palette_ = std::move(cb); }
void TopBar::SetOnToggleSidePanel(std::function<void()> cb) { on_toggle_side_panel_ = std::move(cb); }
void TopBar::SetOnToggleInspector(std::function<void()> cb) { on_toggle_inspector_ = std::move(cb); }
void TopBar::SetOnRun(std::function<void()> cb) { on_run_ = std::move(cb); }
void TopBar::SetOnStop(std::function<void()> cb) { on_stop_ = std::move(cb); }
void TopBar::SetOnSettings(std::function<void()> cb) { on_settings_ = std::move(cb); }
void TopBar::SetOnThemeChanged(std::function<void()> cb) { on_theme_changed_ = std::move(cb); }

void TopBar::SetSidePanelActive(bool on) {
    if (side_btn_ == nullptr) {
        return;
    }
    side_btn_->setText(on ? QStringLiteral("导航 ◧") : QStringLiteral("导航"));
    side_btn_->setCheckable(true);
    side_btn_->setChecked(on);
}

void TopBar::SetInspectorActive(bool on) {
    if (inspector_btn_ == nullptr) {
        return;
    }
    inspector_btn_->setText(on ? QStringLiteral("检查器 ◨") : QStringLiteral("检查器"));
    inspector_btn_->setCheckable(true);
    inspector_btn_->setChecked(on);
}

void TopBar::PopupThemeMenu() {
    ShowThemeMenu(theme_btn_->mapToGlobal(QPoint(0, theme_btn_->height())));
}

void TopBar::ShowThemeMenu(const QPoint& globalPos) {
    QMenu menu(this);
    const bool customNow = theme::CurrentIsCustom();
    for (const theme::ThemeId id : theme::kAllThemes) {
        const std::string_view dn = theme::ThemeDisplayName(id);
        QAction* a = menu.addAction(QString::fromUtf8(dn.data(), static_cast<int>(dn.size())));
        a->setCheckable(true);
        a->setChecked(!customNow && theme::CurrentThemeId() == id);
        connect(a, &QAction::triggered, this, [this, id] {
            theme::ThemeService::Switch(id);
            if (on_theme_changed_) on_theme_changed_();
        });
    }
    if (theme::CustomThemeCount() > 0) {
        menu.addSeparator();
        for (std::size_t i = 0; i < theme::CustomThemeCount(); ++i) {
            const std::string name = theme::CustomThemeName(i);
            QAction* a = menu.addAction(QString::fromStdString(name));
            a->setCheckable(true);
            a->setChecked(customNow && theme::CurrentCustomName() == name);
            connect(a, &QAction::triggered, this, [this, name] {
                theme::ThemeService::SwitchCustom(name);
                if (on_theme_changed_) on_theme_changed_();
            });
        }
    }
    menu.addSeparator();
    QAction* editor = menu.addAction(QStringLiteral("样式编辑器…"));
    connect(editor, &QAction::triggered, this, [this] {
        auto* dlg = new StyleEditorDialog();
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });
    QAction* rm = menu.addAction(QStringLiteral("减少动效"));
    rm->setCheckable(true);
    rm->setChecked(shine::motion::ReduceMotion());
    connect(rm, &QAction::triggered, this, [](bool on) {
        theme::ThemeService::SetReduceMotionPersisted(on);
    });
    menu.exec(globalPos);
}

} // namespace shine::app
