# 小说 Agent · 进度表

> **唯一勾选入口**（本计划线）。做完一项改 `[x]`，并在备注写证据（构建/运行/自检）。  
> 大计划：`00-总纲.md` · 分册在各 `P*/`

图例：`[ ]` 未做 · `[~]` 进行中 · `[x]` 完成 · `[!]` 阻塞

---

## 总览

| 阶段 | 状态 | 完成定义 | 备注 |
|------|------|----------|------|
| P1 OpenAI 客户端 | [x] | 同步+流式可用 | build ok；`SHINE_OPENAI_CHECK=1` 离线自检 PASS；HTTPS 走 libhv WINTLS |
| P2 知识图谱 | [x] | 六域+因果/冲突/谜/依赖 | `NovelDb` v3 + `NovelGraph` API；`SHINE_NOVEL_GRAPH_CHECK=1` PASS |
| P3 记忆/Context | [x] | L1–L4+知情过滤 | `NovelMemory` + `ContextBuilder`；POV 未知情秘密已过滤 |
| P4 Local Tools | [x] | 只读 tools+calling loop | `ToolRegistry` + 内置工具 + `RunToolLoop` mock 自检 |
| P5 Agent 管线 | [x] | Plan→Write→Review→Save | `NovelDirector` mock 自检 PASS |
| P6 ImGui | [x] | 生成本章+流式预览 | 生成/取消/PROPOSED 确认；多模型 Provider 切换 |
| P7 文本 MVP | [x] | 两章上下文一致 | 样例设定 + mock 两章 L2 摘要进入第二章 prompt |
| P8 视觉体系 | [x] | 阶段→九层 Prompt | `NovelVisual` 阶段机 + Assemble；自检 visual:ok |
| P9 出图 | [ ] | PROPOSED 落盘 | |
| P10 MCP | [ ] | 只读 tools/list+call | |

---

## P1 · OpenAI

| ID | 任务 | 状态 | 备注 |
|----|------|------|------|
| P1.1 | Settings openai 段（key 不进仓库/日志） | [x] | `openaiBaseUrl/ApiKey/Model*` 反射序列化；设置窗密码框；日志 MaskKey |
| P1.2 | OpenAITypes + ApiError | [x] | `src/openai/OpenAITypes.h` |
| P1.3 | libhv 同步 Create | [x] | `OpenAIHttp::PostJson` + `Client::Create`；Bearer；中文错误 |
| P1.4 | yyjson 解析 output_text + raw | [x] | `ParseCreateResponse`；raw 调用方 free；样例自检 ok |
| P1.5 | SSE Stream | [x] | `OpenAIStream` + `PostSse`；TextDelta/Created/Completed；修 UAF |
| P1.6 | 超时/HTTP/JSON 错误路径 | [x] | code=`timeout`/`network`/`json`/`no_key`/`http_*` 可区分 |

## P2 · 知识图谱

| ID | 任务 | 状态 | 备注 |
|----|------|------|------|
| P2.B0 | NovelDb 打开 + schemaVersion | [x] | `NovelDb::Open/Migrate`；v1→v3 ALTER 补列 |
| P2.B1 | entities 骨架 + kinds 全量 | [x] | `NovelTypes.h` kind 常量 + entities 表 |
| P2.W | 世界域（规则/历史/社会/科技/文化/宗教/经济/纪元） | [x] | world_* / calendar_* / currency 全表已建 |
| P2.C | 人物域（人设/弧光/状态/情绪/关系/对话/知情） | [x] | personas/arcs/status/emotion/relations/knowledge/dialogue |
| P2.E | 物品/地点距离/势力/能力/资源 | [x] | location/faction/ability/item/resource 表 + ownerships API |
| P2.S | 卷章场拍 + 剧情 + 冲突 + 因果 | [x] | volumes/chapters/scenes/plots/conflict/causal_links |
| P2.M | 伏笔/秘密/mystery/知情矩阵 | [x] | foreshadowings/secrets/secret_knowledge/mysteries |
| P2.Y | Canon/版本/依赖/规则/审计/memories | [x] | canon_logs/entity_versions/dependencies/author_rules/audit_logs/memories |
| P2.API | NovelGraph 查询 API（给 P3/P4） | [x] | Upsert/Get/List + GetEventChain + CharacterSlice/WorldSlice |

## P3 · 记忆

| ID | 任务 | 状态 | 备注 |
|----|------|------|------|
| P3.1 | NovelMemory 写 summary | [x] | `memories` 表 Write/RecentChapterSummaries |
| P3.2 | Character/World Slice | [x] | ContextBuilder 拼 L3 |
| P3.3 | 过滤器（canon/知情/mystery） | [x] | GetSecretsFor + knows=0 不进正文 |
| P3.4 | ContextBuilder::Build | [x] | L1–L4 + UTF-8 截断 + used_entity_ids |
| P3.5 | 样例库自检 | [x] | `SHINE_NOVEL_GRAPH_CHECK` context:ok |

