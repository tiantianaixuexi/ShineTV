#pragma once
// 初始化链（实现入口 src/novel/NovelInit.h；门禁契约见 docs/20-contracts/novel-state.md）：
//   * I1–I16 流水线逐阶段展示（阶段名 / 产物 / 状态 / 可断点续跑）+ 一键跑骨架
//     （`NovelInit::RunInitSkeleton`，路径 B/D，不调 LLM）→ 跑门禁（`CheckInitGate`）→
//     `InitReport` 逐条呈现 n_id / detail / fix_hint（门禁挡住时列具体哪条 + 修复建议）。
//   * 门禁规则可配置 + 人工审批：N1–N14 每条可配置（enforce 启用 / ignore-approved 忽略并人工确认）；
//     「忽略」**必须**走人工审批动作（`ApproveGate` 显式确认 + 记 `audit_logs`）——
//     不允许警告后放行：未审批的忽略申请仍按不通过计；获批的规则在报告里标「人工确认放行」。
//     配置持久化到书的 `work/init_gates.json`，重启读回。
//   * 前情导入（多小说与分卷-架构决策 §7，写时导入为主）：选前作书（project::ListBooks 里非当前书）
//     → **只读**打开前作 db/novel.db 查询（一部一库，绝不写前作库）→ 前情摘要写本作
//     `work/init/prior_import.json` + 可追溯条目落库（条目带出处「(书, 章)」+ canonical 名，
//     S2 的 meta_json 身份约定）。预算护栏：摘要 ≤ budgetChars（默认 2000 字），
//     超限截断并在报告里明说「已截断 N 字」。
//   * 产物查看：阶段产物 `init/I*.json`（JsonTree；`10` §2.8 落盘约定，core 的 InitDir）。
// UI 状态规范（UI.md §3）：空态给下一步 / 跑中（loading）/ 门禁挡住（逐条 + fix_hint）。
// 颜色零内联（check-layers rule 3）：一律 theme::Current() token + widgets::TokenQColor() 运行时拼串。
#include "ui/kit/controls/WidgetCommon.h"

#include <QHash>
#include <QStringList>
#include <QWidget>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class QLabel;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

namespace shine::db::sqlite {
class Database;
}
namespace shine::project {
struct ProjectRef;
}
namespace shine::data {
class DataTable;
class JsonTree;
}
namespace shine::widgets {
class Button;
class EmptyState;
class NumberInput;
class Select;
} // namespace shine::widgets

namespace shine::app {

class InitChainView : public QWidget {
  public:
    explicit InitChainView(QWidget* parent = nullptr);
    ~InitChainView() override;

    // —— 数据接入：开「当前书」db/novel.db 的可写句柄（一部一库；lastNovel 空 = 默认书）。
    //    同时读回书的 work/init_gates.json（门禁配置）与 work/init/prior_import.json（前情导入）。
    void LoadFromRef(const project::ProjectRef& ref, const std::string& lastNovel);
    void CloseDb() noexcept;

    // —— 公开 API（UI 按钮与自动化探针同一路径）——
    // 一键跑骨架（I1–I16 的结构步：书名/文风/硬规则/卷/主线/谜团/世界级秘密 + N12/N13 种子）；
    // 成功后 init/I15_gate.json + I16_commit.json 落书根（可断点续跑：重跑幂等、已有产物保留）。
    bool RunSkeleton(QString* err);
    // 跑门禁（N1–N14 逐条）；report = 逐条判定文本（n_id / 判定 / detail / fix_hint / 人工确认放行）。
    // 返回值 = 门禁是否通过（可开写第 1 章）；失败**不允许警告后放行**。
    bool RunGates(QString* report);
    // 门禁规则配置：mode = "enforce"（启用）| "ignore-approved"（忽略并人工确认）。
    // ⚠️ 设为 ignore-approved 只是**申请**忽略，必须再走 `ApproveGate` 人工审批才放行。
    bool SetGateRule(const QString& nId, const QString& mode, QString* err);
    // 同上的布尔便捷重载（ignore=true → "ignore-approved"，false → "enforce"；UI/探针同路径）。
    bool SetGateRule(const QString& nId, bool ignore, QString* err);
    // 人工审批动作（显式确认）：批准「忽略并人工确认」→ 记 audit_logs(action='gate_ignore_approved')。
    bool ApproveGate(const QString& nId, QString* err);
    // 前情导入（决策 §7）：bookTitles = 前作书名（须在 project::ListBooks 且非当前书）；
    // budgetChars = 摘要字数上限（<=0 → 默认 2000）；report = 导入账（含「已截断 N 字」）。
    bool ImportPriorBooks(const QStringList& bookTitles, int budgetChars, QString* report);

    // —— 自动化探针（S4 判据；产品代码不用）——
    [[nodiscard]] QString StageProbe() const;  // I1–I16 逐阶段（代码/名称/产物/状态/可断点续跑）
    [[nodiscard]] QString GateProbe() const;   // N1–N14 逐条（模式/审批/判定/detail/fix_hint）
    [[nodiscard]] QString ImportProbe() const; // 前情导入产物 + 可追溯条目（出处/canonical）

