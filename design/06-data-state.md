---
id: design.p06-data-state
kind: design-proposal
status: current
scope: ui-redesign
source_of_truth:
  - src/ui/kit/data/Table.h
  - src/ui/kit/data/Table.cpp
  - src/ui/pages/pipeline/GanttView.cpp
  - src/ui/pages/pipeline/LedgerView.cpp
  - src/ui/pages/pipeline/PipelineWorkspace.cpp
  - src/ui/pages/imageflow/BindingView.cpp
  - src/ui/pages/imageflow/BatchRenderView.cpp
  - src/ui/pages/videoflow/ChainView.cpp
  - src/ui/pages/videoflow/VideoTaskView.cpp
  - src/ui/verify/review/P09Review.cpp
  - src/ui/verify/checks/P10Checks.cpp
  - src/pipeline/StageMachine.cpp
  - docs/10-modules/ui-kit.md
last_verified: 2026-09-28
---

# 06 · 表格收敛 `DataTable` + 甘特重做成阶段状态矩阵

**工作量**：大（1 个新控件 + 6 个视图迁移 + 2 个探针同步改）
**风险**：中高——`P09Review` / `P10Checks` 依赖控件树结构，必须同步改并重新跑基线
**收益**：高。截图里最"丑"的一块（31 列"待办"文本表）在这里终结。

## 目标

1. 新增 `kit::data::StateMatrix`：阶段 × 单元的状态网格，替代甘特的文本表。
2. 6 处手搓 `QTableWidget` 全部收敛到 `DataTable`（虚拟化、冻结列、筛选、列宽持久化）。
3. 3 处直连 `QTabWidget` 换成 `kit::widgets::Tabs`，全应用 tab 视觉统一。
4. 状态色一律来自 01 方案的 6 态状态轴，并带形状冗余编码。

## 不做什么

- 不改阶段机语义（30 个阶段 T1–T17 / V0–V11 的定义不动，`src/pipeline/StageMachine.cpp:11-19`）。
- 不改账本/检查点的落盘格式。
- 不引入图表库（甘特仍是网格，不是时间轴曲线图）。

---

## 现状证据

| 问题 | 位置 |
|---|---|
| 甘特每格写"待办"文本，无状态色 | `src/ui/pages/pipeline/GanttView.cpp:36` |
| 表头只有阶段代码，没有阶段名 | `GanttView.cpp:30-32` |
| 叙事段（T）与视觉段（V）不分组 | 同上（`AllStages()` 直接顺序展开） |
| 走原生 `QTableWidget`，非 `DataTable` | `GanttView.cpp:19` |
| `ResizeToContents` 导致表格只占 1/3 宽 | `GanttView.cpp:21` |
| 阶段表 30 项 | `src/pipeline/StageMachine.cpp:11-19` |
| 6 处手搓表格 | `docs/10-modules/ui-kit.md:65` |
| 3 处直连 `QTabWidget` | `docs/10-modules/ui-kit.md:66`；`PipelineWorkspace.cpp:38-44` |
| **探针按控件类名定位，必须同步改** | `src/ui/verify/review/P09Review.cpp:41,44,47`（`findChild<QTabWidget*>`）、`src/ui/verify/checks/P10Checks.cpp:82`（`findChild<QTableWidget*>`）、`P10Checks.cpp:72`（`findChild<QTableView*>`） |
| 性能基线门槛 | `P10Checks.cpp:91`：`canvas.p95 < 17 && table_frame < 17 && tree_frame < 17` |

---

## 方案

### 6.1 新增 `kit::data::StateMatrix`

`src/ui/kit/data/StateMatrix.h/.cpp`（`QAbstractScrollArea` + 自绘，或 `QTableView` + 自定义 delegate）：

```cpp
namespace shine::data {

enum class CellState { None, Pending, Running, Ok, Warn, Fail };

struct MatrixColumn { QString group; QString code; QString name; };  // group = 叙事/视觉

class StateMatrix : public QWidget {
  public:
    void SetRows(const std::vector<QString>& rowLabels);           // 行 = 章
    void SetColumns(const std::vector<MatrixColumn>& columns);     // 列 = 阶段
    void SetCell(int row, int col, CellState s);
    void SetRowLabel(int row, const QString& text);

    void SetOnCellActivated(std::function<void(int row, int col)> cb);
};
}
```