## P4 · Tools

| ID | 任务 | 状态 | 备注 |
|----|------|------|------|
| P4.1 | Tool/ToolRegistry | [x] | ExportOpenAiTools + Execute |
| P4.2 | 只读 tools 全表 | [x] | 9 个核心只读（entity/rel/chapter/foreshadow/secret/chain/own） |
| P4.3 | 写 tools + PROPOSED | [x] | upsert_entity/link_relation/link_causal → canon PROPOSED |
| P4.4 | Function calling loop | [x] | `RunToolLoop` mock 往返通过 |
| P4.5 | 超限/重复保护 | [x] | max=20 / 连续重复 3 次中止 |

## P5 · Agent

| ID | 任务 | 状态 | 备注 |
|----|------|------|------|
| P5.1 | prompts/*.md 加载 | [x] | `LoadPrompt` + 内置默认 |
| P5.2 | Planner | [x] | 状态机 PLAN 阶段 |
| P5.3 | Writer | [x] | WRITE + 可选 stream |
| P5.4 | Critic | [x] | REVIEW + max_revisions |
| P5.5 | Extractor | [x] | summary → memories |
| P5.6 | Director + Async 入口 | [x] | `GenerateChapter`（同步，调用方 RunOnWorker） |

## P6 · UI

| ID | 任务 | 状态 | 备注 |
|----|------|------|------|
| P6.1 | 生成按钮/进度/流式预览 | [x] | `NovelView` 工作区；RunOnWorker + PostToUi |
| P6.2 | issues / PROPOSED 确认 | [x] | 待确认区：标 CANON / NON_CANON |
| P6.3 | 取消与错误态 | [x] | 生成中可取消；错误中文展示 |

## P7 · 文本 MVP

| ID | 任务 | 状态 | 备注 |
|----|------|------|------|
| P7.1 | 样例数据步骤固化 | [x] | `SeedSampleProject` 按钮 |
| P7.2 | 第1章生成入库 | [x] | Director Save 写 chapters.body |
| P7.3 | 第2章上下文一致 | [x] | `RunMvpSelfCheck` L2 进入第二章 |
| P7.4 | 工具调用日志可见 | [x] | ToolLoop callLog / audit_logs |

## P8 · 视觉

| ID | 任务 | 状态 | 备注 |
|----|------|------|------|
| P8.A | visual_assets + clothing_links + refs | [x] | visual_assets 表 + Upsert/Find |
| P8.S | visual_states + ResolveVisualState | [x] | 阶段区间解析（from/to chapter） |
| P8.L | scene_visuals + layouts | [x] | scene_visuals（env/time/weather/mood） |
| P8.K | camera/composition/lighting defs | [x] | 三张 defs 表 + Upsert |
| P8.Q | prompt_layers + Assemble 九层 | [x] | Base→Quality + Negative |
| P8.P | shots + consistency + visual canon | [x] | shots 表 + CheckConsistency + visual_canon_logs |

## P9 · 出图

| ID | 任务 | 状态 | 备注 |
|----|------|------|------|
| P9.1 | ImageBackend + 设置 | [ ] | |
| P9.2 | 队列落盘 PROPOSED | [ ] | |
| P9.3 | UI 预览 + 标 CANON | [ ] | |
| P9.4 | （可选）Critic/checklist | [ ] | |

## P10 · MCP

| ID | 任务 | 状态 | 备注 |
|----|------|------|------|
| P10.1 | stdio Server 骨架 | [ ] | |
| P10.2 | 只读 tools 接 NovelService | [ ] | |
| P10.3 | HTTP/SSE remote | [ ] | |
| P10.4 | Redis 可选缓存/锁 | [ ] | |
| P10.5 | 写 tools + 开关 + audit | [ ] | |
| P10.6 | 客户端配置文档 | [ ] | |

---

## 阻塞与风险

| 项 | 说明 | 状态 |
|----|------|------|
| OPENAI_API_KEY | P1 联调需要；离线自检已过，真实 Create/Stream 需配置密钥后人工点一次 | [~] 待联调 |
| 图片后端 | P9 需本机/自托管或 HTTP | [ ] 未定 |

## 更新约定

1. 只在本文件改状态勾选  
2. 备注一行证据即可（如 `build ok` / `P7.2 章节 body 1.2k 字`）  
3. 阻塞写进「阻塞与风险」，不要只写在聊天里  
