# P6 · ImGui 接入

依赖：P5  
模块：`src/app/novel/*`（UI only）

## 界面

在小说工作区（见 novel-studio）增加：

| 区 | 内容 |
|----|------|
| 工具条 | 生成本章 / 停止 / 重新生成 |
| 进度 | 阶段名（规划/写作/审核/修订/提取）+ 百分比 |
| 流式预览 | Writer delta 追加显示 |
| 结果 | 成功摘要 / Critic issues 列表 |
| 提议 | PROPOSED 实体列表 → 确认 CANON / 忽略（可 P6.2） |

侧栏可加「Agent 日志」只读列表（最近 N 条）。

## 线程

- 点击「生成」→ `async` 启动 `GenerateChapterAsync`
- 回调一律 `async::PostToUi`
- 禁止 UI 线程 HTTP

## 文件

```
NovelAgentPanel.h/.cpp   或并入 NovelWorkspace
```

CMake 登记。

## 验收

- [ ] 无 CLI 完成一章生成
- [ ] 流式逐步刷新
- [ ] 失败时中文错误，不卡死 UI
- [ ] 停止可取消（或标记丢弃结果）

## 任务

| ID | 内容 |
|----|------|
| P6.1 | 生成按钮 + 进度 + 流式预览 |
| P6.2 | issues / PROPOSED 确认 |
| P6.3 | 取消与错误态 |
