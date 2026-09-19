# ShineTV Studio — 进度表（合并后 2026-09-19）

> 唯一进度入口。状态：⬜ 未开始 ｜ 🟡 进行中 ｜ ✅ 通过 ｜ ⛔ 阻塞 ｜ ⏸ 搁置。
>
> **当前**：`main` = land + P5.7 + **P6.1–P6.4 代码** + P8 关键项；自检 PASS

---

## 0. 总览

| 大类 | 内容 | 小任务 | 已完成 | 状态 |
|------|------|--------|--------|------|
| P3 | 节点与图 | 58 | 58 | ✅ |
| P4 | 媒体与纹理 | 27 | 27 | ✅ |
| P5 | 视频分镜 | 36 | 36 | ✅ 代码（P5.7 离线 PASS） |
| P6 | 画布 inpaint | 17 | 17 | ✅ 代码（离线自检 PASS；真机 inpaint 待 SD 模型） |
| P7 | MCP | 21 | 21 | ✅ |
| P8 | 收尾 | 13 | 11 | 🟡 版本单一来源/Shutdown 顺序/worker 兜底/日志落盘/设置画布段/首启开设置窗 |
| G | 图片库 | 79 | 78 | ✅ 线完成（AVIF ⏸） |
| 小说 | P1–P10 | — | P1–P9 ✅；P10 余 1 | 🟡 |
| 归档 | P0–P2.9/R/P3/P4/P7 | — | ✅ | `归档/` |

**现在做哪个**

1. **真机 SD 全流程（进行中）**：Comfy 在线 `:8188`；SD1.5 checkpoint 下载中 → 完成后重启 Comfy，跑 `runtime/sd-e2e` 分镜出图（P5.7 / P6.3）
2. **小说 Agent 异步连写（进行中）**：
   - 离线 seed：`novels/rain-signal` 8 章 / 48 分镜
   - **MCP 工具环（当前主路径）**：`novels/mcp-bridge`《灯语回声》2 章；`novel_upsert_chapter` 已进 `NovelMcpTools.cpp`；trace=`agent/mcp-tool-loop.jsonl`
   - LLM：无 Key 时代笔；可继续 MCP append 或配 API 真 tool-call
3. P8 剩余：10 次开关机压测与设置窗滚动验收
4. 小说 P10.4 Garnet cache + LLM Key（有 Key 后可应用内真调用）
5. （可选）G AVIF

**SD e2e 路径**：`runtime/sd-e2e/{novels/rain-signal,projects,scripts,output}`；Comfy 异步 `scripts/run-comfy-chapter.ps1 -ProjectJson <ch*.json>`

---

## 0.1 小说系统实施（S-doc / S0-pre / S0–S8）🟡

> **规格**：`Doc/小说系统/00-总纲与索引.md` **§6**（现状 / 顺序表 / 八步 Git 流程 / 红线）。
> **一个 S 一个分支**，八步：起分支 → 实现 → `cmake --build` → **自验** → **更新本表 + `证据.md`** → 提交（只 add 本 S 文件）→ 合并 `main` → 下一个。
> 每完成一个 S，把该行 `[ ]` 改成 `[x]`，并把证据位置写在行尾。

- [x] **S-doc** 文档集入库（`Doc/小说系统/` 14 卷 + `Doc/AGENTS.md` 地图行）— 分支 `docs-novel-system`
      ✅ 判据通过：`git ls-files Doc/小说系统` = **14**；14 卷 CR = **0**（纯 LF）；`Doc/AGENTS.md` 文档地图已加行；入库 **4598** 行
- [x] **S0-pre** 孤儿回收 `ReapStaleImageJobs` 入库（`NovelImageStore.h/.cpp` + `NovelView.cpp`）— 分支 `s0pre-orphan-reap`
      ✅ 判据通过：`SHINE_NOVEL_GRAPH_CHECK=1` → `imagegen:ok`；日志 `P9 队列自检通过（… / 孤儿回收）` + `出图孤儿回收：1 条 RUNNING/QUEUED 改判 FAILED`；自检内断言 stale(updated=0)→FAILED、future→保留。证据见 `证据.md`「S0-pre」段
