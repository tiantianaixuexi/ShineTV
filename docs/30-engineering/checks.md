---
id: engineering.checks
kind: operations
status: current
scope: verification
source_of_truth:
  - tools/check-layers.ps1
  - tools/check-colors.ps1
  - tools/check-theme.ps1
  - tools/check-i18n.ps1
  - tools/stability.ps1
  - tools/exception-paths.ps1
  - scripts/studio.ps1
  - scripts/capture_window.ps1
last_verified: 2026-09-25
---

# 检查与自动化脚本

## 静态门禁

```powershell
pwsh -File tools/check-layers.ps1
pwsh -File tools/check-colors.ps1
pwsh -File tools/check-theme.ps1
pwsh -File tools/check-i18n.ps1
```

| 脚本 | 检查内容 | 失败含义 |
|---|---|---|
| `check-layers.ps1` | core 无 Qt、旧 UI 标识、样式硬编码颜色 | 分层或 UI 规则违规 |
| `check-colors.ps1` | `setStyleSheet`/`QColor` 颜色字面量；主题文件数 | 颜色未走 Token 或主题缺失 |
| `check-theme.ps1` | `src/widget/theme/Themes` 至少四份且 JSON 可解析 | 主题资源损坏 |
| `check-i18n.ps1` | C++ 源码中的 Unicode replacement glyph | 编码损坏 |

`check-layers.ps1` 只扫描 `src/` 的 C/C++ 文件；文档和脚本中的说明文字不作为代码违规。

## 稳定性与异常路径

需要已有 `build/ShineTVStudio.exe`：

```powershell
pwsh -File tools/stability.ps1 -Runs 10
pwsh -File tools/exception-paths.ps1
```

这些脚本会启动真实进程并检查退出码。运行前确认没有旧实例占用文件；不要用强制结束掩盖真实启动失败。

## 进程控制

`scripts/studio.ps1` 提供 `status`、`start`、`stop`、`restart`、`build` 和 `all`。它只报告进程/构建状态，不再读取或生成开发进度。

## 截图与视觉验收

- `scripts/capture_window.ps1`：抓取指定窗口；先用标题/句柄确认目标。
- `scripts/crop_zoom.ps1`：放大局部，便于读取小字和状态。
- 评审环境变量会创建隔离 AppData；不要把截图目录或设置写回用户真实配置。
- 截图只能证明可见结果；编译通过、进程存在或文件生成不能替代交互路径验证。

## 自检模式

源码中的 `Run*SelfCheck` / `Run*Check` 函数由 `src/pages/checks` 和环境变量接线。优先使用已有开关，不要创建临时测试主程序。完整变量表见 [`../40-operations/environment.md`](../40-operations/environment.md)。

## 结果记录

每次验证至少记录：命令、工作目录、退出码、关键输出、是否启动真实 UI、是否依赖外部服务。`docs/40-operations/verification.md` 给出统一报告格式。
