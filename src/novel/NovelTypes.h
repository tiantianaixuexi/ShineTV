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

// —— P0 八表（S2）：原先只有表、没有 API，是闭环的阻断点 ——
// 规格：`Doc/小说系统/01` §2.4.2（事件参与）/ §2.4.5（知情）/ §2.5（叙事结构、剧情线）/ §2.3.4（谜团）。
// `CharacterKnowledgeRow` 见上方（本组沿用），其余 7 个结构体在此定义。

// event_participants：谁参与了哪个事件（01 §2.4.2）
struct EventParticipantRow {
    RowId id = 0;
    RowId event_id = 0;
    RowId entity_id = 0;
    std::string role; // actor | victim | witness | beneficiary | faction_actor
};

// scene_cast：这一场「谁在场」（Context 组装与视觉映射的输入，01 §2.5）
struct SceneCastRow {
    RowId id = 0;
    RowId scene_id = 0;
    RowId entity_id = 0;
    std::string role; // pov | cast | present | offscreen
};

// scene_foreshadows：这一场对某条伏笔做了什么（01 §2.5）
struct SceneForeshadowRow {
    RowId id = 0;
    RowId scene_id = 0;
    RowId foreshadowing_id = 0;
    std::string action; // plant | develop | payoff
};

// plots：剧情线（01 §2.5）
struct PlotRow {
    RowId id = 0;
    std::string kind = "main";     // main | sub | romance | revenge …
    std::string title;
    std::string status = "active"; // active | resolved | dropped
    RowId intro_ch = 0;
    RowId target_ch = 0;
    std::string note;
};

// plot_beats：剧情线节拍（`ord` 参与叙事序时间轴，01 §2.5）
struct PlotBeatRow {
    RowId id = 0;
    RowId plot_id = 0;
    RowId chapter_id = 0;
    int ord = 0;
    // setup | rising | turning_point | climax | falling | resolution | revelation
    std::string beat_type = "setup";
    std::string title;
    std::string summary;
    std::string cast_json = "[]";
};

// mysteries：读者侧的谜团节奏（与 foreshadowings 的「作者侧账本」分工不同，01 §2.3.4）
struct MysteryRow {
    RowId id = 0;
    RowId entity_id = 0;
    std::string question;
    std::string answer;
    std::string status = "open"; // open | hinted | revealed | resolved
    RowId ask_ch = 0;
    RowId answer_ch = 0;
    int importance = 50;
};

// mystery_beats：谜团推进节拍（01 §2.3.4）
struct MysteryBeatRow {
    RowId id = 0;
    RowId mystery_id = 0;
    std::string beat_type = "hint"; // question | hint | reveal | answer | red_herring
    RowId chapter_id = 0;
    std::string content;
    RowId target_entity_id = 0;
    int ord = 0;
};

// —— 初始化链（S3-pre）：`10` §2.3 门禁的硬依赖，原先零写入口 ——

// character_arcs：角色弧光阶段（「不同时间段不同性格」的正规落点）。
// `stage` 例：懦弱少年 → 第一次杀人 → 心理变化 → 保护同伴 → 蜕变；`trigger_event_id` 指向触发转变的事件。
struct CharacterArcRow {
    RowId id = 0;
    RowId entity_id = 0;
    int ord = 0; // 阶段序；0 = 未指定
    std::string stage;
    RowId trigger_event_id = 0; // 0 = 无外部触发
    std::string note;
};

// dialogue_styles：**角色级**说话方式（一人一行）。
// 与 `writing_style` 的「全书级单行」分工不同、不能互替（`01` §2.8.2 已定为 P1）。
struct DialogueStyleRow {
    RowId entity_id = 0;
    std::string sentence_len; // 句长倾向
    std::string vocabulary;   // 用词特征
    std::string catchphrase;  // 口头禅
    std::string taboo_words;  // 忌语
    std::string habit;        // 说话习惯（停顿/语气词/尊称…）
};

// themes：主题（`10` 初始化要写；`04` L4 已在读）
struct ThemeRow {
    RowId id = 0;
    std::string title;
    std::string statement;    // 主题陈述
    RowId linked_plot_id = 0; // 关联剧情线
};

// —— P1 余下五表（S7）：表在、API 不在（`01` §2.8.2 的 P1 定级）——
// 其中 `scene_visuals` 属视觉侧，结构体在 `NovelVisual.h`。

// entity_versions：实体在某一时点的**完整状态快照**（`07` §2.2 的"写前快照"落这里）。
// 与 `audit_logs` 分工不同：审计只记"做了什么"，快照要能**读回来还原**（S7 判据：能写能读）。
struct EntityVersionRow {
    RowId id = 0;
    RowId entity_id = 0;
    int ver = 0; // 0 = 自动递增到下一版（默认）；> 0 = 指定版本（一般不用手填）
    std::string snapshot_json = "{}";
    std::string note;
    std::int64_t created = 0;
};

// 快照载荷（`util::reflect` 序列化 → 字段名即 JSON 键、读取宽容）。回滚/写回由 S8 的提交事务做。
struct EntitySnapshot {
    EntityRow entity;
    PersonaRow persona;
    CharacterStatusRow status;
};

// location_distances：两地行程估算（`06` K18 `travel.time_sane` 的数据来源）
struct LocationDistanceRow {
    RowId id = 0;
    RowId from_id = 0;
    RowId to_id = 0;
    double distance_km = 0.0;
    std::string travel_note;
    double days_estimate = 0.0;
};

// writing_style：**全书单行**（主键固定 `id=1`，`ContextBuilder` 已按 id=1 读）
struct WritingStyleRow {
    RowId id = 1;
    std::string pov_mode = "third_limited";
    std::string sentence_len = "medium";
    std::string density;
    double dialogue_ratio = 0.3;
    double action_ratio = 0.3;
    double thought_ratio = 0.2;
    double env_ratio = 0.2;
    int humor = 0;   // 0–100
    int serious = 50; // 0–100
    std::string pacing;
    std::string note;
};

// author_rules：作者硬规则（`severity` = error|warn|info；`ContextBuilder` 只读 error 级）
struct AuthorRuleRow {
    RowId id = 0;
    std::string rule;
    std::string severity = "warn";
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
