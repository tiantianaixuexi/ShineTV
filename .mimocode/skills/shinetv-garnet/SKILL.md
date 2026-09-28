---
name: shinetv-garnet
description: ShineTV 可选 Garnet/Redis 兼容层的连接、命令和限制；用于判断缓存/队列命令是否可用。
---

# Garnet / Redis 边界

先读：[数据层技能](../shinetv-db/SKILL.md)、[编码规则](../../../docs/30-engineering/coding-rules.md)。

## 当前入口

- 连接池与门面：`src/db/Db.*`、`src/db/redis/*`。
- 设置：`AppSettings::redisEnabled/redisHost/redisPort/...`。
- 业务只通过 `shine::db` API；禁止直接 include `hiredis.h`。
- `Client` 非线程安全；并发使用独立 lease。

## 命令选择

- KV/缓存：优先 `GET/SET/DEL/EXPIRE`；
- 结构化行：Hash + 反射辅助；
- 队列：List；
- 去重：Set；排行：Sorted Set；
- 扫描优先 `SCAN`，不要在生产路径使用 `KEYS`。

Garnet 对 Redis 命令的兼容范围随版本变化；使用前查官方兼容表和本机服务版本，不把“Redis 文档支持”直接当成 Garnet 已验证。

## 失败处理

连接池未就绪或服务不可用时返回明确错误/降级状态；不要伪造 `ready`，不要在 UI 线程同步重试。缓存可删除，权威数据仍应在 SQLite/项目文件中。

官方参考：`https://microsoft.github.io/garnet/docs/commands/api-compatibility`。
