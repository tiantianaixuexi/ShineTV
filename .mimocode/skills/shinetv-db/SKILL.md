---
name: shinetv-db
description: ShineTV 数据层 src/db 的用法速查（Redis 连接池 + 编译期命令/反射 Hash + SQLite 封装）。要写 db::Init / AcquireRedis / CmdGet / HSet / HGetAll / sqlite::Database / Prepare / Statement，或问「src/db 怎么用」「Redis 怎么借连接」「SQLite 怎么建表/查询」时使用。
---

# src/db 用法速查

命名空间 `shine::db`。`db/Db.*`（门面）+ `db/redis/*` + `db/sqlite/*`，CMake 里已 link 好（`shine_hiredis` / `shine_sqlite`）。

| 想干的事 | 头文件 / API |
|---|---|
| 起/停 Redis 池 | `db/Db.h` → `db::Init(DbConfig)` / `db::Shutdown()` |
| 借连接 | `db::AcquireRedis()` → `redis::Lease`（RAII） |
| 发命令 | `db/redis/RedisCmd.h` → `Execute(*lease, CmdXxx{...})` |
| 结构体 ↔ Hash | `db/redis/RedisHash.h` → `HSet` / `HGetAll` |
| 异步 | `db/redis/RedisAsync.h` → `GetAsync` / `ExecuteAsync` … |
| 本地库 | `db/sqlite/SqliteDb.h` → `sqlite::Database` |

⚠️ 两条先记住：
- `db::Init()` **目前还没被 app 调用** —— 用前确认，别假设池已起。
- `redis::DefaultClient()` **恒返回 `nullptr`**（占位实现）—— 一律用 `Acquire()`。
- SQLite **不在 `Init` 里**：谁用谁 `Open`（见 `novel/NovelProjects.cpp`）。

## 1. 初始化

```cpp
#include "db/Db.h"

shine::db::DbConfig cfg;                     // enableRedis = true；cfg.redis = PoolOptions
cfg.redis.connect.host = "127.0.0.1";        // + port / password / db / connectTimeout / commandTimeout
cfg.redis.maxConnections = 8;                // 0 => 1
cfg.redis.minConnections = 1;                // 0 = 纯懒加载（不预热）
cfg.redis.acquireTimeout = std::chrono::milliseconds{3000};

if (auto r = shine::db::Init(cfg); !r) {     // [[nodiscard]]：连不上会返回错误，别吞
    log::Warn("Redis 不可用: {}", shine::db::redis::ToChar(r.error().kind));
}
shine::db::redisReady();                     // min>0 → 预热成功；min=0 → 只代表"池已初始化"
shine::db::redisStats();                     // {maxConnections, idle, leased, created, closed}
shine::db::Shutdown();                       // 关池（建议在 async::Shutdown() 之前）
```

- `minConnections > 0`（默认 1）：预热没凑齐 = **Init 失败**，不留半开连接、`ready()==false`。
- `minConnections = 0`：Init 恒成功，首次 `Acquire()` 才真正连。
- Init 失败后，`Acquire()` 返回的是当初那个连接错误真因（不是 `pool not ready`）。

## 2. Redis

### 借连接

```cpp
auto lease = shine::db::AcquireRedis();      // expected<redis::Lease, redis::RedisError>
if (!lease) return;                          // *lease → Client& ；出作用域自动归还
```
`Lease` / `Client` 不可拷贝；`Client` **非线程安全** → 并发必须各自 `Acquire()`。

### 发命令

```cpp
#include "db/redis/RedisCmd.h"
using namespace shine::db::redis;            // 只在本文件局部用，别写进头文件

(void)Execute(*lease, CmdSet{.key = "user:1", .value = "shine"});
auto r = Execute(*lease, CmdGet{.key = "user:1"});   // expected<Value, RedisError>
if (r) { auto v = r->asString(); }

// 自定义命令：一个 struct + name，字段声明序 == argv 顺序
struct CmdLRange {
    static constexpr std::string_view name = "LRANGE";
    std::string_view key;
    std::int64_t start = 0;
    std::int64_t stop = -1;
};
auto items = Execute(*lease, CmdLRange{.key = "q", .stop = 9});
```
- 内置：`CmdPing`(0 参) `CmdGet` `CmdSet` `CmdDel` `CmdExists` `CmdIncr` `CmdExpire` `CmdHSetOne` `CmdHGet` `CmdHGetAll`。
- 字段类型：`string` / `string_view` / `bool`(→`"1"`/`"0"`) / `enum`(→整数) / 算术类型；其它 `static_assert`。
- 任意命令：`Client::Command(std::span<const std::string_view>)`（二进制安全）。

