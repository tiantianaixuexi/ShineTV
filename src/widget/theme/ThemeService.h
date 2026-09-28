#pragma once
// shine::theme::ThemeService —— 主题切换 / 持久化 / 200ms 淡入（P02-S3）
//
// 判据（S3）：切换 4 主题各一次，界面即时换色、无闪烁、无残影。
// 防闪烁（P02 风险表 R6）：QSS **预生成常驻**（每主题一份缓存，首次用时构建），
// 切换用 `setUpdatesEnabled(false)…(true)` 包住整个换肤；换肤后窗口不透明度
// 0.85→1.0 淡入，时长取 `motion::kDurBaseMs`（200ms，总纲 §2.3）。
// 持久化：主题选择存应用配置目录 `theme.json`（QStandardPaths::AppDataLocation）。
// 「减少动效」开关（S4）接上后，淡入自动退化为瞬时。
#include "widget/theme/Theme.h"

class QApplication;

namespace shine::theme {

class ThemeService {
  public:
    // 载入主题数据（未载则载）+ 回读持久化选择 + 应用 QSS（幂等；QApplication 构造后调用）
    static void Initialize(QApplication& app);

    [[nodiscard]] static ThemeId Current();

    // 切换：预生成 QSS 常驻 → 更新禁用包夹换肤 → 200ms 淡入 → 持久化
    static void Switch(ThemeId id, bool animated = true);

    // —— P02-S8：自定义主题 / 实时预览 / 持久化增强 ——
    static void SwitchCustom(const std::string& name, bool animated = true);
    static bool SaveCustom(const std::string& name, const ColorToken& c); // 另存 + 入列
    static void Preview(const ColorToken& c);   // 样式编辑器实时预览（不持久化）
    static void RevertPreview();                // 取消预览（回当前主题）
    static void PersistCurrent();               // 持久化当前（含自定义标记 + 减少动效）
    static void SetReduceMotionPersisted(bool on); // 「减少动效」开关（持久化）

  private:
    [[nodiscard]] static const std::string& QssFor(ThemeId id);
    static void ApplyQss(const std::string& qss, bool animated);
    static void Persist(ThemeId id);
};

} // namespace shine::theme
