---
id: root.agents
kind: instruction
status: current
scope: repository
source_of_truth:
  - README.md
  - docs/README.md
  - CMakeLists.txt
  - src/
last_verified: 2026-09-25
---

# ShineTV Studio AI 协作规则

## 开始工作前

1. 先读根目录 `README.md`、`docs/README.md`，再读与任务对应的一个模块文档；不要从历史计划推断当前实现。
2. 先检查工作区已有改动。 unexpected changes 属于用户，不能回滚、覆盖或顺手整理。
3. 用源码符号和路径定位事实。文档只做导航；源码、CMake 和可执行检查优先。
4. 本仓库不维护开发计划、进度表或阶段交接文档；不要用计划壳替代实现和验证。

## 修改边界

- `shine_core`（`src/core`、`src/util`、`src/net`、`src/db`、`src/llm`、`src/comfy`、`src/media`、`src/flow`、`src/visual`、`src/novel`、`src/paint`、`src/mcp`、`src/project`、`src/pipeline`、`src/gpu`）不得包含 Qt 头；`tools/check-layers.ps1` 是门禁。
- 所有 UI 收敛在唯一根目录 `src/ui/`：`kit/` 可复用套件、`pages/` 业务页、`verify/` 验收取证（checks + review + gallery）、`app/` 装配入口、`layout/` Qt 布局助手。`src/ui/kit` 不依赖业务模块类型。
- 网络、文件扫描、图片解码、LLM 请求、ComfyUI 提交/下载和生成编译放 worker；通过 `async::PostToUi` 投递结果。UI 线程不做同步 IO/同步 HTTP。
- 复用现有基础设施：`shine::async`、`shine::log`、`AppSettings`、`net::HttpClient`、`util::Reflect`、SQLite/Comfy 适配层。新增第二份线程池、HTTP 客户端或 JSON 约定前先证明没有现成实现。
- 外部 C/C++ API（libhv、SQLite、Win32、yyjson、Qt）在边界转换一次；业务层接口遵循项目现有 C++ 约定，不为风格统一改动无关调用点。

## 文档任务

- 文档按职责分类：总览、模块、契约、工程、运行参考、源码索引。
- 每个文件头部写 YAML 元数据：`id`、`kind`、`status`、`source_of_truth`、`last_verified`。
- 只记录可证实事实；把“源码事实”“项目约定”“外部前提”“待运行验证”分开。
- 不保留旧路径的兼容文档或计划壳；删除文档后，搜索并修复代码、脚本和检查中的旧路径引用。
- 文档链接只指向仓库内仍存在的参考文件；删除文档后不留失效路径或兼容壳。

## 验证顺序

1. 文档变更：检查 Markdown 文件、相对链接、源码路径和脚本中的路径。
2. C++ 变更：先运行相关自检/门禁，再 `cmake --build build -j 8 --target ShineTVStudio`，最后按变更路径启动程序或运行 headless 模式。
3. UI 变更：实际启动 `ShineTVStudio.exe`，走对应工作区；截图验收使用 `scripts/capture_window.ps1`，不要只看编译结果。
4. 外部服务变更：先做离线 mock/协议自检，再在 ComfyUI 或 LLM 可用时联调；报告未执行的部分，不把推断写成通过。

## 常用入口

- 构建：[`docs/30-engineering/build.md`](docs/30-engineering/build.md)
- 分层/主题/i18n 门禁：[`docs/30-engineering/checks.md`](docs/30-engineering/checks.md)
- 环境变量和自检：[`docs/40-operations/environment.md`](docs/40-operations/environment.md)
- 源码定位：[`docs/90-reference/source-map.md`](docs/90-reference/source-map.md)
