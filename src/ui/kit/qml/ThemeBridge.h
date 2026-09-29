#pragma once
// ui/kit/qml/ThemeBridge —— 主题 token 的 QML 桥（2026-09-29 架构层）
//
// 为什么要有这个文件：QML 页面若自己写死颜色，就会出现**第二套颜色真值**，
// 主题切换、样式编辑器、自定义主题三处全部对不上。桥的唯一职责是把既有的
// `shine::theme::ColorToken` 原样喂给 QML —— 零新增真值。
//
// 它是**只读投影**，不新增任何 token：
//   * 颜色走 `theme::Current()`，与 QSS 模板的 %1..%31 同源同序；
//   * 几何/动效走 `theme::radius/space/font/motion` 的同一批常量；
//   * 派生值（color-mix 的 tone 底/边）复用 ToneMix.h 的同一份算法
//     （QSS 的 QssBuilder::FillToneMixes 也调它），不在这里重算一遍 ——
//     重算必然漂移。
//
// ============================ 最重要的一条约束 ============================
// **所有值一律用 `Q_PROPERTY` 暴露，不提供取值的 `Q_INVOKABLE` 方法。**
//
// 原因：QML 绑定只对**属性读**建立依赖。`Theme.token("bg.surface")` 这种
// 方法调用在依赖图里是空的，绑定求值一次后就永久缓存 —— 之后无论发多少
// `themeChanged`，QML 都不会重算。
//
// 症状极具欺骗性：切 5 套主题都能出图，PNG 大小还各不相同（float-y 动画
// 每帧相位不同），肉眼看文件大小会以为"生效了"；真正打开图才发现标题换了、
// 配色纹丝不动。这个坑是端到端链路（5 套主题同树热切换 + 抓图）才抓出来的。
//
// 所以：**要随主题变的量 → 属性 + NOTIFY themeChanged**；
// 参数化的取值（本桥已不需要，因为都做成了查表）→ 才可以用方法。
// 加新 token 时照此办理，别为了"写起来顺手"再加回方法。
// ========================================================================
//
// 用法（QML 侧）：
//   import Shine 1.0
//   Rectangle {
//       color: ThemeBridge.colors["bg.surface"]
//       radius: ThemeBridge.radii.md
//       border.color: ThemeBridge.toneEdge["ok"]  // = color-mix(ok 35%, bg.surface)
//   }
//
// 主题切换由 theme 层的换肤漏斗回调 NotifyThemeChanged()，QML 侧绑定据此重算。
// 注册入口见 QuickHost.cpp（它把 theme::SetThemeChangedObserver 接到了这里）。
#include <QColor>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <cstddef>
#include <cstdint>

namespace shine::qml {

// 线程约束：QML 场景图在 GUI 线程，且本桥不做 IO，调用方无需额外同步。
//
// ⚠️ **不要加 QML_ELEMENT / QML_SINGLETON 宏**。那套宏要求引擎自己构造单例
// （`QML_SINGLETON` 的契约是「类有默认构造、由注册代码 new 出来」），而本桥的
// 实例必须是进程唯一的那一个，由 theme 层主动通知。两者同时存在时 moc 生成的
// 注册代码会盖掉外部实例的属性暴露。
//
// ⚠️ **注册时必须显式写模板实参** `qmlRegisterSingletonType<ThemeBridge>(...)`：
// qqml.h 里有同名重载，不指定 <ThemeBridge> 时会落到把 metaObject 传 nullptr
// 的那个，QML 侧能解析到单例却拿不到任何成员（见 QuickHost.cpp 的长注释）。
class ThemeBridge : public QObject {
    Q_OBJECT

  public:
    explicit ThemeBridge(QObject* parent = nullptr);

    // —— 颜色：随主题变，必须是属性 ——
    // 31 项，键为 Token.h 的点分名，顺序与 kColorTokenNames 一致
    [[nodiscard]] Q_PROPERTY(QVariantMap colors READ colors NOTIFY themeChanged)
    QVariantMap colors() const;

