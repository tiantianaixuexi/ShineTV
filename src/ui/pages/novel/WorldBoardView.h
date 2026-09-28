#pragma once
// P04-S2/S3 设定台（PLAN §5 S2/S3 / UI.md §2.2）：页签 [实体][关系][剧情线][伏笔][谜][字段]。
//   P04-S2「实体」「字段」：31 种 kind 分组实体卡 + 动态字段三步校验登记（别名解析 / 类型合法 / 重复键·上限）。
//   P04-S3「关系」「剧情线」「伏笔」「谜」四页 + 知情边界矩阵：
//     · 关系页：from/to/rel_type（`01` §2.4.1 受检 14 类）/生效章区间 登记；点行 → 两端实体卡摘要；
//     · 剧情线页：Plot 列表（kind/title/target_ch）+ PlotBeat 行内列表 + 新建；
//     · 伏笔页：每条伏笔一条五段 Timeline（PLANNED→PLANTED→DEVELOPING→REVEALED→RESOLVED，
//       `01` §2.3.3 状态机）+ 推进按钮（只给合法跃迁）+ 高级指定跃迁（非法跃迁给中文原因）；
//     · 谜页：Mystery 列表 + MysteryBeat 节拍；页下半 = 知情边界矩阵（角色 × 秘密），
//       勾「知道/不知道」+ 章过滤查「第 N 章时谁知道什么」（`01` §2.3.2 规范查询）。
//   知情边界归属「谜」页下半的理由：`01` §1.1.2 把 secret/foreshadowing/mystery 同归「谜」域，
//   §2.3.4 定义「知情 = 角色侧边界」落 secret_knowledge/character_knowledge —— 矩阵随秘密走；
//   且 UI.md §2.2 页签固定六页，不为此加第 7 页签。
// 跨库稳定身份（多小说与分卷-架构决策 §7，2026-09-24 定案）：
//   entities.meta_json 约定 {"canonical":"…","aliases":[…],"source_book":"…","source_ch":N} ——
//   不改 entities 表结构（meta_json 承载）；Drawer 展示 canonical / 别名 / 出处「(书, 章)」。
// 颜色零内联（check-layers rule 3）：一律 theme::Current() token + widgets::TokenQColor() 拼串。
#include "ui/kit/controls/WidgetCommon.h"
#include "novel/NovelTypes.h"

#include <QHash>
#include <QPointer>
#include <QWidget>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class QLabel;
class QGridLayout;
class QPushButton;
class QVBoxLayout;
class QStackedWidget;

namespace shine::db::sqlite {
class Database;
}
namespace shine::project {
struct ProjectRef;
}
namespace shine::data {
class DataTable;
class Timeline;
}
namespace shine::widgets {
class Checkbox;
class Drawer;
class Field;
class NumberInput;
class SearchBox;
class Select;
class Tabs;
class TextArea;
class TextInput;
} // namespace shine::widgets

namespace shine::app {

class WorldBoardView : public QWidget {
  public:
    explicit WorldBoardView(QWidget* parent = nullptr);
    ~WorldBoardView() override;

    // —— 数据接入：开「当前书」db/novel.db 的可写句柄。书 = project::ListBooks（一部一库），
    //    lastNovel 空 = 默认书。重复调用 = 关旧库、开新实例（探针「关库重开」走这条）。
    void LoadFromRef(const project::ProjectRef& ref, const std::string& lastNovel);
    void CloseDb() noexcept;

