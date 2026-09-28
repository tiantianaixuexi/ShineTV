---
id: modules.project
kind: reference
status: current
scope: project
source_of_truth:
  - src/project/Project.h
  - src/project/Project.cpp
  - src/project/ProjectIndex.h
  - src/project/ProjectTemplate.h
  - src/project/ProjectTemplate.cpp
last_verified: 2026-09-25
---

# 项目模块

## 责任

`src/project` 负责项目身份、磁盘契约、项目索引、最近列表和模板生成。它不负责小说内容、视觉生成或 ComfyUI 业务；业务模块通过 `ProjectRef` 获取路径。

## 项目文件

`project.json` 由 `ProjectFile` 序列化，当前 schema 为 `1`。主要字段：

| 字段 | 含义 |
|---|---|
| `schemaVersion` | 文件结构版本；升级必须走 `Migrate` |
| `id` | `prj_<hex>` 稳定 ID |
| `name` | 项目名，允许中文 |
| `premise` | 一句话创意，可空 |
| `templateId` | `blank` / `novel` / `film` |
| `createdAt` | UTC ISO-8601 |
| `themeId` | 项目主题，可空表示跟随应用 |
| `comfyBaseUrl` / `llmProfileId` | 外部能力关联 |
| `runMode` / `budget` | 项目级运行策略 |
| `lastWorkspace` / `lastChapter` / `lastNovel` | UI 恢复状态 |

`Serialize` 固定键序、缩进和尾随换行；同一内存值应产生同一字节串。`Migrate` 宽容处理未知键、缺失键和非法值，但拒绝高于当前 schema 的文件。

## 模板

`ProjectTemplate.cpp` 的 `kContractDirs` 是目录契约的唯一实现，`PreviewTree` 与 `Materialize` 共用同一顺序：

```text
db/
work/
visual/
visual/assets/
visual/scenes/
visual/shots/
assets/
assets/refs/
assets/docs/
assets/fonts/
output/
output/images/
output/videos/
output/final/
flows/
cache/
logs/
```

- `blank`：目录骨架 + `project.json`。
- `novel`：blank + `db/novel.db` + `assets/docs/世界观.md`、`人物.md`、`大纲.md`。
- `film`：novel + `flows/分镜图.flow.json`、`flows/镜头视频.flow.json`。

`project.json` 最后写入；中途失败时不留下可被误认成项目的半成品元数据。

## 运行时引用

`ProjectRef` 提供：

```text
rootDir   项目根
 dbPath   <root>/db/novel.db
workDir   <root>/work
assetsDir <root>/assets
outputDir <root>/output
```

所有业务产物路径应从 `ProjectRef` 派生，不写死 `%APPDATA%` 或 CWD。

## 多书

`ListBooks` 采用一部一库：

- 默认书：项目根的 `db/novel.db` + `work/`；
- 系列其他书：`books/<书名>/db/novel.db` + `books/<书名>/work/`；
- 书内仍以 `volumes.ord → chapters.ord` 寻址；跨书引用不共享库。

## 生命周期

`ProjectService` 提供 `Create`、`Open`、`Save`、`Close`、`Current`、`Recent`、`Templates`。索引默认位于 `%APPDATA%\ShineTVStudio\projects.json`，可通过构造函数注入测试路径。

## 关键符号

- `project::ProjectFile`
- `project::ProjectRef`
- `project::ProjectService`
- `project::Materialize`
- `project::Migrate`
- `project::ListBooks`
- `project::SelfTest` / `TemplateSelfTest`
