---
id: design.p03-scaffold
kind: design-proposal
status: current
scope: ui-redesign
source_of_truth:
  - src/ui/kit/controls/WidgetCommon.h
  - src/ui/kit/controls/Feedback.h
  - src/ui/kit/theme/QssBuilder.cpp
  - src/ui/pages/pipeline/PipelineWorkspace.cpp
  - src/ui/pages/novel/NovelWorkspace.cpp
  - src/ui/pages/assets/AssetWorkspace.cpp
  - src/ui/pages/storyboard/StoryboardWorkspace.cpp
  - src/ui/pages/imageflow/ImageFlowWorkspace.cpp
  - src/ui/pages/videoflow/VideoFlowWorkspace.cpp
  - src/ui/pages/project/ProjectHubView.cpp
  - docs/10-modules/ui-kit.md
last_verified: 2026-09-28
---

# 03 · 页面骨架 `PageScaffold` + 缺失控件补齐

**工作量**：大（新增 3 个控件 + 迁移 10 个页面的 `BuildUi`）
**风险**：中——页面多、迁移面广；但每页只改顶部几行，行为逻辑不动
**收益**：高。所有页面获得一致的标题层级、内边距、工具条与空态位。

## 目标

1. 新增 `kit::PageScaffold`：统一「图标 + 页标题(H1) + 状态 + 右侧工具条 + 内容区」结构。
2. 统一页面内边距（消除 `0,0,0,0` 与 `4,4,4,4` 并存）。
3. 补齐 kit 缺失控件：`StatusDot`、`Confirm`、`SectionHeader`（可折叠分区，替代 `RightPanel.cpp:16-37` 的私有实现）。
4. 迁移全部 6 个工作区 + 项目中心页。

## 不做什么

- 不动业务逻辑、数据流、线程模型。
- 不改外壳（顶栏/活动栏/侧栏/右栏/底栏）结构——那是 04/05。

---

## 现状证据

| 问题 | 位置 |
|---|---|
| 页面内边距两套：`(0,0,0,0)` vs `kSteps[2]` | `PipelineWorkspace.cpp:21` vs `GalleryWorkspace.cpp:67-68` |
| 标题三行块重复 20 余处（注释自述） | `src/ui/kit/controls/WidgetCommon.h:41-44` |
| `SectionTitle` 把所有标题压成同一样式 | `WidgetCommon.h:43-44` |
| 可折叠分区是 `RightPanel.cpp` 里的私有类，别的页面用不了 | `RightPanel.cpp:16-37` |
| 状态点 3 套自绘 | `docs/10-modules/ui-kit.md:67` |
| 确认框 3 种写法 | `docs/10-modules/ui-kit.md:69` |
| 空态手搓 `QLabel("暂无…")` 至少 3 处 | `docs/10-modules/ui-kit.md:68`（正确样板：`ContinuityView.cpp`） |

---

## 方案

### 3.1 `PageScaffold`

新增 `src/ui/kit/controls/Scaffold.h` + `Scaffold.cpp`：

```cpp
namespace shine::widgets {

// 页面骨架：Header(标题 + 状态 + 工具条) / Body(内容) / 可选 Footer(状态条)
class PageScaffold : public QFrame {
  public:
    explicit PageScaffold(const QString& title, QWidget* parent = nullptr);

    void SetIcon(icons::Glyph g);                 // 标题左侧图标（可选）
    void SetSubtitle(const QString& s);           // 标题下方副标题（可选）
    void SetStatus(const QString& s, const char* tone = "");  // 标题右侧状态 Tag
    void SetActions(const std::vector<QWidget*>& items);     // 右侧工具条（自动溢出不折叠）
    [[nodiscard]] QVBoxLayout* BodyLayout();      // 统一 margins=space[5]、spacing=space[3]
    [[nodiscard]] QWidget* HeaderWidget() const;  // 需要自定义页头时用
};
}
```

固定规格（全部取 `theme::space`）：

| 部位 | 规格 |
|---|---|
| Header 高 | 56px（`space[5]*2` + 20px 文字） |
| Header 底边 | 1px `line.subtle` |
| Header 内边距 | 左右 `space[5]`=16，上下 `space[3]`=12 |
| 标题 | `type::Role::H1`（20px/600） |
| 副标题 | `type::Role::Caption`（12px，`text.secondary`） |
| 状态 Tag | `Tag(text, tone)`，`tone` ∈ `accent/info/ok/warn/danger` |
| Body 内边距 | `space[5]`=16，四边 |
| Body 区块间距 | `space[3]`=12 |
| Body 内最小控件高 | 28px（sm）/ 32px（md） |

### 3.2 `StatusDot`

新增 `src/ui/kit/controls/StatusDot.h`（或并入 `Scaffold.cpp` 旁的 `Indicators.h`）：

```cpp
class StatusDot : public QWidget {
  public:
    enum class Tone { Idle, Pending, Busy, Ok, Warn, Danger, None };
    void SetTone(Tone t);
    void SetLabel(const QString& l);   // 双编码：色 + 字形/文字（可达性要求）
    [[nodiscard]] Tone GetTone() const;
};
```

要点：

- 6 个 `Tone` 直接映射 01 方案的新状态色（`status.idle/pending/busy/ok/warn/danger`）；
- **形状冗余编码**：`Idle` 空心圆、`Pending` 空心菱形、`Busy` 半环 + 呼吸动画、`Ok` 实心圆带勾、`Warn` 实心三角、`Danger` 实心方块——避免"只有颜色"；
- 尺寸 `md=10px` / `sm=8px`；动画遵循 `shine::motion::ReduceMotion()`。

