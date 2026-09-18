# 新会话交接（合并后 2026-09-19）

> 进度见 `Plan/PROGRESS.md`；总纲 `Plan/PLAN.md`；规则 `../Doc/AGENTS.md`。

## 开场白

```
读 Plan/PLAN.md 与 Plan/PROGRESS.md。
图库 G 线与小说 P9/P10 源码已在本分支（来自 f7a0cb8）；P5.7 已完成（离线自检 PASS）。
下一步：P6 画布 inpaint（任务/P6-画布inpaint.md），一次一个 S。
```

## 现状

| 线 | 状态 |
|----|------|
| P3/P4/P7 | ✅ |
| P5 | ✅ 代码（P5.7 离线 PASS；真机待 SD 模型） |
| G | ✅ 线完成（AVIF 搁置） |
| 小说 | P1–P9 ✅；P10.4 cache / Key 🟡 |
| P6/P8 | ⬜ |

## 工具链与环境

- GCC 16.1.0：`C:/msys64/mingw64/bin`；`cmake --build build -j 8`
- 设置：`%APPDATA%\ShineTVStudio\settings.json`（**勿写 UTF-8 BOM**）
- ComfyUI：`F:\AI\ComfyUI-aki-v3`（`:8188`）；**无 SD/SDXL** → P5.7 真机降级
- Garnet：`E:\garnet\main\GarnetServer` · `127.0.0.1:6379`
- 脚本：`scripts/comfy.ps1` · `scripts/studio.ps1` · `scripts/capture_window.ps1`

## 禁止

- 重做 G 线；一次写多个大类；UI 线程同步 HTTP；密钥进仓库；改 `third/`；`std::format`。

## 关键设施（合并后）

图库：`src/gallery/{ThumbnailService,Viewer,Resize,cache,decoders}` + `ThumbGrid`  
视频：`src/video/{H3WorkflowBuilder,VideoTaskRunner,SceneToImageBuilder}`  
小说：`src/novel` `src/agent` `src/openai` `src/db` `src/mcp/McpSse|McpRemoteAgent`  
GPU：`src/gpu/` ｜ MCP：`src/mcp/`
