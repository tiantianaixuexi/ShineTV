---
id: modules.data-storage
kind: reference
status: current
scope: data-storage
source_of_truth:
  - src/db/Db.h
  - src/db/redis/Redis.h
  - src/db/sqlite/SqliteDb.h
  - src/novel/NovelDb.h
last_verified: 2026-09-25
---

# 数据存储模块

## 边界

`src/db` 提供两个适配层：

- `shine::db::sqlite`：项目/小说本地库；路径、事务、语句和错误均封装；
- `shine::db::redis`：可选 Redis/Garnet 连接池、lease、命令和异步接口。

业务代码不直接 include `sqlite3.h` 或 `hiredis.h`。第三方句柄只在适配 `.cpp` 内出现。

## SQLite

`Database::Open` 使用 `OpenOptions`；`Exec` 支持多条 SQL，`Prepare` 只准备第一条语句。`Statement` 的绑定参数从 1 开始，结果列从 0 开始；`Statement` 必须先于 `Database` 析构。

小说库由 `NovelDb` 打开和迁移；当前 schema 目标版本为 12。CLI、自检、MCP 和 UI 都应复用同一迁移/建表入口，不复制 DDL。

## Redis

`db::Init(DbConfig)` 初始化可选连接池；`db::AcquireRedis()` 返回 RAII `Lease`。预热失败保持 not ready，调用方必须处理；`minConnections=0` 可把连接延迟到首次 acquire。`Client` 非线程安全，并发使用独立 lease。

设置字段在 `AppSettings`：`redisEnabled`、host、port、password、db、连接数和超时。Redis 不可用不应改变 SQLite/项目文件的权威性。

## 路径与线程

- Windows 路径使用 `std::filesystem::path`；中文路径通过 `util::Encoding.h`/`util::File.h`；
- DB I/O 在 worker，结果回 UI 前不改控件；
- 失败返回 `expected`/明确错误，不能静默使用空数据；
- 事务失败必须回滚，缓存和连接错误要记录来源。

## 相关入口

`src/db/Db.h`、`src/db/redis/*`、`src/db/sqlite/*`、`src/novel/NovelDb.*`、`src/project/Project.*`。
