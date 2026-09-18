---
feature: ui-to-app
status: delivered
updated: 2026-02-14
branch: (no-git — 本仓库当前无 .git，直接在主树施工)
commits: n/a
---

# UI 归置到 app

## Report

**What was built** — 将散落在业务模块中的 ImGui 视图全部迁入 `src/app/`，按功能平铺为 `gallery/`、`novel/`、`shots/`、`output/`，命名空间改为 `shine::app::*`。同时修掉 `gallery` 业务层对 `FolderPicker` 的反向依赖：弹框与 `RequestScan` 编排落在 `app::gallery::PickAndScanLocalFolder`，业务层只保留 `RequestScan`。`graph` / `theme` 的绘制与 token 保持原位。

**Verification** — `cmake --build build -j 8` 通过（链接出 `ShineTVStudio.exe`）；二次增量编译通过；`rg` 无 `gallery/ui/|novel/ui/|media/ui/|video/ui/` 旧路径；业务模块（gallery/novel/media/video）除 Gallery.cpp 一句注释外无 `ImGui::` 调用；`SHINE_EXIT_AFTER_SEC=2` 启动退出码 0。

**Journey log** — 命名空间从 `shine::gallery::ui` 等迁到 `shine::app::*` 后，在 `namespace shine::app` 内写 `gallery::` 会先命中 `shine::app::gallery`，业务 API 必须写成 `::shine::gallery::`。大文件（ShotTableView / OutputView）用 `using namespace ::shine::video/media` 收口，避免逐符号 using。`ShotTableView.h` 的 `EditorState::project` 需要 `::shine::video::VideoProject` 限定。本仓库无 `.git`，未走 worktree。

## [S1] Problem

`src/` 的 UI **不是全部放在 `app/`**，而是混搭：

| 位置 | 内容 | 性质 |
|------|------|------|
| `src/app/**` | shell / panels / views / dialogs / 共享控件 | 正确：应用 UI 层 |
| `src/gallery/ui/` | GalleryView、FolderPicker | 错位：功能视图散落在业务模块 |
| `src/novel/ui/` | NovelView | 同上 |
| `src/media/ui/` | OutputView | 同上 |
| `src/video/ui/` | ShotTableView（含 EditorState / Tick） | 同上 |
| `src/graph/ComfyNode.cpp`、`GraphHost::DrawCanvas` | 节点体控件 / 画布 | **有意保留**：VNS 内部绘制，不属于面板层 |
| `src/theme/` | 色板 token + 应用 ImGuiStyle | **有意保留**：主题基础设施 |

另有分层违规：`gallery/Gallery.cpp`（业务）直接 `#include "gallery/ui/FolderPicker.h"` 并调用 `ui::PickImageFolderForGallery()`，业务层依赖 UI。

调用点（迁移时必须改 include / 限定名）：

- `app/shell/DrawDockedPanels.cpp` — 分镜 / 图库 / 小说 / 查看器
- `app/shell/DrawSideBar.cpp` — 分镜 / 图库 / 小说侧栏
- `app/App.cpp` — OutputView、ShotTableView Tick、PickAndScanLocalFolder
- `app/AppIncludes.h` — 聚合 include
- `app/shell/DrawMenuBar.cpp` — PickAndScanLocalFolder
- `app/views/bottom/DrawBottomPanel.cpp` — OutputView
- `gallery/Gallery.cpp` — FolderPicker（分层违规，本次一并修）

## [S2] Design

### 目标：UI 一律进 `app/`，按功能平铺

用户已定：**平铺 `app/<功能名>/`**（不引入 `features/` 层），**命名空间改为 `shine::app::*`**，**graph / theme 不挪**。

```
src/app/
  shell/ panels/ views/ dialogs/ ui/     # 既有，不动
  gallery/          ← gallery/ui/
    GalleryView.h/.cpp
    FolderPicker.h/.cpp
  novel/            ← novel/ui/
    NovelView.h/.cpp
  shots/            ← video/ui/   （功能名「分镜」，对应 SideView::Shots / FocusWindow::Shots）
    ShotTableView.h/.cpp          （文件名保留；含 EditorState / Tick）
  output/           ← media/ui/   （底栏「输出」页）
    OutputView.h/.cpp
  App.* DockLayout.* Fonts.* FileDialog.* UiState.*
```

业务模块目录只留非 UI 代码：

- `gallery/`：Gallery、Model、Scanner、Loader、decoders…
- `novel/`：NovelProjects
- `media/`：MediaLibrary、ImageFetch、VideoThumb
- `video/`：VideoProject、TaskRunner、H3WorkflowBuilder…（`ui/` 目录删除）

### 命名空间映射

| 旧 | 新 |
|----|----|
| `shine::gallery::ui` | `shine::app::gallery` |
| `shine::novel::ui` | `shine::app::novel` |
| `shine::video::ui` | `shine::app::shots` |
| `shine::media::ui` | `shine::app::output` |

### include 路径映射

| 旧 | 新 |
|----|----|
| `gallery/ui/GalleryView.h` | `app/gallery/GalleryView.h` |
| `gallery/ui/FolderPicker.h` | `app/gallery/FolderPicker.h` |
| `novel/ui/NovelView.h` | `app/novel/NovelView.h` |
| `video/ui/ShotTableView.h` | `app/shots/ShotTableView.h` |
| `media/ui/OutputView.h` | `app/output/OutputView.h` |

