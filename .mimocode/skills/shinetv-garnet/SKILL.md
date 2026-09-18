---
name: shinetv-garnet
description: ShineTV 使用 Garnet（Redis 兼容）时的支持命令清单与 src/db 用法。当用户要写 Redis/Garnet 命令、问「Garnet 支不支持 X」、或用 shine::db 连 Garnet 时使用。数据源：microsoft.github.io/garnet/docs/commands/api-compatibility。
---

# ShineTV × Garnet（Redis 兼容）

本机/开发默认：`127.0.0.1:6379`。客户端封装在 `src/db/`（hiredis master + 连接池 + C++26 反射）。

## 用法入口

| 需求 | 头文件 / API |
|------|----------------|
| 初始化连接池 | `db/Redis.h` → `db::Init(PoolOptions)`（可配 `maxConnections`） |
| 借连接 | `db::Acquire()` → `Lease`（RAII 归还） |
| 编译期命令 | `db/RedisCmd.h` → `Execute(client, CmdGet{.key=...})` / `ExecuteAsync` |
| 结构体 ↔ Hash | `db/RedisHash.h` → `HSet` / `HGetAll`（C++26 反射） |
| 异步 | `db/RedisAsync.h` → `GetAsync` / `SetAsync` / `CommandAsync` / `ExecuteAsync` |
| 底层 argv | `Client::Command(span<string_view>)`（二进制安全） |

**禁止**：业务代码直接 `#include <hiredis.h>`（仅 `RedisClient.cpp` / `RedisValue.cpp` 可碰）。
**线程**：`Client` 非线程安全；并发必须走连接池 `Acquire`。

## 业务侧优先用的命令（Garnet 均 ➕）

### STRING（缓存 / 会话 / 计数）

`SET` `GET` `DEL` `EXISTS` `MGET` `MSET` `MSETNX`  
`INCR` `INCRBY` `INCRBYFLOAT` `DECR` `DECRBY`  
`APPEND` `STRLEN` `GETRANGE` `SETRANGE` `GETSET` `GETDEL` `GETEX`  
`SETNX` `SETEX` `PSETEX` `LCS`

### HASH（会话行 / 任务态 —— 配合 `RedisHash.h` 反射）

`HSET` `HGET` `HGETALL` `HDEL` `HEXISTS` `HKEYS` `HVALS` `HLEN`  
`HINCRBY` `HINCRBYFLOAT` `HMGET` `HMSET`（废弃，仍可用）`HSETNX`  
`HSCAN` `HSTRLEN` `HRANDFIELD`  
`HPEXPIRE` `HPTTL` `HTTL` `HPERSIST` 等字段级 TTL（Garnet 扩展）

### LIST（队列）

`LPUSH` `RPUSH` `LPOP` `RPOP` `LLEN` `LRANGE` `LINDEX` `LSET` `LREM` `LTRIM`  
`LMOVE` `BLPOP` `BRPOP` `LMPOP` `LPOS` `LPUSHX` `RPUSHX`

### SET（标签 / 去重）

`SADD` `SREM` `SISMEMBER` `SMEMBERS` `SCARD` `SMOVE` `SPOP`  
`SINTER` `SUNION` `SDIFF` 及 `*STORE` 变体 `SSCAN` `SISMEMBER` `SMISMEMBER`

### SORTED SET（排行 / 时间线）

`ZADD` `ZREM` `ZSCORE` `ZINCRBY` `ZCARD` `ZRANK` `ZREVRANK`  
`ZRANGE` `ZREVRANGE` `ZRANGEBYSCORE` `ZRANGEBYLEX`  
`ZPOPMIN` `ZPOPMAX` `ZCOUNT` `ZSCAN` `ZMSCORE`

### GENERIC / KEYS

`EXPIRE` `PEXPIRE` `TTL` `PTTL` `PERSIST` `TYPE` `RENAME` `RENAMENX`  
`SCAN` `UNLINK` `DUMP` `RESTORE` `KEYS`（生产慎用）

### 连接 / 服务器

`PING` `ECHO` `HELLO` `AUTH` `SELECT` `QUIT`  
`INFO` `DBSIZE` `TIME` `FLUSHDB` `FLUSHALL`（慎）`CONFIG GET/SET`  
`MULTI` `EXEC` `DISCARD` `WATCH` `UNWATCH`  
`SCRIPT LOAD/EVAL/EVALSHA` `EVAL`

## 明确不支持（Garnet ➖，写代码勿用）

- **STREAM** 全套：`XADD` `XREAD` `XGROUP` `XACK` …
- **FUNCTIONS**：`FCALL` `FUNCTION LOAD` …
- `RANDOMKEY` `COPY` `MOVE` `SORT` `WAIT`
- `KEYS` 在超大库上很慢；列表/扫描优先 `SCAN`

## 与 src/db 映射建议

| 场景 | 建议 |
|------|------|
| 任务状态行 | Hash + `RedisHash.h` 反射结构体 |
| 简单 KV 缓存 | `SET`/`GET`，TTL 用 `EXPIRE` 或 `SET EX` |
| 原子计数 | `INCR` / `INCRBY` |
| 生成队列 | List `LPUSH`+`BRPOP` |
| 标签集合 | Set |
| 热度排行 | ZSet |

## 官方兼容表

https://microsoft.github.io/garnet/docs/commands/api-compatibility  
（➕ 支持 / ➖ 不支持；列表随 Garnet 版本变化）

## 相关 skill

`shinetv-db`（`src/db` 完整用法：池 / 命令 / 反射 Hash / SQLite）、`shinetv-thirdparty`（hiredis 静态库 `shine_hiredis`）、`shinetv-build`。
