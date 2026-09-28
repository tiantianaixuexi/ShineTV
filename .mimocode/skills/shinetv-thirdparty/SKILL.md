---
name: shinetv-thirdparty
description: ShineTV 当前 CMake 使用的 vendored 依赖、适配入口、线程/JSON/图像库边界与新增库步骤。
---

# 第三方依赖

先读：[构建说明](../../../docs/30-engineering/build.md)、[编码规则](../../../docs/30-engineering/coding-rules.md)。

## 当前 CMake 依赖

| 领域 | 依赖/入口 | 规则 |
|---|---|---|
| Qt | Qt 6 Widgets | 只在 `shine_kit`/应用层使用 |
| 网络 | libhv `hv_static` | 业务经 `net`/`comfy` 适配；同步调用 worker |
| JSON | yyjson | 设置/Comfy/MCP；不手写第二套 DOM |
| 内存 | mimalloc | `main` 最早 `mi_process_init` |
| 异步 | stdexec | 统一经 `core/Async` |
| 图像 | libpng、libjpeg-turbo、libwebp | 各自 decoder 适配层隔离符号 |
| 存储 | SQLite、hiredis | 业务经 `src/db` |
| 格式化/日志 | fmt、spdlog | 业务不直接写第三方日志 |
| 其他 | zlib、function2、zmij、imageinfo | 按当前 CMake/模块需要使用 |

`third/glaze` 等目录若未登记 CMake，不视为已接入；不要因为目录存在就直接 include。

## 适配原则

- 第三方 C/C++ 类型在边界转换一次；业务接口不暴露原始句柄。
- 不修改 `third/` 来迎合业务，除非任务明确要求并有兼容验证。
- 新库先放入 `third/`，更新根 CMake、include/link、适配层和本文档。
- 新增 `.cpp` 手工登记对应 target；不要自动 glob 整个 third。
