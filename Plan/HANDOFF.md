# 新会话交接说明（2026-09-19 盘点后）

> 用途：新会话**从这里开始**。规则见 `../Doc/AGENTS.md`；**总纲与盘点见 `PLAN.md` §0**；进度表见 `PROGRESS.md`。

---

## 0. 开场白（直接抄）

**默认（先对齐分支，再干活）**：
```
读 Plan/PLAN.md §0 与 Plan/PROGRESS.md §0。
确认工作树是否已包含 refactor/libhv-log-to-shine @ f7a0cb8（图库 G 全线 + 小说 P9/P10 大部）。
未合并则先完成 T0 合并并 build；禁止重做 G-S5…G-S14。
合并后做 P5.7（施工图 Plan/任务/P5-视频分镜.md），一次一个 S。
```

**只想接着主线干活（合并已确认）**：
```
继续 P5.7（或 P6 / P8）。施工图 Plan/任务/；进度 Plan/PROGRESS.md。
```

**小说线**：
```
读 docs/compose/plans/novel-agent/PROGRESS.md 与 P10-mcp/PLAN.md。
P1–P9 已完成（代码可能在 f7a0cb8）；当前 P10.4 Garnet 实测或配 Key 联调。
```

---

## 1. 项目与工具链

- 工程根：`E:\c++\ShineTV`；GCC 16.1.0 MinGW64（`C:/msys64/mingw64/bin`）。
- 构建：
```powershell
cmake -S . -B build -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe
cmake --build build -j 8
.\build\ShineTVStudio.exe
```
- 设置：`%APPDATA%\ShineTVStudio\settings.json`（**勿用带 BOM 的 UTF-8 写入**，yyjson 会整份解析失败）。
- 图：`%APPDATA%\ShineTVStudio\graph.json`。

---

## 2. 现状一览（真实）

| 线 | 状态 |
|----|------|
| P3 / P4 / P7 | ✅ 全过 |
| P5 | 🟡 31/36，剩 **P5.7** |
| P6 / P8 | ⬜ 未开始 |
| G 图库 | ✅ **线完成**（f7a0cb8；AVIF 搁置）— **本树若无全套源码必须先合并** |
| 小说 | 🟡 P1–P9 ✅；P10 余 cache 实测 + Key |
| 运维脚本 | `comfy.ps1` / `studio.ps1` 在 f7a0cb8 |

---

## 3. 关键设施（合并后可用）

| 设施 | 位置 |
|------|------|
| 异步 | `src/core/Async.h`（4 线程，`move_only_function`） |
| GPU 纹理 | `src/gpu/GpuTextureManager.h` / `GpuTextureCache` |
| 图库业务 | `src/gallery/*`（完成分支含 ThumbnailService / Viewer / cache / JPEG·WebP） |
| 视频 | `src/video/*` + `src/app/shots/ShotTableView.cpp` |
| MCP | `src/mcp/*`；小说工具 `src/novel/NovelMcpTools.*` |
| OpenAI/多模型 | `src/openai/*` |
| DB | `src/db`（SQLite + Redis/Garnet 池） |
| 截图验收 | `scripts/capture_window.ps1`；Comfy/Studio 控制见完成分支脚本 |

---

## 4. 本机环境

- ComfyUI：`F:\AI\ComfyUI-aki-v3`（`main.py --port 8188 --disable-auto-launch`）。
- **无 SD/SDXL checkpoint** → P5.7 真机验收需降级或先放模型。
- Garnet：`E:\garnet\main\GarnetServer` · `dotnet run -c Release -f net10.0` · `127.0.0.1:6379`。
- 字体微软雅黑；截图脚本必须**置前 + CopyFromScreen**（见 `坑与手法.md` §3）。

---

## 5. 禁止

- 按过时 HANDOFF **重做 G 线**。
- 一次写多个大类；UI 线程同步 HTTP；密钥进仓库/日志。
- 改 `third/`；用 MSVC/Clang；`std::format`（用 fmt）。
- 会话隔离下勿 `git checkout` 到共享 ref 上的其它分支而不确认；合并交给编排/用户。

---

## 6. 文档索引

- 盘点+总纲：`Plan/PLAN.md` ｜ 进度：`Plan/PROGRESS.md`
- 施工图：`Plan/任务/P5|P6|P8-*.md`（及合并后的 G/归档）
- 小说：`docs/compose/plans/novel-agent/`
- 规则：`Doc/AGENTS.md` `RULES-AI.md` `RULES-LANG.md` `RULES-COMFY.md`
