---
feature: novel-studio
status: designed
updated: 2026-02-14
branch: (no-git — 主树施工；先交付设计稿评审)
commits: n/a
---

# 小说系统：章节 / 人设 / 人物状态 / 世界观 / 物品 / 剧情规划

## Report

> 关联规格：`docs/compose/spec/novel-agent.md`（OpenAI Responses + 叙事知识图谱 + Agent 管线）。两文档共用 `novel.db`；studio 管编辑 UI，agent 管生成与推演。

## [S1] Problem

小说模块目前只有「新建工程 + 列表」：`CreateProject` 建了 `novel.db`，schema v1 含空的 `chapters` / `characters` / `foreshadowings`，但**没有编辑器**（`NovelView` 里是 TODO）。用户需要完整创作台：

- 章节（正文 / 状态 / 摘要 / POV）
- 人设（外貌 / 性格 / 背景 / 目标 / 秘密）
- 人物状态（按章的动态档案：位置 / 健康 / 情绪 / 知情 / 关系）
- 人物故事状态 / 弧光
- 世界观（势力 / 地点 / 规则 / 历史）
- 物品描述
- 剧情规划（幕 / 节拍 / 章节绑定 / 出场）
- 伏笔（已有表，需 UI）

## [S2] Design

### 信息架构（与设计稿一致）

```
小说工程列表
  └─ 打开工程
       活动栏: 章节 | 人物 | 世界观 | 物品 | 伏笔 | 剧情
       侧栏:   当前分区条目列表
       中央:   编辑主区
       右栏:   选中项属性 / 联动
```

### 数据模型（schema v2+，SQLite `novel.db`）

**统一档案实体**（人物 / 衣服 / 道具 / 法宝 / 物品）+ 四视图 + 持有 + 生命周期。

| 表 | 关键字段 | 说明 |
|----|----------|------|
| `meta` | key/value | `schemaVersion=2` |
| `chapters` | id, title, body, ord, status, summary, pov_character, words, updated | 扩展现有 |
| `entities` | id, kind, name, summary, note, tags_json, created, updated | **kind**: `person`/`clothing`/`prop`/`treasure`/`item` |
| `entity_images` | id, entity_id, role, path, ord, w, h, crop_json | **一张四视图立绘** + 可选细节图。`role`: `sheet`（四视图主图）/`portrait`（头像裁切）/`detail`。crop_json 可选：记录 sheet 上四区块 UV，便于预览高亮 |
| `entity_personas` | entity_id, appearance, personality, background, goal, secret | 仅 kind=person |
| `entity_ownerships` | id, entity_id, owner_id, rel, from_chapter, to_chapter, status, note | status: held/stored/lost/destroyed；to 空=仍持有 |
| `entity_events` | id, entity_id, event_type, chapter_id, from_owner_id, to_owner_id, location, note, ord, created | 流转时间线 |
| `character_status` | id, entity_id, chapter_id, location, health, mood, knowledge, relation, updated | 人物状态（挂 entities） |
| `character_arcs` | id, entity_id, note, ord | 弧光 |
| `worldview` | id, name, kind, note | 势力/地点/规则/历史 |
| `foreshadowings` | id, title, status, note, setup_ch, payoff_ch | 伏笔 |
| `plot_acts` / `plot_beats` | … | 剧情规划 |

**event_type**：`created` | `appeared` | `acquired` | `transferred` | `upgraded` | `damaged` | `lost` | `destroyed` | `restored`

**图片磁盘布局**：`<工程目录>/assets/<entity_id>/sheet.png`（四视图一张）、可选 `portrait.png`、`detail_*.png`。库内只存相对路径。  
**四视图约定**：主预览显示整张 sheet（头像/正/侧/背 同图）；点击可放大；实现阶段可用 crop 只预览某一格，但**存储仍是一张图**。

**持有语义**：
- 「A 拥有 B」= `entity_ownerships(entity_id=B, owner_id=A)`，`status='held'` 且 `to_chapter IS NULL`（或按章区间查询）。
- 历史持有保留区间行，不物理删除；销毁事件写 events，并把该物品未闭合 ownership 的 status 置 `destroyed`。
- UI：人物页「拥有物」列表可 **点击跳转** 到对应档案；物品页「持有者」可跳回人物。

