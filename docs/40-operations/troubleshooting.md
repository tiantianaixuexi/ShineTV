---
id: operations.troubleshooting
kind: operations
status: current
scope: troubleshooting
source_of_truth:
  - src/ui/app/AppEntry.cpp
  - src/comfy/ComfySession.cpp
  - src/visual/VideoTaskRunner.cpp
  - src/util/Encoding.h
  - tools/check-layers.ps1
  - scripts/studio.ps1
last_verified: 2026-09-25
---

# 故障排查

## 程序不启动或立即退出

1. 确认 `build/ShineTVStudio.exe` 存在且不是被旧进程占用。
2. 查看 stdout/stderr 和 `logs/`；GUI 子系统的 printf 可能不显示在终端。
3. 检查是否误设了 `SHINE_EXIT_AFTER_SEC`、`SHINE_THEME_SELFTEST`、`SHINE_PROJECT_SELFTEST` 或 `SHINE_*_REVIEW`。
4. 用 `scripts/studio.ps1 status` 确认进程和 exe 状态。
5. 若怀疑设置污染，临时设置 `SHINE_APPDATA_OVERRIDE` 指向隔离目录。

## 中文路径/乱码

- 使用 `std::filesystem::path` 和 `util::Encoding.h`；不要把宽字符逐字符转成窄字符串。
- 检查 `util::OpenInput/OpenOutput` 是否被绕过。
- Win32 A 版错误文本先 `AcpToUtf8`，否则中文可能显示为 `?`。
- PowerShell 5.1 处理 UTF-8 脚本/文本时显式使用 UTF-8 编码；不要用默认 `Get-Content | Set-Content` 批量改源文件。

## ComfyUI 显示未连接或卡住

先读 `HealthSummary()`、`Busy()`、`LastErrorDetail()` 和最近日志：

- `Disconnected`：检查 base URL、端口、WS 和 `onclose`；
- `Queued/Running`：等待，不要因一次探活超时改成断线；
- `Interrupted`：是中断状态，不等同失败；
- `Stalled`：必须同时满足 WS 静默、运行队列未变和超时阈值；
- 节点输入错误：查看 `/object_info` 与 `/prompt` 的 `node_errors`，不要只看超时。

## 提交前校验 blocked

`VideoTaskRunner::CheckAgainstComfyUI` 在 `/object_info` 未就绪时会返回 blocked。先刷新节点定义，确认节点类名和输入名，再提交；不要把 blocked 改成“校验通过”。

## 生成任务失败或结果不完整

- 查看 `VideoTaskState.error`、`degradations`、job 账和输出目录；
- 确认上传后的文件名只写入工作副本，没有改写工程字段；
- 检查宽高、帧数、参考图数量是否符合对应生成模式；
- 运行对应的 `SHINE_SCENE_IMAGE_CHECK` 或队列自检，区分纯逻辑问题和 ComfyUI 联调问题。

## 小说门禁失败

读取 `InitReport`/`InitFailure` 的 `n_id`、`detail`、`fix_hint`。不要跳过 N1–N14 或把骨架的空内容伪造成已初始化；修复数据后重新运行门禁。

## UI 主题或控件异常

- 先运行 `check-theme.ps1`、`check-colors.ps1`、`check-layers.ps1`；
- 确认颜色来自 `theme::Current()`，没有页面内联 `#RRGGBB`；
- 确认控件五态和 QSS 动态属性；
- 用 `SHINE_SCREENSHOT` 或评审开关实际截图，不只看编译日志。

## 退出卡住

当前退出策略故意不 join worker。若出现残留进程，先确认是否有 ComfyUI/LLM 网络操作；不要在 `_Exit` 前新增无界 join。记录进程、网络和最近日志，再决定是否修复生命周期。