### 调用点改写（行为不变，只改限定名）

```
video::ui::DrawShotTable()        → app::shots::DrawShotTable()
video::ui::DrawVideoSidePanel()   → app::shots::DrawVideoSidePanel()
video::ui::Tick()                 → app::shots::Tick()
gallery::ui::DrawGalleryWindow()  → app::gallery::DrawGalleryWindow()
gallery::ui::DrawGallerySidePanel() → app::gallery::DrawGallerySidePanel()
gallery::ui::DrawGalleryViewerWindow() → app::gallery::DrawGalleryViewerWindow()
novel::ui::DrawNovelWindow()      → app::novel::DrawNovelWindow()
novel::ui::DrawNovelSidePanel()   → app::novel::DrawNovelSidePanel()
media::ui::DrawOutputView()       → app::output::DrawOutputView()
```

`kImagePathPayload`（`ShotTableView.h`）随文件迁到 `shine::app::shots`；当前无其它 TU 引用，无需改名。

### [S2.1] 分层修正：FolderPicker

现状：`gallery::PickAndScanLocalFolder()` 在业务层弹 UI 目录框。

改为：

1. `PickFolder` / `PickImageFolderForGallery` 落在 `shine::app::gallery`（`app/gallery/FolderPicker.*`），继续薄封装 `app::SelectFolderDialog`。
2. **删除** `gallery::PickAndScanLocalFolder`（`Gallery.h` / `Gallery.cpp` 声明与实现）。
3. 在 `app/gallery/` 增加同名编排函数（或直接内联到三个调用点，优先独立函数避免复制）：

```cpp
namespace shine::app::gallery {
// 弹目录框 → 成功则 gallery::RequestScan(Local)；取消写 gallery::State().message
bool PickAndScanLocalFolder();
}
```

4. 调用点改为 `app::gallery::PickAndScanLocalFolder()`（或在 `namespace shine::app` 内直接 `gallery::PickAndScanLocalFolder()`）：
   - `App.cpp`
   - `shell/DrawMenuBar.cpp`
   - `app/gallery/GalleryView.cpp`（原 `gallery/ui/GalleryView.cpp`）

业务模块 `gallery/` 不再 include 任何 `app/` 头。

### CMake

`CMakeLists.txt` 的 `add_executable` 源列表改路径（本项目源文件逐个列举）：

```
src/app/gallery/FolderPicker.cpp
src/app/gallery/GalleryView.cpp
src/app/novel/NovelView.cpp
src/app/shots/ShotTableView.cpp
src/app/output/OutputView.cpp
```

删除对应旧路径条目。

### 文档 / Skill 同步

- `Doc/AGENTS.md`「目录」：补一句 UI 一律在 `src/app/`（功能视图平铺）。
- `.mimocode/skills/shinetv-structure/SKILL.md`：目录表与 `src/app` 分工更新。
- `shinetv-ui-layout`：若提到 gallery/video 的 ui 路径则改；布局行为不变。

### 明确不挪 / 不改

- `src/graph/**`（含 ComfyNode 绘制、DrawCanvas）
- `src/theme/**`
- `src/app/{shell,panels,views,dialogs,ui}` 既有文件位置与职责
- 行为、快捷键、Dock、主题 token、运行时表现

## [S3] Out of Scope

- 不重写/拆分 ShotTableView（61KB 单文件）为 views+panels
- 不把 `app/ui/` 改名为 `widgets/`
- 不改 VNS / third
- 不引入 git/分支（本仓库无 .git）；不做功能增强
- 不迁移 theme / graph 绘制

## Tasks

- [x] T1: 迁 gallery UI → `app/gallery/` + 命名空间 `shine::app::gallery` + 修 FolderPicker 分层（删业务层 `PickAndScanLocalFolder`，编排放 app）— acceptance: 旧 `gallery/ui/` 不存在；`gallery/` 无 `#include "app/..."`；调用点走新路径 (covers: S2, S2.1)
- [x] T2: 迁 novel UI → `app/novel/` + `shine::app::novel` — acceptance: `novel/ui/` 不存在；DrawSideBar/DrawDockedPanels 用新 ns (covers: S2)
- [x] T3: 迁 media UI → `app/output/` + `shine::app::output` — acceptance: `media/ui/` 不存在；DrawBottomPanel 用新 ns (covers: S2)
- [x] T4: 迁 video UI → `app/shots/` + `shine::app::shots` — acceptance: `video/ui/` 不存在；Tick/侧栏/中央窗用新 ns (covers: S2)
- [x] T5: 更新 CMakeLists 源列表 — acceptance: 5 个新 cpp 在列表、旧路径不在 (covers: S2)
- [x] T6: 全量 include/限定名自检 + `cmake --build` 通过 — acceptance: `rg "gallery/ui/|novel/ui/|media/ui/|video/ui/|gallery::ui|novel::ui|media::ui|video::ui"` 无命中；构建成功 (covers: S2, S2.1)
- [x] T7: 更新 Doc/AGENTS.md 与 shinetv-structure（及 ui-layout 若涉及）— acceptance: 文档描述与新目录一致 (covers: S2)
