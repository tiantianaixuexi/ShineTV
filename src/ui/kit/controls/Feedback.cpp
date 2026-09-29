#include "ui/kit/controls/Feedback.h"

#include "ui/kit/motion/Easing.h"
#include "ui/kit/theme/Theme.h"

#include <QHBoxLayout>
#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QStyle>
#include <QStyleOptionFrame>
#include <QVBoxLayout>

#include <cmath>

namespace shine::widgets {

// =============================================================== ProgressBar

ProgressBar::ProgressBar(QWidget* parent) : QProgressBar(parent) {
    SetKind(this, "progressbar");
    setRange(0, 100);
    setValue(0);
    setTextVisible(true);
    // 高度交给 QSS 的 min/max-height: 6px（webui .prog）。此处不再 setMinimumHeight(8)，
    // 否则会压过 QSS 的 6px，比设计稿高一档。
    // 微光相位推进：CSS 是 1.4s linear 无限循环，这里 40ms 一步、相位连续插值。
    timer_.setInterval(40);
    connect(&timer_, &QTimer::timeout, this, [this] {
        shimmer_phase_ += 40.0 / 1400.0;
        if (shimmer_phase_ >= 1.0) {
            shimmer_phase_ -= 1.0;
        }
        update();
    });
}

int ProgressBar::ChunkWidthPx() const {
    // QSS 把高度钉死，宽度即内容区；indeterminate（range 0,0）时按整条处理
    if (IsIndeterminate()) {
        return width();
    }
    const int range = maximum() - minimum();
    if (range <= 0) {
        return 0;
    }
    const double frac = static_cast<double>(value() - minimum()) / static_cast<double>(range);
    return static_cast<int>(std::lround(frac * static_cast<double>(width())));
}

void ProgressBar::SetIndeterminate(bool on) {
    indeterminate_ = on;
    if (on) {
        setRange(0, 0); // Qt 忙碌模式（无数字跳动）
    } else {
        setRange(0, 100);
        setValue(value() == 0 ? 0 : value());
    }
}

void ProgressBar::SetInlineText(const QString& t) {
    setFormat(t.isEmpty() ? QStringLiteral("%p%") : t); // 内嵌文字
}

void ProgressBar::SetState(const char* state) {
    setProperty("state", QString::fromLatin1(state));
    Repolish(this);
}

void ProgressBar::SetThin(bool on) {
    thin_ = on;
    SetSizeAttr(this, on ? "thin" : "md"); // 几何由 QSS 的 shineSize 消费
    update();
}

void ProgressBar::SetShimmer(bool on) {
    if (on && !::shine::motion::ReduceMotion()) {
        shimmer_phase_ = 0.0;
        timer_.start();
    } else {
        timer_.stop(); // 「减少动效」：静态条，语义仍是「运行中」
    }
    update();
}

void ProgressBar::paintEvent(QPaintEvent* ev) {
    QProgressBar::paintEvent(ev); // 基类按 QSS 画胶囊底 + chunk
    if (!timer_.isActive()) {
        return;
    }

    // 只在 chunk 范围内叠微光（CSS 的 ::after 是 inset:0 贴在 .prog > i 上）
    const int chunk = ChunkWidthPx();
    if (chunk <= 0) {
        return; // indeterminate 且此刻没值：没有可高亮的主体
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // 斜向高光带：CSS 是 linear-gradient(100deg, 透明 20% → 白 .35 → 透明 80%)，
    // background-size 200% + 1.4s linear 无限循环。这里用一条随相位平移的渐变带近似。
    const double t = shimmer_phase_;
    const double band = chunk * 0.6;               // 高光带宽（≈渐变的中间 60%）
    const double center = -band + (chunk + 2 * band) * t; // 从左外侧扫到右外侧
    QColor tint = TokenQColor(theme::Current().textInverse);
    tint.setAlphaF(0.35); // CSS 的 rgba(255,255,255,.35)

    QRectF target{0.0, 0.0, static_cast<double>(chunk), static_cast<double>(height())};
    // 胶囊裁剪：高光不能溢出成方角
    QPainterPath clip;
    clip.addRoundedRect(target, height() / 2.0, height() / 2.0);
    p.setClipPath(clip);

    QLinearGradient g{QPointF{center, 0.0}, QPointF{center + band, 0.0}};
    g.setColorAt(0.0, QColor(tint.red(), tint.green(), tint.blue(), 0));
    g.setColorAt(0.5, tint);
    g.setColorAt(1.0, QColor(tint.red(), tint.green(), tint.blue(), 0));
    p.fillRect(target, g);
}

// ================================================================ EmptyState

namespace {

// .empty .glyph 的浮动字形框。
//
// webui ui.css:699-710 给 .glyph 挂了 `animation: float-y 4s ease-in-out infinite`，
// base.css:161-164 的 @keyframes float-y 是「0%/100% → translateY(0)，50% → translateY(-6px)」。
//
// QSS 没有 keyframes，Qt 也没有「自动动画化子控件位移」的现成件，所以自绘：整个框
// （QSS 画的 fill-muted 底 + 虚线边 + r-lg，以及框内 24px 图标）由 paintEvent
// 一次画完，位移用一个连续相位驱动。这样做而不是「套一层布局再 move()」的原因：
// 手动 move() 会和 QVBoxLayout 抢几何（QSS 文档第五节记过这条坑），而 transform 在
// CSS 里同样不占布局空间 —— 位移只发生在绘制阶段，控件本身尺寸不变。
class FloatGlyph final : public QFrame {
  public:
    explicit FloatGlyph(const QString& icon, QWidget* parent = nullptr) : QFrame(parent) {
        SetKind(this, "emptyglyph");
        icon_ = icon;
        setFixedSize(52, 52); // webui .glyph 52×52
        // CSS 周期 4s；这里 40ms 一步（与 ProgressBar 微光同一节律），相位连续插值
        timer_.setInterval(40);
        connect(&timer_, &QTimer::timeout, this, [this] {
            phase_ += 40.0 / kPeriodMs;
            if (phase_ >= 1.0) {
                phase_ -= 1.0;
            }
            // 0%/100% → 0，50% → -6px，每半程各自 ease-in-out。
            // 用铺满整周期的余弦：两端与中点斜率都为 0，与分段 ease-in-out 的观感一致。
            offset_ = -kAmplitudePx * (1.0 - std::cos(2.0 * M_PI * phase_)) / 2.0;
            update();
        });
        SetFloating(true);
    }

    void SetFloating(bool on) {
        if (on && !motion::ReduceMotion()) {
            phase_ = 0.0;
            offset_ = 0.0;
            timer_.start();
        } else {
            timer_.stop();
            offset_ = 0.0; // 「减少动效」下退化为静态（语义不变：仍是空态提示）
            update();
        }
    }

  protected:
    void paintEvent(QPaintEvent* ev) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(0.0, offset_);

        // 外框走 QSS（*[shineKind="emptyglyph"]：fill-muted 底 + 虚线边 + r-lg 14）。
        // 这一步等价于 QFrame::paintEvent 内部做的事，只是要先把 painter 平移。
        QStyleOptionFrame opt;
        opt.initFrom(this);
        style()->drawControl(QStyle::CE_ShapedFrame, &opt, &p, this);

        // 框内图标：webui 是 24×24 的 SVG，本仓统一用字形字符（与页面其余图标同一约定）。
        // 字号/颜色取自原 QSS 规则（24px + text-muted），零字面色值。
        QFont f = font();
        f.setPixelSize(24);
        p.setFont(f);
        p.setPen(TokenQColor(theme::Current().textMuted));
        p.drawText(rect(), Qt::AlignCenter, icon_);
    }

  private:
    static constexpr double kPeriodMs = 4000.0;  // CSS: float-y 4s
    static constexpr double kAmplitudePx = 6.0; // CSS: 50% 处 translateY(-6px)

    QString icon_;
    QTimer timer_;
    double phase_ = 0.0;  // 0→1 一个周期
    double offset_ = 0.0; // 当前位移（px，向上为负）
};

} // namespace

EmptyState::EmptyState(const QString& icon, const QString& title, const QString& subtitle,
                       const QString& actionText, QWidget* parent)
    : QFrame(parent) {
    SetKind(this, "emptystate");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(20, 40, 20, 40); // webui .empty padding: 40px 20px
    col->setSpacing(10);                      // .empty gap: 10px
    col->setAlignment(Qt::AlignCenter);

    // .glyph：52×52 圆角虚线方框 + float-y 浮动（框与图标一体自绘，见 FloatGlyph）
    auto* glyph = new FloatGlyph(icon, this);
    col->addWidget(glyph, 0, Qt::AlignCenter);

    auto* t = new QLabel(title, this);
    SetKind(t, "emptytitle"); // .empty .title：f13 w600 text-secondary
    t->setAlignment(Qt::AlignCenter);
    t->setWordWrap(true);
    t->setMaximumWidth(360); // 同 subtitle：宽度上限让折行点可预期
    col->addWidget(t, 0, Qt::AlignCenter);

    auto* s = new QLabel(subtitle, this);
    SetKind(s, "statesub");
    s->setAlignment(Qt::AlignCenter);
    s->setWordWrap(true);
    // ⚠️ 必须给宽度上限：wordWrap 的 QLabel 在 Qt::AlignCenter 下会按
    // 「父容器当前宽度」折行，若父容器此刻还没完成布局（宽度接近 0 或过大），
    // 换行位置算错就会把整行文字挤成乱码（实测截图里「检测片段默认
    // 校验状态」压成不可读的一团）。给固定上限让折行点可预期。
    s->setMaximumWidth(360);
    col->addWidget(s, 0, Qt::AlignCenter);

    // 主行动按钮：必须给出下一步（UI.md §2.1）
    auto* action = new Button(actionText.isEmpty() ? QStringLiteral("开始") : actionText,
                              Button::Variant::Primary, Button::Size::Md, this);
    connect(action, &Button::clicked, this, [this] {
        if (on_action_) {
            on_action_();
        }
    });
    col->addSpacing(4);
    col->addWidget(action, 0, Qt::AlignCenter);
}

// ================================================================ ErrorState

ErrorState::ErrorState(const QString& title, const QString& detail, QWidget* parent) : QFrame(parent) {
    SetKind(this, "errorstate");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(20, 40, 20, 40); // 与 EmptyState 同一套 .empty 几何
    col->setSpacing(10);
    col->setAlignment(Qt::AlignCenter);

    auto* ic = new QLabel(QStringLiteral("⚠"), this);
    SetKind(ic, "stateicon");
    ic->setAlignment(Qt::AlignCenter);
    col->addWidget(ic, 0, Qt::AlignCenter);

    auto* t = new QLabel(title, this);
    SetKind(t, "statetitle");
    SetSemibold(t, true);
    t->setAlignment(Qt::AlignCenter);
    col->addWidget(t, 0, Qt::AlignCenter);

    auto* detailBox = new QLabel(detail, this);
    SetKind(detailBox, "statedetail");
    detailBox->setWordWrap(true);
    detailBox->setTextFormat(Qt::PlainText); // Error.detail 明文展示
    detailBox->hide();                       // 默认折叠
    col->addWidget(detailBox);

    auto* row = new QHBoxLayout();
    row->setSpacing(8);
    row->addStretch(1);
    auto* retry = new Button(QStringLiteral("重试"), Button::Variant::Primary, Button::Size::Md, this);
    connect(retry, &Button::clicked, this, [this] {
        if (on_retry_) {
            on_retry_();
        }
    });
    row->addWidget(retry);
    auto* more = new Button(QStringLiteral("查看详情"), Button::Variant::Ghost, Button::Size::Md, this);
    more->setCheckable(true);
    connect(more, &Button::clicked, this, [detailBox, more] {
        detailBox->setVisible(more->isChecked()); // 折叠 Error.detail
    });
    row->addWidget(more);
    row->addStretch(1);
    col->addLayout(row);
}

} // namespace shine::widgets
