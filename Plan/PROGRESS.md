# ShineTV Studio — 进度表（合并后 2026-09-19）

> 唯一进度入口。状态：⬜ 未开始 ｜ 🟡 进行中 ｜ ✅ 通过 ｜ ⛔ 阻塞 ｜ ⏸ 搁置。
> **当前**：`main` = land + P5.7 + P6.1–P6.4 代码 + P8 关键项 + 小说系统 S9-auto-run + **S10 K01–K29 全量校验**；自检 **19 项全 ok**。

---

## 0. 总览

| 大类 | 内容 | 小任务 | 已完成 | 状态 |
|------|------|--------|--------|------|
| P3 | 节点与图 | 58 | 58 | ✅ |
| P4 | 媒体与纹理 | 27 | 27 | ✅ |
| P5 | 视频分镜 | 36 | 36 | ✅ 代码（P5.7 离线 PASS） |
| P6 | 画布 inpaint | 17 | 17 | ✅ 代码（离线 PASS；真机 inpaint 待 SD 模型） |
| P7 | MCP | 21 | 21 | ✅ |
| P8 | 收尾 | 13 | 11 | 🟡 余 10 次开关机压测 + 设置窗滚动验收 |
| G | 图片库 | 79 | 78 | ✅ 线完成（AVIF ⏸） |
| 小说 | P1–P10 | — | P1–P9 ✅；P10 余 1 | 🟡 |
| 小说系统 | S 序列 16 个 | 16 | 16 | ✅ 见 §0.1 |

**现在做哪个**

1. **真机 SD 全流程**：Comfy 在线 `:8188`；SD1.5 checkpoint 下载中 → 完成后跑 `runtime/sd-e2e` 分镜出图（P5.7 / P6.3）
2. **小说系统**：S 序列 16 个**已全完成**（见 §0.1）+ **S10/S11**（见 §0.2：K01–K29 全量校验 + 接进 G2/S1）；后续 = ① 章内重试产出阶段（`03` §2.6）；② K09/K23/K24 的承载表（`shots` 起止状态列等）；③ `09` 卷留后续项（09-7/09-8 模型路由与交叉复核、09-11 限流、09-12 全书预算）
3. **小说 Agent 连写**：`novels/mcp-bridge`《灯语回声》2 章（MCP 工具环，trace `agent/mcp-tool-loop.jsonl`）；P10.4 Garnet cache + LLM Key 待测
4. （可选）G AVIF

---

## 0.1 小说系统实施（16 个 S：S-doc-fix / S-doc / S0-pre / S0–S2 / S2b / S3-pre / S3–S9 / S-cleanup）🟡

> **规格**：`Doc/小说系统/00-总纲与索引.md` **§6**。**一个 S 一个分支**，八步：起分支 → 实现 → `cmake --build` → **自验** → 更新本表 + `证据.md` → 提交（只 add 本 S 文件）→ 合并 `main` → 下一个。⚠️ **建分支必须先于第一处编辑**（`00` §6.4 R7）。

