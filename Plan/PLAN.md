# ShineTV Studio — 总纲与索引（合并后 2026-09-19）

> 目标：脱离 UE5.8，用 **C++26 + ImGui Docking + VisualNodeSystem** 重做 Shine 系能力。
> 工具链：**GCC 16.1.0 MinGW64**（`C:/msys64/mingw64/bin`）。规则见 `../Doc/AGENTS.md`。

---

## 0. 合并状态（本分支已落地）

| 来源 | 内容 | 状态 |
|------|------|------|
| `main` @ `ed6e48f` | 基线 + 文档盘点重写 | 已在分支上 |
| `f7a0cb8`（原 `refactor/libhv-log-to-shine`） | **G 图库 S5–S14**、JPEG/WebP/EXIF/查看器/磁盘缓存、**小说 P9/P10 远程 MCP**、`scripts/comfy.ps1`/`studio.ps1`、`Plan/归档/` | **已 restore 进工作树** |
| 本分支 | **P5.7 SceneToImage** + 本文档 | 保留并接好 CMake/自检 |

> 说明：会话隔离禁止 `git merge`，故用 `git restore --source=f7a0cb8` 落地文件后在 **`feat/g-s6-gallery-grid`** 上提交（等价于手工 land）。

**文档地图**：`PROGRESS.md`（进度）｜`任务/`（未完成施工图）｜`归档/`（已完成线）｜`证据.md`｜`HANDOFF.md`｜`../Doc/*`。

---

## 1. 怎么用

1. 读 `PROGRESS.md`；  
2. 挑一个未完成 S；  
3. build → 运行 → 对判据；  
4. 勾选 + 写 `证据.md`。一次一个 S。

构建：

```powershell
cmake -S . -B build -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe
cmake --build build -j 8
.\build\ShineTVStudio.exe
```

---

## 2. 大类状态总表（合并后）

| 大类 | 状态 | 备注 |
|------|------|------|
| P3 节点与图 | ✅ 58/58 | 施工图 `归档/任务/P3-*` |
| P4 媒体与纹理 | ✅ 27/27 | 归档 |
| P5 视频分镜 | ✅ 36/36 代码 | **P5.7 离线自检 PASS**；真机出图待 SD 模型 |
| P6 画布 inpaint | ⬜ 0/17 | **下一步** `任务/P6-画布inpaint.md` |
| P7 MCP 主线 | ✅ 21/21 | 归档；含 HTTP/SSE 扩展（f7a0cb8） |
| P8 收尾 | ⬜ 0/13 | `任务/P8-收尾.md` |
| G 图片库 | ✅ 78/79 线完成 | **AVIF S12c 搁置**；源码已在本树 |
| 小说 Agent | 🟡 P1–P9 ✅；P10 余 cache 实测 | `docs/compose/plans/novel-agent/PROGRESS.md` |
| 归档 | ✅ | `Plan/归档/` |

---

## 3. 依赖与下一步

```text
已合并代码 ──► build 验证 ──► P6.1 画布数据模型 ──► P6.2 UI ──► P6.3 inpaint ──► P6.4 联动
                              └─► P8 收尾（建议最后）
小说并行：P10.4 Garnet cache + LLM Key
可选：放 SD checkpoint 后真机验 P5.7；AVIF 永久搁置亦可
```

**明确不做**：SQLite 图库索引 / RAW / libvips / 自建线程池等（见 `Doc/BASELINE.md` §8）。

---

## 4. 产品形态（七区）

VS Code 风格：顶栏 | 活动栏 | 侧栏 | 中央（图/分镜/图库/画布/小说） | 右栏 | 底栏 | 状态栏。  
图库网格/查看器、分镜表、小说工作区、MCP 均已挂进布局（见 `src/app/shell/`）。
