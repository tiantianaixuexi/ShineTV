#pragma once
// 叙事知识图谱查询/写入 API（给 P3/P4/P5 用）。写操作默认 PROPOSED + audit。
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "novel/NovelDb.h"
#include "novel/NovelTypes.h"

namespace shine::novelcore {

class NovelGraph {
public:
    explicit NovelGraph(db::sqlite::Database& db) noexcept : db_(&db) {}

    // —— 实体 ——
    [[nodiscard]] std::expected<RowId, DbError> UpsertEntity(const EntityRow& row);
    [[nodiscard]] std::expected<EntityRow, DbError> GetEntity(RowId id) const;
    [[nodiscard]] std::expected<std::vector<EntityRow>, DbError>
    ListEntities(std::string_view kind = {}, std::string_view nameFilter = {}, int limit = 200) const;

    // —— 关系 ——
    [[nodiscard]] std::expected<RowId, DbError> UpsertRelation(const RelationRow& row);
    [[nodiscard]] std::expected<std::vector<RelationRow>, DbError>
    GetRelations(RowId entityId, bool includeIncoming = true) const;

    // —— 人设 / 状态 ——
    [[nodiscard]] std::expected<void, DbError> UpsertPersona(const PersonaRow& row);
    [[nodiscard]] std::expected<PersonaRow, DbError> GetPersona(RowId entityId) const;
    [[nodiscard]] std::expected<RowId, DbError> UpsertCharacterStatus(const CharacterStatusRow& row);
    [[nodiscard]] std::expected<CharacterStatusRow, DbError>
    GetLatestCharacterStatus(RowId entityId, RowId chapterId = 0) const;

    // —— 章 / 卷 / 场 ——
    [[nodiscard]] std::expected<RowId, DbError> UpsertVolume(const VolumeRow& row);
    [[nodiscard]] std::expected<RowId, DbError> UpsertChapter(const ChapterRow& row);
    [[nodiscard]] std::expected<ChapterRow, DbError> GetChapter(RowId id) const;
    [[nodiscard]] std::expected<std::vector<ChapterRow>, DbError> ListChapters(int limit = 50) const;
    [[nodiscard]] std::expected<RowId, DbError> UpsertScene(const SceneRow& row);
    [[nodiscard]] std::expected<std::vector<SceneRow>, DbError> ListScenes(RowId chapterId) const;

    // —— 事件 / 因果 ——
    [[nodiscard]] std::expected<RowId, DbError> UpsertEvent(const EntityRow& entity,
                                                            const EventDetailRow& detail);
    [[nodiscard]] std::expected<RowId, DbError> UpsertCausalLink(const CausalLinkRow& row);
    // BFS 因果链（depth 跳），返回边
    [[nodiscard]] std::expected<std::vector<CausalLinkRow>, DbError>
    GetEventChain(RowId eventId, int depth = 3) const;

    // —— 伏笔 / 秘密 / 知情 ——
    [[nodiscard]] std::expected<RowId, DbError> UpsertForeshadow(const ForeshadowRow& row);
    [[nodiscard]] std::expected<std::vector<ForeshadowRow>, DbError>
    ListOpenForeshadows() const; // PLANNED|PLANTED|DEVELOPING
    [[nodiscard]] std::expected<RowId, DbError> UpsertSecret(const SecretRow& row);
    [[nodiscard]] std::expected<void, DbError> SetSecretKnowledge(RowId secretId, RowId entityId,
                                                                  int knows, RowId chapterKnown);
    [[nodiscard]] std::expected<std::vector<SecretRow>, DbError>
    GetSecretsFor(RowId entityId, RowId chapterId = 0) const;

    // —— P0 八表（S2）：原先只有表、没有 API，是闭环的阻断点 ——
    // 规格 `Doc/小说系统/01` §2.4.2 / §2.4.5 / §2.5 / §2.3.4；判据见 `00` §6.2 的 S2 行。

    // 知情（character_knowledge）：不变式 I2 与 `06` K05 的数据来源
    [[nodiscard]] std::expected<RowId, DbError> UpsertKnowledge(const CharacterKnowledgeRow& row);
    // chapterId > 0 → 只返回「该章时点已知」的行（chapter_known=0 或 <= chapterId）；0 = 不限章
    [[nodiscard]] std::expected<std::vector<CharacterKnowledgeRow>, DbError>
    ListKnowledge(RowId entityId, RowId chapterId = 0) const;
    // 「截至第 N 章是否知道 (fact_kind, fact_id)」—— `01` §2.3.2 的规范查询
    [[nodiscard]] std::expected<bool, DbError> CharacterKnows(RowId entityId,
                                                              std::string_view factKind, RowId factId,
                                                              RowId chapterId = 0) const;

    // 事件参与（event_participants）：`06` K04 dead_not_acting 的数据来源
    [[nodiscard]] std::expected<RowId, DbError>
    UpsertEventParticipant(const EventParticipantRow& row);
    [[nodiscard]] std::expected<std::vector<EventParticipantRow>, DbError>
    ListEventParticipants(RowId eventId) const;
    // 实体 → 其参与过的事件（role 传空 = 全部）
    [[nodiscard]] std::expected<std::vector<EventParticipantRow>, DbError>
    ListEntityParticipations(RowId entityId, std::string_view role = {}, int limit = 200) const;