替换：`StatusBar.cpp:16-44` 的私有 `Dot`、`NovelWorkspace.cpp`、`ProjectHubView.cpp` 的自绘。

### 3.3 `Confirm`

新增 `kit::widgets::Confirm::Ask(parent, title, body, okText, danger)` → `std::optional<bool>`（右上角，或 `widgets::Dialog` 的危险变体）。

替换三处：`RefLibraryView.cpp`（自绘）、`InitChainView.cpp`（原生 `QMessageBox`）、`ProjectWizardDialog.cpp`（自绘）。原生 `QMessageBox` 在深色主题下按钮不受 QSS 完整控制，必须换掉。

### 3.4 `SectionHeader`

把 `RightPanel.cpp:16-37` 的私有 `Section` 提升为 `kit::widgets::SectionHeader`（折叠/展开、箭头图标、`H3` 标题），供右栏与各页面共用。

### 3.5 迁移清单

每个页面的 `BuildUi` 开头从「QVBoxLayout + SectionTitle + 工具条」改为「PageScaffold」：

```
src/ui/pages/pipeline/PipelineWorkspace.cpp:19-48
src/ui/pages/novel/NovelWorkspace.cpp          （章头 headRow 部分）
src/ui/pages/assets/AssetWorkspace.cpp
src/ui/pages/storyboard/StoryboardWorkspace.cpp
src/ui/pages/imageflow/ImageFlowWorkspace.cpp
src/ui/pages/videoflow/VideoFlowWorkspace.cpp
src/ui/pages/project/ProjectHubView.cpp       （项目中心页，Display 标题）
src/ui/verify/gallery/GalleryWorkspace.cpp:65-152   （作为样板先改，其余照抄）
```

同时把 3 处直连 `QTabWidget` 换成 `kit::widgets::Tabs`（见 `06-data-state.md`，或在本方案顺带做，因为 Scaffold 与 Tabs 常常同时出现）。

### 3.6 页面标题文案同步

现有页面标题是营销口吻（`PipelineWorkspace.cpp:23`「全流程总控台 · 一句话到成片」）。改为「主标题 + 副标题」两行：

| 页面 | 主标题 | 副标题 |
|---|---|---|
| 总控台 | 全流程总控 | 章节 → 分镜 → 成片，一条流水线 |
| 小说 | 小说工作区 | 书 / 卷 / 章 · 初始化 · 生成 · 评审 |
| 视觉资产 | 视觉资产 | 角色 / 场景 / 道具的一致性基线 |
| 分镜 | 叙事分镜 | 从场景到镜头的结构与连续性 |
| 出图 | 出图工作流 | ComfyUI 提交、批次与结果回收 |
| 出片 | 出片工作流 | 视频任务、成片与成本 |

---

## 改动清单

```
新增  src/ui/kit/controls/Scaffold.h / Scaffold.cpp
新增  src/ui/kit/controls/StatusDot.h / StatusDot.cpp
新增  src/ui/kit/controls/Confirm.h（可并入 Surfaces.h）
新增  src/ui/kit/controls/SectionHeader.h（可并入 Scaffold.h）
修改  src/ui/kit/theme/QssBuilder.cpp        页头/分区样式
修改  src/ui/kit/controls/Feedback.h/.cpp    EmptyState 改用 StatusDot/图标
修改  上列 8 个页面的 BuildUi
修改  CMakeLists.txt                         登记新增源文件
```

## 验收

```powershell
pwsh -File tools/check-layers.ps1
cmake --build build -j 8 --target ShineTVStudio
$env:SHINE_GALLERY_SHOTS = "$PWD\build\gallery"; & .\build\ShineTVStudio.exe --widget-gallery
```

判据：

1. 画廊新增 `gallery-scaffold.png`：页头、工具条、内容区三段结构，规格与上表一致。
2. 逐页截图（总控 / 小说 / 资产 / 分镜 / 出图 / 出片 / 项目中心）：**7 张截图的页头高度、内边距、标题字号完全一致**。
3. 窗口宽度从 1280 拉到 1900 时，内容左对齐不居中、不拉伸；缩到 1100 时无横向溢出（工具条溢出走省略）。
4. 右侧栏分区改用 `SectionHeader` 后与页面内分区**视觉一致**。
5. `grep -n "setContentsMargins(0, 0, 0, 0)" src/ui/pages` 在 6 个工作区的顶层布局中**不再出现**（子控件内部允许）。
6. 原有 P03–P10 评审截图包仍能跑通（`SHINE_P0x_REVIEW`），说明没有破坏既有取证路径。

## 风险

| 风险 | 缓解 |
|---|---|
| 页面多，迁移易漏 | 一次只改 2 个页面并各自截图；改完用 `P0xReview` 跑一遍回归 |
| 统一 16px 内边距后窄页面内容变少 | 骨架允许页面按需关掉内边距（`SetFlush(true)`），但需在代码里显式声明 |
| 新控件放入 `kit` 后被业务页依赖倒挂 | `Scaffold`/`StatusDot` 不引用任何 `project/novel/visual` 类型（`AGENTS.md` 要求 `src/ui/kit` 不依赖业务模块） |
