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

**🔴 跑真实流程的唯一缺口 = LLM API Key（外部资源）**：`settings.json` 里 `openaiApiKey` 等**全部为空**。代码路径已通（见 S15 的实跑日志）：
填好 Key 后，`SHINE_NOVEL_OPEN=rain-signal` + `SHINE_NOVEL_GENERATE=0`（或直接点「生成本章」）即可真生成一章。

**仍未做的（都不阻塞跑流程，见 §0.3 的说明）**：`03` 的 T1–T17 细阶段机（真阶段级续跑）、V9 分镜链本体（影视化链）、`10` 初始化链编排、`09` 的 09-7/09-8/09-11/09-12、K19–K21 由生成侧回填、真机 SD 出图、P8 的开关机压测。
- [x] **S28** **K19–K21 的盘上回填** + `--novel-checks` CLI（章级 K 校验）+ **main.cpp 子命令分派根治** — `s28-k19-21-readback`｜① `NovelChecks` 新增 `LoadGenerationChecks`：从 `work/ch<NNN>/generation_checks.json`（V11 落）读回 `GenerationCheckInput`，**键名与结构一一对应**；`RunChapterChecks` 里"调用方给了就用、没给就读盘"（同 `shots`/`prompt_state_hash` 的模式）⇒ **K19–K21 不再是恒 `n/a` 的摆设**（实测 K20/K21 由 `n/a` → **`pass`**，K19 在后端非 Comfy 时**如实 `n/a`**）；V11 的 JSON 同步改字段（`sizeAligned` 布尔 → `size_corrections` 计数，对齐 `GenerationCheckInput`）；② 新增 CLI **`--novel-checks <ch>`**（此前章级 K 校验只能在提交路径内部跑，命令行看不到）+ `--all`（打印全部 29 条，验收要看"哪几条真有结论"）；③ 🔴 **`main.cpp` 的子命令白名单表被漏过 3 次**（S21 `--novel-init`、S27 `--novel-generate-images`、S28 `--novel-checks`）⇒ 改成**前缀匹配 `--novel-`**，以后加子命令不必再改本文件（漏登记的后果不是报错，是"照常进 GUI 卡住不退出"）；**真跑**：`--novel-prompt 1`（`新写 6` —— PV4 让旧产物失效）→ `--novel-generate-images 1`（`6/0/0`）→ `--novel-checks 1 --all`（`pass=17 fail=7 n/a=5`，K09/K22/K23 **pass**）；**23 项全 ok**
- [x] **S27** **V11 `GENERATION` 出图接线**（`03` §2.2 的**可选下游动作**）+ **PV4/PV5 收口**（`generation_ref` 有值）— `s27-v11-generation`｜新增 `src/novel/NovelGeneration.*`（登记 CMake；App 自检 **22→23 项**）：该章每个**最新版** `PromptArtifact` → `video::ToGenShot`（桥，出 `VideoProject` + K20/K21 记账）→ `RunImageJob`（出图队列）→ 写 `visual_artifacts`（**shot 层**，`prompt_artifact_id` 从此不空）→ **回填 `prompt_artifacts.generation_ref`**（PV5：`va:<id>`；该镜无可挂资产时 `img:<generated_images.id>`）；**不调 LLM、不做评审**（`09` §2.4）、**单镜失败不阻断**（`09` §2.2）、**`generation_ref` 非空 ⇒ 跳过**（幂等）；K19–K21 的账落 `work/ch<NNN>/generation_checks.json`；CLI `--novel-generate-images`（`--force` 重出 / `--image-backend` **只覆盖本次进程**）；**PV4**：`prompt_layers` 进哈希清单 + `SetLayer` 版本递增（改层文本 → 哈希变 → 重算 + Prompt `version+1`）；🔴 **顺手修 3 个真问题**：① 自检必须把出图后端换 `mock`（默认 comfy 是 stub —— 照 P9 自检做法 + **RAII** 保证恢复设置）② **每镜失败原因没进 `warnings`**（真跑只报"没有产出任何出图任务"，无从排查）③ **无可挂资产的镜永不回填 `generation_ref` ⇒ 每次重跑都重复出图**（幂等被破坏，run2 实测 `新提交 2`）；**真跑**：run1 `新提交 6/跳过 0/失败 0`、run3 `2/4`、**run4 `0/6`（完全幂等）**；**23 项全 ok**
- [x] **S26** `prompt_artifacts` 加 `version`/`generation_ref`（**schema v10**）+ 空间层进 prompt（`Assemble` 九层→**十层**）— `s26-prompt-artifact-columns`｜① **PV3 多版本保留**：V10 改**新写一行 + `version + 1`**（原先是覆盖同 id，版本历史不可回溯）；`generation_ref` 列就位但**值留空**（等 V11 才有对象可指）；② **第 5 层 `spatial`**（`12` §2.5：`layers{foreground,midground,background}` + `facing`/`distance_m`/`occlusion`）—— V9 只存盘，V10 从 `work/ch<NNN>/storyboard.json` 读（`11` §2.2 说的"V10 时从那读"就是这里）；③ 🔴 **顺带修两个真 bug**：(a) **CLI 自己开库、从不走 schema 迁移** ⇒ 旧库上永远补不出新列（实测 `no column named version`，而 GUI 路径是好的）⇒ 新增 **`NovelDb::EnsureSchemaUpToDate`** 一处收口；(b) 自检第一版在**同一内存库** DROP/重建旧形状表 → **毒害**后面的 v9 段（`no such column: shot_id`）⇒ 改**独立内存库**；④ **规则版本 2→3**（S26 又改了组装规则 —— S25 那条教训**当天复用**）；**真跑**：ch1 `新写 0/复用 6`、ch2 **`新写 6/复用 0`**（证明 v10 两列写入正常）；**22 项全 ok**
- [x] **S25** 收 S24 留的两个尾巴：**多角色拼装** + **组装规则版本参与哈希**（`04` §2.5 / `13` PV7）— `s25-multi-character-prompt`｜① `AssemblePromptInput` 加 `character_ids`，`Assemble` 对**每个**出场角色各拼 `base`/`stage`（原先只拼第一个 ⇒ 一镜两人时第 2 人隐形）；同时删掉 `Assemble` 里"用第一个角色预解析 `assetId`"的老逻辑（它把多角色分支整个挡掉 —— 自检撞出来的）；② 🔴 **更深一层根因**：改拼装规则后哈希不变 ⇒ **真实工程的旧产物永远被复用**（修了 bug 却不生效）⇒ `04` §2.5 的哈希清单新增 **`prompt_rule_version`**（规则变 = 版本 +1，当前 2），并把 **`chain=visual` 的视觉输入**（`visual_assets`/`visual_states`）正式写进规格（原先只在代码注释里）；`13` §2.7 新增 **PV7**；**真跑证据**：加版本号前对 `rain-signal` 重跑是「复用 6」（旧残缺 prompt 留着），加版本号后 → **「新写 6」**、再跑一次 → **「复用 6」**（失效与复用双向都对）
- [x] **S24** 影视化链 **V10 `PROMPT_GEN`**（九层组装 → `PromptArtifact` 账）— `s24-prompt-gen`｜**先查后写**：组装**早就存在**（`NovelVisual::Assemble` 九层 + `prompt_layers`），V10 缺的只是**账** ⇒ 新增 `src/novel/NovelPromptGen.*`（已登记 CMake）把 `Assemble` 结果写成 `PromptArtifact`（`chain=visual`/`stage=V10`/`target_kind=shot`，带 `input_state_hash` + `references_json` = 出场角色资产的 `sheet_rel_path`）；**不调 LLM**；PV1/PV2/I9：同哈希**复用**、变了**重算**；CLI `--novel-prompt <chapter_id>`（同步登记 `main.cpp` 分派表）；🔴 **修两个真问题（自检逼出来的）**：① V10 原先没把角色传给 `Assemble` → 拼不出 base/阶段外观；② **`ComputeInputStateHash` 的输入清单没有视觉输入** → 改角色 `base_desc` 哈希不变、产物被误判可复用 ⇒ 为 `chain=visual` 补充 `visual_assets`/`visual_states`；自检 **21 → 22 项**（`promptgen=ok`）；**真跑**（对真实工程 `rain-signal`）：第一次 `V10 已产出：6 镜（新写 6，参考图 4 条）`、第二次 `新写 0 / 复用 6`（PV2 实证）
- [x] **S23** 影视化链 **V9 `STORYBOARD`**（`NarrativeShot[]` → `shots` 表）— `s23-storyboard`｜新增 `src/agent/NovelStoryboard.*`（已登记 CMake）：读该章 `scenes` → **一次 LLM 推演 V1–V8**（SCENE_BREAKDOWN…AUDIO 合并，同 `03` §2.2 允许 T2–T9 合并）→ 解析 `02` §2.7 契约 → 落 `shots`；`character_ids_json` 从 `start_state.characters` 抽出（**K22 的受检对象**）、`timeline_json` 用 `{duration_s,beats}`（K24 的读法）、`duration`→`duration_note`；**无专列的契约字段只存盘**（`work/ch<NNN>/storyboard.json` 存完整契约 + `warnings` 显式说明，不硬塞语义不符的列）；**不做 V8 的判定**（CONTINUITY 归 K09）；CLI `--novel-storyboard <chapter_id>`（**已同步登记 `main.cpp` 分派表**）；🔧 **顺带修既有 bug**：`NovelVisual::UpsertShot` 名为 Upsert 却**只有 INSERT** → 加 `id>0` 更新分支（第二遍落库不再插重复行）；自检 **20 → 21 项**（`storyboard=ok`）；真跑：`--novel-storyboard 1` → **秒退** ExitCode=1、`[no_key] 未配置 OpenAI API 密钥`
- [x] **S22** 修 `--novel-init` 未被 headless 分派（**S21 的真 bug**）— `s22-cli-dispatch`｜`main.cpp` 的分派条件只列了 `--novel-generate`/`--novel-run`，**漏了 `--novel-init`** ⇒ 该命令**照常进 GUI 建窗口**（表现为"卡住不退出"）。改为**子命令列表**遍历（`--novel-init`/`--novel-generate`/`--novel-run`），并在注释里写明"漏登记的后果不是报错而是进 GUI"。**真跑验证**：① `--novel-init --gate-only` 对 `rain-signal` → **秒退**、ExitCode=1、报出 5 条缺失（N1/N2/N3/N8/N9）；② 新库从零初始化 → **约 0.9 秒**建 8 项骨架、门禁只剩 N4/N5/N6（内容类）、`init/I15_gate.json`+`I16_commit.json` 落盘
- [x] **S21** 初始化链：**「可开写」门禁 N1–N14 + 结构骨架 + 阶段表**（收口 `10` 的差距 10-1/10-2/10-3，三条都是 A 级）— `s21-init-chain`｜新增 `src/novel/NovelInit.*`（已登记 CMake）：`CheckInitGate` 逐条判定 N1–N14 → `InitReport{passed, failures[{n_id,detail,fix_hint}]}`（**不允许警告后放行**）；`RunInitSkeleton`（路径 B/D，不调 LLM）建结构骨架（书题/文风/硬规则/1 卷/主线/谜团/世界级秘密 + `field_defs` 11 种子 + 15 Agent）—— ⚠️ **内容类条件不伪造**（主角/人设/地点仍由门禁如实报缺）；`InitStageCatalog` = I1–I16 阶段表（**单一来源**）+ `init/` 落盘（`I15_gate.json`/`I16_commit.json`）；CLI **`--novel-init [--book X] [--target-chapters N] [--gate-only]`**（退出码 **1 = 还不能开写**，逐条打印缺什么 + 修法）；自检 **19 → 20 项**（`init=ok`）：空库必报 N1–N10/N12/N13、N11/N14 属「存在则必须合规」型（另构造 fail 验证）、骨架后结构类转通过而内容类仍缺、补齐内容后**门禁转通过**
- [x] **S20** `PromptArtifact` 落库（**K23 从 `n/a` 变真跑** + 收口 `03-11`）— `s20-prompt-artifact`｜新增 `NovelStageLedger::RecordStageArtifact` / `LoadStageHashRecord`：正文链 4 个输入侧阶段（`CONTEXT_ASSEMBLY`/`SCENE_EVENT_ORDER`/`CHAPTER_REVIEW`/`CHAPTER_REPAIR`）落盘时**同时记账到 `prompt_artifacts`**（`chain=text`、`stage`、`input_state_hash`；同章同阶段**只留一行**；`prompt` 只存前 400 字节 + `model_hint=bytes=N` —— 库做**哈希账**，完整产物在 `work/`）；**`STATE_VALIDATE` 只落盘不落库**（它是提交后的结论账，进库会污染复用判定）；`NovelDirector` 改调 `RecordStageArtifact`；自检：Director 断言库里有阶段账、Checks 断言**库里有产物（哈希一致）→ K23 pass（不再 n/a）**、改坏哈希 → K23 fail（I9）；**19 项全 ok**
- [x] **S19** 正文链**阶段产物落盘 + 断点续跑**（`03` §2.7 的 P1/P2/P4/P5）— `s19-stage-machine`｜新增 `src/novel/NovelStageLedger.*`（`03` §2.7 的文件名表是**单一来源**；包装格式含 `input_state_hash`；`FindResumeIndex` 做 P1/P2/P5 判定）；`NovelDirector` 在 T5/T10/T12/T13/T15 落盘（`04_context_pack.json`/`09_chapter_plan.json`/`10_review.json`/`11_repair_receipt.json`/`13_validation.json`）；**续跑**：`GenerateChapterRequest::resume`（**连跑默认开**、UI「生成本章」默认关=重写、CLI 加 `--resume`）—— 哈希一致的阶段**跳过 LLM**（P1）、`chapters.body` 已落库则**不再请求 Writer**（P4）、状态变了则该阶段及下游重跑（P2）；`_manifest.json` 阶段账 **3 → 7 段**；自检：产物落盘 4 文件 + P1/P2/P5 三向判定 + 端到端「第二遍 Writer 未再请求、Planner 按 P2 重跑」；**19 项全 ok**（⚠️ 未做：T2–T9 逐步展开 = `03-9`；`PromptArtifact` 落库 = `03-11`，故 K23 仍 `n/a`）
- [x] **S18** MCP 工具 `novel_generate_chapter`（AI 助手可直接驱动生成）+ 修 stdio 日志污染｜⚠️ **流程偏差**：本 S **未先建分支**，提交直接落在 `main`（`2e50ad4`，无合并提交）—— 与 S2b/S8 同款偏差，**如实记账、不改写历史**（内容与自检均已复核）｜生成实现由装配层**注入**（`NovelMcpTools::SetChapterGenerator`，`src/novel` 不依赖 `openai`/`app`），注入的就是 UI/CLI 那条路（`NovelPipeline::GenerateOneChapter`）；写工具（受 `McpWriteAllowed` 管，未注入实现时明确报错不假装成功）；novel 模块工具数 **25 → 26**；**端到端实测**（`--mcp-stdio` + `SHINE_MCP_ALLOW_WRITE=1`）：`tools/call novel_generate_chapter {chapter_id:1}` → `{"chapter_id":1,"result":"生成失败：未配置 OpenAI 的 API Key（设置 → LLM）"}`；🔴 **顺带修既有真 bug**：stdio 模式下日志写进了 **stdout**（协议流被污染、客户端会解析失败）→ 加 `SHINE_LOG_TO_STDERR`（`log::Init` 支持；`--mcp-stdio` 分支用 **`_putenv_s`** 打开，`SetEnvironmentVariableA` 不同步给 `getenv`）；修后 stdout **只剩 2 行 JSON-RPC**、日志 49 行进 stderr；**19 项全 ok**
- [x] **S17** headless CLI（`--novel-generate` / `--novel-run`）+ 生成/连跑抽成 **UI 与 CLI 共用入口** — `s17-cli-entry`｜新增 `src/app/novel/NovelPipeline.*`（`MakeLlmCall` 按 `LlmRole` 选模型 / `CrossReviewEffective` / `GenerateOneChapter` / `FillPreconditions` / `RunOnce`）与 `NovelCli.*`（手写参数解析，无新依赖）；`main.cpp` 在 `--mcp-stdio` 之后加分支（**不建窗口**）；`NovelView` 改调 pipeline（删掉本地 LLM 回调与内联 worker —— 上一轮只是 UI 内部抽函数，这轮才真正跨载体共用）；退出码 0/1/2（成功 / 业务失败 / 参数错）；`SHINE_NOVEL_CHECK_OUT` 追加 `novel-cli:ok|fail <detail>`；**真跑**：`--novel-generate 0` → 自动选章 #1 → `生成失败：未配置 OpenAI 的 API Key（设置 → LLM）`、ExitCode=1（⚠️ 顺带修真 bug：CLI 路径没调 `LoadSettings()`，库路径读不到）；**19 项全 ok**
- [x] **S16** `09` 卷四项收口（**09-7 模型分层 / 09-8 交叉复核 / 09-11 限流 / 09-12 全书预算**）— `s16-model-routing`｜`agent::LlmCallFn`/`LlmStreamFn` 加 **`LlmRole`**（Planner/Writer/Critic/Extractor）→ 调用方按 `openai::ResolveModel(LlmRoleName(role))` 选模型 → `openai::LlmComplete(..., model)` 可指定模型（Chat/Responses/Anthropic 三路都通）；**09-8 硬要求**：判**生效模型** `critic ≠ writer`，相同则 UI ⚠ 提示 + **`auto` 前置⑤拒绝**（manual/semi 只提示）；**09-12**：`EstimateBookBudget`（剩余章 × 每章上限 40）+ 新配置 `novelMaxTotalLlmCalls`（0=不限）→ **前置⑥拒绝**，拒绝原因带估算明细（⚠️ 单价未纳入：无 token 计数，不假装算钱）；**09-11** 并发=1/间隔 200ms/429 退避（S9 已有，S16 补断言）；自检新增「四个 LlmRole 均到回调 0b1111」「⑤/⑥ 拒绝且原因可读」「预算估算 min(剩余,上限)」；**19 项全 ok**
- [x] **S15** 真实流程前置收口（LLM 真判 + 一键跑法）— `s15-real-flow`｜🔴 修真 bug：`Run` 里 `ProbeAutoPrecondition(*db_, true)` **硬编码** → 空 Key 也能启动 auto（新增 `RunRequest::llm_ready` 由调用方给真实值）；抽 `StartChapterGeneration`（按钮与验收开关**共用同一条路**）+ 空 Key 可读提示不进 worker；「连跑」同样前置检查；新增 **`SHINE_NOVEL_GENERATE=<id>`** 验收开关（⚠️ **时间驱动**而非帧驱动 —— 实测 12 秒跑不到 30 帧）；**真实工程实跑**：`SHINE_NOVEL_OPEN=rain-signal` + `SHINE_NOVEL_GENERATE=0` → 自动选章 #1 → `章节 #1 未开始生成：OpenAI 的 API Key 为空`，UI 显示「未配置 OpenAI 的 API Key：设置 → LLM 里填好后重试」；**19 项全 ok**
- [x] **S14** `_manifest.json` 的「阶段 → 产物」（`03` §2.7 P5 的输入，原先空列）— `s14-manifest-stages`｜新增 `ScanChapterStages()`（产物路径 + 字节数 + FNV 内容指纹 + 是否齐备），`ManifestJson` 增写 `stage_artifacts`（EXTRACT/COMMIT/COST 三段，`_manifest.json` 自身不入清单避免自指）；自检断言「有 StateDiff→EXTRACT complete / 无快照→COMMIT incomplete / 补上变 complete / 16 位指纹 / JSON 可解析」；**19 项全 ok** ⚠️ 真·从中间阶段续跑仍缺 `03` 的 T1–T17 阶段机（本步只给账，续跑粒度仍是章）
- [x] **S13** K09/K22/K23/K24 的数据来源落地（**schema v9**）— `s13-shot-state-artifacts`｜`shots` 补 `start_state_json`/`end_state_json`/`timeline_json`（`12` §1.4 的缺口）；新增 `prompt_artifacts` 表（`02` §2.10 原先无承载）；`NovelVisual` 加 `ListShotsByChapter` + `UpsertPromptArtifact`/`GetPromptArtifact`/`ListPromptArtifacts`；`NovelChecks` 这四条从 `contract-input` 升为 `library`（显式传值优先，不传查库）；**真实旧库迁移 8→9**：`shots` 19→22 列默认 `{}`、`prompt_artifacts` 建出、旧数据零变化（18/8/48/6/7）；**19 项全 ok**
- [x] **S12** 「回到产出阶段重做」的真回路 + 落实不变式 I10 的产物落盘 — `s12-validate-retry`｜`NovelDirector` 在 EXTRACT↔提交门禁之间成环：**G2 机器校验挡下 → 重跑 extractor**（把 `checks_describe` 作为「请只修这些问题」喂回 user 消息），上限 `max_validate_retry=2`（`RunLimits::validation_retries` 下发）；**只有机器校验挡下才重做**（G1/G3/G4 类重跑无用）；重做后通过**不计**失败（否则误报 S1）；`CommitChapterState` 把本章 `StateDiff` 落成 `work/ch<ord>/12_state_diff.json`；端到端自检：第 1 版引用不存在实体 → 重做 1 次 → 第 2 版提交成功；**19 项全 ok**
- [x] **S11** 把 K01–K29 接进生产线（G2 真门禁 + S1 的数据来源）— `s11-k-checks-gate`｜`CommitChapterState` 在**章级快照之后**自己跑 `RunChapterChecks`（`g2_source="inline"`；调用方给报告则 `"caller"`），失败项以 `{check_id+name, severity, detail}` 并入 issue 账供 G5 与拒绝原因；`CommitResult::checks_describe`/`failed_check_ids` → `NovelDirector` → `ChapterObservation` → **S1 实测触发**（`命中停止条件 S1（chapter=1 check_id=K02 连续失败 2 次）`）；`CommitContext::project_dir` 补上；**顺带修** `plot_beats`/`mystery_beats` 的 `ord` 写死 0（K08 判不严格递增的成因）；**19 项自检全 ok**

