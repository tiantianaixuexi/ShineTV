---
id: overview.runtime
kind: reference
status: current
scope: runtime
source_of_truth:
  - src/ui/app/main.cpp
  - src/ui/app/AppEntry.cpp
  - src/ui/app/StartupChecks.cpp
  - src/ui/app/AcceptanceChecks.cpp
last_verified: 2026-09-25
---

# 运行时与生命周期

## 启动顺序

`src/ui/app/main.cpp` 的入口非常短：初始化 `mimalloc`，然后调用 `shine::app::RunApp`。`RunApp` 的实际顺序如下：

1. `ConfigureAppDataSandbox()`：在 QApplication、设置和项目索引读取前处理 `SHINE_APPDATA_OVERRIDE` 及评审沙盒环境变量。
2. 读取 Windows 宽字符命令行。
3. 若命令包含 `--mcp-stdio`：设置日志到 stderr，直接进入 `mcp::RunStdioServerMain()`。
4. 若命令包含 `--novel-`：进入 `novel::RunNovelCli()`。
5. 创建 `QApplication`，设置应用名、组织名和中文字体回退链。
6. `RunStartupChecks()`：加载主题/设置/动效；根据环境变量执行自检、首启、评审或截图模式。
7. 初始化 `shine::async` 和图库；创建 15ms UI 定时器，持续 `DrainUiQueue()` 和图库 tick。
8. 创建并显示 `MainWindow`。
9. `acceptance::RegisterChecks()` 注册页面验收设施；某些环境变量会接管进程并提前退出。
10. 进入 `QApplication::exec()`。

## 退出顺序

正常事件循环退出后：

```text
gallery::Shutdown()
→ async::Shutdown()
→ log::Shutdown()
→ std::_Exit(result)
```

`AppEntry.cpp` 明确不 join worker，注释原因是 HTTP connect 或静态析构可能让快速关窗永久卡住。新增退出逻辑时必须保持：先停止业务订阅，再关闭 UI/图库，再停止日志；不要在 `_Exit` 前加入不可控阻塞操作。

## 命令行模式

| 命令 | 入口 | 行为 |
|---|---|---|
| `--mcp-stdio` | `mcp::RunStdioServerMain` | stdin/stdout JSON-RPC；stdout 不写日志 |
| `--novel-init` | `novel::RunNovelCli` | 初始化骨架/门禁 |
| `--novel-storyboard <chapter>` | 同上 | 生成/持久化叙事分镜 |
| `--novel-generate <chapter>` | 同上 | 生成一章并执行状态回写链 |
| `--novel-run <mode>` | 同上 | 手动/半自动/自动连跑 |
| 无参数 | `QApplication` | 正常 Qt 工作台 |

小说 CLI 的完整参数以 `src/novel/NovelCli.h` 为准；不要在文档中复制一份可能漂移的参数表。

## UI 定时器

UI 定时器每 15ms 调用：

- `shine::async::DrainUiQueue()`：执行 worker 投递到 UI 的结果回调；
- `shine::gallery::Tick()`：推进图库/媒体缓存状态。

任何新的后台结果都应沿用 `PostToUi`，不要直接从 worker 触碰 Qt 控件。

## AppData 与项目路径

- 全局设置默认位于 `%APPDATA%\ShineTVStudio\settings.json`。
- 启动时可用 `SHINE_APPDATA_OVERRIDE` 替换 `APPDATA`，用于隔离测试和评审。
- 项目配置是 `<project-root>\project.json`；项目业务产物由 `ProjectRef` 派生。
- 评审变量会把 `APPDATA` 指向对应评审目录下的 `_appdata`，避免污染用户设置。

路径转换必须经过 `std::filesystem::path` 与 `util::Encoding.h`，不能把宽字符逐字符拼成窄字符串。
