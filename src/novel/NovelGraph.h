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
