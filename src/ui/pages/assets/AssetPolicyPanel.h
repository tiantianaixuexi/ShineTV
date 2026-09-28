#pragma once
// P05-S4 资产依赖策略：策略 C（缺依赖挂起）为主，超期后策略 B（无参考图降级）兜底；
// 严格模式关闭策略 B。面板只编辑策略，不直接执行任务。
#include <QWidget>
#include <QString>


#include <cstdint>

class QLabel;

namespace shine::widgets {
class NumberInput;
class Toggle;
} // namespace shine::widgets

namespace shine::app {

struct AssetPolicy {
    std::int64_t suspendTimeoutMs = 30 * 60 * 1000;
    bool allowDegrade = true;
};

class AssetPolicyPanel : public QWidget {
  public:
    explicit AssetPolicyPanel(QWidget* parent = nullptr);

    [[nodiscard]] AssetPolicy Policy() const;
    void SetPolicy(const AssetPolicy& policy);
    void SetRuntimeState(const QString& layer, const QString& phase, const QString& detail,
                         bool active, bool degraded);
    [[nodiscard]] QString PolicyProbe() const;

  private:
    void Refresh();

    widgets::Toggle* allow_degrade_ = nullptr;
    widgets::NumberInput* timeout_minutes_ = nullptr;
    QLabel* summary_ = nullptr;
    QLabel* runtime_ = nullptr;
    QString runtime_phase_;
    QString runtime_detail_;
    QString runtime_layer_;
    bool runtime_active_ = false;
    bool runtime_degraded_ = false;
};

} // namespace shine::app