    // —— 公开 API（新建/登记表单与自动化探针走同一路径）——
    // 自动化/截图包：0 实体 / 1 关系 / 2 剧情线 / 3 伏笔 / 4 谜与知情 / 5 字段。
    void ShowPage(int index);
    // status 空 = active（active|dead|destroyed|archived）；meta_json 按决策 §7 约定承载身份。
    bool CreateEntity(const QString& name, const QString& kind, const QString& summary,
                      const QString& canonical, const QStringList& aliases,
                      const QString& sourceBook, int sourceCh, QString* err,
                      const QString& status = QString{});
    bool RefreshEntities();
    // 动态字段新建登记（人工登记 → CANON）；enumJson 为 JSON 字符串数组（如 ["a","b"]）。
    bool RegisterField(const QString& fieldKey, const QString& scope, const QString& entityKind,
                       const QString& title, const QString& valueType, const QString& enumJson,
                       const QString& description, QString* err);
    // 实体动态字段值编辑保存（Drawer 里「已登记字段」行的保存按钮）。
    bool SaveEntityField(qint64 entityId, const QString& fieldKey, const QString& valueText,
                         const QString& valueJson, const QString& layer, const QString& note,
                         QString* err);
    bool RefreshFields();

    // —— 关系页（P04-S3）：时间化关系边登记（`01` §2.4.1）——
    // relType 须在受检 14 类内（family/love/friend/…/knows_secret）；toChapter=0 = 至今仍有效。
    bool CreateRelation(qint64 fromId, qint64 toId, const QString& relType, int strength,
                        qint64 fromChapter, qint64 toChapter, const QString& reason, QString* err);
    bool RefreshRelations();

    // —— 剧情线页（P04-S3）：Plot + PlotBeat（`01` §2.5）——
    bool CreatePlot(const QString& kind, const QString& title, qint64 introCh, qint64 targetCh,
                    const QString& note, QString* err);
    // beatType 须在 7 值内（setup|rising|turning_point|climax|falling|resolution|revelation）
    bool CreatePlotBeat(qint64 plotId, qint64 chapterId, int ord, const QString& beatType,
                        const QString& title, const QString& summary, QString* err);
    bool RefreshPlots();

    // —— 伏笔页（P04-S3）：五段状态机（`01` §2.3.3 不变式 I8）——
    bool CreateForeshadow(const QString& title, const QString& content, qint64 setupCh,
                          qint64 payoffCh, int importance, const QString& truth, QString* err);
    // 推进到状态机默认下一状态（PLANNED→PLANTED→…）；终点/非法给中文原因
    bool AdvanceForeshadow(qint64 id, QString* err);
    // 指定目标状态的跃迁入口（UI 高级跃迁与探针同路径）：合法保存成功，非法拦下给中文原因
    bool TransitionForeshadow(qint64 id, const QString& toStatus, QString* err);
    bool RefreshForeshadows();
    // 探针：每条伏笔的状态 + 当前段序号（与五段 Timeline 的高亮段同源）
    [[nodiscard]] QString ForeshadowProbe() const;

    // —— 谜页 + 知情边界矩阵（P04-S3）——
    bool CreateMystery(const QString& question, const QString& answer, qint64 askCh,
                       qint64 answerCh, int importance, QString* err);
    // beatType 须在 5 值内（question|hint|reveal|answer|red_herring）
    bool CreateMysteryBeat(qint64 mysteryId, const QString& beatType, qint64 chapterId,
                           const QString& content, QString* err);
    bool CreateSecret(const QString& content, const QString& truth, qint64 revealCh,
                      const QString& revealCondition, qint64 entityId, const QString& scope,
                      QString* err);
    // 矩阵勾「知道/不知道」：character_knowledge + secret_knowledge 双落（`01` §2.4.5）；
    // chapter = 生效章（0 = 书前即知）
    bool SetKnows(qint64 entityId, qint64 secretId, bool knows, qint64 chapter, QString* err);
    // 「第 N 章时谁知道什么」（`01` §2.3.2 规范查询；chapter<=0 = 不限章时点）
    [[nodiscard]] bool WhoKnowsWhat(int chapter, QString* probe) const;
    bool RefreshMysteries();

    // —— 自动化探针（S2/S3 判据；产品代码不用）——
    [[nodiscard]] QString BookProbe() const;
    [[nodiscard]] int EntityCount(const QString& kind) const;

