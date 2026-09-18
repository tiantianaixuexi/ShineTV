---
name: shinetv-structure
description: ShineTV Studio 源码目录、模块职责与分层边界。当用户问「代码在哪」「这个功能改哪个文件」「src 下各目录干什么」「如何加新模块」时使用。
---

# ShineTV 源码结构

独立 C++26 ComfyUI 工作站（非 UE 插件）。工作区根：`E:\c++\ShineTV`。

## 分层（从下到上）

```
third/          第三方（不改业务）
  ↓
core/           基础设施：日志 / 异步 / 设置
  ↓
comfy/          ComfyUI 协议与会话（P1）
graph/          VisualNodeSystem 宿主（P2；节点体绘制在 graph 内部）
theme/          主题 Token
gallery/ novel/ media/ video/   业务模型（无 ImGui 面板）
  ↓
app/            全部 ImGui 面板/视图（shell + panels + views + 功能域平铺）
  ↓
main.cpp        入口：mimalloc → 字体 → DX11 → 主循环
```

业务模块（gallery/novel/media/video）**只保留模型与 IO**；视图一律在 `src/app/`。

## 目录职责

| 路径 | 职责 | 关键文件 |
|------|------|----------|
| `src/main.cpp` | `wWinMain`、DX11、每帧 `app::DrawFrame` | `mi_process_init()` 最早 |
| `src/app/` | 宿主 + **全部 UI** | `App.cpp`、`DockLayout.*`、`Fonts.*` |
| `src/app/shell/` | 顶栏/活动栏/侧栏切换/停靠/状态栏 | `Draw*.cpp` |
| `src/app/panels/` | 侧栏面板（assets/nodes/workflows/comfy） | `Draw*Panel.cpp` |
| `src/app/views/` | 中央/右栏/底栏（graph/inspector/preview/bottom） | `Draw*Panel.cpp` |
| `src/app/dialogs/` | 浮窗（设置/模板/节点） | `Draw*Window.cpp` |
| `src/app/ui/` | 共享控件 | `Widgets.*` |
| `src/app/gallery/` | 图库视图 + 文件夹选择（`shine::app::gallery`） | `GalleryView.*`、`FolderPicker.*` |
| `src/app/novel/` | 小说视图（`shine::app::novel`） | `NovelView.*` |
| `src/app/shots/` | 分镜表（`shine::app::shots`） | `ShotTableView.*` |
| `src/app/output/` | 底栏输出页（`shine::app::output`） | `OutputView.*` |
| `src/theme/` | 配色 Token + 预设 | `Theme.h` |
| `src/core/` | 基础设施 | `Log.*`、`Async.*`、`Settings.*` |
| `src/net/` | libhv 就绪 + 出站 HTTP（comfy/openai 共用） | `LibhvReady.*`、`HttpClient.*` |
| `src/comfy/` | ComfyUI 客户端 | skill `shinetv-comfy` |
| `src/graph/` | VNS 画布 + 节点工厂 + 存盘 | skill `shinetv-graph` |
| `src/gallery/` | 图库业务（扫描/模型/解码，**无 UI**） | `Gallery.*`、`ImageScanner.*` |
| `src/media/` | 输出库业务 | `MediaLibrary.*` |
| `src/video/` | 分镜工程业务 | `VideoProject.*`、`VideoTaskRunner.*` |
| `src/novel/` | 小说工程 + 知识图谱（**命名空间 `shine::novelcore`**） | `NovelProjects.*`、`NovelDb.*`、`NovelGraph.*`、`NovelTypes.h` |

## `src/app` 内部分工

- 活动栏 `DrawActivityBar` + `SideView`
- 面板：Assets / Nodes / Workflows / ComfySide / Graph / Inspector / Preview / Queue / Log / Gallery / Novel / Shots / Output
- **状态栏** `DrawStatusBar`（连接/队列/图/缩放/选中/主题/图片）
- `DrawFrame`：Tick → DrainUiQueue → 快捷键 → Host → 活动栏 + Dock + 状态栏
- `DockLayout`：侧栏|图|分镜|图库|小说|右栏|底栏；活动栏与状态栏在 Dock 外
- 中文字体：`Fonts.cpp`

## 新模块约定

1. 新建业务模块 `src/<module>/`（**不要放 ImGui 面板**）
2. 对应视图放 `src/app/<feature>/`，命名空间 `shine::app::<feature>`
3. 根 `CMakeLists.txt` `add_executable` 加源文件
4. 异步 `shine::async`，日志 `shine::log`，业务 JSON 用 yyjson
5. UI 不直接依赖 third 完整类型（参见 graph 的 incomplete type 封装）
6. 更新 `PLAN.md` 与本 skill 目录表

## 只读参考

`Plugins/Shine`、`Plugins/ShineMCP`（UE 原实现，不直接编译）。

## 相关 skill

`shinetv-build` · `shinetv-comfy` · `shinetv-graph` · `shinetv-ui-layout` · `shinetv-thirdparty`
