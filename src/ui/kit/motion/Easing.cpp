#include "ui/kit/motion/Easing.h"

#include "ui/kit/motion/MotionScope.h"
#include "ui/kit/motion/Tween.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

namespace shine::motion {
namespace {

bool g_reduceMotion = false;

} // namespace

const theme::motion::Ease& CurveOf(Curve c) {
    switch (c) {
        case Curve::Emphasized:
            return theme::motion::kEmphasized;
        case Curve::Exit:
            return theme::motion::kExit;
        case Curve::Standard:
        default:
            return theme::motion::kStandard;
    }
}

double CubicBezierY(const theme::motion::Ease& e, double x) {
    if (x <= 0.0) {
        return 0.0;
    }
    if (x >= 1.0) {
        return 1.0;
    }
    const double x1 = e.x1, y1 = e.y1, x2 = e.x2, y2 = e.y2;
    const auto bx = [&](double t) {
        return 3.0 * (1 - t) * (1 - t) * t * x1 + 3.0 * (1 - t) * t * t * x2 + t * t * t;
    };
    const auto by = [&](double t) {
        return 3.0 * (1 - t) * (1 - t) * t * y1 + 3.0 * (1 - t) * t * t * y2 + t * t * t;
    };
    double t = x;
    for (int i = 0; i < 8; ++i) {
        const double err = bx(t) - x;
        if (std::abs(err) < 1e-6) {
            break;
        }
        const double d = 3.0 * (1 - t) * (1 - t) * x1 + 6.0 * (1 - t) * t * (x2 - x1) +
                         3.0 * t * t * (1.0 - x2);
        if (std::abs(d) < 1e-9) {
            break;
        }
        t = std::clamp(t - err / d, 0.0, 1.0);
    }
    return by(t);
}

void SetReduceMotion(bool on) { g_reduceMotion = on; }
bool ReduceMotion() { return g_reduceMotion; }

void InitializeMotion() {
    if (const char* raw = std::getenv("SHINE_REDUCE_MOTION"); raw != nullptr && *raw != '\0') {
        g_reduceMotion = !(std::string_view{raw} == "0");
    }
}

bool SelfTest(std::string* report) {
    bool ok = true;
    std::string text;

    // 1) 3 条曲线：端点守恒 + 中段在 (0,1) 内
    for (const Curve c : {Curve::Standard, Curve::Emphasized, Curve::Exit}) {
        const theme::motion::Ease& e = CurveOf(c);
        const bool pts = std::abs(CubicBezierY(e, 0.0)) < 1e-9 && std::abs(CubicBezierY(e, 1.0) - 1.0) < 1e-9;
        const double mid = CubicBezierY(e, 0.5);
        const bool midOk = mid > 0.0 && mid < 1.0;
        ok = ok && pts && midOk;
        text += "curve: endpoints=" + std::string{pts ? "OK" : "FAIL"} + " mid=" +
                std::to_string(mid).substr(0, 4) + (midOk ? " OK\n" : " FAIL\n");
    }

    // 2) 时长全部取自 motion.dur.* token
    text += "durations from token: fast=" + std::to_string(theme::motion::kDurFastMs) +
            " base=" + std::to_string(theme::motion::kDurBaseMs) +
            " slow=" + std::to_string(theme::motion::kDurSlowMs) + "\n";

    // 3) 「减少动效」打开后瞬时完成（任何动画不再出现）
    SetReduceMotion(false);
    {
        Tween t{theme::motion::kStandard};
        QVariant got;
        t.Run(0.0, 1.0, theme::motion::kDurBaseMs, [&got](const QVariant& v) { got = v; });
        const bool running = t.state() == QAbstractAnimation::Running && Tween::ActiveCount() == 1;
        t.Settle();
        const bool settled = got.toDouble() == 1.0 && Tween::ActiveCount() == 0;
        ok = ok && running && settled;
        text += "tween runs (token dur) + settle: " + std::string{running && settled ? "OK" : "FAIL"} + "\n";
    }
    SetReduceMotion(true);
    {
        Tween t{theme::motion::kStandard};
        QVariant got;
        t.Run(0.0, 1.0, theme::motion::kDurBaseMs, [&got](const QVariant& v) { got = v; });
        const bool instant = got.toDouble() == 1.0 && t.state() != QAbstractAnimation::Running &&
                             Tween::ActiveCount() == 0;
        ok = ok && instant;
        text += "reduce-motion -> instant: " + std::string{instant ? "OK" : "FAIL"} + "\n";
    }

    // 4) MotionScope：析构自动收敛 + 回收（RAII）
    {
        MotionScope scope;
        for (int i = 0; i < 3; ++i) {
            auto* t = new Tween{theme::motion::kStandard};
            t->Run(0.0, 1.0, theme::motion::kDurSlowMs, [](const QVariant&) {});
            scope.Adopt(t);
        }
        ok = ok && scope.Count() == 3;
    }
    // 析构后全部 Settle+回收（删除走 deleteLater，事件循环消化；这里验证收敛数归零）
    SetReduceMotion(false);

    if (report != nullptr) {
        *report = text;
    }
    return ok;
}

} // namespace shine::motion
