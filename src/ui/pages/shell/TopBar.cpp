#include "ui/pages/shell/TopBar.h"

#include "ui/pages/settings/StyleEditorDialog.h"
#include "ui/kit/motion/Easing.h"
#include "ui/kit/theme/CssColor.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/ThemeService.h"
#include "ui/kit/theme/Token.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Surfaces.h"
#include "ui/kit/controls/WidgetCommon.h"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLinearGradient>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>

namespace shine::app {

namespace {

// 项目名胶囊里的色点（webui shell.css:73-79 .proj-chip .pdot）：
// 8×8 / r3 / accent。QSS 无渐变，这里取 accent.primary 纯色（docs §一 已记为能力边界）。
QFrame* MakeProjectDot(QWidget* parent) {
    auto* dot = new QFrame(parent);
    dot->setObjectName(QStringLiteral("projDot"));
    dot->setFixedSize(8, 8);
    return dot;
}

// 主题色卡（shell.css:147-154 .swatch）：26×16 / r4 / line-normal 边。
// 渐变在 QSS/painter 里都拿不到 CSS 的 linear-gradient，这里用 accentPrimary →
// accentSecondary 两色对角渐变近似（色值全部来自该主题自身的 token）。
QPixmap ThemeSwatch(theme::ThemeId id) {
    const theme::ColorToken& c = theme::ThemeColorsOf(id);
    QPixmap pm(26, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addRoundedRect(QRectF(0.5, 0.5, 25.0, 15.0), 4.0, 4.0);
    p.fillPath(path, shine::widgets::TokenQColor(c.bgSurface));
    p.save();
    p.setClipPath(path);
    QLinearGradient g(0.0, 0.0, 26.0, 16.0);
    g.setColorAt(0.0, shine::widgets::TokenQColor(c.accentPrimary));
    g.setColorAt(1.0, shine::widgets::TokenQColor(c.accentSecondary));
    p.fillRect(QRectF(0.0, 0.0, 26.0, 16.0), g);
    p.restore();
    p.setPen(QPen(shine::widgets::TokenQColor(c.lineNormal), 1.0));
    p.drawPath(path);
    return pm;
}

// 顶栏全局搜索位（shell.css:81-83 .topsearch：w260 / h28）。点击开命令面板（Ctrl+K）。
QPushButton* MakeTopSearch(QWidget* parent, std::function<void()> on_click) {
    auto* btn = new QPushButton(parent);
    btn->setObjectName(QStringLiteral("topSearch"));
    btn->setFixedSize(260, 28);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setToolTip(QStringLiteral("搜索命令、页面与项目内容"));
    auto* lay = new QHBoxLayout(btn);
    lay->setContentsMargins(10, 0, 8, 0);
    lay->setSpacing(8);
    auto* icon = new QLabel(QStringLiteral("🔍"), btn);
    icon->setObjectName(QStringLiteral("topSearchIcon"));
    auto* text = new QLabel(QStringLiteral("搜索命令"), btn);
    text->setObjectName(QStringLiteral("topSearchText"));
    lay->addWidget(icon);
    lay->addWidget(text, 1);
    auto* kbd = new shine::widgets::Kbd(QStringLiteral("Ctrl K"), btn);
    kbd->setObjectName(QStringLiteral("topSearchKbd"));
    lay->addWidget(kbd);
    // 按钮自绘文字已清空，QPushButton::sizeHint 不含内部布局 —— 但这里用了 setFixedSize，
    // 尺寸不依赖 sizeHint，两个标签仍然要各自转成透明背景才不会画出灰底
    for (QWidget* w : {static_cast<QWidget*>(icon), static_cast<QWidget*>(text)}) {
        w->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    kbd->setAttribute(Qt::WA_TransparentForMouseEvents);
    QObject::connect(btn, &QPushButton::clicked, btn, [on_click] {
        if (on_click) {
            on_click();
        }
    });
    return btn;
}

} // namespace

TopBar::TopBar(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("topBar"));
    // webui shell.css:19-30 .topbar：h46 / gap 10 / padding 0 12
    setFixedHeight(46);
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(12, 0, 12, 0);
    lay->setSpacing(10);

    // 品牌位（shell.css:31-53 .brand）：22×22 圆角方块 + 站名，点击回项目列表
    brand_ = new QPushButton(QStringLiteral("ShineTV Studio"), this);
    brand_->setObjectName(QStringLiteral("brand"));
    brand_->setCursor(Qt::PointingHandCursor);
    brand_->setToolTip(QStringLiteral("返回项目列表"));
    connect(brand_, &QPushButton::clicked, this, [this] {
        if (on_show_hub_) on_show_hub_();
    });
    lay->addWidget(brand_);

    // 项目名（▾ 菜单：项目列表 / 关闭项目 / 在资源管理器中显示）
    proj_chip_ = new QWidget(this);
    proj_chip_->setObjectName(QStringLiteral("projChip"));
    auto* chip_lay = new QHBoxLayout(proj_chip_);
    chip_lay->setContentsMargins(10, 0, 10, 0);
    chip_lay->setSpacing(7); // .proj-chip gap 7
    chip_lay->setSizeConstraint(QLayout::SetFixedSize);
    proj_dot_ = MakeProjectDot(proj_chip_);
    project_btn_ = new shine::widgets::Button(QStringLiteral("未打开项目"),
                                              shine::widgets::Button::Variant::Ghost,
                                              shine::widgets::Button::Size::Sm, proj_chip_);
    connect(project_btn_, &shine::widgets::Button::clicked, this, [this] {
        QMenu menu(this);
        QAction* hub = menu.addAction(QStringLiteral("项目列表…"));
        connect(hub, &QAction::triggered, this, [this] {
            if (on_show_hub_) on_show_hub_();
        });
        QAction* reveal = menu.addAction(QStringLiteral("在资源管理器中显示"));
        reveal->setEnabled(has_project_);
        connect(reveal, &QAction::triggered, this, [this] {
            if (on_reveal_project_) on_reveal_project_();
        });
        QAction* close = menu.addAction(QStringLiteral("关闭项目"));
        close->setEnabled(has_project_);
        connect(close, &QAction::triggered, this, [this] {
            if (on_close_project_) on_close_project_();
        });
        // .menu-pop：top calc(100% + 6px)
        menu.exec(proj_chip_->mapToGlobal(QPoint(0, proj_chip_->height() + 6)));
    });
    chip_lay->addWidget(proj_dot_);
    chip_lay->addWidget(project_btn_);
    lay->addWidget(proj_chip_);

    // 🔍 命令面板入口（Ctrl+K）
    palette_ = MakeTopSearch(this, [this] {
        if (on_open_palette_) on_open_palette_();
    });
    shine::widgets::Tooltip::Attach(palette_, QStringLiteral("命令 / 页面 / 项目内容"),
                                    QStringLiteral("Ctrl+K"));
    lay->addWidget(palette_);

    lay->addStretch();

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
    // webui Shell.jsx:74 用的是 28×28 的 .icon-btn + tip，这里照搬（不再是文字按钮）
    auto* settings = new shine::widgets::IconButton(QStringLiteral("⚙"),
                                                    QStringLiteral("设置 · 主题 / Comfy 地址 / LLM Key"),
                                                    shine::widgets::IconButton::Size::Md, this);
    settings->setFixedSize(28, 28);
    connect(settings, &shine::widgets::IconButton::clicked, this, [this] {
        if (on_settings_) on_settings_();
    });
    lay->addWidget(settings);
}

void TopBar::SetProjectName(const QString& name) {
    const bool open = !name.isEmpty();
    has_project_ = open;
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
    ShowThemeMenu(theme_btn_->mapToGlobal(QPoint(0, theme_btn_->height() + 6)));
}

void TopBar::ShowThemeMenu(const QPoint& globalPos) {
    QMenu menu(this);
    // .menu-pop：bg-overlay + r-md + line-normal（shell.css:101-111）。
    // QMenu 是独立顶层弹层，不在 #shineShell 子树内，因此单独给它挂一份局部 QSS。
    const theme::ColorToken& t = theme::Current();
    menu.setStyleSheet(
        QStringLiteral("QMenu { background-color: %1; border: 1px solid %2; "
                       "border-radius: %3px; padding: 5px; }"
                       "QMenu::item { padding: 7px 10px; border-radius: %4px; }"
                       "QMenu::item:selected { background-color: %5; color: %6; }"
                       "QMenu::separator { height: 1px; margin: 5px 6px; background-color: %2; }")
            .arg(shine::widget::CssRgb(t.bgOverlay), shine::widget::CssRgb(t.lineNormal),
                 QString::number(theme::radius::kMd), QString::number(theme::radius::kSm),
                 shine::widget::CssRgb(t.fillHover), shine::widget::CssRgb(t.textPrimary)));

    const bool customNow = theme::CurrentIsCustom();
    menu.addSection(QStringLiteral("内置主题")); // .mlabel 分组标题
    for (const theme::ThemeId id : theme::kAllThemes) {
        const std::string_view dn = theme::ThemeDisplayName(id);
        QAction* a = menu.addAction(QString::fromUtf8(dn.data(), static_cast<int>(dn.size())));
        // .swatch 26×16 r4：色值取自该主题自己的 token，不是写死色
        a->setIcon(QIcon(ThemeSwatch(id)));
        a->setCheckable(true);
        a->setChecked(!customNow && theme::CurrentThemeId() == id);
        connect(a, &QAction::triggered, this, [this, id] {
            theme::ThemeService::Switch(id);
            if (on_theme_changed_) on_theme_changed_();
        });
    }
    if (theme::CustomThemeCount() > 0) {
        menu.addSection(QStringLiteral("自定义主题"));
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
