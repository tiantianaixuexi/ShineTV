# ShineTV Studio — 进度表（合并后 2026-09-19）

> 唯一进度入口。状态：⬜ 未开始 ｜ 🟡 进行中 ｜ ✅ 通过 ｜ ⛔ 阻塞 ｜ ⏸ 搁置。
>
> **当前**：`main` @ GitHub（`25d37c3`）= f7a0cb8 图库/小说 land + P5.7 + P6.1。

---

## 0. 总览

| 大类 | 内容 | 小任务 | 已完成 | 状态 |
|------|------|--------|--------|------|
| P3 | 节点与图 | 58 | 58 | ✅ |
| P4 | 媒体与纹理 | 27 | 27 | ✅ |
| P5 | 视频分镜 | 36 | 36 | ✅ 代码（P5.7 离线 PASS） |
| P6 | 画布 inpaint | 17 | 9 | 🟡 P6.1–P6.2 ✅ → **P6.3** |
| P7 | MCP | 21 | 21 | ✅ |
| P8 | 收尾 | 13 | 0 | ⬜ |
| G | 图片库 | 79 | 78 | ✅ 线完成（AVIF ⏸） |
| 小说 | P1–P10 | — | P1–P9 ✅；P10 余 1 | 🟡 |
| 归档 | P0–P2.9/R/P3/P4/P7 | — | ✅ | `归档/` |

**现在做哪个**

1. **P6.2** 画布 UI（`任务/P6-画布inpaint.md`）  
2. P6.3 inpaint 服务 → P6.4 联动  
3. P8 收尾  
4. 小说 P10.4 / Key（可并行）

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

明细：`docs/compose/plans/novel-agent/PROGRESS.md`（合并后已更新）与 `Plan/归档/novel-agent/PROGRESS.md`。

---

## 4. P6 / P8（未开始）

### P6 — 画布 inpaint 🟡 5/17

- [x] P6.1 数据模型 5/5  
- [ ] P6.2 画布 UI 0/4  
- [ ] P6.3 inpaint 服务 0/5  
- [ ] P6.4 图库/媒体联动 0/3  

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
