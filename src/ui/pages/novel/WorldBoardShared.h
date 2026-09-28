#pragma once
// P04 设定台共享件（S2/S3 共用）：
//   * kind（31 种元类别）/ 实体状态 / 关系类型（`01` §2.4.1 受检 14 类）/ 剧情与谜团节拍 / 秘密范围 的中英标签；
//   * 伏笔状态机（`01` §2.3.3，不变式 I8）：PLANNED→PLANTED→DEVELOPING→REVEALED→RESOLVED，
//     逐段前进 + 唯一例外「短伏笔 PLANNED 直接跳 REVEALED」；只许前进不得回退。core 不强制
//   （UpsertForeshadow 只写串），合法性判定与中文原因在 app 层（本头）收口，UI 与探针同路径。
//   * Select 聚合助手 FirstChecked；布局工具见 util/QtLayout.h，色串见 widget/theme/CssColor.h。
// 颜色零内联（check-layers rule 3）：一律 theme token + widgets::TokenQColor() 运行时拼串。
#include "ui/layout/QtLayout.h"
#include "ui/kit/controls/Inputs.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "ui/kit/theme/CssColor.h"

#include <QColor>
#include <QLabel>
#include <QLayout>
#include <QString>

#include <array>
#include <string>
#include <string_view>

namespace shine::app {

// —— kind 目录（稳定类别顺序 = NovelTypes.h；数据语义见 docs/20-contracts/novel-state.md）——
struct KindInfo {
    std::string_view key;
    const char* label;
    const char* group;
};

inline constexpr KindInfo kKinds[] = {
    {"universe", "宇宙", "世界"},
    {"world_rule", "世界法则", "世界"},
    {"history", "历史", "世界"},
    {"culture", "文化", "世界"},
    {"language", "语言", "世界"},
    {"religion", "宗教", "世界"},
    {"economy", "经济", "世界"},
    {"tech", "科技", "世界"},
    {"society", "社会", "世界"},
    {"calendar", "历法", "世界"},
    {"person", "人物", "人物/生物"},
    {"creature", "生物", "人物/生物"},
    {"clothing", "服饰", "装备"},
    {"prop", "道具", "装备"},
    {"treasure", "珍宝", "装备"},
    {"item", "物品", "装备"},
    {"location", "地点", "空间/势力"},
    {"faction", "势力", "空间/势力"},
    {"power_system", "力量体系", "能力"},
    {"ability", "能力", "能力"},
    {"event", "事件", "叙事"},
    {"plot", "剧情线", "叙事"},
    {"arc", "弧光", "叙事"},
    {"conflict", "冲突", "叙事"},
    {"theme", "主题", "叙事"},
    {"motif", "意象", "叙事"},
    {"ending", "结局", "叙事"},
    {"secret", "秘密", "谜"},
    {"foreshadowing", "伏笔", "谜"},
    {"mystery", "谜", "谜"},
    {"resource", "资源", "资源"},
};

[[nodiscard]] inline QString KindLabelOf(std::string_view key) {
    for (const KindInfo& k : kKinds) {
        if (k.key == key) {
            return QString::fromUtf8(k.label);
        }
    }
    return QString::fromStdString(std::string{key});
}

[[nodiscard]] inline std::string KindKeyOfLabel(const QString& label) {
    if (label.startsWith(QStringLiteral("不限"))) {
        return {};
    }
    for (const KindInfo& k : kKinds) {
        if (label == QString::fromUtf8(k.label)) {
            return std::string{k.key};
        }
    }
    return label.toStdString();
}

[[nodiscard]] inline bool IsKnownKind(std::string_view key) {
    for (const KindInfo& k : kKinds) {
        if (k.key == key) {
            return true;
        }
    }
    return false;
}

// 实体状态（EntityRow.status：active|dead|destroyed|archived）→ 中文 + Tag tone
[[nodiscard]] inline QString StatusLabelOf(std::string_view status) {
    if (status == "dead") return QStringLiteral("已故");
    if (status == "destroyed") return QStringLiteral("已毁");
    if (status == "archived") return QStringLiteral("归档");
    return QStringLiteral("活跃");
}

[[nodiscard]] inline const char* StatusToneOf(std::string_view status) {
    if (status == "dead") return "info";
    if (status == "destroyed") return "danger";
    if (status == "archived") return "";
    return "ok";
}

[[nodiscard]] inline std::string StatusKeyOfLabel(const QString& label) {
    if (label == QStringLiteral("已故")) return "dead";
    if (label == QStringLiteral("已毁")) return "destroyed";
    if (label == QStringLiteral("归档")) return "archived";
    return "active";
}

// —— 关系类型（`01` §2.4.1 收敛的固定集，扩展需登记；界面不放行自由串）——
struct RelInfo {
    std::string_view key;
    std::string_view label;
};

inline constexpr RelInfo kRelTypes[] = {
    {"family", "家人 family"},        {"love", "恋人 love"},
    {"friend", "朋友 friend"},        {"ally", "盟友 ally"},
    {"enemy", "仇敌 enemy"},          {"rival", "对手 rival"},
    {"mentor", "导师 mentor"},        {"student", "学生 student"},
    {"subordinate", "下属 subordinate"}, {"superior", "上级 superior"},
    {"member_of", "成员 member_of"},  {"owns", "持有 owns"},
    {"counter", "克制 counter"},      {"knows_secret", "知情 knows_secret"},
};

[[nodiscard]] inline QString RelLabelOf(std::string_view key) {
    for (const RelInfo& r : kRelTypes) {
        if (r.key == key) {
            return QString::fromUtf8(r.label.data(), static_cast<qsizetype>(r.label.size()));
        }
    }
    return QString::fromStdString(std::string{key});
}

[[nodiscard]] inline std::string RelKeyOfLabel(const QString& label) {
    for (const RelInfo& r : kRelTypes) {
        if (label == QString::fromUtf8(r.label.data(), static_cast<qsizetype>(r.label.size()))) {
            return std::string{r.key};
        }
    }
    return label.toStdString();
}

[[nodiscard]] inline bool IsKnownRelType(std::string_view key) {
    for (const RelInfo& r : kRelTypes) {
        if (r.key == key) {
            return true;
        }
    }
    return false;
}

// —— 剧情线节拍（`01` §2.5：plot_beats.beat_type 7 值）——
inline constexpr std::array<std::string_view, 7> kPlotBeatTypes = {
    "setup", "rising", "turning_point", "climax", "falling", "resolution", "revelation"};
inline constexpr std::array<std::string_view, 7> kPlotBeatLabels = {
    "setup 铺垫", "rising 上升", "turning_point 转折", "climax 高潮",
    "falling 下落", "resolution 收束", "revelation 揭示"};

[[nodiscard]] inline QString PlotBeatLabelOf(std::string_view key) {
    for (std::size_t i = 0; i < kPlotBeatTypes.size(); ++i) {
        if (kPlotBeatTypes[i] == key) {
            return QString::fromUtf8(kPlotBeatLabels[i].data(),
                                     static_cast<qsizetype>(kPlotBeatLabels[i].size()));
        }
    }
    return QString::fromStdString(std::string{key});
}

[[nodiscard]] inline std::string PlotBeatKeyOfLabel(const QString& label) {
    for (std::size_t i = 0; i < kPlotBeatLabels.size(); ++i) {
        if (label == QString::fromUtf8(kPlotBeatLabels[i].data(),
                                       static_cast<qsizetype>(kPlotBeatLabels[i].size()))) {
            return std::string{kPlotBeatTypes[i]};
        }
    }
    return label.toStdString();
}

[[nodiscard]] inline bool IsKnownPlotBeat(std::string_view key) {
    for (std::string_view k : kPlotBeatTypes) {
        if (k == key) {
            return true;
        }
    }
    return false;
}

// —— 谜团节拍（`01` §2.3.4：mystery_beats.beat_type 5 值）——
inline constexpr std::array<std::string_view, 5> kMysteryBeatTypes = {
    "question", "hint", "reveal", "answer", "red_herring"};
inline constexpr std::array<std::string_view, 5> kMysteryBeatLabels = {
    "question 提问", "hint 线索", "reveal 揭示", "answer 答案", "red_herring 误导"};

[[nodiscard]] inline QString MysteryBeatLabelOf(std::string_view key) {
    for (std::size_t i = 0; i < kMysteryBeatTypes.size(); ++i) {
        if (kMysteryBeatTypes[i] == key) {
            return QString::fromUtf8(kMysteryBeatLabels[i].data(),
                                     static_cast<qsizetype>(kMysteryBeatLabels[i].size()));
        }
    }
    return QString::fromStdString(std::string{key});
}

[[nodiscard]] inline std::string MysteryBeatKeyOfLabel(const QString& label) {
    for (std::size_t i = 0; i < kMysteryBeatLabels.size(); ++i) {
        if (label == QString::fromUtf8(kMysteryBeatLabels[i].data(),
                                       static_cast<qsizetype>(kMysteryBeatLabels[i].size()))) {
            return std::string{kMysteryBeatTypes[i]};
        }
    }
    return label.toStdString();
}

[[nodiscard]] inline bool IsKnownMysteryBeat(std::string_view key) {
    for (std::string_view k : kMysteryBeatTypes) {
        if (k == key) {
            return true;
        }
    }
    return false;
}

// 谜团状态（MysteryRow.status：open|hinted|revealed|resolved）
[[nodiscard]] inline QString MysteryStatusLabelOf(std::string_view status) {
    if (status == "hinted") return QStringLiteral("有线索 hinted");
    if (status == "revealed") return QStringLiteral("已揭示 revealed");
    if (status == "resolved") return QStringLiteral("已收束 resolved");
    return QStringLiteral("未解 open");
}

// —— 秘密范围（SecretRow.scope：world|character|faction）——
inline constexpr std::array<std::string_view, 3> kSecretScopes = {"world", "character", "faction"};
inline constexpr std::array<std::string_view, 3> kSecretScopeLabels = {
    "世界 world", "角色 character", "势力 faction"};

[[nodiscard]] inline QString SecretScopeLabelOf(std::string_view key) {
    for (std::size_t i = 0; i < kSecretScopes.size(); ++i) {
        if (kSecretScopes[i] == key) {
            return QString::fromUtf8(kSecretScopeLabels[i].data(),
                                     static_cast<qsizetype>(kSecretScopeLabels[i].size()));
        }
    }
    return QString::fromStdString(std::string{key});
}

[[nodiscard]] inline std::string SecretScopeKeyOfLabel(const QString& label) {
    for (std::size_t i = 0; i < kSecretScopeLabels.size(); ++i) {
        if (label == QString::fromUtf8(kSecretScopeLabels[i].data(),
                                       static_cast<qsizetype>(kSecretScopeLabels[i].size()))) {
            return std::string{kSecretScopes[i]};
        }
    }
    return label.toStdString();
}

[[nodiscard]] inline bool IsKnownSecretScope(std::string_view key) {
    for (std::string_view k : kSecretScopes) {
        if (k == key) {
            return true;
        }
    }
    return false;
}

// —— 伏笔状态机（`01` §2.3.3 不变式 I8）——
namespace fstate {

inline constexpr std::array<std::string_view, 5> kStates = {"PLANNED", "PLANTED", "DEVELOPING",
                                                            "REVEALED", "RESOLVED"};
inline constexpr std::array<std::string_view, 5> kLabels = {
    "计划 PLANNED", "已埋 PLANTED", "推进中 DEVELOPING", "已揭示 REVEALED", "已收束 RESOLVED"};
inline constexpr std::array<std::string_view, 5> kPhrases = {"计划未埋", "已埋待推", "推进中",
                                                             "已揭示待收束", "已收束"};

// 状态 → 段序号（1..5；0 = 未知状态）
[[nodiscard]] inline int SegOf(std::string_view status) {
    for (int i = 0; i < static_cast<int>(kStates.size()); ++i) {
        if (kStates[static_cast<std::size_t>(i)] == status) {
            return i + 1;
        }
    }
    return 0;
}

[[nodiscard]] inline QString LabelOf(std::string_view status) {
    const int seg = SegOf(status);
    return seg > 0 ? QString::fromUtf8(kLabels[static_cast<std::size_t>(seg - 1)].data(),
                                       static_cast<qsizetype>(kLabels[static_cast<std::size_t>(seg - 1)].size()))
                   : QString::fromStdString(std::string{status});
}

[[nodiscard]] inline QString PhraseOf(std::string_view status) {
    const int seg = SegOf(status);
    return seg > 0 ? QString::fromUtf8(kPhrases[static_cast<std::size_t>(seg - 1)].data(),
                                       static_cast<qsizetype>(kPhrases[static_cast<std::size_t>(seg - 1)].size()))
                   : QStringLiteral("状态未知");
}

// 合法跃迁（`01` §2.3.3）：沿主链前进 1 步；唯一例外「短伏笔」PLANNED→REVEALED
[[nodiscard]] inline bool Legal(int fromSeg, int toSeg) {
    return toSeg == fromSeg + 1 || (fromSeg == 1 && toSeg == 4);
}

// 默认下一状态 key（推进一步）；空 = 已是终点 RESOLVED
[[nodiscard]] inline std::string_view NextOf(std::string_view from) {
    const int seg = SegOf(from);
    if (seg <= 0 || seg >= static_cast<int>(kStates.size())) {
        return {};
    }
    return kStates[static_cast<std::size_t>(seg)];
}

// 目标跃迁校验：空串 = 合法；否则 = 中文原因（原因 + 出路）
[[nodiscard]] inline QString TransitionError(std::string_view from, std::string_view to) {
    const int fs = SegOf(from);
    const int ts = SegOf(to);
    const QString fromView = LabelOf(from);
    const QString toView = LabelOf(to);
    if (ts == 0) {
        return QStringLiteral("目标状态：'%1' 不在伏笔状态机五态（PLANNED/PLANTED/DEVELOPING/"
                              "REVEALED/RESOLVED）内（`01` §2.3.3）。请从下拉里选合法目标。")
            .arg(QString::fromStdString(std::string{to}));
    }
    if (fs == 0) {
        return QStringLiteral("当前状态：'%1' 不在伏笔状态机五态内，无法判定跃迁合法性；"
                              "请先把 foreshadowings.status 修成合法状态再推进。")
            .arg(QString::fromStdString(std::string{from}));
    }
    if (Legal(fs, ts)) {
        return {}; // 合法：逐段前进 1 步，或短伏笔 PLANNED→REVEALED（`01` §2.3.3）
    }
    if (ts == fs) {
        const std::string_view next = NextOf(from);
        return QStringLiteral("无需跃迁：已是 %1。%2")
            .arg(fromView,
                 next.empty() ? QStringLiteral("这条伏笔已收束（状态机终点）。")
                              : QStringLiteral("要推进请选下一状态「%1」。").arg(LabelOf(next)));
    }
    if (ts < fs) {
        return QStringLiteral("不得回退：%1 → %2 —— 伏笔状态机（`01` §2.3.3 不变式 I8）只许沿 "
                              "PLANNED→PLANTED→DEVELOPING→REVEALED→RESOLVED 方向推进，"
                              "回退视为世界状态冲突（进 `07` 的冲突处理）。")
            .arg(fromView, toView);
    }
    return QStringLiteral("非法跃迁：%1 → %2 —— 只能逐段推进（PLANNED→PLANTED→DEVELOPING→"
                          "REVEALED→RESOLVED），或短伏笔 PLANNED 直接跳 REVEALED（`01` §2.3.3）。"
                          "%3")
        .arg(fromView, toView,
             (fs == 1 && ts > 2) ? QStringLiteral("若这是短伏笔，请改为跃迁到 REVEALED。")
                                 : QStringLiteral("%1 的合法下一状态是「%2」。")
                                       .arg(fromView, LabelOf(NextOf(from))));
}

} // namespace fstate

// —— Select 聚合 ——

[[nodiscard]] inline QString FirstChecked(const widgets::Select& sel) {
    const std::vector<QString> got = sel.Checked();
    return got.empty() ? QString{} : got.front();
}

using util::ClearLayout;
using util::ElideText;
using util::SectionLabel;
using widget::CssRgb;

} // namespace shine::app