- [x] **S0** MCP 工具 `input_schema` 序列化修复 + 自检写保护 — 分支 `s0-mcp-schema`
      ✅ 判据通过：`mcp schema 序列化失败` 警告 **0**（原 94 条）；自检新增 `inputSchema.properties` 断言通过；`SHINE_MCP_ALLOW_WRITE=1` 时仍 `novelmcp=ok`。
      **根因①**：`Schema.cpp:ToJsonString` 漏 `set_root` + `AddStrn` 把**局部 `std::string` 的 `c_str()`** 当 key 传进 `yyjson_mut_obj_add_strncpy`（yyjson 该 API 对 key **只引用不拷贝**，`yyjson.c: key->uni.str = _key`）→ 键名悬空 → `code=7 invalid utf-8` → 全部工具 schema 退化成 `{"type":"object"}`。
      **根因②**：`McpWriteAllowed()` 是 `g_mcpAllowWrite || Settings().mcpAllowWrite || EnvWriteOn()` 三源 OR，自检仅 `SetMcpAllowWrite(false)` 关不掉后两个 → 新增 `SetMcpWriteForceDeny` + RAII guard。证据见 `证据.md`「S0」段
- [ ] **S1** `visual_assets.status` + `visual_artifacts`（schema v7）— 分支 `s1-visual-status-artifacts`
- [ ] **S2** P0 八表补 API — 分支 `s2-p0-graph-api`
- [ ] **S3** `V0 ASSET_PIPELINE` 编排 + 依赖等待 C+B — 分支 `s3-asset-pipeline`
- [ ] **S4** 生成侧严谨性（`object_info` 不跳过 + 降级记账）— 分支 `s4-gen-strictness`
- [ ] **S5** `job_ids_json` + 小队列 + 角色资产优先 — 分支 `s5-shot-multi-job`
- [ ] **S6** `ToGenShot` 桥（小说分镜 → `VideoProject`）— 分支 `s6-togen-shot-bridge`
- [ ] **S7** P1 九表补 API + 快照 — 分支 `s7-p1-graph-api`
- [ ] **S8** 闭环回写（`StateDiff` + 门禁 G1–G5 + 快照）— 分支 `s8-state-commit`

**基线坑（起分支前必须知道）**：`main` 上仍有**他人**未提交的 `src/novel/NovelMcpTools.cpp`；`runtime/` 未跟踪（**不要提交**）。
~~31 个 `Plan/归档/` 删除~~ 已于 2026-09-19 正式提交（`0bdf565` / 合并 `d872896`）。
→ **每个 S 只 `git add` 自己那几个文件，绝不 `git add -A`。**

---

## 1. P5 视频分镜 ✅

- [x] P5.1–P5.6（详见 `归档`/历史证据）  
- [x] **P5.7 SceneToImage** — `src/video/SceneToImageBuilder.*`  
  - [x] S1 骨架  
  - [x] S2 img2img / EmptyLatent（64 对齐，dpmpp_2m+karras）  
  - [x] S3 ControlNet 串接 / 降级  
  - [x] S4 `VideoProject.scene*` + 缺 checkpoint 中文错误  
  - [x] S5 UI「出分镜图」+ `SHINE_SCENE_IMAGE_CHECK` **pass=10 fail=0**  
  - 备注：真机出图需本机 SD/SDXL checkpoint  

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

明细：`docs/compose/plans/novel-agent/PROGRESS.md`（合并后已更新）；原 `Plan/归档/novel-agent/PROGRESS.md` **已删除**（2026-09-19，`0bdf565`），原文用 `git show 0bdf565^:Plan/归档/novel-agent/PROGRESS.md` 取。

---

## 4. P6 / P8（未开始）

### P6 — 画布 inpaint 🟡 5/17

- [x] P6.1 数据模型 5/5  
- [x] P6.2 画布 UI 4/4（tab/工具/缩放平移/纹理上传；截图已验）  
- [x] P6.3 inpaint 服务 5/5（PngCodec+九节点图+StartInpaint；SHINE_INPAINT_CHECK PASS）  
- [x] P6.4 图库/媒体联动 3/3（发送到画布/结果落盘/导出 PNG）  

### P8 — 收尾 ⬜ 0/13

- [ ] P8.1–P8.4 设置 / 快捷键 / 打包 / 稳定性  

---

## 5. 合并与运维

| 项 | 说明 |
|----|------|
| 代码来源 | `git restore --source=f7a0cb8 --worktree -- …` + 本分支 P5.7 |
| 运维脚本 | `scripts/comfy.ps1`（status/start/stop/restart/progress）· `scripts/studio.ps1`（含 build） |
| 验收日志 | `SHINE_LOG_FILE=<abs>`；自检：`SHINE_SCENE_IMAGE_CHECK` / `SHINE_MCP_*CHECK` / novel checks |

---

## 6. 更新约定

1. 只在本文件勾进度；证据写 `证据.md`。  
2. 发现「代码有、表没有」先 `git log --all -- <path>`。  
3. 一次一个 S；规则 `Doc/RULES-AI.md`。
