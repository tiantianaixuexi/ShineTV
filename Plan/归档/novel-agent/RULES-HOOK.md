# 小说 Agent · 项目规范挂接

> 本文**不复制** `Doc/` 全文，只写「哪条约束本计划线、小说侧有何特例」。  
> 开工仍须先读：`Doc/AGENTS.md` → `Doc/RULES-AI.md` → 需要时 `RULES-LANG` / `BUILD` / `STYLE-UI` / skill `shinetv-db`。

## 必须遵守（硬性）

| 来源 | 条目 | 对小说 Agent 的含义 |
|------|------|---------------------|
| `Doc/AGENTS.md` | 工具链 GCC 16.1 / C++26 | 不用 MSVC/Clang 混编 |
| `Doc/AGENTS.md` | 技术栈表 | libhv · yyjson · stdexec · spdlog+fmt · mimalloc；**禁止** nlohmann / std::format / 新引 curl |
| `Doc/AGENTS.md` | 语言特性 + `RULES-LANG` §13 | `string_view`/`span`/`expected`/`ranges`/`[[nodiscard]]`；接口禁止 `bool+out` |
| `Doc/RULES-AI` §11.1–2 | 一次一任务；每步 build+运行 | 对照 `PROGRESS.md` 勾选，禁止一次写完 P1–P10 |
| `Doc/RULES-AI` §11.3 | `.cpp` 手写进 CMake | `src/openai` `src/agent` `src/novel` 新文件全部登记 |
| `Doc/RULES-AI` §11.5 | JSON=yyjson | OpenAI body、MCP、布局 json 一律 yyjson |
| `Doc/RULES-AI` §11.6 | 线程纪律 | HTTP/SSE/出图只在 worker；UI 禁止同步 HTTP；结果 `PostToUi` |
| `Doc/RULES-AI` §11.7 | 不重复造设施 | 用 `shine::async` / `shine::log` / `AppSettings` / `src/db`；勿自建线程池 |
| `Doc/RULES-AI` §11.9 | 不过度设计 | 对照本计划 `S3 Out of Scope` 与 `Doc/BASELINE.md` §8 |
| `Doc/RULES-AI` §11.10 | 中文 UI + theme | `app/novel` 文案中文，取色 `theme::Current()` |
| `Doc/RULES-AI` §11.16 | 新代码风格 | 触碰旧签名可顺手升级，但**不做跨模块大重构** |
| `Doc/BUILD.md` | configure/build 命令 | 每阶段验收用同一套命令 |
| `Doc/STYLE-UI.md` | 色板 token | 小说工作区、生成预览不硬编码色 |
| skill `shinetv-db` | SQLite/Redis 用法 | `novel.db` 走 `src/db/sqlite`；Redis 缓存/锁走既有池 |
| skill `shinetv-structure` | UI 只进 `app/` | 视图在 `src/app/novel/`；`src/novel`/`src/agent` 无 ImGui |

## 有意特例（须写进实现 PR 说明）

| 来源原约定 | 小说线特例 | 理由 |
|------------|------------|------|
| `RULES-AI` §11.4：libhv 只在 `comfy/` 与 `mcp/` | **允许** `src/openai/`（及 P10 若在 `src/mcp/`）使用 libhv | Responses/SSE 与 MCP 传输；仍禁止业务 UI 直接摸 libhv |
| `RULES-AI` §11.12–15：Comfy 协议错误/忙碌 | OpenAI/图片后端**类比**：错误从 HTTP body/协议取；禁止用超时猜内容；日志能答「现在在干什么」 | 精神一致，字段不同 |
| `RULES-COMFY.md` | 仅当 **P9 复用 ComfyUI 出图** 时全文适用 | 纯 OpenAI/自托管 HTTP 后端则不套 Comfy WS 规则 |
| VNS / `graph/` | 小说线**不改** VNS | 中央节点图与小说库无关 |

## 密钥与安全（小说特有，补充 Doc/）

| 项 | 规则 |
|----|------|
| `OPENAI_API_KEY` | 仅 Settings/环境变量；不进 git、不进日志、不进 MCP 回复 |
| MCP 写工具 | 默认关；开启后仍 PROPOSED + `audit_logs` |
| 路径 | 资产/导出限制在当前工程目录内 |

## 进度与文档

| 项 | 位置 |
|----|------|
| 进度勾选 | **只改** `docs/compose/plans/novel-agent/PROGRESS.md` |
| 阶段细则 | `P*/PLAN.md` 与分册 |
| 契约 | `docs/compose/spec/novel-agent.md` |
| 项目总规则 | `Doc/AGENTS.md` / `RULES-AI.md`（冲突时：语言与线程以 Doc 为准；小说产品细节以本计划为准） |

## 开工检查清单（复制用）

- [ ] 已读 `Doc/AGENTS.md` + `Doc/RULES-AI.md`
- [ ] 已知 build 命令（`Doc/BUILD.md`）
- [ ] 新 `.cpp` 会进 CMake
- [ ] 不用 nlohmann / std::format
- [ ] UI 线程不发同步 HTTP
- [ ] 密钥不入库
- [ ] 只勾 `PROGRESS.md`，一次做一个 ID
