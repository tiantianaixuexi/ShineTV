#pragma once
// 小说知识图谱行类型（契约 docs/compose/spec/novel-agent.md S2.3）
#include <cstdint>
#include <string>
#include <vector>

namespace shine::novelcore {

using RowId = std::int64_t;

// entities.kind 全量（六域）
namespace kind {
inline constexpr std::string_view universe = "universe";
inline constexpr std::string_view world_rule = "world_rule";
inline constexpr std::string_view history = "history";
inline constexpr std::string_view culture = "culture";
inline constexpr std::string_view language = "language";
inline constexpr std::string_view religion = "religion";
inline constexpr std::string_view economy = "economy";
inline constexpr std::string_view tech = "tech";
inline constexpr std::string_view society = "society";
inline constexpr std::string_view calendar = "calendar";
inline constexpr std::string_view person = "person";
inline constexpr std::string_view creature = "creature";
inline constexpr std::string_view clothing = "clothing";
inline constexpr std::string_view prop = "prop";
inline constexpr std::string_view treasure = "treasure";
inline constexpr std::string_view item = "item";
inline constexpr std::string_view location = "location";
inline constexpr std::string_view faction = "faction";
inline constexpr std::string_view power_system = "power_system";
inline constexpr std::string_view ability = "ability";
inline constexpr std::string_view event = "event";
inline constexpr std::string_view plot = "plot";
inline constexpr std::string_view arc = "arc";
inline constexpr std::string_view conflict = "conflict";
inline constexpr std::string_view theme = "theme";
inline constexpr std::string_view motif = "motif";
inline constexpr std::string_view ending = "ending";
inline constexpr std::string_view secret = "secret";
inline constexpr std::string_view foreshadowing = "foreshadowing";
inline constexpr std::string_view mystery = "mystery";
inline constexpr std::string_view resource = "resource";
} // namespace kind

struct EntityRow {
    RowId id = 0;
    std::string kind;
    std::string name;
    std::string summary;
    std::string status = "active"; // active|dead|destroyed|archived
    std::string meta_json = "{}";
    RowId created_chapter = 0;
    std::int64_t updated = 0;
};

struct RelationRow {
    RowId id = 0;
    RowId from_id = 0;
    RowId to_id = 0;
    std::string rel_type; // friend/enemy/love/master/family/rival/debt/ally/control/counter…
    int strength = 50;    // 0–100
    RowId from_chapter = 0;
    RowId to_chapter = 0; // 0 = 至今
    std::string reason;
    std::string status = "active"; // active|ended
};

struct PersonaRow {
    RowId entity_id = 0;
    std::string age;
    std::string appearance;
    std::string personality;
    std::string background;
    std::string values;
    std::string desire;
    std::string goal;
    std::string fear;
    std::string weakness;
    std::string strength;
    std::string ability_note;
    std::string knowledge_note;
    std::string memory_note;
};

struct CharacterStatusRow {
    RowId id = 0;
    RowId entity_id = 0;
    RowId chapter_id = 0;
    RowId location_id = 0;
    std::string body_state;
    std::string mind_state;
    std::string emotion_json = "{}";
    std::string goal;
    std::string relation_note;
    std::string resource_note;
    std::string secret_note;
    std::int64_t updated = 0;
};

struct ForeshadowRow {
    RowId id = 0;
    std::string title;
    std::string content;
    std::string status = "PLANNED"; // PLANNED|PLANTED|DEVELOPING|REVEALED|RESOLVED
    RowId setup_ch = 0;
    RowId payoff_ch = 0;
    int importance = 50;
    std::string truth;
    std::string entity_ids_json = "[]";
};

struct ChapterRow {
    RowId id = 0;
    RowId volume_id = 0;
    int ord = 0;
    std::string title;
    std::string status = "draft"; // draft|writing|review|done
    std::string summary;
    std::string body;
    RowId pov_entity_id = 0;
    int words = 0;
    std::int64_t updated = 0;
};

struct VolumeRow {
    RowId id = 0;
    std::string title;
    int ord = 0;
    std::string summary;
};

struct SceneRow {
    RowId id = 0;
    RowId chapter_id = 0;
    int ord = 0;
    std::string title;
    RowId location_id = 0;
    std::string time_label;
    RowId pov_entity_id = 0;
    RowId conflict_id = 0;
    std::string goal;
    std::string action;
    std::string conflict;
    std::string result;
    std::string emotion;
    std::string info_reveal;
    std::string hook;
    std::string body;
};

struct EventDetailRow {
    RowId entity_id = 0;
    std::string time_label;
    RowId location_id = 0;
    std::string cause_note;
    std::string result_note;
};

struct CausalLinkRow {
    RowId id = 0;
    RowId cause_event_id = 0;
    RowId effect_event_id = 0;
    std::string link_type = "causes"; // causes|enables|prevents|escalates|reveals
    std::string note;
    int ord = 0;
};

struct SecretRow {
    RowId id = 0;
    std::string content;
    std::string truth;
    RowId reveal_ch = 0;
    std::string reveal_condition;
    RowId entity_id = 0;
    std::string scope = "character"; // world|character|faction
};

struct CharacterKnowledgeRow {
    RowId id = 0;
    RowId entity_id = 0;
    std::string fact_kind;
    RowId fact_id = 0;
    std::string fact_text;
    int knows = 1;
    RowId chapter_known = 0;
};

struct OwnershipRow {
    RowId id = 0;
    RowId owner_id = 0;
    RowId item_id = 0;
    RowId from_chapter = 0;
    RowId to_chapter = 0;
    std::string how;
    std::string note;
};

// 供 ContextBuilder 的切片
struct CharacterSlice {
    EntityRow entity;
    PersonaRow persona;
    CharacterStatusRow status;
    std::vector<RelationRow> relations;
    std::vector<CharacterKnowledgeRow> knowledge;
    std::vector<ForeshadowRow> openForeshadows;
};

struct WorldSlice {
    std::vector<EntityRow> rules;
    std::vector<EntityRow> locations;
    std::vector<EntityRow> factions;
    std::vector<EntityRow> powerSystems;
};

} // namespace shine::novelcore