- [x] **S-cleanup** 收口 S2b 残余（种子失败改 **Warn**（原 `(void)` 静默）；字段表 DDL **收敛为 `NovelFields::EnsureSchema` 唯一来源**（原 4 份，`Migrate` 那份缺 `status` 列且 ALTER 跑在建表之前）；落实 `08` §2.3 的 **500 条硬上限**）— `chore-post-s2b-hardening`｜规范 DDL 只剩 **1** 份；第 501 键被拒而更新既有键不受限；**15 项全 ok**、`[error]` 0 条
- [x] **S-doc-fix** 规格矛盾修正（`dialogue_styles` 提级 P2→P1 并提前；`character_arcs`/`world_meta`/`themes` 提前；新增 `S2b`/`S3-pre`；计数统一为 P0 8 / P1 10 / P2 23 / P3 3）— `docs-spec-conflicts`｜`P0+P1+P2+P3 = 44` 不变式核对通过；15 个 S→分支名两处一致
- [x] **S-doc** 文档集入库（14 卷 + `Doc/AGENTS.md`）— `docs-novel-system`｜`git ls-files` = 14、全 LF
- [x] **S0-pre** 孤儿回收 `ReapStaleImageJobs` — `s0pre-orphan-reap`｜`imagegen:ok`
- [x] **S0** MCP `input_schema` 修复（G22）+ 自检写保护 — `s0-mcp-schema`｜警告 94→0、`novelmcp=ok`
- [x] **S1** `visual_assets.status` + `visual_artifacts`（schema v7）— `s1-visual-status-artifacts`｜自检 v7 全绿；真实 v6 库迁移后旧数据零变化
- [x] **S2** P0 八表补 API（`plots`/`plot_beats`/`mysteries`/`mystery_beats`/`character_knowledge`/`event_participants`/`scene_cast`/`scene_foreshadows`）— `s2-p0-graph-api`｜8/8 表各有 `Upsert*`+`List*`（21 个接口）；`graph=ok`；K04/K05 入口可用
- [x] **S2b** 字段门禁（`08` §2.2 三步：归一化/查定义/校值）+ `field_defs.status`（schema v8）+ `field_aliases` 别名表 + `layer` 枚举化 + JSON 转义 — `s2b-field-gate`｜`fields=ok agents=ok novelmcp=ok`、**15 项全 ok**、ExitCode=0；`schema=v8` 自检覆盖 status 加列与回填 ⚠️ **本 S 未先建分支**，提交直接落在 `main`（`68d9aec`，无合并提交）
- [x] **S3-pre** 初始化链 API 前置（`character_arcs`/`dialogue_styles`/`world_meta`/`themes`）— `s3pre-init-api`｜**8 个接口**（`UpsertArc`/`ListArcs`、`UpsertDialogueStyle`/`GetDialogueStyle`、`SetWorldMeta`/`GetWorldMeta`、`UpsertTheme`/`ListThemes`）；`graph=ok`、**15 项全 ok**、`[error]` 0 条
- [x] **S3** `V0 ASSET_PIPELINE` 编排骨架（正脸→四视图→基础身体→服装）+ 依赖等待 C+B — `s3-asset-pipeline`｜`asset=ok`、**16 项全 ok**、ExitCode=0；四层父子链 + 幂等复用（不重复出图）；缺依赖按 C 挂起（未超期不降级、不产生任务）；超期按 B 降级 `degraded=1` + `audit_logs` + 降级清单；严格模式拒绝降级；失败路径资产置 `FAILED`
- [x] **S4** 生成侧严谨性（`object_info` 不跳过 + 降级记账）— `s4-gen-strictness`｜`SHINE_SCENE_IMAGE_CHECK` **pass=21 fail=0**；未就绪 → `blocked=true ok=false`（拒绝提交，不再假通过）；降级类型化 `no_reference`/`no_controlnet`/`size_aligned` + `DegradationsToJson` + `<输出目录>/degradations.jsonl` 追加落盘 + `VideoTaskState.degradations`；小说侧 16 项自检无回归
- [x] **S5** `job_ids_json` + 小队列 + 角色资产优先 — `s5-shot-multi-job`｜`Shot.jobs` 多 job 账（**后到不覆盖先到**）+ 存盘往返逐字节一致 + 旧工程宽容读取；队列**忙时入队**（不再静默丢弃）、优先级 `Asset(0) < SceneImage(10) < ShotVideo(20)`、中断顺带清队列；H3 侧降级类型化（`no_reference`/`ref_truncated`/`param_unified`/`chain_ignored`/`first_frame_ignored`/`name_collision`）；S5 自检 **25 项全 PASS**、16 项无回归；UI 截图验收通过
- [x] **S6** `ToGenShot` 桥（小说分镜 → `VideoProject`）— `s6-togen-shot-bridge`｜新增 `src/video/NovelShotBridge.*`（已登记 CMake）；**纯函数**（同输入同输出，`ToJson` 逐字节一致）；`seed` 由稳定键派生（**永不 -1**）；K20/K21 记账（`issues{checkId,severity}` + 降级账，纠正规则仍唯一来源 `Sanitize()`）；自带首帧的非首镜**不接链式**（否则首帧被盖）；空 prompt 整场失败不产半成品；S6 自检 **20 项全 PASS**、16 项无回归
- [x] **S7** P1 余下五表补 API + 快照（`entity_versions`/`location_distances`/`scene_visuals`/`writing_style`/`author_rules`；`dependencies` 延后）— `s7-p1-graph-api`｜**15 个新接口**（快照 6：写/读/列表/最新/打包快照/读回载荷；地点距离 3；写作风格 2；作者规则 2；场景视觉 2）；`graph=ok visual=ok`、16 项全 ok；**顺带修 A 级 bug**：`entity_personas."values"` 是 SQLite 关键字却被当裸标识符 → **人设从来写不进也读不出**（`GetCharacterSlice` 的 persona 一直空）；另修 S3 引入的逐项结果文件截断（`wb`→`ab`）
- [x] **S8** 闭环回写（`StateDiff` + 门禁 G1–G5）— `s8-state-commit`｜新增 `src/novel/NovelCommit.*`（已登记 CMake）；**22 项断言全 PASS**；端到端「生成一章 → 状态确有变化」（`character_status`/`foreshadowings`/`relations`/`entity_ownerships`/`event_participants`/`character_knowledge`/`canon_logs(PROPOSED)`/`chapters(done)` 逐块落库）；**重复提交幂等**（3 张表行数不变）；章级快照 `chNNN.json` + 提交前实体快照（I11）；D4/D7/D8 与 `auto` 模式一律拒绝；`commit=ok`、**17 项全 ok** ⚠️ **本 S 未先建分支**，提交直接落在 `main`（`638f283`，无合并提交 —— 与 S2b 同款偏差，已记录不再改写历史）
- [x] **S9-auto-run** 无人值守运行骨架（运行模式 / 停止条件 S1–S12 / **检查点每 10 章**（含 `08` §2.3 自动升格）/ 断点续跑 / 重试退避 / 单章调用上限 / `stop_report.md`）— `s9-auto-run`｜新增 `src/novel/NovelRunLoop.*`（登记 CMake）；**18 项全 ok**（`runloop=ok`）、`[error]` 1 条（S3 既有失败路径）；三态落 `audit_logs(run_mode)`；S1–S12 由纯函数 `EvaluateStop` 逐条可构造触发（自检各造一次 + 边界「不触发」）；`auto` 前置不满足**拒绝启动并给原因**（K01–K29 未全量）；`auto` Canon 打开（写 CANON，**门禁 G1–G5 不跳**）；检查点产出 `checkpoint_ch<A>_<B>.md` + 四条件 `PROPOSED`→`CANON`（含 `promote_field` 审计）；续跑不重跑已完成章；`cost_report.json`/`_manifest.json`/`stop_report.md` 落盘 ⚠️ **粒度偏差**：`03` 的 T1–T17 细阶段机未落地 → 记账/续跑粒度 = **章**、`_manifest.json` 的「产物文件」列为空；**本步不做** 09-7/09-8（模型路由与交叉复核）、09-11（限流）、09-12（全书预算）、UI 入口（`NovelView`）

