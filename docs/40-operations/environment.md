---
id: operations.environment
kind: operations
status: current
scope: environment
source_of_truth:
  - src/ui/app/AppEntry.cpp
  - src/ui/app/StartupChecks.cpp
  - src/ui/verify/checks/
  - src/novel/NovelCli.h
  - src/mcp/HttpServer.h
last_verified: 2026-09-25
---

# 环境变量与运行模式

## 稳定入口

| 变量 | 作用 | 读取位置 |
|---|---|---|
| `SHINE_APPDATA_OVERRIDE` | 在创建 QApplication/读设置前替换 `APPDATA` | `AppEntry.cpp` |
| `SHINE_LOG_TO_STDERR` | stdio MCP 模式把日志写 stderr | `AppEntry.cpp` |
| `SHINE_MCP_ALLOW_WRITE` | 允许 MCP 写工具；默认关闭 | `NovelMcpTools` / 设置 |
| `SHINE_NOVEL_DB` | MCP/CLI 小说库覆盖路径 | MCP/CLI 入口 |
| `SHINE_EXIT_AFTER_SEC` | 定时退出 GUI 自检 | `P02Checks.cpp` |
| `SHINE_SCREENSHOT` | 启动后用 `QWidget::grab` 截图 | `P02Checks.cpp` |
| `SHINE_QSS_DUMP` | 导出当前 QSS | `StartupChecks.cpp` |
| `SHINE_QSS_PROBE` | QSS 动态属性诊断 | `P02Checks.cpp` |
| `SHINE_THEME_SET` | 启动时切换并持久化主题 | `StartupChecks.cpp` |
| `SHINE_THEME_SELFTEST` | 主题 JSON/反射往返自检 | `StartupChecks.cpp` |
| `SHINE_MOTION_SELFTEST` | 动效自检 | `StartupChecks.cpp` |
| `SHINE_PROJECT_SELFTEST` | 项目模型/索引/模板自检 | `StartupChecks.cpp` |

## 验收开关

`src/ui/verify/checks/` 中存在大量按阶段命名的开关，例如：

```text
SHINE_P02_REVIEW / SHINE_P02_SCREENSHOT
SHINE_P03_BOOTSTRAP / SHINE_P03_FOLD / SHINE_P03_PROBE / SHINE_P03_FW / SHINE_P03_REVIEW
SHINE_P04_S1…S10 / SHINE_P04_REVIEW
SHINE_P05_S1…S8 / SHINE_P05_REVIEW
SHINE_P06_S1…S8 / SHINE_P06_REVIEW
SHINE_P07_S1…S13 / SHINE_P07_REVIEW
SHINE_P08_S1…S8 / SHINE_P08_REVIEW
SHINE_P09_S1…S10 / SHINE_P09_REVIEW
SHINE_P10_S1…S10 / SHINE_P10_REVIEW
SHINE_GALLERY_SHOTS
SHINE_THUMB_BENCH / SHINE_TABLE_BENCH / SHINE_VIEWER_SELFTEST / SHINE_STYLEEDITOR_SELFTEST
SHINE_SCENE_IMAGE_CHECK
```

这些是源码内部验收入口，不是稳定用户 API；具体变量以对应 `Register*Checks` 实现为准。变量名使用 ASCII，值可用 UTF-8 路径；PowerShell 脚本本身保持 ASCII，避免 Windows PowerShell 5.1 误解码。

P05 的八个场景有现成跑法：`scripts/run_p05.ps1`（离屏、逐场景清空同族环境变量、
把报告写到 `build/p05/`，末行汇总哪些不是 PASS）。八个场景现在**全部真跑**，
没有 NOT-COVERED。

场景选择两种写法都收：`-Scenes S7,S8` 或直接位置参数 `S7,S8`（`powershell -File`
会把每个 token 都塞进 `$args`，只读 `$args[0]` 会把 `-Scenes` 当成场景名 ——
app 匹配不到任何检查就进正常主循环，harness 无限等）。每个场景有 **120s 硬超时**，
超时按 TIMEOUT 计入而不是挂死。

## 评审沙盒

`SHINE_P03_REVIEW` 至 `SHINE_P10_REVIEW` 会在启动早期把 `APPDATA` 指向评审目录下的 `_appdata`。不要在这些模式下写真实用户设置；评审结束后检查输出目录和 manifest。

## 命令行

- `--mcp-stdio`：stdio MCP；
- `--novel-init`、`--novel-storyboard`、`--novel-generate`、`--novel-run`：小说 CLI；
- `--widget-gallery`：控件画廊。

CLI 参数的精确语义见 `src/novel/NovelCli.h` 和 `src/ui/app/AcceptanceChecks.cpp`，不要在脚本中复制一份。

## 外部服务前提

- ComfyUI 默认 `http://127.0.0.1:8188`；
- LLM key/provider 来自 `settings.json` 或环境，不要提交；
- Redis 可选，默认本机 6379；不可用时应按业务降级，不应让整个应用崩溃。
