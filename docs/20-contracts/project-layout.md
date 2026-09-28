---
id: contracts.project-layout
kind: contract
status: current
scope: project-files
source_of_truth:
  - src/project/ProjectTemplate.cpp
  - src/project/Project.h
  - src/project/ProjectIndex.h
last_verified: 2026-09-25
---

# 项目目录契约

## 目录树

`ProjectTemplate.cpp` 的 `kContractDirs` 定义三套模板共享的目录；`project.json` 永远最后写入。

```text
<project-root>/
├─ project.json
├─ db/
│  └─ novel.db                    # novel/film 模板
├─ work/                          # 阶段产物、报告、检查点
├─ visual/
│  ├─ assets/
│  ├─ scenes/
│  └─ shots/
├─ assets/
│  ├─ refs/                       # 参考图
│  ├─ docs/                       # 预置设定文档（novel/film）
│  └─ fonts/
├─ output/
│  ├─ images/
│  ├─ videos/
│  └─ final/
├─ flows/                         # film 模板预置两个工作流
├─ cache/                         # 可删除
└─ logs/                          # 运行日志/临时诊断
```

## 模板差异

| 模板 | 额外内容 |
|---|---|
| `blank` | 无 |
| `novel` | `db/novel.db`、`assets/docs/世界观.md`、`assets/docs/人物.md`、`assets/docs/大纲.md` |
| `film` | novel 全部，加 `flows/分镜图.flow.json`、`flows/镜头视频.flow.json` |

## 路径规则

- 项目配置路径：`ProjectFilePath(rootDir)`。
- 业务根路径：`MakeRef(file, rootDir)` 生成 `ProjectRef`。
- 所有工作/视觉/输出路径从 `ProjectRef` 派生；禁止在模块中拼接 CWD 或硬编码 AppData。
- `cache/` 可删除；删除后正确性不能改变。
- `project.json` 是项目身份标志；不要用它判断半成品目录，半成品目录不应有该文件。

## 文件格式原则

- `project.json` 使用固定键序和宽容迁移；未来 schema 只能通过 `Migrate` 升级。
- 设定文档是用户可编辑的初始模板，不是世界状态权威。
- `novel.db` 是小说世界状态权威；`work/` 和 `visual/` 中的 JSON 是阶段产物/账本。
- 工作流 JSON 是可编辑资源；Comfy API JSON 是运行时提交格式，两者不能混称。