> 🔧 **schema 单一来源（2026-09-19，chore，非 S）**：删掉 **9 处自检夹具**与 `AgentKit` 手抄的建表 SQL（合计 **121 条 `CREATE TABLE`**），统一走 `NovelDb::ApplyCanonicalSchema()`（夹具，幂等全量）/ `NovelDb::EnsureAgentSchema()`（`EnsureSchemaAndSeed` 这种**高频**入口只建 agent 表）。此后全仓 `CREATE TABLE` 只剩 **`NovelDb.cpp`（规范 v3–v8，75 条）**+ **`NovelFields.cpp`（字段三表，5 条，S-cleanup 已定）**；`graph=ok context=ok … asset=ok` 16 项全 ok、视频侧 21 项 PASS。

> 📄 **文档对齐（2026-09-19，doc-only，非 S）**：S4/S5/S6 落地后把规格回写实现 —— `02` §2.8 运行期字段改 `jobs`；`11` §2.5 补「实现」+ 链式/seed 细化、§2.7 **W2** 补降级账数据源、**W4/W5** 标已实现、差距 **11-16** 标已修、§4 勾 W4/W5 判据；`12` §1.3 状态隔离改为多 jobId；`13` **U8** 改为"已能承载"；`00` §6.2 S5 行注明实际字段名。理由与逐条对照见 `Plan/证据.md`「文档对齐」。

