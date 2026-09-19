# ShineTV Studio — 进度表（合并后 2026-09-19）

> 唯一进度入口。状态：⬜ 未开始 ｜ 🟡 进行中 ｜ ✅ 通过 ｜ ⛔ 阻塞 ｜ ⏸ 搁置。
> **当前**：`main` = land + P5.7 + P6.1–P6.4 代码 + P8 关键项 + 小说系统 S0；自检全绿。

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
| 小说系统 | S-doc/S0-pre/S0–S8 | 11 | 3 | 🟡 见 §0.1 |

**现在做哪个**

1. **真机 SD 全流程**：Comfy 在线 `:8188`；SD1.5 checkpoint 下载中 → 完成后跑 `runtime/sd-e2e` 分镜出图（P5.7 / P6.3）
2. **小说系统 S1–S8**：见 §0.1，规格 `Doc/小说系统/00-总纲与索引.md` §6
3. **小说 Agent 连写**：`novels/mcp-bridge`《灯语回声》2 章（MCP 工具环，trace `agent/mcp-tool-loop.jsonl`）；P10.4 Garnet cache + LLM Key 待测
4. （可选）G AVIF

---

## 0.1 小说系统实施（S-doc / S0-pre / S0–S8）🟡

> **规格**：`Doc/小说系统/00-总纲与索引.md` **§6**。**一个 S 一个分支**，八步：起分支 → 实现 → `cmake --build` → **自验** → 更新本表 + `证据.md` → 提交（只 add 本 S 文件）→ 合并 `main` → 下一个。

- [x] **S-doc** 文档集入库（14 卷 + `Doc/AGENTS.md`）— `docs-novel-system`｜`git ls-files` = 14、全 LF
- [x] **S0-pre** 孤儿回收 `ReapStaleImageJobs` — `s0pre-orphan-reap`｜`imagegen:ok`
- [x] **S0** MCP `input_schema` 修复（G22）+ 自检写保护 — `s0-mcp-schema`｜警告 94→0、`novelmcp=ok`
- [ ] **S1** `visual_assets.status` + `visual_artifacts`（schema v7）— `s1-visual-status-artifacts`
- [ ] **S2** P0 八表补 API — `s2-p0-graph-api`
- [ ] **S3** `V0 ASSET_PIPELINE` 编排 + 依赖等待 C+B — `s3-asset-pipeline`
- [ ] **S4** 生成侧严谨性（`object_info` 不跳过 + 降级记账）— `s4-gen-strictness`
- [ ] **S5** `job_ids_json` + 小队列 + 角色资产优先 — `s5-shot-multi-job`
- [ ] **S6** `ToGenShot` 桥（小说分镜 → `VideoProject`）— `s6-togen-shot-bridge`
- [ ] **S7** P1 九表补 API + 快照 — `s7-p1-graph-api`
- [ ] **S8** 闭环回写（`StateDiff` + 门禁 G1–G5）— `s8-state-commit`

**基线坑**：`main` 上仍有**他人**的 `src/novel/NovelMcpTools.cpp`；`runtime/` 未跟踪（**不要提交**）。**每个 S 只 `git add` 自己那几个文件，绝不 `git add -A`。**

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
