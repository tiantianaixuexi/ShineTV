---
id: root.readme
kind: index
status: current
scope: repository
source_of_truth:
  - CMakeLists.txt
  - src/
  - scripts/
  - tools/
last_verified: 2026-09-25
---

# ShineTV Studio

> Qt 6 Widgets 桌面项目：把小说设定、章节生产、视觉资产、分镜、ComfyUI 出图/出片和流水线控制放进同一个项目工作台。

本文档入口面向人类维护者和 AI 编码代理。**本仓库不再保存阶段计划、进度表或交接任务清单**；当前行为以源码、构建配置和可执行验证为准。

## 快速开始

```powershell
# 依赖：CMake 3.20+、MSYS2 MinGW64 GCC/G++ 16、Qt 6 Widgets
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
cmake -S . -B build -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/mingw32-make.exe `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j 8 --target ShineTVStudio
```

构建目标：`shine_core`（业务/协议静态库）→ `shine_kit`（Qt UI 套件）→ `ShineTVStudio`（可执行程序）。详见 [`docs/30-engineering/build.md`](docs/30-engineering/build.md)。

## 文档地图

| 类别 | 入口 | 用途 |
|---|---|---|
| 总览 | [`docs/00-overview/architecture.md`](docs/00-overview/architecture.md) | 分层、依赖方向、运行边界 |
| 项目 | [`docs/10-modules/project.md`](docs/10-modules/project.md) | 项目文件、模板、目录和生命周期 |
| 数据存储 | [`docs/10-modules/data-storage.md`](docs/10-modules/data-storage.md) | SQLite、Redis、项目/小说库边界 |
| 小说 | [`docs/10-modules/novel.md`](docs/10-modules/novel.md) | SQLite 世界状态、章节生成和状态回写 |
| 视觉 | [`docs/10-modules/visual-storyboard.md`](docs/10-modules/visual-storyboard.md) | 资产、参考图、分镜、生成任务 |
| 流程/ComfyUI | [`docs/10-modules/flow-comfy.md`](docs/10-modules/flow-comfy.md) | 图模型、编译、WS/HTTP 协议 |
| 编排 | [`docs/10-modules/pipeline.md`](docs/10-modules/pipeline.md) | 阶段机、预算、检查点和流水线 UI |
| 媒体/绘制 | [`docs/10-modules/media-paint.md`](docs/10-modules/media-paint.md) | 图库、解码、缓存、inpaint |
| LLM/MCP | [`docs/10-modules/llm-mcp.md`](docs/10-modules/llm-mcp.md) | 模型网关、Agent 工具循环、MCP |
| UI 套件 | [`docs/10-modules/ui-kit.md`](docs/10-modules/ui-kit.md) | 主题、控件、FlowCanvas 和工作区 |
| 工程约定 | [`docs/30-engineering/`](docs/30-engineering/) | 构建、代码规则、检查脚本 |
| 运行参考 | [`docs/40-operations/environment.md`](docs/40-operations/environment.md) | 环境变量、自检、故障排查 |
| 索引 | [`docs/90-reference/source-map.md`](docs/90-reference/source-map.md) | 按任务定位源码和符号 |

完整索引和阅读路径见 [`docs/README.md`](docs/README.md)。

## 当前系统边界

- **UI**：Qt 6 Widgets；页面在 `src/pages/`，可复用控件在 `src/widget/`，装配入口在 `src/app/`。
- **业务核心**：`shine_core` 不得依赖 Qt；网络、存储、ComfyUI、LLM、小说和流程图均可脱离窗口使用。
- **数据**：项目目录保存配置和产物；小说世界状态以 SQLite `novel.db` 为权威；缓存可删除。
- **外部服务**：ComfyUI 通过 HTTP/WebSocket 接入；LLM 通过同步客户端在 worker 线程调用；MCP 默认只监听本机。
- **线程**：IO、解析、解码、下载和生成在 worker；Qt 模型/UI 状态只在线程泵回调中更新。

这些是源码可证实的边界；外部服务是否可用、具体模型是否可用，必须按运行参考实际检查。

## 事实优先级

1. `CMakeLists.txt` 与当前源码头/实现；
2. `tools/`、`scripts/` 中的可执行检查；
3. 本目录中的当前参考文档；
4. 注释、历史记录或第三方文档。

文档与源码冲突时，先修源码或文档，不得用旧计划覆盖当前实现。

## 目录速览

```text
src/app/       应用装配入口、启动与验收开关
src/pages/     Qt 页面：shell/project/novel/storyboard/assets/image/videoflow/pipeline/gallery/settings/review/checks
src/widget/    可复用 UI：controls/images/data/canvas/theme/motion
src/core/      设置、日志、异步基础设施
src/util/      纯工具 + Qt 布局助手（QtLayout.h）
src/net/       libhv 出站 HTTP 薄封装
src/db/        SQLite / Redis
src/llm/       LLM Provider 与 AgentKit
src/comfy/     ComfyUI HTTP/WS/节点定义/队列
src/gpu/       DX11 纹理资源
src/flow/      纯图模型、工作流 IO、图编译与校验
src/media/     图库、媒体库、图片解码、缓存
src/visual/    资产、参考库、分镜/视频模型、任务执行器
src/novel/     小说数据库、图谱、初始化、生成、状态提交
src/paint/     CPU inpaint 画布
src/mcp/       MCP 工具注册、HTTP/SSE、stdio 传输
src/project/   项目模型、索引、模板
src/pipeline/  阶段机、预算、账本、检查点、Runner
third/         CMake 使用的 vendored 依赖
tools/         分层、主题、颜色、i18n、稳定性检查
scripts/       启动、构建辅助、ComfyUI、截图和打包辅助
```

`cpp_proposals/` 是独立的 C++ 特性研究/测试资料，`third/` 是 vendored 依赖，`Plugins/` 是外部参考；它们不属于 ShineTV 当前项目文档入口，也不能覆盖 `src/` 与 `CMakeLists.txt` 的事实。

## 文档维护规则

- 每份文档只回答一个问题；跨模块事实放在 `00-overview`，稳定接口放在 `20-contracts`，操作命令放在 `30-engineering`/`40-operations`。
- 文档头部保留 `id`、`kind`、`status`、`source_of_truth` 和 `last_verified` 元数据，便于 AI 检索和判断新鲜度。
- 不在文档中写未由源码或运行结果支持的“已完成”“必然成功”。
- 新增或删除源码文件后，同步更新 [`docs/90-reference/source-map.md`](docs/90-reference/source-map.md)；新增外部协议时同步对应模块文档。