**已知布局问题（既有，非本次引入）→ 已修（2026-09-19，见下）**：原先"`小说` dock 面板偏矮、`生成本章` 与 `S9 连跑` 被裁掉"。**实测定性有误**：真正原因是该 dock 页被 `SetNextWindowFocus()` 的 `ScrollToBringRectIntoView` **滚下去了**（页首的「工作区」整段滚出视口），不是高度不够。修法：`DrawNovelWindow` 开头 `ImGui::SetScrollY(0)` 钉住页首 + 工作区套**自带滚动的子窗**（`##novel_workspace`，高度 = 页面剩余 − 200px 给工程列表）+ 章节列表改**定高滚动子窗**（`##novel_chapters`，150px），于是「生成本章」重新落在可视区。

### 0.3 剩下什么（当前全部剩余项，按「影不影响跑真实流程」分三类）

**A. 只差外部资源 —— 代码已就绪，拿到就能跑**

| 项 | 缺什么 | 怎么触发 |
|---|---|---|
| **真实生成一章** | **LLM API Key**（`settings.json` 里 `openaiApiKey` / `mimoApiKey` / `minimaxApiKey` 全空） | 设置 → LLM 填 Key，然后 `SHINE_NOVEL_OPEN=rain-signal` + `SHINE_NOVEL_GENERATE=0`，或直接点「生成本章」 |
| 连跑（`auto`） | 同上（前置③，S15 起真判）+ **Critic 模型要 ≠ Writer**（前置⑤，S16 起真判；`openaiModelCritic` 为空则与 Writer 同模型 → 拒绝）| 设置 → LLM：填 Key **并**给 Critic 指定一个不同模型；`manual`/`semi` 无此要求 |
| 真机 SD 出图 | SD/SDXL checkpoint + Comfy 在线 `:8188` | `runtime/sd-e2e` 分镜出图（P5.7 / P6.3） |
| P8 余 2 项 | 人工开关机 **10 次**压测、设置窗滚动验收 | 手动 |
| G 线 AVIF | 已搁置（⏸） | — |

