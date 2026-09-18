# ShineTV Studio — 总纲与索引（2026-09-19 盘点重写）

> 目标：脱离 UE5.8，用 **C++26 + ImGui Docking + VisualNodeSystem** 重做
> `Plugins/Shine` + `Plugins/ShineMCP` 的 ComfyUI 工作流 / 贴图 / AI 视频 / MCP / 图库 / 小说 Agent 能力。
> 工具链固定 **GCC 16.1.0**（`C:/msys64/mingw64/bin`）。规则见 `../Doc/AGENTS.md`。

---

## 0. 盘点结论（先读，避免重复施工）

### 0.1 根因：文档/代码分叉，不是「图库没做完」

| 引用点 | 内容 | 对开工的影响 |
|--------|------|----------------|
| **`main` / 当前工作树**（含误开的 `feat/g-s6-gallery-grid`） | `Plan/*` 仍写「下一步 G-S5」；`src/gallery/` 只有 S0–S5 级代码（列表形态）；**无** ThumbnailService / Viewer / JPEG·WebP / 磁盘缓存 | 若只读这里的 HANDOFF/PROGRESS，会**重做已完成的图库** |
| **`refactor/libhv-log-to-shine` @ `c14133e` + `f7a0cb8`** | **项目真实最新实现**：G-S5–S14 完成（AVIF 搁置）、小说 P1–P9 完成、P10 大部分完成、`scripts/comfy.ps1`/`studio.ps1`、文档归档到 `Plan/归档/` | **功能与勾选以该提交为准** |
| 两分支关系 | `c14133e`/`f7a0cb8` **不在 `main` 祖先链上** | 开工前必须先合并/检出该线，否则代码与进度表对不上 |

**已核实（本机 git）**

- `git merge-base --is-ancestor c14133e main` → **否**
- `git diff --name-status ed6e48f f7a0cb8` → 图库全套源文件、`ThumbGrid`、JPEG/WebP、EXIF、FileActions、Viewer、ThumbnailService、disk/CPU 缓存、novel 出图与 MCP 远程、运维脚本等 **仅存在于 `f7a0cb8` 一侧**
- 当前树 `Test-Path src/gallery/ThumbnailService.h` → **False**；`src/novel/NovelImageGen.*` → **False**；`scripts/comfy.ps1` → **False**

### 0.2 「做了但没记全 / 记了却不在本树」清单

| 事项 | 实现证据 | 本树文档曾如何写 | 现结论 |
|------|----------|------------------|--------|
| G 图库线 G-S5–S14 | `c14133e` 提交说明 + `src/gallery/{ThumbnailService,Viewer,Resize,cache,decoders/Jpeg,Webp,…}` | main PROGRESS：G-S5 ✅、G-S6 起 ⬜；HANDOFF 仍写「继续 G-S5」 | **线完成（AVIF 搁置）**；禁止在 main 重做 |
| 小说 P9 出图 | `f7a0cb8`：`NovelImageGen/NovelImageStore`；归档 PROGRESS：P9 ✅ `imagegen:ok` | main `docs/.../novel-agent/PROGRESS.md`：P9 全 `[ ]` | **P9 完成（在完成分支）**；main 文档滞后 |
| 小说 P10 MCP | 归档/f7a0cb8 PROGRESS：P10.1–3/5/6 ✅，P10.4 Garnet `[~]`；`McpRemoteAgent/McpSse/McpHttpClient` | main novel PROGRESS：P10 全 `[ ]` | **P10 除 cache 实测外基本完成** |
| 主线 P5.1–P5.6 | main 源码已有 `src/video/*`、`ShotTableView`；main PROGRESS 已 ✅ | `Plan/任务/P5-视频分镜.md` 全文仍标 ⬜ | **代码与 PROGRESS 一致为已完成**；施工图状态未回写 |
| P7 MCP 主线 | main 有 `SHINE_MCP_*CHECK`、HttpServer、MCPServer；PROGRESS ✅ | `Doc/BASELINE.md` 仍写 `WITH_HTTP_SERVER` OFF、object_info 未解析 | **BASELINE 过时**（CMake 现为 ON；P3 已解析） |
| 运维脚本 | `f7a0cb8`：`scripts/comfy.ps1`、`studio.ps1` | main **无此二文件**（仅 `capture_window.ps1`/`crop_zoom.ps1`）；完成分支 HANDOFF 已写用法 | 合并后才可用；旧文档若写「已有 comfy.ps1」在本树为假 |
| 小说附加交付 | `AgentKit` / `NovelFields` / `OpenAIProvider·Chat·Anthropic` / `NovelDb` schema **v5** | novel PROGRESS 只勾到 P8，未单列这些 | **代码已完成**，已写入 `PROGRESS.md` §4.2 |
| 本树 novel P10 部分实现 | `NovelMcpTools` + `SHINE_MCP_ALLOW_WRITE` + bootstrap 注册 | novel PROGRESS 仍全 `[ ]` | **文档漏记**；P10.3 远程与 P9 仍在完成分支 |
| 施工图状态行 | `任务/P5-*.md`、`任务/G-图片库.md` 头部 ⬜ | 与 PROGRESS/代码矛盾 | 本次已改 P5；G 完成态以合并后完成分支施工图为准 |
| `Doc/BASELINE.md` / 根 `AGENTS.md`「当前阶段」 | 仍写「下一阶段 P3」、HTTP Server OFF、无 novel 目录 | 与 P3–P7/G/小说代码矛盾 | 本次已改 BASELINE 当前阶段脚注；AGENTS 文档地图合并后按 `Plan/归档/` 再对齐 |
| 完成分支 `Plan/归档/README.md` | 仍写 G「下一步 G-S7 / 40/79」 | 与同分支 PROGRESS（G 线完成）矛盾 | 合并后改 README |
| 本会话误做 G-S6 | `feat/g-s6-gallery-grid` 上曾改 GalleryView/Resize 等 | 与 `c14133e` 重复 | **已 `git restore` 还原工作树**；该分支勿再开发 |