**基线坑**：`main` 上仍有**他人**的 `src/novel/NovelMcpTools.cpp`；`runtime/` 未跟踪（**不要提交**）。**每个 S 只 `git add` 自己那几个文件，绝不 `git add -A`。**

### 0.2 后续补丁（S 序列外）

> 16 个 S 已全完成，下面是「S 序列外」的收口项；同样一分支一提交。

- [x] **S9-ui** `NovelView` 无人值守 UI 入口（S9 只有代码 API + 自检，无法真跑）— `s9-runloop-ui`｜「小说」页新增「无人值守（S9）」段（运行模式 `manual`/`semi`/`auto` + 连跑章数上限 + 检查点周期 + 自动建下一章 + 「连跑」/「停止连跑」+ 状态与停止报告路径）；worker 调 `NovelRunLoop::Run`、进度与结果 `PostToUi` 回 UI；顺带加截图开关 `SHINE_NOVEL_OPEN=<书名>` 与 `SHINE_SIDE_VIEW/WINDOW=novel`；截图验收通过、**18 项自检无回归**
- [x] **S10** `06` §2.3 的 **K01–K29 全量机器校验**（`auto` 的唯一阻塞项）— `s10-k-checks`｜新增 `src/novel/NovelChecks.*`（已登记 CMake）；**29/29 实现点**（目录 `CheckCatalog()`，`CheckSpec::availability` 分 `library`/`artifact`/`contract-input` 三类），四态结果 `pass`/`fail`/`missing`/`n/a`（`n/a` = 无受检对象，空真放行但**分开记账**；`low` 按 `07` §2.3 放行）；`ValidationReport::RanIds()` = `06` §2.7 M1 的 `checks_run`；与 `07` G2 对接（`CommitContext::validation` + `CommitGateReport::g2_from_checks`）；`ProbeAutoPrecondition` 的 `verifiers_complete` 改由 `VerifiersComplete()` 判定（**不再恒 false**）；`sha1` 实现在本模块（`04` §2.5，FIPS 向量自检）；自检项 18 → **19**（`checks`）、**19 项全 ok**、ExitCode=0

**待办（未做）**：① K09 的 `shots` 起止状态列（`12` §1.4 缺口）与 `Beat[]`/`PromptArtifact` 的承载表 —— 这三组目前是 `contract-input`，传空即 `n/a`；② 按阶段继续跑（`03` §2.7 的阶段级续跑：`_manifest.json` 的「阶段 → 产物」列仍空）。
- [x] **S12** 「回到产出阶段重做」的真回路 + 落实不变式 I10 的产物落盘 — `s12-validate-retry`｜`NovelDirector` 在 EXTRACT↔提交门禁之间成环：**G2 机器校验挡下 → 重跑 extractor**（把 `checks_describe` 作为「请只修这些问题」喂回 user 消息），上限 `max_validate_retry=2`（`RunLimits::validation_retries` 下发）；**只有机器校验挡下才重做**（G1/G3/G4 类重跑无用）；重做后通过**不计**失败（否则误报 S1）；`CommitChapterState` 把本章 `StateDiff` 落成 `work/ch<ord>/12_state_diff.json`；端到端自检：第 1 版引用不存在实体 → 重做 1 次 → 第 2 版提交成功；**19 项全 ok**
- [x] **S11** 把 K01–K29 接进生产线（G2 真门禁 + S1 的数据来源）— `s11-k-checks-gate`｜`CommitChapterState` 在**章级快照之后**自己跑 `RunChapterChecks`（`g2_source="inline"`；调用方给报告则 `"caller"`），失败项以 `{check_id+name, severity, detail}` 并入 issue 账供 G5 与拒绝原因；`CommitResult::checks_describe`/`failed_check_ids` → `NovelDirector` → `ChapterObservation` → **S1 实测触发**（`命中停止条件 S1（chapter=1 check_id=K02 连续失败 2 次）`）；`CommitContext::project_dir` 补上；**顺带修** `plot_beats`/`mystery_beats` 的 `ord` 写死 0（K08 判不严格递增的成因）；**19 项自检全 ok**