  private:
    // 门禁规则配置（persist 到书的 work/init_gates.json）
    struct GateRuleCfg {
        QString mode = QStringLiteral("enforce"); // enforce | ignore-approved
        bool approved = false;                    // 人工审批（audit_logs）后才放行
        QString approvedAt;                       // 审批时刻（ISO-8601，审计展示用）
    };
    // 一条门禁判定（CheckInitGate 的失败项 + 配置裁决）
    struct GateLine {
        QString nId;
        QString name;
        bool rawPass = false; // CheckInitGate 的原始判定
        bool waived = false;  // ignore-approved 且已人工审批 → 人工确认放行
        bool pending = false; // 已申请忽略但未审批（仍按不通过计，不允许警告后放行）
        QString detail;
        QString fixHint;
    };
    // 前情导入的一条可追溯条目（出处「(书, 章)」+ canonical 名，S2 meta_json 身份约定）
    struct ImportEntry {
        QString canonical;
        QString kind;
        QString sourceBook; // 出处书（前作）
        int sourceCh = 0;   // 出处章
        QString source;     // 「(书, 章)」展示串
        QString excerpt;    // 摘录（进前情摘要文本）
        QString name;       // 落库实体名
    };

    [[nodiscard]] QWidget* BuildStagesArea();
    [[nodiscard]] QWidget* BuildGatesArea();
    [[nodiscard]] QWidget* BuildImportArea();
    void RebuildStages();
    void RebuildGates();
    void RebuildImportBooks();
    void ShowArtifact(const QString& fileName);
    void SetRunning(const QString& step, bool on);
    [[nodiscard]] std::vector<GateLine> EvaluateGates() const;
    void LoadGateConfig();
    void SaveGateConfig() const;
    [[nodiscard]] std::filesystem::path GateConfigPath() const;
    [[nodiscard]] std::filesystem::path PriorContextPath() const; // work/init/prior_import.json
    // 落库前情条目（本作库）：entities + meta_json 身份；幂等（同 canonical + source_book 不重复）
    bool PersistEntries(const std::vector<ImportEntry>& entries, int* written, QString* report) const;

    // —— 数据 ——
    std::unique_ptr<db::sqlite::Database> db_;
    std::unique_ptr<project::ProjectRef> refStore_;
    std::filesystem::path bookRoot_; // 书根（默认书 = 项目根）：init/ 产物落这里
    std::filesystem::path bookWork_; // 书的 work/：init_gates.json、init/prior_import.json
    std::filesystem::path bookDbPath_;
    std::string lastNovel_;
    QString bookTitle_;
    QString openError_;
    QHash<QString, GateRuleCfg> gateCfg_; // "N1".."N14"
    std::vector<GateLine> lastGates_;     // 最近一次 RunGates 的逐条结果
    bool gatesRan_ = false;
    QHash<QString, qint64> stageMs_; // 阶段耗时（最近一次跑骨架的整链耗时，逐阶段展示用）

    // —— 流水线区 ——
    QLabel* state_ = nullptr; // 空态 / 跑中 / 结果（UI.md §3）
    widgets::Button* runSkeletonBtn_ = nullptr;
    widgets::Button* runGatesBtn_ = nullptr;
    data::DataTable* stageTable_ = nullptr;
    QLabel* stageNote_ = nullptr;
    widgets::Select* artifactSel_ = nullptr;
    QStackedWidget* artifactStack_ = nullptr;
    widgets::EmptyState* artifactEmpty_ = nullptr;
    data::JsonTree* artifactTree_ = nullptr;

    // —— 门禁区（N1–N14 逐条 + 配置/审批动作）——
    QLabel* gateState_ = nullptr;
    QWidget* gateHost_ = nullptr;
    QVBoxLayout* gateCol_ = nullptr;
    struct GateRowUi {
        QString nId;
        QLabel* name = nullptr;
        widgets::ElidedLabel* verdict = nullptr; // 省略 + hover 全文 + 点击展开（长判定文本）
        QLabel* mode = nullptr;
        QPushButton* ignoreBtn = nullptr;  // 忽略并人工确认（申请）
        QPushButton* approveBtn = nullptr; // 人工审批（显式确认 + audit_logs）
        QPushButton* enforceBtn = nullptr; // 恢复启用
    };
    std::vector<GateRowUi> gateRows_;

    // —— 前情导入区 ——
    widgets::Select* priorSel_ = nullptr;
    widgets::NumberInput* budget_ = nullptr;
    widgets::Button* importBtn_ = nullptr;
    QLabel* importResult_ = nullptr;
    QStackedWidget* importStack_ = nullptr;
    widgets::EmptyState* importEmpty_ = nullptr;
    data::JsonTree* importTree_ = nullptr;
};

} // namespace shine::app
