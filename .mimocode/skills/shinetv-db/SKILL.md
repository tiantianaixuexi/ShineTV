---
name: shinetv-db
description: ShineTV 当前 SQLite/Redis 数据层用法、线程边界、错误处理与 schema 入口。
---

# 数据层

先读：[项目布局](../../../docs/20-contracts/project-layout.md)、[小说状态](../../../docs/20-contracts/novel-state.md)。

## SQLite

`src/db/sqlite/SqliteDb.h` 提供 `Database`、`OpenOptions`、`Statement`。路径使用 `std::filesystem::path`；`Open`/`Exec`/`Prepare` 返回 `expected`，错误不能吞掉。

```cpp
db::sqlite::Database db;
if (auto r = db.Open({.path = project / "db/novel.db", .create = true}); !r) return r.error();
if (auto r = db.Exec("PRAGMA foreign_keys=ON;")) { /* log or handle */ }
auto stmt = db.Prepare("SELECT id, title FROM chapters");
```

`Statement` 先于 `Database` 析构；绑定参数从 1 开始，结果列从 0 开始。小说 schema/迁移的唯一入口是 `NovelDb.cpp` 与 `NovelDb::EnsureSchemaUpToDate`，不要在夹具或 CLI 手抄 DDL。

## Redis

`src/db/redis` + `Db.cpp` 提供连接池、RAII lease、命令和异步接口。业务代码不直接 include `hiredis.h`；`Client` 非线程安全，并发各自 `Acquire`。Redis 可选，连接失败要按业务降级并记录，不让整个应用崩溃。

## 线程与路径

- DB/网络/序列化在 worker；UI 结果经 `async::PostToUi`。
- 文件路径和中文目录使用 `util` 编码适配。
- 事务失败回滚；`expected`/错误信息原样进入日志或 UI。
- 新增数据库源文件同步根 `CMakeLists.txt`。