    // 派生色：与 QssBuilder::FillToneMixes 同一算法（tone 按比例混 bg.surface）。
    // 键为 tone 名：accent / info / ok / warn / danger / busy / pending
    [[nodiscard]] Q_PROPERTY(QVariantMap toneBg READ toneBackgrounds NOTIFY themeChanged)
    QVariantMap toneBackgrounds() const;

    [[nodiscard]] Q_PROPERTY(QVariantMap toneEdge READ toneEdges NOTIFY themeChanged)
    QVariantMap toneEdges() const;

    // —— 几何 / 动效：与 tokens.css 的 --r-* / --sp-* / --dur-* 同值 ——
    // 键：radii = xs/sm/md/lg/xl/pill；durations = fast/base/slow
    [[nodiscard]] Q_PROPERTY(QVariantMap radii READ radii NOTIFY themeChanged)
    QVariantMap radii() const;

    // 7 项，下标 0 对应 --sp-1（4px），依次 8/12/16/24/32/48
    [[nodiscard]] Q_PROPERTY(QVariantMap spaces READ spaces NOTIFY themeChanged)
    QVariantMap spaces() const;

    [[nodiscard]] Q_PROPERTY(QVariantMap durations READ durations NOTIFY themeChanged)
    QVariantMap durations() const;

    // —— 基准字号（px）。⚠️ QML 的 font.pixelSize 接受实数，与 QSS 的整数化不同，
    // 这里返回 token 的整数档（13），QML 侧不要再自行加小数。
    [[nodiscard]] Q_PROPERTY(int baseFontPx READ baseFontPx NOTIFY themeChanged)
    int baseFontPx() const;

    // 每主题字体族（**随主题变**：水墨为衬线族）；与 QSS 的 $FONTFAMILY$ 同源
    [[nodiscard]] Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY themeChanged)
    QString fontFamily() const;

    // 主题显示名（QML 标题栏/调试角标用）
    [[nodiscard]] Q_PROPERTY(QString themeName READ themeName NOTIFY themeChanged)
    QString themeName() const;

    // 「减少动效」开关。QML 侧所有动画必须读它来短路：
    //   animation: ThemeBridge.reduceMotion ? null : NumberAnimation {...}
    [[nodiscard]] Q_PROPERTY(bool reduceMotion READ reduceMotion NOTIFY themeChanged)
    bool reduceMotion() const;

    // 场景图是否支持 layer.effect（MultiEffect 阴影）。
    // software 后端下挂 layer 的 item 会被整个吞掉，必须由 C++ 侧探测后告知 QML，
    // 否则 QML 侧无从区分「阴影没画出来」和「控件整个没了」。
    [[nodiscard]] Q_PROPERTY(bool layerEffectsAvailable READ layerEffectsAvailable NOTIFY
                                 themeChanged)
    bool layerEffectsAvailable() const;

  signals:
    // 主题切换 / 颜色变化时发出。**所有属性的 NOTIFY 都是它** —— 这是 QML 绑定
    // 唯一的重算入口。
    void themeChanged();

  public:
    // 由 theme 层换肤漏斗调用（唯一入口，避免两处订阅）
    static void NotifyThemeChanged();

    // 由 QuickHost 构造时按 quickWindow()->sceneGraphBackend() 写入。
    static void SetLayerEffectsAvailable(bool ok);
};

// 进程内单例（QML 工厂返回的就是它）。
// ⚠️ 刻意 new 且永不回收，见 ThemeBridge.cpp 的注释（引擎会 delete 它）。
[[nodiscard]] ThemeBridge& Instance();

// 内部：tone 名字（accent/info/ok/warn/danger/busy/pending）对应的 token 值。
// 与 ToneMix.h 的下标同序；QML 侧按名字索引，不直接依赖下标。
[[nodiscard]] std::uint32_t ToneValue(std::size_t tone_index) noexcept;

} // namespace shine::qml
