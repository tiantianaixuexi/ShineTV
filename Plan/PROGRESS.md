# ShineTV Studio — 进度表（2026-09-19 盘点重写）

> **唯一进度入口**。状态口径：⬜ 未开始 ｜ 🟡 进行中 ｜ ✅ 通过 ｜ ⛔ 阻塞 ｜ 📦 在其它分支已有实现（本树无源码）。
>
> **权威实现提交**：`refactor/libhv-log-to-shine` → `c14133e`（图库 G 完成）→ `f7a0cb8`（运维脚本 + 小说/MCP 增量）。
> **当前工作树**：`main` 系（文档曾滞后；本文件已按真实完成态重写）。开工前先读 `PLAN.md` §0。

---

## 0. 工作树 vs 权威提交（必读）

| 检查项 | 权威 `f7a0cb8` | 当前工作树 | 动作 |
|--------|----------------|------------|------|
| G 图库全套（缩略图管线/查看器/JPEG/WebP/EXIF/磁盘缓存） | 有，G-S5–S14 ✅ | **无**（仅列表级 S0–S5） | **T0 合并**，禁止重做 |
| `scripts/comfy.ps1` `studio.ps1` | 有 | 无 | T0 合并 |
| 小说 P9 `NovelImageGen/Store` | 有，P9 ✅ | 无 | T0 合并 |
| 小说 P10 远程 MCP（SSE/stdio/RemoteAgent） | 有，除 cache 实测外 ✅ | 仅部分进程内 MCP | T0 合并 |
| P5 视频 P5.1–P5.6 | ✅ | ✅ 源码在 | 可直接做 P5.7 |
| P3/P4/P7 主线 | ✅ | ✅ | 已完成 |
| 本会话 `feat/g-s6-gallery-grid` | — | 已 `git restore` 还原 | **废弃，勿合并为图库实现** |

---

## 1. 总览（真实完成态）

| 大类 | 内容 | 小任务 | 已完成 | 状态 | 备注 |
|------|------|--------|--------|------|------|
| P3 | 节点与图 | 58 | 58 | ✅ | 含 P3.7 远端工作流/节点浏览器 |
| P4 | 媒体与纹理 | 27 | 27 | ✅ | |
| P5 | 视频分镜 | 36 | 36 | ✅ 代码完成 | **P5.7 离线自检 PASS**；真机出图待 SD 模型 |
| P6 | 画布 inpaint | 17 | 0 | ⬜ | |
| P7 | MCP 主线 | 21 | 21 | ✅ | `WITH_HTTP_SERVER=ON` |
| P8 | 收尾 | 13 | 0 | ⬜ | |
| G | 图片库 | 79 | 78 | ✅ 线完成 | **AVIF S12c 搁置**；源码在 f7a0cb8 |
| 小说 Agent | P1–P10 | — | P1–P9 ✅；P10 余 1 项 | 🟡 | 见 §4 |
| 归档 | P0/P1/P2/P2.9/R | — | ✅ | ✅ | |

**下一步（按序）**

0. **T0** 合并 `f7a0cb8` 进工作分支并 build 通过（**本会话隔离禁止 merge，待编排层**）  
1. **P6** 画布 inpaint — `任务/P6-画布inpaint.md`（P5.7 代码已齐）  
2. **P8** 收尾 — `任务/P8-收尾.md`  
3. **小说 P10.4** Garnet cache 实测 + **LLM Key** 联调  
4. （可选）本机放入 SD checkpoint 后真机验收 P5.7 出图  

---

## 2. 主线明细

### P3 — 节点与图 ✅ 58/58

- [x] P3.0 WS 错误层 10/10  
- [x] P3.1 节点定义 JSON 9/9  
- [x] P3.2 ShineComfyNode 7/7  
- [x] P3.3 动态注册 7/7  
- [x] P3.4 图编译器 7/7  
- [x] P3.5 提交接线 6/6  
- [x] P3.6 导入导出 9/9  
- [x] P3.7 从 ComfyUI 拉取工作流 3/3  