**B. 代码增强 —— 不影响「正文链」跑通，可后补**

- ✅ **已完成 S16**：`09` 的 09-7（模型分层）/ 09-8（交叉复核）/ 09-11（限流）/ 09-12（全书预算）
- 🟡 `03` 的 **T1–T17 阶段机** → **S19 已做**：阶段产物落盘 + P1/P2/P4/P5 断点续跑 + `_manifest` 7 段账；**仍未做**：T2–T9 逐步展开（`03-9`，规格允许合并）、`PromptArtifact` 落库（`03-11`，K23 因此仍 `n/a`）
- 🟡 `10` 的**初始化链 I1–I16**：**S21 已做**「可开写」门禁 N1–N14 + 结构骨架（路径 B/D）+ 阶段表/落盘；**未做**：路径 C（AI 分域生成，`G1`–`G7`）与路径 A（导入既有设定）
- 🟡 **V9/V10/V11 影视化链**：**S23 已做 V9**（`NarrativeShot[]` → `shots`，K22/K24 的数据源）、**S24→S26 已做 V10**（**十层**组装 → `PromptArtifact` 账：S24 落地账 + 视觉输入进哈希、S25 多角色 + 规则版本进哈希、S26 `version`/`generation_ref` 两列（**schema v10**）+ 第 5 层**空间层**（谁在前景/谁在背景））；**未做**：**V11 出图接线**（含 **K19–K21 回填** —— `ToGenShot` 已实现但**仍只有自检调用**）、V1–V8 逐步展开
- **K19–K21 由生成侧回填**（结论本来产生在 `video` 侧，`novel` 不复刻规则）
- ✅ **已完成 S17**：headless **CLI**（`--novel-generate` / `--novel-run`，无界面可跑）
- ✅ **已完成 S18**：**MCP 工具** `novel_generate_chapter`（AI 助手可直接驱动；实现由装配层注入 `NovelPipeline`）
- **K19–K21 的真相（2026-09-20 核实）**：它们的"事实"来自出图侧（`VideoProject::Sanitize` / `object_info`），而 **`ToGenShot` 至今只有自检调用、没有生产调用点** ⇒ 这三条**不是"忘了回填"，而是"共享了 V9/V10/V11 影视化链的缺失"** —— 先有"小说 → 分镜 → 出图"的生产路径，才有事实可回填。已并入下面 V9/V10 那一批。

**C. 已定不做（设计如此，非遗漏）**

- `NarrativeShot` 的细粒度位置/朝向/姿态**不拆列**（写在 `StateSnapshot` JSON 内，K09 按顶层字段组比对）
- `dependencies` 表（延后到实现依赖传播时）
- `shots` 的 `Sequence` 层级（当前只有 Scene→Shot）

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
