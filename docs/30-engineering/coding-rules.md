---
id: engineering.coding-rules
kind: instruction
status: current
scope: coding
source_of_truth:
  - AGENTS.md
  - CMakeLists.txt
  - tools/check-layers.ps1
  - src/util/Reflect.h
  - src/core/Async.h
last_verified: 2026-09-28
---

# 编码与工程规则

## 分层

- `shine_core` 禁止 Qt include；页面与控件全在 `src/ui` 这一个 UI 根目录下。
- `src/ui` 内部按内容切分：`kit/` 可复用套件、`pages/` 业务页、`verify/` 验收取证、`app/` 装配入口、`layout/` Qt 布局助手。
- `src/ui/kit` 不得依赖业务类型（不 include `novel/`、`project/` 等）；Qt 布局助手在 `src/ui/layout/QtLayout.h`。
- 不让业务层回调 QWidget；UI 通过公开 DTO、命令和回调交互。
- 业务层不得直接依赖 ComfyUI/数据库的第三方 C 类型；适配层负责转换。

## 接口与错误

- 新接口优先使用 `std::string_view`、`std::span`、`std::expected`、`std::optional`、`std::filesystem::path` 和 `std::chrono`。
- 纯借用回调（非拥有、不跨调用存活）用 `std::function_ref`；需要所有权时用 `std::move_only_function`。
- 需要持有或返回拥有的文本使用 `std::string`；不要返回内部成员引用。
- 查询或可失败函数按语义加 `[[nodiscard]]`；不抛异常的函数再加 `noexcept`。
- 不用 `bool + out` 作为新 API 的默认形状；外部 C API 的边界例外在适配层转换一次。
- 错误必须可读、可诊断、可记录；不要静默降级。降级要进入结构化账本或明确 UI 状态。

现有模块中仍保留部分历史 `bool + out` 结果结构（如 Comfy HTTP 解析结果），这是兼容契约，不应为了风格统一扩大重构范围。

## 路径、编码与 JSON

- Windows 路径用 `std::filesystem::path`；字符串边界走 `util::PathFromUtf8` / `util::PathToUtf8`。
- Win32 A 版 API 返回文本先转 UTF-8；不要把 ANSI/GBK 直接塞入 UI 字符串。
- 文本格式化走项目既有的 `fmt::format`（不要另起第二套约定）；颜色一律 `theme` token。
- 项目设置、主题和反射对象优先用 `util::Reflect`；Comfy API JSON 用 yyjson。
- 未知 JSON 字段宽容忽略，缺失字段使用默认值；根结构非法必须报错。

## 线程

```text
worker: 读文件 / 扫描 / 解码 / 网络 / LLM / Comfy 编译与下载
  → async::PostToUi(result)
UI:     DrainUiQueue → 更新模型、控件和 GPU 资源
```

- 不在 UI 线程执行同步 HTTP、目录扫描、图片解码或阻塞式模型调用。
- 不从 WS/IO 线程直接修改 Qt 控件或 UI 模型。
- 不新建线程池、任务队列或 UI 泵；复用 `shine::async`。
- 资源生命周期要明确：worker 产出拥有所有权的数据，跨线程只传必要的 move-only 句柄或值。

## 命名与文件

- 文件用名词或领域名：`Project.*`、`VideoTaskRunner.*`。
- 类型 `PascalCase`；函数使用动词短语；成员通常带尾下划线。
- 命名空间使用 `shine::<domain>`。历史上有 `shine::novelcore`、`shine::novel`、`shine::agents`、`shine::videos` 等兼容命名空间，新增代码沿用所在模块约定并在头文件说明。
- 注释写不明显的约束和原因，不复述代码。

## 验证

代码改动至少要：

1. 编译受影响 target；
2. 运行相关离线自检或门禁；
3. 若改 UI/运行时，实际启动程序并走对应路径；
4. 记录命令、退出码和关键输出，不用“应该可以”替代证据。