### P4 — 媒体与纹理 ✅ 27/27

- [x] P4.1 共享 GPU + `/view` 8/8  
- [x] P4.2 媒体历史与本地缓存 6/6  
- [x] P4.3 右栏预览 4/4  
- [x] P4.4 输出列表 5/5  
- [x] P4.5 生成中预览 4/4  

### P5 — 视频分镜 🟡 31+/36

- [x] P5.1 工程模型 + JSON 5/5  
- [x] P5.2 `@image`/`@char`/`{{Mixed}}` 5/5  
- [x] P5.3 分镜表 UI 5/5  
- [x] P5.4 H3 工作流编译器 7/7  
- [x] P5.5 视频任务执行器 6/6  
- [x] P5.6 视频结果预览 3/3  
- [x] **P5.7 分镜图生成 5/5（离线自检 PASS；真机出图待 SD 模型）**  
  - [x] S1 骨架 `SceneToImageBuilder`（`src/video/SceneToImageBuilder.*` + CMake）  
  - [x] S2 基础 img2img / 无图 EmptyLatent 降级（宽高 **64 对齐**，dpmpp_2m+karras，Save+Preview）  
  - [x] S3 ControlNet 串接（depth/normal；配置但无控制图 → 自动降级 + 告警）  
  - [x] S4 参数进 `VideoProject.scene*`（反射存盘）+ Sanitize 对齐 64 + 缺 checkpoint 中文错误  
  - [x] S5 UI「出分镜图」+ `SHINE_SCENE_IMAGE_CHECK=1` **pass=10 fail=0**；Done 回填 `firstFramePath`  
  - 备注：本机无 SD/SDXL → 真机 `/prompt` 仍会失败，属预期降级；放置模型后即可用 `StartSceneImage` 通路  

### P6 — 画布 inpaint ⬜ 0/17

- [ ] P6.1 数据模型 0/5  
- [ ] P6.2 画布 UI 0/4  
- [ ] P6.3 inpaint 服务 0/5  
- [ ] P6.4 与图库/媒体联动 0/3  

### P7 — MCP 主线 ✅ 21/21

- [x] P7.1–P7.5 全部（Tool Registry / HTTP / 协议端点 / 内置工具 / 设置安全）  
- 自检：`SHINE_MCP_CHECK` / `SHINE_MCP_HTTP_CHECK` / `SHINE_MCP_PROTO_CHECK`  

### P8 — 收尾 ⬜ 0/13

- [ ] P8.1 设置集中化 0/3  
- [ ] P8.2 快捷键系统 0/3  
- [ ] P8.3 打包与首启 0/3  
- [ ] P8.4 稳定性收尾 0/4  

---

## 3. G 图片库（📦 权威在 f7a0cb8）

| 步骤 | 状态 | 说明 |
|------|------|------|
| G-S0 前置接入 | ✅ | 本树亦有 |
| G-S1 Image/MetaProbe | ✅ | 本树亦有 |
| G-S2 PNG 解码 | ✅ | 本树亦有 |
| G-S3 GPU 纹理接入 | ✅ | 与 P4.1 共用 |
| G-S4 ImageScanner | ✅ | 本树亦有 |
| G-S5 挂进六区 | ✅ | 本树有列表版 |
| G-S6 Resize+网格 | ✅ 📦 | Thumbnail/ThumbGrid/Resize — **仅 f7a0cb8** |
| G-S7 异步缩略图 | ✅ 📦 | ThumbnailService |
| G-S8 CPU/GPU LRU | ✅ 📦 | CpuThumbCache + GpuTextureCache |
| G-S9 虚拟化网格 | ✅ 📦 | |
| G-S10 查看器 | ✅ 📦 | Viewer + 缩放平移 |
| G-S11 右键/@image/拖拽 | ✅ 📦 | FileActions |
| G-S12a/b JPEG/WebP | ✅ 📦 | JpegDecoder / WebpDecoder |
| G-S12c AVIF | ⏸ 搁置 | 允许永久搁置 |
| G-S13 EXIF+元数据 | ✅ 📦 | ExifOrientation |
| G-S14 磁盘缓存+搜索排序 | ✅ 📦 | DiskThumbCache |

