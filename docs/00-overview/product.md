---
id: overview.product
kind: reference
status: current
scope: product-boundary
source_of_truth:
  - src/ui/pages/shell/MainWindow.h
  - src/project/Project.h
  - src/novel/NovelPipeline.h
  - src/visual/VideoTaskRunner.h
last_verified: 2026-09-25
---

# 产品形态与边界

## 产品形态

ShineTV Studio 是一个**项目中心制**的桌面生产工作台：

1. 启动进入项目列表；可创建 `blank`、`novel` 或 `film` 模板项目。
2. 打开项目后进入 Qt Widgets 工坊：顶栏、活动栏、侧栏、中央文档页、右侧检查器、底部队列和状态栏。
3. 小说工作区提供书/卷/章、初始化、章节流水线、评审、模型、状态提交和自动运行视图。
4. 资产、分镜、出图和出片工作区消费同一项目上下文；项目路径由 `project::ProjectRef` 派生。
5. ComfyUI、LLM 和 MCP 是可替换的外部能力，不改变项目文件的核心边界。

## 核心对象

| 对象 | 定义入口 | 说明 |
|---|---|---|
| 项目 | `project::ProjectFile` / `ProjectRef` | 目录、配置、模板和 UI 状态 |
| 书 | `project::BookRef` | 一部一库；默认书使用项目根，其他书使用 `books/<书名>/` |
| 小说世界状态 | `novelcore::NovelGraph` + SQLite | 实体、关系、章节、场景、伏笔、状态等 |
| 叙事分镜 | `novelcore::ShotRow` / V1–V9 阶段产物 | 从小说场景到镜头结构 |
| 生成分镜 | `video::Shot` / `VideoProject` | 交给 ComfyUI 的可提交模型 |
| 生成任务 | `video::VideoTaskRunner` | 上传、编译、提交、运行、历史、落盘 |
| 流程图 | `flow::GraphHost` / `kit::FlowCanvas` | 模型和 Qt 画布分离 |
| 流水线 | `pipeline::Runner` | 阶段顺序、预算、账本、检查点 |

## 当前不做的事情

以下是当前代码边界或明确的产品约束，不应从旧计划推断为待办：

- 不恢复 UE 插件、UObject 或 `.uasset` 作为主数据模型；`Plugins/` 仅是外部参考目录。
- 不把 3D 场景截图、纹理节点图或视频内嵌播放器作为主链路；视频播放交给系统程序。
- 不把项目之外的 CWD 或硬编码 `%APPDATA%` 当作业务路径；路径由 `ProjectRef`/设置派生。
- 不绕过 ComfyUI `/object_info` 校验提交未知输入；“未校验”不能伪装成“校验通过”。
- 不在文档中维护阶段计划或进度；实现状态由源码、测试/自检和验证记录表达。

## 事实与目标

- “项目模板包含哪些目录”是源码事实，见 `ProjectTemplate.cpp`。
- “一镜可有多个生成任务”是 `VideoTypes.h` 的数据契约。
- “自动运行有预算和停止条件”是 `NovelRunLoop.h` 的接口事实；是否满足具体外部模型条件必须运行验证。
- 页面视觉目标是设计约定；实际可见效果以当前 Qt 截图和运行结果为准。