    // 场次在场 / 场次伏笔（scene_cast / scene_foreshadows）
    [[nodiscard]] std::expected<RowId, DbError> UpsertSceneCast(const SceneCastRow& row);
    [[nodiscard]] std::expected<std::vector<SceneCastRow>, DbError>
    ListSceneCast(RowId sceneId) const;
    [[nodiscard]] std::expected<RowId, DbError> UpsertSceneForeshadow(const SceneForeshadowRow& row);
    [[nodiscard]] std::expected<std::vector<SceneForeshadowRow>, DbError>
    ListSceneForeshadows(RowId sceneId) const;

    // 剧情线（plots / plot_beats）
    [[nodiscard]] std::expected<RowId, DbError> UpsertPlot(const PlotRow& row);
    [[nodiscard]] std::expected<PlotRow, DbError> GetPlot(RowId id) const;
    [[nodiscard]] std::expected<std::vector<PlotRow>, DbError>
    ListPlots(std::string_view kind = {}, int limit = 100) const;
    [[nodiscard]] std::expected<RowId, DbError> UpsertPlotBeat(const PlotBeatRow& row);
    [[nodiscard]] std::expected<std::vector<PlotBeatRow>, DbError> ListPlotBeats(RowId plotId) const;

    // 谜团（mysteries / mystery_beats）：未解谜团是 `00` §2.5「开放线索」的组成
    [[nodiscard]] std::expected<RowId, DbError> UpsertMystery(const MysteryRow& row);
    [[nodiscard]] std::expected<MysteryRow, DbError> GetMystery(RowId id) const;
    [[nodiscard]] std::expected<std::vector<MysteryRow>, DbError> ListOpenMysteries() const;
    [[nodiscard]] std::expected<std::vector<MysteryRow>, DbError>
    ListMysteries(RowId entityId = 0, int limit = 100) const;
    [[nodiscard]] std::expected<RowId, DbError> UpsertMysteryBeat(const MysteryBeatRow& row);
    [[nodiscard]] std::expected<std::vector<MysteryBeatRow>, DbError>
    ListMysteryBeats(RowId mysteryId) const;

    // —— 初始化链 API 前置（S3-pre）：`10` §2.3 门禁的硬依赖，原先四张表零写入口 ——

    // 角色弧光（character_arcs）：门禁要求「主角必须有一条 arc，stage 至少含起点与终点」
    [[nodiscard]] std::expected<RowId, DbError> UpsertArc(const CharacterArcRow& row);
    // entityId <= 0 → 全表；按 ord、id 升序
    [[nodiscard]] std::expected<std::vector<CharacterArcRow>, DbError>
    ListArcs(RowId entityId = 0) const;

    // 角色级说话方式（dialogue_styles，一人一行）
    [[nodiscard]] std::expected<RowId, DbError> UpsertDialogueStyle(const DialogueStyleRow& row);
    [[nodiscard]] std::expected<DialogueStyleRow, DbError> GetDialogueStyle(RowId entityId) const;

    // 世界级键值（world_meta）：`10` §2.3 N1 要写 `book_title`
    [[nodiscard]] std::expected<void, DbError> SetWorldMeta(std::string_view key,
                                                            std::string_view value);
    [[nodiscard]] std::expected<std::string, DbError> GetWorldMeta(std::string_view key) const;

    // 主题（themes）：`10` 初始化要写；`04` L4 已在读
    [[nodiscard]] std::expected<RowId, DbError> UpsertTheme(const ThemeRow& row);
    [[nodiscard]] std::expected<std::vector<ThemeRow>, DbError> ListThemes() const;

    // —— 持有 ——
    [[nodiscard]] std::expected<RowId, DbError> UpsertOwnership(const OwnershipRow& row);
    [[nodiscard]] std::expected<std::vector<OwnershipRow>, DbError>
    GetOwnerships(RowId ownerId) const;

    // —— 切片（给 ContextBuilder）——
    [[nodiscard]] std::expected<CharacterSlice, DbError>
    GetCharacterSlice(RowId entityId, RowId chapterId = 0) const;
    [[nodiscard]] std::expected<WorldSlice, DbError> GetWorldSlice() const;

    // —— Canon / 审计 ——
    [[nodiscard]] std::expected<void, DbError> LogAudit(std::string_view actor,
                                                        std::string_view action,
                                                        std::string_view targetKind,
                                                        RowId targetId, std::string_view detail);
    [[nodiscard]] std::expected<void, DbError> SetCanon(std::string_view targetKind, RowId targetId,
                                                        std::string_view status,
                                                        std::string_view note = {});

    // 最小图谱自检（内存库）
    [[nodiscard]] static bool RunGraphSelfCheck();

private:
    db::sqlite::Database* db_;

    [[nodiscard]] EntityRow ReadEntity(db::sqlite::Statement& st) const;
};

} // namespace shine::novelcore