**勾选纪律**：T0 合并前不要在 PROGRESS 把 📦 改成「本树已验」；合并并 build/运行通过后，可在备注写「合并自 f7a0cb8，复验通过」。

---

## 4. 小说 Agent（docs/compose/plans/novel-agent）

### 4.1 阶段状态（双轨）

| 阶段 | f7a0cb8（权威） | 当前工作树代码 | 当前树 novel PROGRESS（盘点前） | 结论 |
|------|-----------------|----------------|--------------------------------|------|
| P1–P8 | ✅ | ✅ 模块齐全 + 自检钩子 | ✅ [x] | 一致，可信 |
| P9 出图 | ✅ `NovelImageGen/Store` | **无** | [ ] | 完成分支有、本树无 → **T0 合并** |
| P10.1 stdio | ✅ | 部分 | [ ] | 以完成分支为准 |
| P10.2 只读 tools | ✅ | **有** `NovelMcpTools`（~13 `novel_*`） | [ ] **漏记** | 本树已实现，文档未勾 |
| P10.3 HTTP/SSE | ✅ `McpSse/RemoteAgent` | 无 | [ ] | 完成分支 |
| P10.4 Garnet cache | 🟡 | `src/db/redis` 在、小说未接 cache | [ ] | **真未完成**（实测） |
| P10.5 写开关 | ✅ | **有** `SHINE_MCP_ALLOW_WRITE` / Settings | [ ] **漏记** | 本树已实现 |
| P10.6 客户端文档 | ✅ CLIENT.md | 完成分支有 | [ ] | 合并后可见 |

### 4.2 做了但 PROGRESS 未单列的附加交付（本树代码已存在）

| 交付 | 证据 | 处理 |
|------|------|------|
| **AgentKit 多 Agent**（15 内置 + route + 工具白名单∩MCP） | `src/agent/AgentKit.*`、`docs/novel-多Agent名册.md`、自检 `agents:ok` | 进度表记入「附加交付 ✅」 |
| **NovelFields 动态字段** | `src/novel/NovelFields.*`、`fields:ok`、schema 含 field_defs | 同上 |
| **多模型 LLM**（OpenAI / MiMo / MiniMax / Custom + Anthropic 协议） | `OpenAIProvider/Chat/Anthropic/Config` | 同上（P6 备注里只带过一句） |
| **NovelDb schema v5**（v4 视觉 + v5 agents） | `NovelDb.cpp` `kTargetSchemaVersion=5` | 旧文案「v3」作废 |
| **进程内 MCP 注册 + 自检** | `McpBootstrap` + `novel_route_task` / `novel_agent_tools` | 归入 P10.2/10.5 已做部分 |

阻塞（仍有效）：**LLM Key 联调**、**Garnet cache 实测**、图片后端（P9 在完成分支已有实现后，本树合并前仍缺）。

---

## 5. 归档与文档债务

| 项 | 说明 |
|----|------|
| P0/P1/P2/P2.9/R | ✅ 见 `归档-已完成.md`（合并后为 `归档/归档-已完成.md`） |
| 施工图状态未回写 | main 上 `任务/P5-*.md` 曾全文 ⬜ — 本次已改 P5 标题为与本表一致 |
| BASELINE 过时 | HTTP Server 已 ON；object_info 已解析 — 已加脚注 |
| 证据.md | 只保留最近条目；合并后以 f7a0cb8 侧证据为准 |

---

## 6. 更新约定

1. 只在本文件改进度；施工图改做法不改勾选权威。  
2. 📦 项合并复验后改为 ✅ 并写一行证据（提交号/日志/截图路径）。  
3. 发现「代码有、表没有」时：先查 `git log --all --oneline -- <path>`，禁止直接当未做开工。  
4. 一次只做一个 S；规则见 `Doc/RULES-AI.md`。
