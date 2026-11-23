---
name: knowledge-base-index
description: ShineTV Studio 工程知识库唯一入口。查构建、目录职责、门禁与判据体系、文档分工，全从本页进。
---

# ShineTV Studio 工程知识库 · 总入口

**一句话定位**：Dear ImGui（OpenGL 3.3 / WGL）前端 1:1 复刻 `webui/`（React + CSS）设计稿；
业务层 `shine_core` 零 Qt；构建 `Build.bat` + MinGW（MSYS2 MinGW64）。

> 本页是**唯一入口**。本库是**薄层**：只放速查与指针；设计稿规范 / 进度 / 坑的长文在 `refactor/`，
> 规则常驻在 [`AGENTS.md`](../AGENTS.md)。**只写指针，不复制正文。**

**可信度标记（全库统一）**：✅〔A〕= 出自 [`AGENTS.md`](../AGENTS.md)（2026-09-30 核验）｜✅〔源〕= 本次读源码 / 目录 / 工具实测｜△ INFERRED（有依据未验证）｜❓ UNKNOWN（写明缺什么）。

## 库内分区

| 分区 | 内容 | 什么时候进 |
|---|---|---|
| [00_Project/RepositoryMap.md](00_Project/RepositoryMap.md) | 仓库目录职责表（含「不入库」标注） | 不知道代码 / 脚本在哪个目录时 |
| [00_Project/BuildAndVerify.md](00_Project/BuildAndVerify.md) | 构建命令、验证顺序、1:1 回归、像素取证 | 每次编译 / 验收前 |
| [00_Project/GatesAndJudgments.md](00_Project/GatesAndJudgments.md) | 门禁与扫描器、自动化判据族、假信号表 | 判据红了 / 要加门禁时 |
| [08_AI_Memory/README.md](08_AI_Memory/README.md) | 记忆边界：事件记忆 vs 工程事实 | 要写 / 查会话记忆时 |

**长文层（`refactor/`，不在本库，本库只指不复制）**：

| 文件 | 内容 | 什么时候读 |
|---|---|---|
| [refactor/README.md](../refactor/README.md) | **文件地图**（要改什么改哪个文件）+ 固定动作 | 每次开工 |
| [refactor/design-spec.md](../refactor/design-spec.md) | 视觉规范：几何字号、31×5 色表、逐页布局、验收方法 | 动任何视觉之前 |
| [refactor/architecture.md](../refactor/architecture.md) | 分层、线程契约、纹理路径、门禁规则 | 动宿主 / 线程 / 纹理时 |
| [refactor/进度.md](../refactor/进度.md) | 现状、当前计划、判据清单、『视觉 1:1 符合性审计』 | 改之前先查，避免重复踩 |
| [refactor/轮次记录.md](../refactor/轮次记录.md) | 历史轮次记录（各轮读数与结论，归档） | 查旧结论 / 历史读数时 |
| [refactor/记忆.md](../refactor/记忆.md) | 坑与纪律、『跨轮硬事实』 | 查「为什么有这条门禁」 |
| [refactor/dynamic-fields.md](../refactor/dynamic-fields.md) | 动态字段：AI 按小说风格扩展字段的机制、KV 可行性、一致性与扩展性 | 改字段机制 / 给新风格加维度时 |
| [refactor/animations.md](../refactor/animations.md) | 动画体系：ImAnim 接线、通道约定、逐控件动画清单、取证确定性、imgui/ImAnim 升级记录 | 改动效 / 升级 imgui 时 |
| [refactor/dpi-layout.md](../refactor/dpi-layout.md) | UI 体系：DPI 缩放契约、布局管理器（Stack/Row/Flow + 棘轮）、颜色对比度门禁 | 改缩放 / 布局 / 颜色时 |
| [refactor/phases.md](../refactor/phases.md) | P0–P7 历史实施步骤 | 查某个 P 编号指什么 |

## 30 秒模型

1. **双端**：`webui/` 是视觉唯一基准（**不入库**，`.gitignore` 末行）；`src/ui/imgui/` 是 1:1 复刻的真界面；`shine_core`（`src/` 其余 15 模块）零 Qt、零 ImGui、**一行不改**。
2. **构建**：`Build.bat`（Debug 默认）/ `Build.bat --release` → `build\debug|release\ShineTVStudio.exe`；两配置各用独立 build 目录。
3. **验证顺序**：`check-layers` → `check-colors` → `check-theme` → `Build.bat --release` → 实际启动肉眼比对 webui 同页面。
4. **判据**：`out\_run_review.ps1` 产物 `shots-manifest.txt` 结尾 `overall=PASS|FAIL` 必须与退出码一致（PASS↔0 / FAIL↔1）；「换个目录名结果就翻」是判据前提坏了的指纹。

## 什么时候读哪页

| 任务 | 先读 |
|---|---|
| 开工 / 要改什么改哪个文件 | [`AGENTS.md`](../AGENTS.md) + [`refactor/README.md`](../refactor/README.md)『3. 文件地图』 |
| 改视觉 / 核对色值字号 | [`refactor/design-spec.md`](../refactor/design-spec.md) + [`refactor/进度.md`](../refactor/进度.md)『视觉 1:1 符合性审计』 |
| 编译 / 跑回归 / 取证 | [00_Project/BuildAndVerify.md](00_Project/BuildAndVerify.md) |
| 判据红了 / 加门禁扫描器 | [00_Project/GatesAndJudgments.md](00_Project/GatesAndJudgments.md) |
| 目录在哪 / 入不入库 | [00_Project/RepositoryMap.md](00_Project/RepositoryMap.md) |
| 动宿主 / 线程 / 纹理 | [`refactor/architecture.md`](../refactor/architecture.md)『4. 纹理与图片路径』『5. 线程与取证的契约』 |
| 查历史踩坑 / 为什么有这条门禁 | [`refactor/记忆.md`](../refactor/记忆.md)『跨轮硬事实』+ [`refactor/进度.md`](../refactor/进度.md)『判据清单』 |
| 写 / 查会话记忆 | [08_AI_Memory/README.md](08_AI_Memory/README.md) |

## 本库维护

1. 新增速查页要登记进本 INDEX（「库内分区」表）。
2. 与 `refactor/` 冲突时**以源码为准**并改文档，不改记忆。
3. 不建第二套文件地图：[`refactor/README.md`](../refactor/README.md) 仍是文件地图，本库只做职责级目录表。