### 结构体 ↔ Hash

```cpp
struct SessionRow { std::string id; std::string state; std::int64_t updatedAt = 0; };

SessionRow row{.id = "s1", .state = "running", .updatedAt = 1234567890};
(void)redis::HSet(*lease, "session:s1", row);        // 字段名 == 成员名（含大小写，要和外部写入者对齐）

SessionRow back;
(void)redis::HGetAll(*lease, "session:s1", back);    // 缺字段保留原值，解析失败忽略
```
支持类型同上（读侧 `bool` 也认 `"true"` / `"TRUE"`）。

### 异步

```cpp
redis::GetAsync("k", [](std::expected<redis::Value, redis::RedisError> r) {
    // ⚠️ 回调在 worker 线程！改 UI 状态前必须 async::PostToUi(...)
});
```
`GetAsync / SetAsync / DelAsync / PingAsync / CommandAsync(vector<string>, cb) / ExecuteAsync(cmd, cb)`。
内部自动借还连接，失败走回调的 `unexpected`。

### 取值 / 错误

```cpp
v.valid(); v.type(); v.isNil(); v.isError(); v.isOkStatus();
v.asInt(); v.asDouble(); v.asBool(); v.asString(); v.asView(); v.asArray(); v.asStringArray();  // 全是 expected
redis::ToChar(err.kind);   // None Connect Io Protocol Command Timeout Closed
```

## 3. SQLite

```cpp
#include "db/sqlite/SqliteDb.h"

sqlite::Database db;                                    // 路径用 std::filesystem::path（中文安全）
if (auto r = db.Open({.path = dir / "novel.db", .create = true}); !r) return r.error().message;
// OpenOptions{ path, readOnly=false, create=true, memory=false }；memory → ":memory:"

(void)db.Exec("PRAGMA journal_mode=WAL;"
              "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);");  // 可分号多条

(void)db.Begin();
// ...
(void)db.Commit();          // 出错 db.Rollback()；LastInsertRowId() 取最近 INSERT 的 rowid
```

```cpp
// 写
auto ins = db.Prepare("INSERT INTO chapters(title, body) VALUES(?1, ?2)");
(void)ins->BindText(1, title);                  // ⚠️ 下标从 1 开始
(void)ins->BindText(2, body);
(void)ins->Step();                              // 写语句 Step 一次即 Done

// 读
auto sel = db.Prepare("SELECT id, title FROM chapters");
for (;;) {
    auto s = sel->Step();                                   // StepResult::Row / Done
    if (!s || *s == sqlite::StepResult::Done) break;
    const auto id = sel->ColumnInt(0);                      // 列下标从 0 开始
    const auto title = sel->ColumnText(1);
}
```
- Bind 族：`BindInt` / `BindDouble` / `BindText` / `BindBlob` / `BindNull`；`Reset()` = reset + clear_bindings（复用语句先 Reset 再 Bind）。
- `Statement` **必须先于其 `Database` 析构**。
- `Exec` 可跑多条；`Prepare` **只取第一条语句**。
- `SqliteError{ code, message }`；`SQLITE_MISUSE` = 未打开 / 语句无效。

## 4. 硬规则

1. 业务代码**禁止** `#include <hiredis.h>` / `<sqlite3.h>`（只有 `RedisClient.cpp`、`RedisValue.cpp`、`SqliteDb.cpp`、`SqliteStmt.cpp` 可碰）。
2. DB / 网络操作**必须异步**：`core/Async.h` 的 `RunOnWorker`；改 UI 用 `PostToUi`。别自建线程池。
3. `Database` / `Statement` / `Lease` / `Value` / `Client` 全部**可移动不可拷贝** → 入队要 `std::move`。
4. 新增 `.cpp` 必须手工登记到根 `CMakeLists.txt` 的 `src/db/...` 段。
5. 拼接统一 `fmt::format`，日志 `log::Info/Warn/Error`；返回的 `std::expected` 不许吞。

## 5. 相关

`shinetv-garnet`（Garnet 命令兼容表）· `shinetv-thirdparty`（hiredis / sqlite 静态库）· `shinetv-build` · `shinetv-structure`
