---
id: reference.glossary
kind: reference
status: current
scope: terminology
source_of_truth:
  - src/project/Project.h
  - src/novel/NovelTypes.h
  - src/novel/NovelRunLoop.h
  - src/visual/VideoTypes.h
  - src/pipeline/StageMachine.h
  - src/comfy/ComfyTypes.h
last_verified: 2026-09-25
---

# 术语表

| 术语 | 含义 |
|---|---|
| Project | 项目根目录及 `project.json` 的身份集合 |
| ProjectRef | 从项目配置派生的运行时路径引用 |
| BookRef | 一部小说及其独立 `novel.db`/`work/` |
| novel.db | 小说世界状态 SQLite 权威库 |
| StateDiff | 正文产生的结构化世界状态变化 |
| Canon | 已确认的规范状态；与 PROPOSED/DRAFT 区分 |
| V1–V11 | 小说到视觉产物的阶段/链标识 |
| T1–T17 | 通用/小说章节阶段标识 |
| Shot | 可提交给生成后端的单个镜头模型 |
| GenShot | 从叙事镜头转换出的生成镜头/工程 |
| Flow | 节点、端口、连线和值的纯图模型 |
| API JSON | ComfyUI `/prompt` 接受的提交格式 |
| Workflow JSON | 带布局/节点元数据的编辑器工作流格式 |
| Object Info | ComfyUI `/object_info` 返回的节点定义 |
| BusyState | ComfyUI 空闲/排队/运行/中断/疑似卡住状态 |
| MCP tool | 通过 JSON-RPC 注册和调用的外部工具 |
| Agent | 带角色、prompt 和工具白名单的模型调用单元 |
| `PostToUi` | 将 worker 结果投递到 UI 邮箱的线程边界 |
| AppData | `%APPDATA%\ShineTVStudio` 下的设置、布局和缓存根目录 |
| Token | 主题/几何/动效的命名视觉常量 |
| DTO | 不含 UI/业务所有权的传输结构；画布和跨模块接口常用 |

## 状态词约定

- `draft` / `DRAFT`：草稿或未确认；
- `PROPOSED`：模型或流程提出，未升为 Canon；
- `CANON`：已确认规范；
- `submitted`：已提交外部生成任务；
- `done`：任务完成；
- `failed`：明确失败；
- `cancelled` / `Interrupted`：中断，不等同失败；
- `degraded`：走了可用但有损的降级路径，必须有账；
- `Unavailable`：来源没有可显示资源，不等同请求失败。

## 读文档时的判断

看到 `Pxx`、`Sxx`、`Txx`、`Vxx`、`Kxx`、`Gxx` 等编号时，先判断它属于：

1. 当前代码中的枚举/表/门禁；
2. 验收输出或错误码；
3. 历史上下文。

只有前两类能作为当前行为依据；第三类不应被重新包装成开发计划。
