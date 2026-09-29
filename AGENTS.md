---
id: root.agents
kind: instruction
status: current
scope: repository
source_of_truth:
  - README.md
  - refactor/
  - CMakeLists.txt
  - src/
last_verified: 2026-09-30
---

# ShineTV Studio AI 协作规则

> ⚠️ **本仓库正在做 Qt → Dear ImGui 前端重构。** `refactor/` 是本次重构的**唯一权威**。
> 仓库里的 `docs/` 是 Qt 时代的旧文档，**不引用、不更新、不维护**，也不要按它推断当前实现。
> 重构步骤见 [`refactor/phases.md`](refactor/phases.md)，进度见 [`refactor/PROGRESS.md`](refactor/PROGRESS.md)。

## 开始工作前

1. 先读根目录 `README.md`、`refactor/README.md`，再按任务读 `refactor/` 下的对应一篇；不要从历史计划推断当前实现。
2. 先检查工作区已有改动。 unexpected changes 属于用户，不能回滚、覆盖或顺手整理。
3. 用源码符号和路径定位事实。文档只做导航；源码、CMake 和可执行检查优先。
4. 除 `refactor/phases.md` 与 `refactor/PROGRESS.md` 外，本仓库不维护别的开发计划、进度表或阶段交接文档；不要用计划壳替代实现和验证。

## 修改边界

- `shine_core`（`src/core`、`src/util`、`src/net`、`src/db`、`src/llm`、`src/comfy`、`src/media`、`src/flow`、`src/visual`、`src/novel`、`src/paint`、`src/mcp`、`src/project`、`src/pipeline`、`src/gpu`）不得包含 Qt 头，也不得包含 ImGui 头；`tools/check-layers.ps1` 是门禁。
- 所有 UI 收敛在唯一根目录 `src/ui/`。**P7 已删掉整棵 Qt 树**（`src/ui/{app,kit,layout,pages,qml,verify}` 共 198 个文件），现在 `src/ui/` 下只有 `imgui/` 一棵树；CMake 也不再有任何 `find_package(Qt6)` / `AUTOMOC` / `AUTORCC`，配过与构建全程零 Qt 依赖。
  - `imgui/host/` 宿主与入口 · `imgui/theme/` 主题与字体 · `imgui/kit/` 组件套件 · `imgui/pages/` 业务页 · `imgui/verify/` 取证 · `imgui/app/main.cpp` 属于 exe 不进静态库。
  - 不要再引用 `SHINE_UI_QT`、`shine_kit`、`shine_qml` 或 `scripts/package-qt.ps1` —— 这些都没了（打包用 `scripts/package-imgui.ps1`）。
- 重构期**任何情况下都不改 `shine_core`**：业务层零 Qt 已成立，需要接的只有 `gpu::AttachDevice()` 这一处宿主注入。
- 网络、文件扫描、图片解码、LLM 请求、ComfyUI 提交/下载和生成编译放 worker；通过 `async::PostToUi` 投递结果。UI 线程不做同步 IO/同步 HTTP。
- 复用现有基础设施：`shine::async`、`shine::log`、`AppSettings`、`net::HttpClient`、`util::Reflect`、SQLite/Comfy 适配层。新增第二份线程池、HTTP 客户端或 JSON 约定前先证明没有现成实现。
- 外部 C/C++ API（libhv、SQLite、Win32、yyjson、Qt）在边界转换一次；业务层接口遵循项目现有 C++ 约定，不为风格统一改动无关调用点。

### 源码编码

- `src/ui/imgui/**` 全树是**无 BOM 的 UTF-8**。读写它只用 `read` / `edit` / `write` 工具。
- **不要**用 PowerShell 的 `Set-Content` / `Out-File` / `[System.IO.File]::WriteAllText` 改源码：它们按系统 ANSI 码页（中文 Windows = GBK/936）重编码，会静默把 `⚠️`、`→` 这类 GBK 编不了的字符换成 `?`，编译照过、中文照在，但注释里的标记没了。必须用脚本改字节时，全程只走 `ReadAllBytes` / `WriteAllBytes` 的纯字节替换，不经过任何编码器。
- 判别编码别看「高位字节里 0xC2-0xDF 多还是 0x81-0xFE 多」——GBK 汉字首字节本来就落在 0xB0-0xF7，这个方法会把 UTF-8 文件误判成 GBK。用严格解码试一次：

  ```powershell
  $s = New-Object System.Text.UTF8Encoding($false,$true)
  try { $null = $s.GetString([System.IO.File]::ReadAllBytes($f)); "$f UTF-8" } catch { "$f 非 UTF-8" }
  ```

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
5. 取证（多图）：跑完**先排 md5** —— 任意两张逐字节相同 = 有一张没拍到它承诺的状态。等待结果要写进 manifest，别丢返回值；每张图的前置动作显式写全，别依赖"默认状态恰好是我要的"。
6. 像素差**不是**内容指标：离屏渲染跨进程约 2000px 噪声、跨构建约 40000px。小于它不必当回事，**大于它也不能**直接判成回归。判"内容有没有丢"用同一次运行内的对照页 + 无文字纯色区域的精确相同率。

## 常用入口

- **重构总览与边界**：[`refactor/README.md`](refactor/README.md)
- **实施步骤（51 步）**：[`refactor/phases.md`](refactor/phases.md)
- **进度看板**：[`refactor/PROGRESS.md`](refactor/PROGRESS.md)
- **目标架构**：[`refactor/architecture.md`](refactor/architecture.md)
- **webui 1:1 视觉规范**：[`refactor/design-spec.md`](refactor/design-spec.md)
- 构建：`cmake --build build -j 8 --target ShineTVStudio`（工具链 MSYS2 MinGW64，`C:/msys64/mingw64`）
- 分层门禁：`powershell -File tools\check-layers.ps1`
- 截图取证：`scripts\capture_window.ps1`