**渲染规格**：

| 元素 | 规格 |
|---|---|
| 行高 | 28px |
| 列宽 | 32px（代码做竖排或仅 tooltip 显示） |
| 分组表头 | 叙事 T1–T17 / 视觉 V0–V11 两段，段间 8px 间隙 + 段标题 |
| 单元格 | 圆角 3px 的 12px 圆点/菱形/三角/方块，**形状 + 颜色双编码**（见下） |
| `None` | 直径 4px 空心点，`text.muted` 30% |
| `Pending` | 空心菱形，`status.pending` |
| `Running` | 半环 + 1.6s 呼吸（`motion::ReduceMotion()` 时静止为实心环） |
| `Ok` | 实心圆带勾，`status.ok` |
| `Warn` | 实心三角带 !，`status.warn` |
| `Fail` | 实心方块带 ×，`status.danger` |
| 行标签列 | 冻结在左侧，宽 140px，`DataTable::SetFixedColumns` 同款体验 |
| hover | 整行 `fill.hover`；单元格 tooltip 显示 `阶段名 · 状态 · 耗时 · 产物路径` |
| 点击 | 触发 `OnCellActivated` → 打开阶段详情 Drawer |
| 行选择 | 单击行头即选中整行；选中行背景 `fill.selected` |

> 文字只在两种情况出现：**悬停 tooltip** 与**选中行**。截图里"31 列全是字"的密度问题由此消失。

### 6.2 `GanttView` 重写

```cpp
// 目标形态
void GanttView::SetChapters(int n);
void GanttView::SetStageState(int chapter, pipeline::StageId stage, const QString& state);
QString GanttView::Probe() const;   // 保留，格式可加字段但 P09Review 依赖"可解析"
```

- 状态字符串 → `CellState` 映射：`"待办"→None`、`"排队"→Pending`、`"运行"→Running`、`"完成"→Ok`、`"降级"→Warn`、`"停止"/"失败"→Fail`；
- 保留 `SetChapters` / `SetStageState` 的**原签名**（`PipelineWorkspace.cpp:61-64`、`:74-75` 在调用），减少改动面；
- 增加 `SetColumns()` 从 `pipeline::AllStages()` 一次性生成两段分组表头（阶段中文名来自 `StageDefinition::name`，`StageMachine.cpp:9-28`）；
- 顶部工具条：筛选（全部 / 仅未完成 / 仅异常）+ 图例（6 态说明）。

### 6.3 6 处表格收敛到 `DataTable`

| 视图 | 表 id（持久化键） | 特殊要求 |
|---|---|---|
| `pipeline/GanttView` | — | 改用 StateMatrix，见 6.2 |
| `pipeline/LedgerView` | `ledger` | 冻结前 2 列（阶段 / 输入哈希） |
| `imageflow/BindingView` | `binding` | 行内操作列 `SetActionColumn` |
| `imageflow/BatchRenderView` | `batch-render` | 状态列用 6 态 |
| `videoflow/ChainView` | `chain` | 空态改 `EmptyState` |
| `videoflow/VideoTaskView` | `video-task` | 冻结首列；操作列 |

`DataTable` 已具备：虚拟化、排序、任意列筛选、冻结列、行内操作、列宽按 id 持久化（`src/ui/kit/data/Table.h:28-47`）。迁移时**列 id 用稳定键**（阶段代码 / 字段名），绝不用索引（`Table.h:23-25` 的注释就是这条规则）。

### 6.4 3 处 `QTabWidget` → `kit::widgets::Tabs`

`pages/{Pipeline,VideoFlow,ImageFlow}Workspace.cpp`。注意 kit 的 `Tabs` 只画指示条，内容切换需自持 `QStackedWidget`：

```cpp
tabs_ = new widgets::Tabs({"甘特", "账本", "停止报告"}, this);
stack_ = new QStackedWidget(this);
tabs_->SetOnChanged([this](int i) { stack_->setCurrentIndex(i); });
```

### 6.5 探针同步改（必做，否则取证链断）

