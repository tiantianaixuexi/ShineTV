# AGENTS.md（入口指针）

> 本仓库的规则与计划文档**按功能分类**放进 `Doc/`（规则与参考）与 `Plan/`（计划与施工图），根目录不再堆放。开工前按需读：

## 规则（`Doc/`）

| 文档 | 作用 |
|------|------|
| `Doc/AGENTS.md` | 工具链 / 字体 / 布局 / 技术栈 / 语言特性 —— **硬性规则总入口（含完整文档地图）** |
| `Doc/RULES-AI.md` | AI 执行规则（一次一步、验收纪律、线程纪律）+ 项目 Skill 索引 |
| `Doc/RULES-LANG.md` | C++26 语言特性与代码风格、能力探测结论、API 边界例外、反射与 `util` 用法 |
| `Doc/RULES-COMFY.md` | ComfyUI 错误捕获与连接健康（WS 事件、"忙碌 ≠ 卡死"、日志模板） |
| `Doc/STYLE-UI.md` | 默认视觉方向（色板 token，禁止硬编码颜色） |
| `Doc/BASELINE.md` | 现状盘点 / 技术栈与目录 / 已完成基线事实 / **明确不做清单** |
| `Doc/BUILD.md` | 构建方式（工具链、configure / build 命令） |

## 计划（`Plan/`）

| 文档 | 作用 |
|------|------|
| `Plan/PLAN.md` | **总纲 + 文档地图 + 大类/小类总表 + 依赖顺序**（领任务先看） |
| `Plan/任务/<大类>.md` | **施工图**：每个小类的 S 做什么 / 判据（`P3-节点与图` … `G-图片库`，7 个） |
| `Plan/PROGRESS.md` | **唯一勾选入口**：小任务 `[ ]` / `[x]` 与状态 |
| `Plan/证据.md` | 实测证据（每个小类汇总 + 每个 S 的 ✅ 逐条记录） |
| `Plan/坑与手法.md` | 踩过的坑、事故、验收手法（截图 / 离线 / 无头） |
| `Plan/归档-已完成.md` | 已完成的旧线（P0 / P1 / P2 / P2.9 / R） |
| `Plan/HANDOFF.md` | 新会话交接（开场白 + 现状 + 基础设施 + 本机环境） |

## 开工标准流程

1. 读 `Plan/PLAN.md`（文档地图 + 大类/小类总表）与 `Doc/AGENTS.md`、`Doc/RULES-AI.md`；
2. 打开 `Plan/任务/<大类>.md`，**只挑一个 S**（写码照 `Doc/RULES-LANG.md`，动 ComfyUI 照 `Doc/RULES-COMFY.md`）；
3. 做完 `configure → build → 运行`，逐条对 S 里的判据；
4. 回 `Plan/PROGRESS.md` 勾选 + 在 `Plan/证据.md` 写证据，再挑下一个 S。

> 历史文档里出现的 `PLAN.md §x`、`RULES-LANG.md §x`、`Plan/Pn/Px.y-*.md`、`Plan/G/G-S*` 等旧写法，
> 一律对应上表的新路径（旧路径已不存在）。
