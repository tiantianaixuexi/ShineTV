# 仓库目录职责表（Repository Map）

> **本页回答**：这层目录是干什么的、关键入口在哪、入不入库。**职责级**目录表——文件级「要改什么改哪个文件」在 [`refactor/README.md`](../../refactor/README.md)『3. 文件地图』，本页不重复。
> **可信度**：✅〔源〕= 本次 glob / 目录浏览 / `.gitignore` 实测｜✅〔A〕= 出自 [`AGENTS.md`](../../AGENTS.md)（2026-09-30 核验）｜△ INFERRED｜❓ UNKNOWN。

## 1. 源码（`src/`）

| 目录 | 职责 | 关键文件 / 入口 | 指针 |
|---|---|---|---|
| `src/`（`core util net db llm comfy gpu media flow visual novel paint mcp project pipeline` 共 15 模块） | **shine_core 业务层**：零 Qt、零 ImGui，**一行不改** | — | [`refactor/README.md`](../../refactor/README.md)『1. 分层与目录』 |
| `src/ui/imgui/` | **shine_imgui 界面层**（Dear ImGui 1:1 复刻 webui） | `app/main.cpp`（只做 CLI 分派） | [`refactor/README.md`](../../refactor/README.md)『1. 分层与目录』 |
| `src/ui/imgui/theme/` | 主题表（31 token）、派生色、`ImGuiStyle` 生成、持久化 | `Tokens.h`、`Theme.cpp`、`Themes/*.json`（5 套） | [`refactor/README.md`](../../refactor/README.md)『3.1 主题与样式』 |
| `src/ui/imgui/kit/` | 组件库：15 族 `Widget_*` + 原语（`Draw/Fonts/Text/Image/Scroll/Anim/Overlays/Views`…） | `Widgets.h`（约定写全在顶部） | [`refactor/README.md`](../../refactor/README.md)『3.2 组件库』 |
| `src/ui/imgui/pages/` | 外壳 + 6 工作区 + 项目中心 + 组件画廊（回归基准） | `Shell.h`、`Gallery.cpp` | [`refactor/README.md`](../../refactor/README.md)『3.3 / 3.4』 |
| `src/ui/imgui/host/` | Win32 窗口 + WGL/GL3 + 帧循环 + DPI + 取证注入点 | `Host.h`（`SetFrame*Override`）、`AppEntry.cpp` | [`refactor/README.md`](../../refactor/README.md)『3.5 宿主与验收』 |
| `src/ui/imgui/verify/` | 回归取证：抓图 / 探针 / 判定 | `Review.cpp` 总入口、`ReviewVerdict.cpp` **唯一判定处**、`ReviewScene.cpp`（判据写入）、`Capture.cpp`（RGBA）、`ReviewFixture.cpp` | [BuildAndVerify.md](BuildAndVerify.md) |

## 2. 脚本与工具

| 目录 | 职责 | 关键文件 / 入口 | 入库 | 指针 |
|---|---|---|---|---|
| `tools/` | 门禁 4（`check-*`）+ 扫描器 4（`find-*`，含棘轮基线）；另有 `find-fixed-width.ps1` / `find-manual-list.ps1`（带基线，未入 AGENTS.md 表，△抓什么未核） | `check-layers.ps1` 等 | ✅ | [GatesAndJudgments.md](GatesAndJudgments.md) |
| `out/` | 1:1 回归与取证脚本 + fixture SQL 留档 | `_make_fixture.ps1`、`_run_review.ps1`、`_colorcount.ps1`、`_fixture_{schema,data,seed}.sql` | 脚本 / SQL ✅；`_fixture/`、`review-*/`、`*.stdout.txt` ❌ | [BuildAndVerify.md](BuildAndVerify.md) |
| `scripts/` | 运行与截图辅助 | `capture_window.ps1`、`run_reviews.ps1`（备选回归）、`studio.ps1`、`comfy.ps1` | ✅ | [BuildAndVerify.md](BuildAndVerify.md) |

## 3. 文档与记忆

| 目录 / 文件 | 职责 | 入库 | 指针 |
|---|---|---|---|
| `AGENTS.md`（根） | 常驻协作规则、构建、门禁、判据、地雷 | ✅ | 全库事实的第一来源 |
| `refactor/` | **长文层**：文件地图 / 视觉规范 / 架构 / 进度 / 坑与纪律 / P0–P7 | ✅ | [INDEX.md](../INDEX.md)『库内分区』 |
| `KnowledgeBase/` | **本库**：薄层速查 + 唯一入口 | ✅ | [INDEX.md](../INDEX.md) |
| `.agents/` | DSH 技能定义（如 `skills/memsearch/SKILL.md`） | ✅（不在 `.gitignore`）〔源〕 | [08_AI_Memory/README.md](../08_AI_Memory/README.md) |
| `.memsearch/memory/` | 事件记忆正文（唯一真源，memsearch 写入与召回）；索引是派生缓存（`~/.memsearch/milvus.db`，见 `.memsearch.toml`） | 建议入库（〔源〕`.memsearch.toml` 注释）；❓本机当前未见该目录 | [08_AI_Memory/README.md](../08_AI_Memory/README.md) |

## 4. 设计稿与生成物（多数不入库）

| 目录 / 文件 | 职责 | 入库 | 指针 |
|---|---|---|---|
| `webui/` | React + CSS **设计稿**：视觉唯一基准，只做效果不管功能（`src/components/UI.jsx`、`src/styles/*.css`、`dist/`） | ❌ **不入库**（`.gitignore` 末行；新 clone 拿不到，色值以 `design-spec.md` §2 内联表为准） | [`refactor/design-spec.md`](../../refactor/design-spec.md)『2.0 权威在哪』 |
| `third/` | 第三方库源码（`imgui` **`ImAnim`** `VisualNodeSystem` `libhv` `sqlite` `yyjson` `spdlog` `fmt` `function2` `glaze` `mimalloc` `stdexec` `hiredis` `libpng` `libjpeg-turbo` `libwebp` `zlib` `imageinfo` `zmij`） | 源码 ✅；`third/**/build/` 等构建产物 ❌ | `AGENTS.md`『常用入口』 |
| `build/` | 构建输出（`debug/`、`release/`）+ 取证产物（`shots-*`、`*.stdout.txt`、`evidence-*.png`） | ❌ 不入库 | [BuildAndVerify.md](BuildAndVerify.md) |
| `runtime/` | 运行时数据（如 `sd-e2e/`） | ❌ **不入库** | — |
| `CMakeLists.txt` / `Build.bat` / `.memsearch.toml` / `README.md`（根） | 构建定义 / 一键构建 / memsearch 项目配置 / 仓库自述 | ✅ | [BuildAndVerify.md](BuildAndVerify.md) |
| `compile_commands.json`（根） | clangd 生成物（`Build.bat` 从 build 目录回拷） | ❌ | — |
| `.cache/` `.clice/` `.git/` | clangd 索引 / IDE 目录 / 版本库 | `.cache/`、`.clice/` ❌（`.gitignore`）〔源〕 | — |

---

**不入库小结**：`webui/`、`runtime/`、`build/`、`compile_commands.json`、`out/_fixture/` 与 `out/review-*/`、`third/**` 构建产物、`.cache/`、`.clice/`。✅〔源〕（逐条对过 `.gitignore`）。

详见 [`refactor/README.md`](../../refactor/README.md)『1. 分层与目录』（分层图）与 [`refactor/architecture.md`](../../refactor/architecture.md)『1. 分层』。