### 0.3 开工顺序（硬性）

```text
T0  把 refactor/libhv-log-to-shine（f7a0cb8）合入工作分支 / 检出该分支
    → 全量 cmake build 确认可编译
    → 以合并后的 src/ + Plan/为准（本文件与 PROGRESS 已按「真实完成态」重写）
T1  主线 P5.7 分镜图（缺 SD 模型时先中文降级）
T2  主线 P6 画布 inpaint
T3  主线 P8 收尾
T4  小说 P10.4 Garnet cache 实测 + LLM Key 联调
T5  （可选）G-S12c AVIF；文档归档同步到 main
```

**在 T0 完成前：禁止新写图库功能、禁止勾选 G-S6…G-S14、禁止按旧 HANDOFF 重做 G-S5。**

---

## 0. 文档地图

| 文档 | 放什么 | 何时读 |
|------|--------|--------|
| `PLAN.md`（本文件） | 总纲 + 盘点结论 + 大类表 + 依赖与下一步 | 领任务时（**每次开工先读 §0**） |
| `PROGRESS.md` | **唯一进度表**（真实完成态 + 工作树缺口） | 每次开工 / 做完一个 S |
| `任务/<大类>.md` | 施工图（未完成大类） | 做 P5.7 / P6 / P8 时 |
| `归档/`（完成分支） | 已完成线施工图与 novel P1–P9 | 查历史 |
| `证据.md` | 实测证据（只保留最近） | 复核 |
| `坑与手法.md` | 踩坑与验收手法 | 卡住时 |
| `HANDOFF.md` | 新会话交接 | 新会话第一眼 |
| `../Doc/AGENTS.md` 等 | 硬性规则 | 每次开工前 |
| `docs/compose/plans/novel-agent/` | 小说线当前施工图（P10） | 做小说时 |
| `docs/compose/spec/` | MCP Tool Registry 等 spec | 动 MCP 时 |

---

## 1. 怎么用（每次开工 4 步）

1. 读 `PROGRESS.md` §0–§2，确认工作树是否已在 **`f7a0cb8` 及之后**；
2. 在 §3 大类表挑 **一个未完成 S**（先 T0，再 P5.7 / P6 / P8 / novel）；
3. 照施工图 + `Doc/RULES-LANG.md` 写码 → configure → build → 运行 → 对判据；
4. 勾 `PROGRESS.md` + 写 `证据.md`。**不通过不勾、不往下走。一次只做一个 S。**

---

## 2. 产品形态（VS Code 风格七区）

```text
顶栏菜单+连接灯 | 活动栏 | 侧栏 | 中央（图/分镜/图库/画布/小说） | 右栏属性预览 | 底栏队列日志输出 | 状态栏
```

图库网格、查看器、分镜表、小说工作区在完成分支均已挂进布局（细节见 `f7a0cb8` 的 `DockLayout` / `DrawDockedPanels`）。