**已知布局问题（既有，非本次引入）**：`小说` dock 面板偏矮，「生成本章」与「S9 连跑」段在默认 1600×900 窗口下会被裁掉（该 dock 窗口是 `NoScrollbar|NoScrollWithMouse`）→ 需手动拖大面板，或后续给该页加内部滚动。

---

## 1. P5 视频分镜 ✅

- [x] P5.1–P5.6（详见 `证据.md` 各段）
- [x] **P5.7 SceneToImage** — `src/video/SceneToImageBuilder.*`；`SHINE_SCENE_IMAGE_CHECK` **pass=10 fail=0**；真机出图需本机 SD/SDXL checkpoint

---

## 2. G 图片库 ✅（f7a0cb8 源码已在本树）

| 步骤 | 状态 |
|------|------|
| G-S0–S5 | ✅ |
| G-S6–S11, S13–S14 | ✅ Resize / ThumbnailService / LRU / 虚拟化 / Viewer / FileActions / EXIF / DiskThumbCache |
| G-S12a/b JPEG/WebP | ✅ `decoders/JpegDecoder` `WebpDecoder` + `third/libjpeg-turbo` `libwebp` |
| G-S12c AVIF | ⏸ 搁置 |

关键路径：`src/gallery/`、`src/app/ui/ThumbGrid.*`、`src/app/gallery/GalleryView.cpp`。

---

## 3. 小说 Agent

| 项 | 状态 |
|----|------|
| P1–P8 | ✅ 本树代码 |
| P9 出图 | ✅ 源码已 restore（`NovelImageGen`/`NovelImageStore`） |
| P10.1/2/3/5/6 | ✅（stdio/HTTP/SSE/tools/写开关/CLIENT.md） |
| P10.4 Garnet cache | 🟡 待本机 `127.0.0.1:6379` 实测 |
| 附加 | AgentKit / NovelFields / 多模型 LLM / schema v5 ✅ |

明细：`docs/compose/plans/novel-agent/PROGRESS.md`；原 `Plan/归档/novel-agent/PROGRESS.md` 已删除（`0bdf565`），原文 `git show 0bdf565^:Plan/归档/novel-agent/PROGRESS.md`。

---

## 4. P6 / P8

### P6 — 画布 inpaint ✅ 17/17

- [x] P6.1 数据模型 5/5 ｜ P6.2 画布 UI 4/4 ｜ P6.3 inpaint 服务 5/5（`SHINE_INPAINT_CHECK` PASS）｜ P6.4 图库/媒体联动 3/3

### P8 — 收尾 🟡 11/13

- [x] 版本单一来源 / Shutdown 顺序 / worker 兜底 / 日志落盘 / 设置画布段 / 首启开设置窗
- [ ] 10 次开关机压测、设置窗滚动验收

---

## 5. 合并与运维

| 项 | 说明 |
|----|------|
| 代码来源 | `git restore --source=f7a0cb8 --worktree -- …` + 本分支 P5.7 |
| 运维脚本 | `scripts/comfy.ps1`（status/start/stop/restart/progress）· `scripts/studio.ps1`（含 build） |
| 验收日志 | `SHINE_LOG_FILE=<abs>`；自检 `SHINE_SCENE_IMAGE_CHECK` / `SHINE_MCP_*CHECK` / `SHINE_NOVEL_GRAPH_CHECK` |
| SD e2e | `runtime/sd-e2e/{novels/rain-signal,projects,scripts,output}`；`scripts/run-comfy-chapter.ps1 -ProjectJson <ch*.json>` |

---

## 6. 更新约定

1. 只在本文件勾进度；证据写 `证据.md`（**结论 + 关键数字**，不要原始日志块与逐文件清单）。
2. 发现「代码有、表没有」先 `git log --all -- <path>`。
3. 一次一个 S；规则 `Doc/RULES-AI.md`。