**查询**：
```sql
-- 人物当前持有
SELECT e.* FROM entity_ownerships o
JOIN entities e ON e.id=o.entity_id
WHERE o.owner_id=?1 AND o.status='held' AND o.to_chapter IS NULL;

-- 物品时间线
SELECT * FROM entity_events WHERE entity_id=?1 ORDER BY ord, id;
```

迁移：v1 → v2 建新表；原 `characters`/`items` 数据可迁入 `entities`（kind 对应）+ `entity_personas`。

设计稿（本轮）：`novel-assets-preview.html` — 四视图网格、点击放大、拥有/被持有跳转、生命周期时间线、Schema 页。

### UI 模块（`src/app/novel/`）

| 文件 | 职责 |
|------|------|
| `NovelView.*` | 工程列表（保留）+ 打开后工作区外壳 |
| `NovelWorkspace.*` | 活动栏分区切换、侧栏列表、中央/右栏分发 |
| `NovelChapters.*` | 章节列表 + 正文编辑 + 快照按钮 |
| `NovelEntities.*` | 档案网格（按 kind 过滤）+ 详情：四视图 / 概览 / 拥有 / 时间线 |
| `NovelEntityImages.*` | 一张四视图 sheet 预览/放大、导入图片 |
| `NovelCharacters.*` | 人设 / 状态时间线 / 弧光（基于 entities kind=person） |
| `NovelWorldPlot.*` | 世界观 + 伏笔 + 剧情节拍 |

业务读写在 `src/novel/`（`NovelDb.*` / `NovelQueries.*` / `NovelAssets.*`），**不写 ImGui**。图片解码复用 `gallery` 的 PNG 路径或独立最小加载。

### 交互约定

- 档案网格 + 详情四视图（头像/正面/侧面/背面），点击放大
- 持有关系、事件里的实体名均可 **点击跳转** 到对应档案
- 生命周期按 `ord` 时间线展示；新建事件类型固定枚举
- 列表用 `ImGuiListClipper`；颜色 `theme::Current()`
- 线程：UI 线程短事务写 SQLite；大图异步解码后回 UI（同 gallery）

### 设计稿

| 文件 | 内容 |
|------|------|
| `novel-design-preview.html` | 工作区六分区总览 |
| `novel-assets-preview.html` | **本轮**：档案网格、四视图、拥有跳转、流转时间线、Schema |

## [S3] Out of Scope

- 云同步 / 多作者协作
- 自动续写、LLM 生成正文
- 3D 模型 / 视频预览（仅静态四视图位图）
- 版本历史 diff、导出 epub
- 将 HTML 嵌入 C++ 宿主

## Tasks

- [ ] T1: schema v2+（entities/images/personas/ownerships/events）+ 迁移 + CRUD — acceptance: 新建库表齐全；v1 可升级 (covers: S2)
- [ ] T2: 工程工作区外壳（分区切换）— acceptance: 打开书后分区可切换 (covers: S2)
- [ ] T3: 章节编辑器 — acceptance: 增删改章与正文/状态/摘要 (covers: S2)
- [ ] T4: 档案网格 + 一张四视图 sheet 详情 + 图片导入 — acceptance: 五类 kind 过滤；详情展示整张立绘可放大；落盘 assets/<id>/sheet.png (covers: S2)
- [ ] T5: 持有关系双向跳转 — acceptance: 人物「拥有物」与物品「持有者」可点进对方详情 (covers: S2)
- [ ] T6: 生命周期时间线 CRUD — acceptance: 九种事件可添加并按序展示 (covers: S2)
- [ ] T7: 人物状态/弧光 + 世界观/伏笔/剧情 — acceptance: 与档案实体 ID 打通 (covers: S2)
- [ ] T8: 编译运行 + 文档同步 — acceptance: build 通过；skill 目录说明更新 (covers: S2)
