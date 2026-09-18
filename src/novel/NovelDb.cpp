#include "novel/NovelDb.h"

#include "core/Log.h"
#include "util/Encoding.h"
#include "util/Time.h"

#include <fmt/format.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <yyjson.h>

namespace shine::novelcore {
namespace {

constexpr int kTargetSchemaVersion = 5;

// v5：多 Agent + 动态字段（不写死小说体系）
constexpr std::string_view kSchemaV5Agents = R"SQL(
CREATE TABLE IF NOT EXISTS agent_defs(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  agent_id TEXT NOT NULL UNIQUE,
  name TEXT NOT NULL DEFAULT '',
  role_tags TEXT NOT NULL DEFAULT '',
  system_prompt TEXT NOT NULL DEFAULT '',
  tools_json TEXT NOT NULL DEFAULT '[]',
  output_hint TEXT NOT NULL DEFAULT '',
  enabled INTEGER NOT NULL DEFAULT 1,
  is_builtin INTEGER NOT NULL DEFAULT 1,
  version INTEGER NOT NULL DEFAULT 1,
  updated INTEGER NOT NULL DEFAULT 0);
)SQL";

// 视觉体系（P8）：追加表，不破坏 v3
constexpr std::string_view kSchemaV4Visual = R"SQL(
CREATE TABLE IF NOT EXISTS visual_assets(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  entity_id INTEGER NOT NULL DEFAULT 0,
  kind TEXT NOT NULL DEFAULT 'character',
  name TEXT NOT NULL DEFAULT '',
  base_desc TEXT NOT NULL DEFAULT '',
  materials_colors TEXT NOT NULL DEFAULT '',
  permanent_tags_json TEXT NOT NULL DEFAULT '[]',
  sheet_rel_path TEXT NOT NULL DEFAULT '',
  canon_status TEXT NOT NULL DEFAULT 'DRAFT',
  note TEXT NOT NULL DEFAULT '');
CREATE INDEX IF NOT EXISTS idx_vasset_entity ON visual_assets(entity_id);

CREATE TABLE IF NOT EXISTS visual_states(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  asset_id INTEGER NOT NULL,
  stage_key TEXT NOT NULL DEFAULT '',
  stage_label TEXT NOT NULL DEFAULT '',
  ord INTEGER NOT NULL DEFAULT 0,
  from_chapter INTEGER NOT NULL DEFAULT 0,
  to_chapter INTEGER NOT NULL DEFAULT 0,
  appearance TEXT NOT NULL DEFAULT '',
  materials_colors TEXT NOT NULL DEFAULT '',
  clothing_asset_id INTEGER NOT NULL DEFAULT 0,
  item_asset_ids_json TEXT NOT NULL DEFAULT '[]',
  effects TEXT NOT NULL DEFAULT '',
  environment_hint TEXT NOT NULL DEFAULT '',
  canon_status TEXT NOT NULL DEFAULT 'DRAFT',
  note TEXT NOT NULL DEFAULT '');
CREATE INDEX IF NOT EXISTS idx_vstate_asset ON visual_states(asset_id);

CREATE TABLE IF NOT EXISTS scene_visuals(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  scene_id INTEGER NOT NULL,
  env_desc TEXT NOT NULL DEFAULT '',
  time_of_day TEXT NOT NULL DEFAULT '',
  weather TEXT NOT NULL DEFAULT '',
  mood TEXT NOT NULL DEFAULT '',
  canon_status TEXT NOT NULL DEFAULT 'DRAFT');

CREATE TABLE IF NOT EXISTS scene_layouts(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  scene_id INTEGER NOT NULL,
  layout_name TEXT NOT NULL DEFAULT '',
  layout_json TEXT NOT NULL DEFAULT '{}',
  note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS camera_defs(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  shot_size TEXT NOT NULL DEFAULT 'medium',
  angle TEXT NOT NULL DEFAULT 'eye',
  lens_note TEXT NOT NULL DEFAULT '',
  movement TEXT NOT NULL DEFAULT 'static',
  text TEXT NOT NULL DEFAULT '',
  note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS composition_defs(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  rule TEXT NOT NULL DEFAULT 'rule_of_thirds',
  framing TEXT NOT NULL DEFAULT '',
  text TEXT NOT NULL DEFAULT '',
  note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS lighting_defs(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  time_hint TEXT NOT NULL DEFAULT '',
  key_light TEXT NOT NULL DEFAULT '',
  mood TEXT NOT NULL DEFAULT '',
  text TEXT NOT NULL DEFAULT '',
  note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS visual_styles(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL DEFAULT 'default',
  payload_json TEXT NOT NULL DEFAULT '{}',
  text TEXT NOT NULL DEFAULT '',
  note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS prompt_layers(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  owner_kind TEXT NOT NULL DEFAULT 'asset',
  owner_id INTEGER NOT NULL DEFAULT 0,
  layer TEXT NOT NULL DEFAULT 'base',
  text TEXT NOT NULL DEFAULT '',
  model_hint TEXT NOT NULL DEFAULT '',
  version INTEGER NOT NULL DEFAULT 1,
  canon_status TEXT NOT NULL DEFAULT 'DRAFT');
CREATE INDEX IF NOT EXISTS idx_player_owner ON prompt_layers(owner_kind, owner_id);

CREATE TABLE IF NOT EXISTS shots(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  scene_id INTEGER NOT NULL,
  ord INTEGER NOT NULL DEFAULT 0,
  duration_note TEXT NOT NULL DEFAULT '',
  camera_id INTEGER NOT NULL DEFAULT 0,
  character_ids_json TEXT NOT NULL DEFAULT '[]',
  action TEXT NOT NULL DEFAULT '',
  expression TEXT NOT NULL DEFAULT '',
  prop_ids_json TEXT NOT NULL DEFAULT '[]',
  lighting_id INTEGER NOT NULL DEFAULT 0,
  composition_id INTEGER NOT NULL DEFAULT 0,
  dialogue TEXT NOT NULL DEFAULT '',
  narration TEXT NOT NULL DEFAULT '',
  sfx TEXT NOT NULL DEFAULT '',
  mood TEXT NOT NULL DEFAULT '',
  prompt_text TEXT NOT NULL DEFAULT '',
  negative_text TEXT NOT NULL DEFAULT '',
  reference_json TEXT NOT NULL DEFAULT '{}',
  canon_status TEXT NOT NULL DEFAULT 'PROPOSED');
CREATE INDEX IF NOT EXISTS idx_shots_scene ON shots(scene_id);

CREATE TABLE IF NOT EXISTS visual_canon_logs(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  target_kind TEXT NOT NULL,
  target_id INTEGER NOT NULL,
  status TEXT NOT NULL DEFAULT 'PROPOSED',
  note TEXT NOT NULL DEFAULT '',
  created INTEGER NOT NULL DEFAULT 0);
)SQL";

// v3 全量 schema（IF NOT EXISTS；升级只补缺表/缺列）
constexpr std::string_view kSchemaV3 = R"SQL(
PRAGMA foreign_keys=ON;

CREATE TABLE IF NOT EXISTS meta(
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL);

CREATE TABLE IF NOT EXISTS entities(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  kind TEXT NOT NULL,
  name TEXT NOT NULL,
  summary TEXT NOT NULL DEFAULT '',
  status TEXT NOT NULL DEFAULT 'active',
  meta_json TEXT NOT NULL DEFAULT '{}',
  created_chapter INTEGER NOT NULL DEFAULT 0,
  updated INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS idx_entities_kind ON entities(kind);
CREATE INDEX IF NOT EXISTS idx_entities_name ON entities(name);

CREATE TABLE IF NOT EXISTS entity_images(
  entity_id INTEGER PRIMARY KEY,
  rel_path TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS entity_ownerships(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  owner_id INTEGER NOT NULL,
  item_id INTEGER NOT NULL,
  from_chapter INTEGER NOT NULL DEFAULT 0,
  to_chapter INTEGER NOT NULL DEFAULT 0,
  how TEXT NOT NULL DEFAULT '',
  note TEXT NOT NULL DEFAULT '');
CREATE INDEX IF NOT EXISTS idx_own_owner ON entity_ownerships(owner_id);
CREATE INDEX IF NOT EXISTS idx_own_item ON entity_ownerships(item_id);

CREATE TABLE IF NOT EXISTS entity_events(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  entity_id INTEGER NOT NULL,
  event_id INTEGER NOT NULL,
  role TEXT NOT NULL DEFAULT '',
  note TEXT NOT NULL DEFAULT '');
CREATE INDEX IF NOT EXISTS idx_ee_entity ON entity_events(entity_id);
CREATE INDEX IF NOT EXISTS idx_ee_event ON entity_events(event_id);

CREATE TABLE IF NOT EXISTS relations(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  from_id INTEGER NOT NULL,
  to_id INTEGER NOT NULL,
  rel_type TEXT NOT NULL,
  strength INTEGER NOT NULL DEFAULT 50,
  from_chapter INTEGER NOT NULL DEFAULT 0,
  to_chapter INTEGER NOT NULL DEFAULT 0,
  reason TEXT NOT NULL DEFAULT '',
  status TEXT NOT NULL DEFAULT 'active');
CREATE INDEX IF NOT EXISTS idx_rel_from ON relations(from_id);
CREATE INDEX IF NOT EXISTS idx_rel_to ON relations(to_id);

-- 人物
CREATE TABLE IF NOT EXISTS entity_personas(
  entity_id INTEGER PRIMARY KEY,
  age TEXT NOT NULL DEFAULT '',
  appearance TEXT NOT NULL DEFAULT '',
  personality TEXT NOT NULL DEFAULT '',
  background TEXT NOT NULL DEFAULT '',
  "values" TEXT NOT NULL DEFAULT '',
  desire TEXT NOT NULL DEFAULT '',
  goal TEXT NOT NULL DEFAULT '',
  fear TEXT NOT NULL DEFAULT '',
  weakness TEXT NOT NULL DEFAULT '',
  strength TEXT NOT NULL DEFAULT '',
  ability_note TEXT NOT NULL DEFAULT '',
  knowledge_note TEXT NOT NULL DEFAULT '',
  memory_note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS character_arcs(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  entity_id INTEGER NOT NULL,
  ord INTEGER NOT NULL DEFAULT 0,
  stage TEXT NOT NULL DEFAULT '',
  trigger_event_id INTEGER NOT NULL DEFAULT 0,
  note TEXT NOT NULL DEFAULT '');
CREATE INDEX IF NOT EXISTS idx_arc_entity ON character_arcs(entity_id);

CREATE TABLE IF NOT EXISTS character_status(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  entity_id INTEGER NOT NULL,
  chapter_id INTEGER NOT NULL DEFAULT 0,
  location_id INTEGER NOT NULL DEFAULT 0,
  body_state TEXT NOT NULL DEFAULT '',
  mind_state TEXT NOT NULL DEFAULT '',
  emotion_json TEXT NOT NULL DEFAULT '{}',
  goal TEXT NOT NULL DEFAULT '',
  relation_note TEXT NOT NULL DEFAULT '',
  resource_note TEXT NOT NULL DEFAULT '',
  secret_note TEXT NOT NULL DEFAULT '',
  updated INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS idx_cstat_entity ON character_status(entity_id);
CREATE INDEX IF NOT EXISTS idx_cstat_ch ON character_status(chapter_id);

CREATE TABLE IF NOT EXISTS emotion_events(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  entity_id INTEGER NOT NULL,
  chapter_id INTEGER NOT NULL DEFAULT 0,
  event_id INTEGER NOT NULL DEFAULT 0,
  dim TEXT NOT NULL,
  delta INTEGER NOT NULL DEFAULT 0,
  note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS character_knowledge(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  entity_id INTEGER NOT NULL,
  fact_kind TEXT NOT NULL DEFAULT '',
  fact_id INTEGER NOT NULL DEFAULT 0,
  fact_text TEXT NOT NULL DEFAULT '',
  knows INTEGER NOT NULL DEFAULT 1,
  chapter_known INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS idx_ck_entity ON character_knowledge(entity_id);

CREATE TABLE IF NOT EXISTS dialogue_styles(
  entity_id INTEGER PRIMARY KEY,
  sentence_len TEXT NOT NULL DEFAULT '',
  vocabulary TEXT NOT NULL DEFAULT '',
  catchphrase TEXT NOT NULL DEFAULT '',
  taboo_words TEXT NOT NULL DEFAULT '',
  habit TEXT NOT NULL DEFAULT '');

-- 世界
CREATE TABLE IF NOT EXISTS world_meta(
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS universe_details(
  entity_id INTEGER PRIMARY KEY,
  parent_id INTEGER NOT NULL DEFAULT 0,
  kind TEXT NOT NULL DEFAULT 'world',
  note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS world_rule_details(
  entity_id INTEGER PRIMARY KEY,
  can_do TEXT NOT NULL DEFAULT '',
  cannot_do TEXT NOT NULL DEFAULT '',
  cost TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS history_details(
  entity_id INTEGER PRIMARY KEY,
  era TEXT NOT NULL DEFAULT '',
  summary TEXT NOT NULL DEFAULT '',
  related_json TEXT NOT NULL DEFAULT '[]');

CREATE TABLE IF NOT EXISTS society_details(
  entity_id INTEGER PRIMARY KEY,
  class_note TEXT NOT NULL DEFAULT '',
  system_note TEXT NOT NULL DEFAULT '',
  law TEXT NOT NULL DEFAULT '',
  custom TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS tech_details(
  entity_id INTEGER PRIMARY KEY,
  level TEXT NOT NULL DEFAULT '',
  transport TEXT NOT NULL DEFAULT '',
  comm TEXT NOT NULL DEFAULT '',
  productivity TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS culture_details(
  entity_id INTEGER PRIMARY KEY,
  art TEXT NOT NULL DEFAULT '',
  food TEXT NOT NULL DEFAULT '',
  dress TEXT NOT NULL DEFAULT '',
  taboo_note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS language_details(
  entity_id INTEGER PRIMARY KEY,
  script TEXT NOT NULL DEFAULT '',
  speakers_faction_id INTEGER NOT NULL DEFAULT 0,
  sample TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS religion_details(
  entity_id INTEGER PRIMARY KEY,
  gods_json TEXT NOT NULL DEFAULT '[]',
  doctrine TEXT NOT NULL DEFAULT '',
  church_faction_id INTEGER NOT NULL DEFAULT 0,
  relics_json TEXT NOT NULL DEFAULT '[]',
  rituals_json TEXT NOT NULL DEFAULT '[]',
  taboos_json TEXT NOT NULL DEFAULT '[]',
  heresy_note TEXT NOT NULL DEFAULT '',
  myth_note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS economy_details(
  entity_id INTEGER PRIMARY KEY,
  note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS currency(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  symbol TEXT NOT NULL DEFAULT '',
  approx_value_note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS price_samples(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  item_name TEXT NOT NULL,
  currency_id INTEGER NOT NULL DEFAULT 0,
  amount REAL NOT NULL DEFAULT 0,
  era_note TEXT NOT NULL DEFAULT '',
  chapter_id INTEGER NOT NULL DEFAULT 0);

CREATE TABLE IF NOT EXISTS calendar_eras(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  start_note TEXT NOT NULL DEFAULT '',
  end_note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS calendar_seasons(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  era_id INTEGER NOT NULL DEFAULT 0,
  name TEXT NOT NULL,
  ord INTEGER NOT NULL DEFAULT 0);

-- 地点/势力/能力/物品/资源
CREATE TABLE IF NOT EXISTS location_details(
  entity_id INTEGER PRIMARY KEY,
  parent_id INTEGER NOT NULL DEFAULT 0,
  loc_type TEXT NOT NULL DEFAULT 'special',
  geo_note TEXT NOT NULL DEFAULT '',
  weather TEXT NOT NULL DEFAULT '',
  environment TEXT NOT NULL DEFAULT '',
  population TEXT NOT NULL DEFAULT '',
  danger TEXT NOT NULL DEFAULT '',
  resource_note TEXT NOT NULL DEFAULT '',
  special_rule TEXT NOT NULL DEFAULT '',
  history_note TEXT NOT NULL DEFAULT '',
  controlling_faction_id INTEGER NOT NULL DEFAULT 0);

CREATE TABLE IF NOT EXISTS location_distances(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  from_id INTEGER NOT NULL,
  to_id INTEGER NOT NULL,
  distance_km REAL NOT NULL DEFAULT 0,
  travel_note TEXT NOT NULL DEFAULT '',
  days_estimate REAL NOT NULL DEFAULT 0);

CREATE TABLE IF NOT EXISTS faction_details(
  entity_id INTEGER PRIMARY KEY,
  faction_type TEXT NOT NULL DEFAULT 'org',
  leader_id INTEGER NOT NULL DEFAULT 0,
  base_location_id INTEGER NOT NULL DEFAULT 0,
  goal TEXT NOT NULL DEFAULT '',
  "values" TEXT NOT NULL DEFAULT '',
  politics TEXT NOT NULL DEFAULT '',
  military TEXT NOT NULL DEFAULT '',
  economy_note TEXT NOT NULL DEFAULT '',
  secret TEXT NOT NULL DEFAULT '',
  history_note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS faction_members(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  faction_id INTEGER NOT NULL,
  entity_id INTEGER NOT NULL,
  role TEXT NOT NULL DEFAULT '',
  from_chapter INTEGER NOT NULL DEFAULT 0,
  to_chapter INTEGER NOT NULL DEFAULT 0);

CREATE TABLE IF NOT EXISTS power_system_details(
  entity_id INTEGER PRIMARY KEY,
  note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS ability_details(
  entity_id INTEGER PRIMARY KEY,
  system_id INTEGER NOT NULL DEFAULT 0,
  ability_type TEXT NOT NULL DEFAULT 'skill',
  level TEXT NOT NULL DEFAULT '',
  attr_note TEXT NOT NULL DEFAULT '',
  cost TEXT NOT NULL DEFAULT '',
  "limit" TEXT NOT NULL DEFAULT '',
  side_effect TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS item_details(
  entity_id INTEGER PRIMARY KEY,
  item_type TEXT NOT NULL DEFAULT 'misc',
  origin TEXT NOT NULL DEFAULT '',
  maker TEXT NOT NULL DEFAULT '',
  props TEXT NOT NULL DEFAULT '',
  power TEXT NOT NULL DEFAULT '',
  appear_chapter INTEGER NOT NULL DEFAULT 0,
  state TEXT NOT NULL DEFAULT '',
  secret_note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS resource_holdings(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  owner_id INTEGER NOT NULL,
  resource_id INTEGER NOT NULL,
  amount REAL NOT NULL DEFAULT 0,
  chapter_id INTEGER NOT NULL DEFAULT 0,
  note TEXT NOT NULL DEFAULT '');

-- 事件/因果
CREATE TABLE IF NOT EXISTS event_details(
  entity_id INTEGER PRIMARY KEY,
  time_label TEXT NOT NULL DEFAULT '',
  location_id INTEGER NOT NULL DEFAULT 0,
  cause_note TEXT NOT NULL DEFAULT '',
  result_note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS event_participants(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  event_id INTEGER NOT NULL,
  entity_id INTEGER NOT NULL,
  role TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS causal_links(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  cause_event_id INTEGER NOT NULL,
  effect_event_id INTEGER NOT NULL,
  link_type TEXT NOT NULL DEFAULT 'causes',
  note TEXT NOT NULL DEFAULT '',
  ord INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS idx_causal_cause ON causal_links(cause_event_id);
CREATE INDEX IF NOT EXISTS idx_causal_effect ON causal_links(effect_event_id);

-- 叙事结构
CREATE TABLE IF NOT EXISTS volumes(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  title TEXT NOT NULL,
  ord INTEGER NOT NULL DEFAULT 0,
  summary TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS chapters(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  volume_id INTEGER NOT NULL DEFAULT 0,
  ord INTEGER NOT NULL DEFAULT 0,
  title TEXT NOT NULL DEFAULT '',
  status TEXT NOT NULL DEFAULT 'draft',
  summary TEXT NOT NULL DEFAULT '',
  body TEXT NOT NULL DEFAULT '',
  pov_entity_id INTEGER NOT NULL DEFAULT 0,
  words INTEGER NOT NULL DEFAULT 0,
  updated INTEGER NOT NULL DEFAULT 0);

CREATE TABLE IF NOT EXISTS scenes(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  chapter_id INTEGER NOT NULL,
  ord INTEGER NOT NULL DEFAULT 0,
  title TEXT NOT NULL DEFAULT '',
  location_id INTEGER NOT NULL DEFAULT 0,
  time_label TEXT NOT NULL DEFAULT '',
  pov_entity_id INTEGER NOT NULL DEFAULT 0,
  conflict_id INTEGER NOT NULL DEFAULT 0,
  goal TEXT NOT NULL DEFAULT '',
  action TEXT NOT NULL DEFAULT '',
  conflict TEXT NOT NULL DEFAULT '',
  result TEXT NOT NULL DEFAULT '',
  emotion TEXT NOT NULL DEFAULT '',
  info_reveal TEXT NOT NULL DEFAULT '',
  hook TEXT NOT NULL DEFAULT '',
  body TEXT NOT NULL DEFAULT '');
CREATE INDEX IF NOT EXISTS idx_scenes_ch ON scenes(chapter_id);

CREATE TABLE IF NOT EXISTS scene_cast(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  scene_id INTEGER NOT NULL,
  entity_id INTEGER NOT NULL,
  role TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS scene_foreshadows(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  scene_id INTEGER NOT NULL,
  foreshadowing_id INTEGER NOT NULL,
  action TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS plots(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  kind TEXT NOT NULL DEFAULT 'main',
  title TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'active',
  intro_ch INTEGER NOT NULL DEFAULT 0,
  target_ch INTEGER NOT NULL DEFAULT 0,
  note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS plot_beats(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  plot_id INTEGER NOT NULL,
  chapter_id INTEGER NOT NULL DEFAULT 0,
  ord INTEGER NOT NULL DEFAULT 0,
  beat_type TEXT NOT NULL DEFAULT 'setup',
  title TEXT NOT NULL DEFAULT '',
  summary TEXT NOT NULL DEFAULT '',
  cast_json TEXT NOT NULL DEFAULT '[]');

CREATE TABLE IF NOT EXISTS themes(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  title TEXT NOT NULL,
  statement TEXT NOT NULL DEFAULT '',
  linked_plot_id INTEGER NOT NULL DEFAULT 0);

CREATE TABLE IF NOT EXISTS motifs(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  symbol TEXT NOT NULL,
  meaning TEXT NOT NULL DEFAULT '',
  first_ch INTEGER NOT NULL DEFAULT 0,
  last_ch INTEGER NOT NULL DEFAULT 0,
  transform_note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS conflict_details(
  entity_id INTEGER PRIMARY KEY,
  conflict_type TEXT NOT NULL DEFAULT 'person_vs_person',
  side_a_json TEXT NOT NULL DEFAULT '[]',
  side_b_json TEXT NOT NULL DEFAULT '[]',
  cause TEXT NOT NULL DEFAULT '',
  goal_a TEXT NOT NULL DEFAULT '',
  goal_b TEXT NOT NULL DEFAULT '',
  escalation TEXT NOT NULL DEFAULT '',
  resolution TEXT NOT NULL DEFAULT '',
  consequence TEXT NOT NULL DEFAULT '',
  intro_ch INTEGER NOT NULL DEFAULT 0,
  peak_ch INTEGER NOT NULL DEFAULT 0,
  end_ch INTEGER NOT NULL DEFAULT 0);

CREATE TABLE IF NOT EXISTS writing_style(
  id INTEGER PRIMARY KEY CHECK(id=1),
  pov_mode TEXT NOT NULL DEFAULT 'third_limited',
  sentence_len TEXT NOT NULL DEFAULT 'medium',
  density TEXT NOT NULL DEFAULT '',
  dialogue_ratio REAL NOT NULL DEFAULT 0.3,
  action_ratio REAL NOT NULL DEFAULT 0.3,
  thought_ratio REAL NOT NULL DEFAULT 0.2,
  env_ratio REAL NOT NULL DEFAULT 0.2,
  humor INTEGER NOT NULL DEFAULT 0,
  serious INTEGER NOT NULL DEFAULT 50,
  pacing TEXT NOT NULL DEFAULT '',
  note TEXT NOT NULL DEFAULT '');

-- 谜/伏笔/秘密
CREATE TABLE IF NOT EXISTS foreshadowings(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  title TEXT NOT NULL,
  content TEXT NOT NULL DEFAULT '',
  status TEXT NOT NULL DEFAULT 'PLANNED',
  setup_ch INTEGER NOT NULL DEFAULT 0,
  payoff_ch INTEGER NOT NULL DEFAULT 0,
  importance INTEGER NOT NULL DEFAULT 50,
  truth TEXT NOT NULL DEFAULT '',
  entity_ids_json TEXT NOT NULL DEFAULT '[]');

CREATE TABLE IF NOT EXISTS secrets(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  content TEXT NOT NULL,
  truth TEXT NOT NULL DEFAULT '',
  reveal_ch INTEGER NOT NULL DEFAULT 0,
  reveal_condition TEXT NOT NULL DEFAULT '',
  entity_id INTEGER NOT NULL DEFAULT 0,
  scope TEXT NOT NULL DEFAULT 'character');

CREATE TABLE IF NOT EXISTS secret_knowledge(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  secret_id INTEGER NOT NULL,
  entity_id INTEGER NOT NULL,
  knows INTEGER NOT NULL DEFAULT 0,
  chapter_known INTEGER NOT NULL DEFAULT 0);

CREATE TABLE IF NOT EXISTS mysteries(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  entity_id INTEGER NOT NULL DEFAULT 0,
  question TEXT NOT NULL,
  answer TEXT NOT NULL DEFAULT '',
  status TEXT NOT NULL DEFAULT 'open',
  ask_ch INTEGER NOT NULL DEFAULT 0,
  answer_ch INTEGER NOT NULL DEFAULT 0,
  importance INTEGER NOT NULL DEFAULT 50);

CREATE TABLE IF NOT EXISTS mystery_beats(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  mystery_id INTEGER NOT NULL,
  beat_type TEXT NOT NULL DEFAULT 'hint',
  chapter_id INTEGER NOT NULL DEFAULT 0,
  content TEXT NOT NULL DEFAULT '',
  target_entity_id INTEGER NOT NULL DEFAULT 0,
  ord INTEGER NOT NULL DEFAULT 0);

-- 记忆/系统
CREATE TABLE IF NOT EXISTS memories(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  kind TEXT NOT NULL DEFAULT 'short_term',
  entity_id INTEGER NOT NULL DEFAULT 0,
  chapter_id INTEGER NOT NULL DEFAULT 0,
  content TEXT NOT NULL DEFAULT '',
  summary TEXT NOT NULL DEFAULT '',
  embedding_blob BLOB,
  created INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS idx_mem_kind ON memories(kind);
CREATE INDEX IF NOT EXISTS idx_mem_ch ON memories(chapter_id);

CREATE TABLE IF NOT EXISTS entity_versions(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  entity_id INTEGER NOT NULL,
  ver INTEGER NOT NULL DEFAULT 1,
  snapshot_json TEXT NOT NULL DEFAULT '{}',
  note TEXT NOT NULL DEFAULT '',
  created INTEGER NOT NULL DEFAULT 0);

CREATE TABLE IF NOT EXISTS dependencies(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  from_kind TEXT NOT NULL,
  from_id INTEGER NOT NULL,
  to_kind TEXT NOT NULL,
  to_id INTEGER NOT NULL,
  dep_type TEXT NOT NULL DEFAULT 'mentions',
  note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS author_rules(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  rule TEXT NOT NULL,
  severity TEXT NOT NULL DEFAULT 'warn',
  note TEXT NOT NULL DEFAULT '');

CREATE TABLE IF NOT EXISTS canon_logs(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  target_kind TEXT NOT NULL,
  target_id INTEGER NOT NULL,
  status TEXT NOT NULL DEFAULT 'PROPOSED',
  note TEXT NOT NULL DEFAULT '',
  created INTEGER NOT NULL DEFAULT 0);

CREATE TABLE IF NOT EXISTS audit_logs(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  actor TEXT NOT NULL DEFAULT 'user',
  action TEXT NOT NULL,
  target_kind TEXT NOT NULL DEFAULT '',
  target_id INTEGER NOT NULL DEFAULT 0,
  detail TEXT NOT NULL DEFAULT '',
  created INTEGER NOT NULL DEFAULT 0);
)SQL";

} // namespace

NovelDb& NovelDb::Instance() noexcept {
    static NovelDb inst;
    return inst;
}

std::expected<void, DbError> NovelDb::ExecAll(std::string_view sql) {
    return db_.Exec(sql);
}

std::expected<void, DbError> NovelDb::Open(const std::filesystem::path& dbPath) {
    Close();
    if (dbPath.empty()) {
        return std::unexpected(DbError{0, "db 路径为空"});
    }
    if (auto r = db_.Open({.path = dbPath, .create = true}); !r) {
        return r;
    }
    path_ = dbPath;
    return Migrate();
}

void NovelDb::Close() noexcept {
    db_.Close();
    path_.clear();
    schemaVersion_ = 0;
}

std::expected<void, DbError> NovelDb::Migrate() {
    if (auto r = ExecAll(kSchemaV3); !r) {
        return r;
    }
    if (auto r = ExecAll(kSchemaV4Visual); !r) {
        return r;
    }
    if (auto r = ExecAll(kSchemaV5Agents); !r) {
        return r;
    }
    {
        // field_defs / entity_fields（NovelFields）
        if (auto r = db_.Exec(R"SQL(
CREATE TABLE IF NOT EXISTS field_defs(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  scope TEXT NOT NULL DEFAULT 'entity',
  entity_kind TEXT NOT NULL DEFAULT '',
  field_key TEXT NOT NULL,
  title TEXT NOT NULL DEFAULT '',
  value_type TEXT NOT NULL DEFAULT 'text',
  enum_json TEXT NOT NULL DEFAULT '[]',
  description TEXT NOT NULL DEFAULT '',
  created_by TEXT NOT NULL DEFAULT '',
  is_system INTEGER NOT NULL DEFAULT 0,
  updated INTEGER NOT NULL DEFAULT 0);
CREATE UNIQUE INDEX IF NOT EXISTS idx_field_defs_key ON field_defs(scope, entity_kind, field_key);
CREATE TABLE IF NOT EXISTS entity_fields(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  entity_id INTEGER NOT NULL DEFAULT 0,
  field_key TEXT NOT NULL,
  value_text TEXT NOT NULL DEFAULT '',
  value_json TEXT NOT NULL DEFAULT 'null',
  chapter_scope INTEGER NOT NULL DEFAULT 0,
  chapter_to INTEGER NOT NULL DEFAULT 0,
  layer TEXT NOT NULL DEFAULT 'global',
  note TEXT NOT NULL DEFAULT '',
  created_by TEXT NOT NULL DEFAULT '',
  updated INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS idx_ef_entity ON entity_fields(entity_id);
CREATE INDEX IF NOT EXISTS idx_ef_key ON entity_fields(field_key);
CREATE UNIQUE INDEX IF NOT EXISTS idx_ef_uniq
  ON entity_fields(entity_id, field_key, chapter_scope, layer);
)SQL"); !r) {
            return r;
        }
    }
    // 读旧版本号（CreateProject 可能写过 '1'）
    int oldVer = 0;
    if (auto sel = db_.Prepare("SELECT value FROM meta WHERE key='schemaVersion'")) {
        if (auto s = sel->Step(); s && *s == db::sqlite::StepResult::Row) {
            const std::string v = sel->ColumnText(0);
            oldVer = v.empty() ? 0 : std::atoi(v.c_str());
        }
    }
    // v1 → v3：旧 chapters/characters/foreshadowings 已由 IF NOT EXISTS 兼容；
    // 旧 chapters 无 volume_id 等列时用 ALTER 补列（忽略已存在错误）
    if (oldVer > 0 && oldVer < 3) {
        auto tryAlter = [this](std::string_view sql) {
            (void)db_.Exec(sql); // 列已存在则失败，忽略
        };
        tryAlter("ALTER TABLE chapters ADD COLUMN volume_id INTEGER NOT NULL DEFAULT 0");
        tryAlter("ALTER TABLE chapters ADD COLUMN status TEXT NOT NULL DEFAULT 'draft'");
        tryAlter("ALTER TABLE chapters ADD COLUMN summary TEXT NOT NULL DEFAULT ''");
        tryAlter("ALTER TABLE chapters ADD COLUMN pov_entity_id INTEGER NOT NULL DEFAULT 0");
        tryAlter("ALTER TABLE chapters ADD COLUMN words INTEGER NOT NULL DEFAULT 0");
        tryAlter("ALTER TABLE chapters ADD COLUMN updated INTEGER NOT NULL DEFAULT 0");
        tryAlter("ALTER TABLE foreshadowings ADD COLUMN content TEXT NOT NULL DEFAULT ''");
        tryAlter("ALTER TABLE foreshadowings ADD COLUMN setup_ch INTEGER NOT NULL DEFAULT 0");
        tryAlter("ALTER TABLE foreshadowings ADD COLUMN payoff_ch INTEGER NOT NULL DEFAULT 0");
        tryAlter("ALTER TABLE foreshadowings ADD COLUMN importance INTEGER NOT NULL DEFAULT 50");
        tryAlter("ALTER TABLE foreshadowings ADD COLUMN truth TEXT NOT NULL DEFAULT ''");
        tryAlter("ALTER TABLE foreshadowings ADD COLUMN entity_ids_json TEXT NOT NULL DEFAULT '[]'");
        // 旧库若已有未加引号失败的半成品，不处理；全量建表已含 IF NOT EXISTS
    }
    const std::string now = std::to_string(util::NowMillis() / 1000);
    if (auto r = db_.Exec(fmt::format(
            "INSERT INTO meta(key,value) VALUES('schemaVersion','{}')"
            "ON CONFLICT(key) DO UPDATE SET value=excluded.value;",
            kTargetSchemaVersion)); !r) {
        return r;
    }
    schemaVersion_ = kTargetSchemaVersion;
    log::Info("NovelDb opened: {} schema=v{}", util::PathToUtf8(path_), schemaVersion_);
    return {};
}

bool NovelDb::RunSchemaSelfCheck() {
    NovelDb tmp;
    // 不碰单例；直接用局部 Database 跑内存库
    db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) {
        log::Error("NovelDb 自检：打开内存库失败 {}", r.error().message);
        return false;
    }
    if (auto r = mem.Exec(kSchemaV3); !r) {
        log::Error("NovelDb 自检：建 schema 失败 {}", r.error().message);
        const char* path = std::getenv("SHINE_NOVEL_CHECK_OUT");
        if (path && *path) {
            FILE* f = std::fopen(path, "wb");
            if (f) {
                const std::string msg = "schema:fail " + r.error().message + "\n";
                std::fwrite(msg.data(), 1, msg.size(), f);
                std::fclose(f);
            }
        }
        return false;
    }
    // 最小 CRUD：entity + chapter + foreshadow
    auto ins = mem.Prepare("INSERT INTO entities(kind,name,summary) VALUES('person','林默','主角')");
    if (!ins || !ins->Step()) {
        log::Error("NovelDb 自检：insert entity 失败");
        return false;
    }
    const auto eid = mem.LastInsertRowId();
    auto insCh = mem.Prepare("INSERT INTO chapters(title,body,ord) VALUES('第一章','正文',1)");
    if (!insCh || !insCh->Step()) {
        log::Error("NovelDb 自检：insert chapter 失败");
        return false;
    }
    auto insFs = mem.Prepare(
        "INSERT INTO foreshadowings(title,content,status) VALUES('黑戒指','戒指有秘密','PLANTED')");
    if (!insFs || !insFs->Step()) {
        log::Error("NovelDb 自检：insert foreshadow 失败");
        return false;
    }
    auto sel = mem.Prepare("SELECT COUNT(*) FROM entities WHERE kind='person'");
    if (!sel) {
        return false;
    }
    if (auto s = sel->Step(); !s || *s != db::sqlite::StepResult::Row || sel->ColumnInt(0) != 1) {
        log::Error("NovelDb 自检：select 计数失败");
        return false;
    }
    (void)eid;
    log::Info("NovelDb schema 自检通过（v{} 全表 + CRUD）", kTargetSchemaVersion);
    {
        const char* path = std::getenv("SHINE_NOVEL_CHECK_OUT");
        if (path && *path) {
            FILE* f = std::fopen(path, "wb");
            if (f) {
                const char* line = "schema:ok\n";
                std::fwrite(line, 1, std::strlen(line), f);
                std::fclose(f);
            }
        }
    }
    return true;
}

} // namespace shine::novelcore