| 文件 | 现状 | 改法 |
|---|---|---|
| `src/ui/verify/review/P09Review.cpp:41,44,47` | `workspace->findChild<QTabWidget*>()->setCurrentIndex(n)` | 给 `PipelineWorkspace` 加 `SetTab(int)`，探针改调它（别再用 findChild） |
| `src/ui/verify/checks/P10Checks.cpp:79-88` | `view.findChild<QTableWidget*>()->scrollToBottom()` | StateMatrix 不再是 `QTableWidget`；改为调用 `GanttView::ScrollToBottom()` 或 `StateMatrix::ScrollToEnd()` |
| `src/ui/verify/checks/P10Checks.cpp:67-77` | `ShotTableView::findChild<QTableView*>()` | `DataTable` 派生自 `QTableView`，**仍然命中**，无需改 |
| `src/ui/verify/checks/P10Checks.cpp:91` | `tree_frame < 17` | 重测基线；若 StateMatrix 自绘更慢，改为滚动时只画可视行（必须满足） |

---

## 改动清单

```
新增  src/ui/kit/data/StateMatrix.h / StateMatrix.cpp
修改  src/ui/pages/pipeline/GanttView.h/.cpp        重写为 StateMatrix 容器
修改  src/ui/pages/pipeline/{LedgerView,PipelineWorkspace}.{h,cpp}
修改  src/ui/pages/imageflow/{BindingView,BatchRenderView}.{h,cpp}
修改  src/ui/pages/videoflow/{ChainView,VideoTaskView}.{h,cpp}
修改  src/ui/pages/{videoflow,imageflow}Workspace.{h,cpp}   QTabWidget → Tabs + StackedWidget
修改  src/ui/verify/review/P09Review.cpp           findChild → 显式 SetTab
修改  src/ui/verify/checks/P10Checks.cpp           findChild → 显式滚动接口
修改  src/ui/kit/theme/QssBuilder.cpp              表格/矩阵选中态样式
修改  CMakeLists.txt
```

## 验收

```powershell
cmake --build build -j 8 --target ShineTVStudio

# 取证包（必跑，确认没改坏）
$env:SHINE_P09_REVIEW = "$PWD\build\p09"; & .\build\ShineTVStudio.exe
$env:SHINE_P10_REVIEW = "$PWD\build\p10"; & .\build\ShineTVStudio.exe

# 性能基线（迁移后必须重测）
$env:SHINE_P10_S1 = "$PWD\build\p10s1.txt"; & .\build\ShineTVStudio.exe

# 主窗口 + 局部放大
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\capture_window.ps1 -Out build\_gantt.png
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\crop_zoom.ps1 -In build\_gantt.png -X 240 -Y 90 -W 900 -H 320 -Out build\_gantt_crop.png
```

判据：

1. 甘特区**不再出现任何"待办"文字**；未开始的阶段是 4px 空心点。
2. 表头分两段：「叙事 T1–T17」「视觉 V0–V11」，悬停显示阶段中文名。
3. 6 态在色盲模拟（转灰度）下仍可区分（形状不同）。
4. 中央标签栏与底栏标签**外观一致**（同一指示条样式）。
5. 5 处迁移后的表格：冻结列可用、任意列筛选可用、列宽刷新后保持。
6. `SHINE_P09_REVIEW` 退出码 0，`gantt-full.png` / `ledger-full.png` 均生成。
7. `SHINE_P10_S1` 中 `tree_frame_ms` 仍 < 17（千章矩阵滚动）。

## 风险

| 风险 | 缓解 |
|---|---|
| StateMatrix 自绘比 `QTableWidget` 慢 | 只画可视行 + 行高固定 + 单元格预生成 pixmap 缓存；P10_S1 卡口 |
| 6 处迁移漏掉 | `grep -n "QTableWidget" src/ui/pages` 迁移后应零命中（`kit` 内部除外） |
| 探针失配导致评审包为空 | 6.5 是必做项；改完立刻跑 `SHINE_P09_REVIEW` 看 manifest |
| 列宽持久化键冲突 | 表 id 与列 id 都用稳定字符串；迁移期保留旧 id 以读到旧 `layout.dat` |
