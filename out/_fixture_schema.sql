
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
  status TEXT NOT NULL DEFAULT 'PENDING',
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
  -- v9（S13）：叙事分镜的**可校验结构**（`12` §1.4 原先缺的正是这三列）
  --   `start_state_json` / `end_state_json` = `02` §2.7 的 `StateSnapshot`（K09 连续性逐字段比对的对象）
  --   `timeline_json` = `{"duration_s":N,"beats":[{"begin_s":..,"end_s":..}]}`（`02` §2.9 Beat[]；K24）
  start_state_json TEXT NOT NULL DEFAULT '{}',
  end_state_json TEXT NOT NULL DEFAULT '{}',
  timeline_json TEXT NOT NULL DEFAULT '{}',
  -- v11（S36）：`02` §2.7 的 V2 `DIRECTOR_INTENT`（七问：see/know/not_know/emotion/
  -- emotion_shift/climax/pace + intensity）。原先是**无处可落**（S33 记账的洞：V2 的产物
  -- 在 `NarrativeShot` 无承载字段 ⇒ 落库时丢掉 ⇒ 无从比对）。旧库走 `AddShotIntentColumn`。
  intent_json TEXT NOT NULL DEFAULT '{}',
  canon_status TEXT NOT NULL DEFAULT 'PROPOSED');
CREATE INDEX IF NOT EXISTS idx_shots_scene ON shots(scene_id);

CREATE TABLE IF NOT EXISTS visual_canon_logs(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  target_kind TEXT NOT NULL,
  target_id INTEGER NOT NULL,
  status TEXT NOT NULL DEFAULT 'PROPOSED',
  note TEXT NOT NULL DEFAULT '',
  created INTEGER NOT NULL DEFAULT 0);


CREATE TABLE IF NOT EXISTS generated_images(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  job_id TEXT NOT NULL DEFAULT '',
  backend TEXT NOT NULL DEFAULT '',
  model TEXT NOT NULL DEFAULT '',
  prompt TEXT NOT NULL DEFAULT '',
  negative TEXT NOT NULL DEFAULT '',
  width INTEGER NOT NULL DEFAULT 0,
  height INTEGER NOT NULL DEFAULT 0,
  steps INTEGER NOT NULL DEFAULT 0,
  rel_path TEXT NOT NULL DEFAULT '',
  source_kind TEXT NOT NULL DEFAULT '',
  source_id INTEGER NOT NULL DEFAULT 0,
  chapter_id INTEGER NOT NULL DEFAULT 0,
  scene_id INTEGER NOT NULL DEFAULT 0,
  status TEXT NOT NULL DEFAULT 'PROPOSED',
  error TEXT NOT NULL DEFAULT '',
  raw_meta TEXT NOT NULL DEFAULT '{}',
  checklist_json TEXT NOT NULL DEFAULT '{}',
  created INTEGER NOT NULL DEFAULT 0,
  updated INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS idx_gimg_status ON generated_images(status);
CREATE INDEX IF NOT EXISTS idx_gimg_source ON generated_images(source_kind, source_id);


CREATE TABLE IF NOT EXISTS visual_artifacts(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  asset_id INTEGER NOT NULL DEFAULT 0,
  layer TEXT NOT NULL DEFAULT '',
  chapter_scope INTEGER NOT NULL DEFAULT 0,
  chapter_to INTEGER NOT NULL DEFAULT 0,
  rel_path TEXT NOT NULL DEFAULT '',
  parent_artifact_id INTEGER NOT NULL DEFAULT 0,
  prompt_artifact_id INTEGER NOT NULL DEFAULT 0,
  job_id TEXT NOT NULL DEFAULT '',
  status TEXT NOT NULL DEFAULT 'PENDING',
  degraded INTEGER NOT NULL DEFAULT 0,
  note TEXT NOT NULL DEFAULT '',
  created INTEGER NOT NULL DEFAULT 0,
  updated INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS idx_vartifact_asset ON visual_artifacts(asset_id);
CREATE INDEX IF NOT EXISTS idx_vartifact_layer ON visual_artifacts(asset_id, layer);
CREATE INDEX IF NOT EXISTS idx_vartifact_status ON visual_artifacts(status);


CREATE TABLE IF NOT EXISTS prompt_artifacts(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  chapter_id INTEGER NOT NULL DEFAULT 0,
  scene_id INTEGER NOT NULL DEFAULT 0,
  shot_id INTEGER NOT NULL DEFAULT 0,
  target_kind TEXT NOT NULL DEFAULT 'shot',
  target_id INTEGER NOT NULL DEFAULT 0,
  chain TEXT NOT NULL DEFAULT 'visual',
  stage TEXT NOT NULL DEFAULT '',
  input_state_hash TEXT NOT NULL DEFAULT '',
  model_hint TEXT NOT NULL DEFAULT '',
  prompt TEXT NOT NULL DEFAULT '',
  negative TEXT NOT NULL DEFAULT '',
  references_json TEXT NOT NULL DEFAULT '[]',
  canon_status TEXT NOT NULL DEFAULT 'DRAFT',
  created INTEGER NOT NULL DEFAULT 0,
  updated INTEGER NOT NULL DEFAULT 0,
  -- v10（S26）：PV3 多版本保留 / PV5 结果关联（`13` §2.7）。旧库走 `AddPromptArtifactColumns`。
  version INTEGER NOT NULL DEFAULT 1,
  generation_ref TEXT NOT NULL DEFAULT '');
CREATE INDEX IF NOT EXISTS idx_partifact_chapter ON prompt_artifacts(chapter_id);
CREATE INDEX IF NOT EXISTS idx_partifact_shot ON prompt_artifacts(shot_id);


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

-- ===== v12 stage_artifacts（EnsureStageArtifactsTable，NovelDb.cpp:858-869）=====
CREATE TABLE IF NOT EXISTS stage_artifacts(
id INTEGER PRIMARY KEY AUTOINCREMENT,
chapter_id INTEGER NOT NULL,
stage TEXT NOT NULL,
scene_ord INTEGER NOT NULL DEFAULT 0,
shot_ord INTEGER NOT NULL DEFAULT 0,
payload_json TEXT NOT NULL DEFAULT '{}',
input_state_hash TEXT NOT NULL DEFAULT '',
created INTEGER NOT NULL DEFAULT 0,
UNIQUE(chapter_id,stage,scene_ord,shot_ord));
CREATE INDEX IF NOT EXISTS idx_stage_artifacts_chapter_stage ON stage_artifacts(chapter_id,stage);