---

## 3. 大类 / 小类总表（真实状态）

状态以 **`f7a0cb8` 代码 + 该侧 PROGRESS** 为准；「本树」列 = 当前 main 检出是否已有对应源文件。

| 大类 | 内容 | 小类 | 任务数 | 真实状态 | 本树源码 | 施工图 |
|------|------|------|--------|----------|----------|--------|
| P0–P2 / P2.9 / R | Shell / ComfyCore / GraphHost / 现代化 / 拆分 | — | 归档 | ✅ 完成 | 有 | `归档-已完成.md` |
| **P3** | 节点与图 NodeKit（含 P3.7 远端模板） | P3.0–P3.7 | 58 | ✅ 58/58 | 有 | 完成分支 `Plan/归档/任务/P3-*` |
| **P4** | 媒体与纹理 | P4.1–P4.5 | 27 | ✅ 27/27 | 有 | 归档 |
| **P5** | 视频分镜 | P5.1–P5.7 | 36 | 🟡 **31/36**（仅剩 **P5.7**） | P5.1–5.6 有；P5.7 无 | `任务/P5-视频分镜.md` |
| **P6** | 画布 inpaint | P6.1–P6.4 | 17 | ⬜ 0/17 | 无 `src/paint` | `任务/P6-画布inpaint.md` |
| **P7** | MCP 主线 | P7.1–P7.5 | 21 | ✅ 21/21 | 有（HTTP Server 已 ON） | 归档 |
| **P8** | 收尾 | P8.1–P8.4 | 13 | ⬜ 0/13 | 无 | `任务/P8-收尾.md` |
| **G** | 图片库 | G-S0–G-S14 | 79 | ✅ **78/79**（**线完成**；仅 AVIF S12c 搁置） | 本树仅 S0–S5 级；全套在 f7a0cb8 | 完成分支 `任务/G-图片库.md` |
| 小说 Agent | compose 线 | P1–P10 | — | 🟡 **P1–P9 ✅（P9 源码在 f7a0cb8）**；P10.1–3/5/6 ✅（本树已有 tools/写开关）；**P10.4** cache 实测；**附加交付**：AgentKit / NovelFields / 多模型 LLM / schema v5 **已完成但旧 PROGRESS 未单列 | 本树缺 P9 与 P10 远程 MCP 文件 | `docs/compose/plans/novel-agent/`（P10）+ 归档 |

**合计口径**：主线+G 约 251 个小任务；按真实完成态 ≈ **P3+P4+P5(31)+P7+G(78)** 已完，余 **P5.7(5)+P6(17)+P8(13)+AVIF(搁置)+novel P10.4/联调**。

---

## 4. 重叠与裁决（保持）

| 重叠点 | 裁决 |
|--------|------|
| `src/gpu/` | P4.1 先建；图库只接入 |
| CPU 图 + libpng | 全仓 `gallery::Image` + `PngDecoder` |
| `@image` / `SHINE_IMAGE_PATH` | G 产出，主线只读 |

---

## 5. 依赖顺序（更新）

```text
T0 合并 f7a0cb8 ──► 真实代码与 PROGRESS 对齐
        │
        ├─► P5.7 分镜图（可先降级）
        ├─► P6 画布 inpaint（依赖 P4 + 图库选图，完成分支已具备图库）
        └─► P8 收尾（建议最后）
小说线可并行：P10.4 Garnet / Key 联调
G AVIF 可选、可永久搁置
```

---

## 6. 明确不做（摘 `Doc/BASELINE.md` §8）

SQLite 图库索引 / RAW / libvips / Texture Atlas / 自建线程池 / 图片批量重命名编码等 —— 仍不做。

---

## 7. 相关文档同步说明

本次重写同步了：

- `Plan/PROGRESS.md` — 真实进度表 + 本树缺口 + 下一步
- `Plan/HANDOFF.md` — 交接改为「禁止重做 G；先 T0 合并」
- `Plan/任务/P5-视频分镜.md` — 小类标题状态与 PROGRESS 对齐（P5.1–5.6 ✅）
- `docs/compose/plans/novel-agent/PROGRESS.md` — 按 f7a0cb8 真实小说进度回写
- `Doc/BASELINE.md` — 「当前阶段」与过时基线事实脚注

未在 main 删除 `Plan/任务/P3|P4|P7`：完成分支已移到 `Plan/归档/任务/`，合并后自动对齐。
