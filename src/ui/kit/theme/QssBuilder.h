#pragma once
// shine::theme::QssBuilder —— Token → 整张 QSS（P02-S2）
//
// 约束（判据 S2）：生成的 QSS 里**没有任何字面颜色** —— 模板只含 %1..%28 占位符，
// 与 kColorTokenNames 序一一对应（%1 bg.void … %28 shadow.scrim）；
// 替换按**降序**（%28→%1）进行，避免 %1 误伤 %10/%11（落点明文要求）。
// 内联硬编码色值另有 tools/check-layers.ps1 rule 3 拦截。
#include "ui/kit/theme/Token.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace shine::theme {

class QssBuilder {
  public:
    // Token → 全局 QSS 文本（覆盖 QWidget/QPushButton/QListView/QScrollBar/QMenu/QToolTip…）
    [[nodiscard]] static std::string Build(const ColorToken& c);

    // S2 自检：QSS 里每个色值必须等于某个 Token 的发射串（#RRGGBB 或 rgba(...)）
    [[nodiscard]] static bool SelfCheck(const ColorToken& c, std::string* detail);

    // 调试：把生成的 QSS 落盘（SHINE_QSS_DUMP=<路径> 用）
    [[nodiscard]] static bool DumpToFile(const ColorToken& c, const std::filesystem::path& path);
};

} // namespace shine::theme
