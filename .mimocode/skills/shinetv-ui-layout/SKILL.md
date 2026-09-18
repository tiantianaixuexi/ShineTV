---
name: shinetv-ui-layout
description: ShineTV ImGui 布局（活动栏/侧栏/图/右栏/底栏/状态栏）与高性能列表、快捷键约定。当用户改 Dock、面板、状态栏、主题应用到 UI，或日志/队列卡顿时使用。
---

# UI 布局与性能

实现：`src/app/App.cpp` + `src/app/DockLayout.cpp`。

## 七区

```
① 菜单栏（Host MenuBar）
② 活动栏 46px（Dock 外）
③ 侧栏「侧栏」（SideView 切换）
④ 中央「图」（VNS，见 shinetv-graph）
⑤ 右栏「属性」「预览」
⑥ 底栏「队列」（Tab：队列/日志/输出）
⑦ 状态栏 24px（Dock 外，整行）
```

### 活动栏 `SideView`

| 图标 | 枚举 | 侧栏 |
|------|------|------|
| 资 | `Assets` | 资源树 |
| 节 | `Nodes` | 节点搜索/点击/拖放 |
| 流 | `Workflows` | 存盘/加载 graph.json |
| C | `Comfy` | 连接与 REST |
| ⚙ | — | 设置 |

- 再点当前项收起侧栏；`Ctrl+B` 同
- 选中：左侧 2.5px Accent 条

### 状态栏 `DrawStatusBar`

左：连接灯 · 队列 · 图 n/m · 缩放 · 选中名  
右：主题名 · `ShineTV 0.2.0 · P2`  
背景 = `menuBar` Token。

### Dock 分割

```
left 18% 侧栏 | right 22% 属性+预览 | bottom 26% 队列 | center 图
```

`BuildDefaultLayout(dockspaceId, dockSize)` — 尺寸是**活动栏右侧、状态栏上方**。

## Host 骨架

```cpp
DrawMenuBar();
workH = content.y - 24;          // 状态栏
DrawActivityBar(workH);
SameLine(0,0);
DrawDockedPanels({content.x-46, workH});
DrawStatusBar(content.x, 24);
```

每帧：`Session::Tick` + `async::DrainUiQueue`；`Ctrl+B` / `Ctrl+S`（存图）。

## 性能约定（硬性）

1. 长列表必须 `ImGuiListClipper`
2. 日志用 `log::Version()` 缓存快照
3. 画布：屏外 AABB 剔除（VNS 自带渲染；占位画布时代规则仍适用）
4. 正式日志用 fmt `{}`，禁止 printf

## 主题

`theme::Current()` 是 `float[4]` 数组，用下标构造 `ImVec4`。

## 中文字体

`Fonts.cpp` / msyh.ttc；UI 默认中文。

## 新增侧栏视图

`SideView` + `kActivities[]` + `DrawXxxPanel` + `DrawSideBar` switch + 更新本文件与 PLAN 布局图。

## 相关 skill

`shinetv-graph` · `shinetv-structure` · `shinetv-comfy` · `shinetv-build`