  private:
    [[nodiscard]] QWidget* BuildEntitiesPage();
    [[nodiscard]] QWidget* BuildFieldsPage();
    [[nodiscard]] QWidget* BuildRelationsPage();   // P04-S3
    [[nodiscard]] QWidget* BuildPlotsPage();       // P04-S3
    [[nodiscard]] QWidget* BuildForeshadowPage();  // P04-S3
    [[nodiscard]] QWidget* BuildMysteryPage();     // P04-S3（下半 = 知情边界矩阵）
    void RebuildKindBadges();
    void RebuildCards();
    void RebuildRelations();   // P04-S3
    void RebuildPlots();       // P04-S3
    void RebuildForeshadows(); // P04-S3
    void RebuildMysteries();   // P04-S3
    void RebuildMatrix();      // P04-S3（角色 × 秘密）
    void OpenEntityDrawer(qint64 id);
    void OpenRelationDrawer(qint64 relationId); // P04-S3：两端实体卡摘要
    void ToggleCreateForm(bool show);
    void ToggleRegisterForm(bool show);
    void SubmitCreateEntity();
    void SubmitRegisterField();
    void SubmitCreateRelation();   // P04-S3
    void SubmitCreatePlot();       // P04-S3
    void SubmitCreateForeshadow(); // P04-S3
    void SubmitCreateMystery();    // P04-S3
    void SubmitCreateSecret();     // P04-S3
    void ClearFormErrors();

    // 整表只读（core 无 ListAll* 的表：relations/foreshadowings/secrets —— app 层直查，
    // 与 S2 的 RebuildKindBadges / EntityCount 同款；写入仍走 NovelGraph）
    [[nodiscard]] std::vector<novelcore::RelationRow> ReadRelations() const;
    [[nodiscard]] std::vector<novelcore::ForeshadowRow> ReadForeshadows() const;
    [[nodiscard]] std::vector<novelcore::SecretRow> ReadSecrets() const;

    // —— 数据 ——
    std::unique_ptr<db::sqlite::Database> db_;
    std::unique_ptr<project::ProjectRef> refStore_; // 撑重试（ErrorState「重试」）
    std::filesystem::path bookDbPath_;
    std::string lastNovel_;
    QString bookTitle_;
    QString openError_;
    QString kindFilter_; // 空 = 全部
    QString nameFilter_;

    // —— 页签与实体页 ——
    widgets::Tabs* tabs_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    std::vector<QPushButton*> kindBtns_; // [0]=全部，其后 = 31 kind（目录序）
    widgets::SearchBox* search_ = nullptr;
    QLabel* entityCount_ = nullptr;
    QWidget* cardsHost_ = nullptr;
    QGridLayout* cardsGrid_ = nullptr;
    QWidget* createForm_ = nullptr;
    widgets::TextInput* fName_ = nullptr;
    widgets::Select* fKind_ = nullptr;
    widgets::TextArea* fSummary_ = nullptr;
    widgets::Select* fStatus_ = nullptr;
    widgets::TextInput* fCanonical_ = nullptr;
    widgets::TextInput* fAliases_ = nullptr;
    widgets::TextInput* fSourceBook_ = nullptr;
    widgets::NumberInput* fSourceCh_ = nullptr;
    widgets::Field* fwName_ = nullptr;
    widgets::Field* fwKind_ = nullptr;
    widgets::Field* fwStatus_ = nullptr;
    widgets::Field* fwSummary_ = nullptr;
    widgets::Field* fwSourceCh_ = nullptr;

    // —— 字段页 ——
    QWidget* registerForm_ = nullptr;
    widgets::TextInput* gKey_ = nullptr;
    widgets::Select* gScope_ = nullptr;
    widgets::Select* gEntityKind_ = nullptr;
    widgets::TextInput* gTitle_ = nullptr;
    widgets::Select* gType_ = nullptr;
    widgets::TextInput* gEnum_ = nullptr;
    widgets::TextInput* gDesc_ = nullptr;
    widgets::Field* gwKey_ = nullptr;
    widgets::Field* gwScope_ = nullptr;
    widgets::Field* gwEntityKind_ = nullptr;
    widgets::Field* gwType_ = nullptr;
    widgets::Field* gwEnum_ = nullptr;
    widgets::ElidedLabel* fieldCount_ = nullptr;
    data::DataTable* fieldTable_ = nullptr;

    // —— P04-S3 四页控件组（按页聚合，避免成员平铺）——
    struct RelationUi {
        QWidget* form = nullptr;
        widgets::Select* fromSel = nullptr;
        widgets::Select* toSel = nullptr;
        widgets::Select* typeSel = nullptr;
        widgets::NumberInput* strength = nullptr;
        widgets::NumberInput* fromCh = nullptr;
        widgets::NumberInput* toCh = nullptr;
        widgets::TextInput* reason = nullptr;
        widgets::Field* wFrom = nullptr;
        widgets::Field* wTo = nullptr;
        widgets::Field* wType = nullptr;
        QLabel* count = nullptr;
        QWidget* listHost = nullptr;
        QVBoxLayout* listCol = nullptr;
        QHash<QString, qint64> entityIds; // 「名称（#id）」→ 实体 id（Select 只回文本）
    };

    struct PlotUi {
        QWidget* form = nullptr;
        widgets::Select* kindSel = nullptr;
        widgets::TextInput* title = nullptr;
        widgets::NumberInput* introCh = nullptr;
        widgets::NumberInput* targetCh = nullptr;
        widgets::TextInput* note = nullptr;
        widgets::Field* wKind = nullptr;
        widgets::Field* wTitle = nullptr;
        QLabel* count = nullptr;
        QWidget* listHost = nullptr;
        QVBoxLayout* listCol = nullptr;
    };

    struct ForeshadowUi {
        QWidget* form = nullptr;
        widgets::TextInput* title = nullptr;
        widgets::TextArea* content = nullptr;
        widgets::NumberInput* setupCh = nullptr;
        widgets::NumberInput* payoffCh = nullptr;
        widgets::NumberInput* importance = nullptr;
        widgets::TextInput* truth = nullptr;
        widgets::Field* wTitle = nullptr;
        widgets::Field* wSetupCh = nullptr;
        QLabel* count = nullptr;
        QWidget* listHost = nullptr;
        QVBoxLayout* listCol = nullptr;
    };

    struct MysteryUi {
        QWidget* form = nullptr;
        widgets::TextInput* question = nullptr;
        widgets::TextInput* answer = nullptr;
        widgets::NumberInput* askCh = nullptr;
        widgets::NumberInput* answerCh = nullptr;
        widgets::NumberInput* importance = nullptr;
        widgets::Field* wQuestion = nullptr;
        QLabel* count = nullptr;
        QWidget* listHost = nullptr;
        QVBoxLayout* listCol = nullptr;
        // 知情边界矩阵（角色 × 秘密）
        QWidget* secretForm = nullptr;
        widgets::TextInput* sContent = nullptr;
        widgets::TextInput* sTruth = nullptr;
        widgets::NumberInput* sRevealCh = nullptr;
        widgets::TextInput* sRevealCond = nullptr;
        widgets::Select* sScope = nullptr;
        widgets::Field* wSContent = nullptr;
        widgets::Field* wSScope = nullptr;
        widgets::NumberInput* queryCh = nullptr;     // 查询章：第 N 章时谁知道什么（0 = 不限）
        widgets::NumberInput* effectiveCh = nullptr; // 生效章：勾选记为第 N 章知道（0 = 书前即知）
        QLabel* matrixNote = nullptr;
        QWidget* matrixHost = nullptr;
        QGridLayout* matrixGrid = nullptr;
    };

    RelationUi rel_;
    PlotUi plot_;
    ForeshadowUi fs_;
    MysteryUi ms_;

    QPointer<widgets::Drawer> drawer_;
};

} // namespace shine::app
