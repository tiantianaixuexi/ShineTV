---
id: refactor.readme
kind: overview
status: current
source_of_truth:
  - CMakeLists.txt
  - src/
  - webui/src/
  - webui/src/styles/tokens.css
last_verified: 2026-09-30
---

# ShineTV Studio：Qt → Dear ImGui 重构

把 `src/` 下的 Qt（QWidget + QML）前端整体换成 Dear ImGui，**视觉 1:1 对齐 `webui/` 设计稿**，业务层一行不改。

> 本文与 `refactor/` 下其余四篇是本次重构的**唯一权威**。
> 仓库里的 `docs/` 是 Qt 时代的旧文档，本次重构不引用、不更新、不维护。

---

## 1. 一句话结论

业务层已经是现成的、无 Qt 的、**而且本来就是为 ImGui 写的**——
`src/gpu` 是 D3D11 设备与纹理层，`src/media` 已经把解码后的图上传成 DX11 纹理，
`gpu::AttachDevice()` 就是一个**至今没人调用的注入点**。
所以这次重构不是"把 Qt 换成 ImGui"，而是**把一个等了很久的宿主接上去**。

真正的工作量集中在两块：**ImGui 组件套件**和**6 个工作区页面**。

---

## 2. 基线数字（2026-09-30 实测）

| 项 | 数值 | 来源 |
|---|---|---|
| 现有可执行文件体积 | **392 MB** | `build/ShineTVStudio.exe` |
| `ShineTVStudio` 目标源文件 | **143** 个（`src/ui/**`） | `CMakeLists.txt` |
| `shine_kit` 目标源文件 | **20** 个 `.cpp` | `CMakeLists.txt` |
| `shine_qml` 目标源文件 | 2 个 `.cpp` + **52** 个 `.qml` | `CMakeLists.txt` |
| 待替换 UI 代码合计 | **168 C++ + 52 QML ≈ 220 文件** | 上两行相加 |
| `shine_core`（业务层）源文件 | 约 130，**零 Qt 头** | `CMakeLists.txt` |
| 设计稿组件 | `UI.jsx` 导出 20 个 + `StageFlow.jsx` 2 个 + 约 25 个纯 CSS 组件 | `webui/src/components/` |
| 设计稿图标 | **46** 个，24 网格 / 1.6 描边 | `webui/src/components/Icon.jsx` |
| 主题 | **5** 套（深空/薄暮/纸墨/水墨/极夜） | `src/ui/kit/theme/Themes/` |
| 颜色 token | **31** 项（⚠️ `tokens.css` 注释写的 28 已过时） | `src/ui/kit/theme/Token.h:69` |
| 工作区（导航目的地） | **6** 个 | `src/ui/pages/shell/ActivityRail.cpp:30` |

**分层门禁现状**：`tools/check-layers.ps1` → PASS（0 violations）。

---

## 3. 两条权威线（先定这个，否则会白干）

| 维度 | 权威来源 | 理由 |
|---|---|---|
| **视觉 / 样式 / 布局** | `webui/` | 用户要求 1:1；它是 CSS 设计稿，值齐全且一致 |
| **功能 / 数据 / 行为** | `src/`（Qt 实现） | 只有它真正接了 `project`/`novel`/`flow`/`comfy` 等后端 |

两者的差集是**已知的、不打算补的**：

- `webui` 的 `.doctabs`（`shell.css:248-319`）是**死 CSS**，无任何 JSX 引用 → **不实现文档标签栏**。
- `webui` 的 `Ctrl+N` 只有 toast、没有按键处理（`App.jsx:54`）→ 不当作行为契约。
- `webui` 样式编辑器的"另存为主题"只发 toast（`Shell.jsx:834`）→ 不当作行为契约。
- `webui` 无主题持久化（全仓 0 处 `localStorage`）；**Qt 侧有**（`theme.json`）→ **保留 Qt 的持久化**。
- `src/ui/qml/Storyboard.qml`、`ImageFlow.qml` 已写但**没有任何生产代码加载**（只有取证表引用）→ 按 **Widgets 版行为**实现，不按这两份 QML 实现。

---

## 4. 最大的三个风险

### 4.1 中文字体（必须最先解决）

ImGui 默认字体是 ASCII 的，**整个界面是中文**（主题名就叫「深空/薄暮/纸墨/水墨/极夜」）。
不解决就是满屏豆腐块，而且**不报错**。

- 目标字体：`C:\Windows\Fonts\msyh.ttc`（微软雅黑，19.7 MB）——与 `--font-ui` 的
  `"Segoe UI", "Microsoft YaHei UI", …` 族序一致（`tokens.css:30`）。
- ⚠️ `msyh.ttc` 是 **TrueType Collection**，需要 ImGui 的 `ImFontConfig::FontNo` 选子字体。
- ⚠️ 全字集 `GetGlyphRangesChineseFull()` 会让纹理图集爆掉；用
  `GetGlyphRangesChineseSimplifiedCommon()`（约 2500 常用字），并按需追加。
- 水墨主题是**唯一有字体覆盖**的一套（衬线族，`tokens.css:195`）→ 需要第二套字体
  （`C:\Windows\Fonts\simsun.ttc`），或在主题切换时重建图集。

### 4.2 分层门禁要反过来

`tools/check-layers.ps1:29` 现在**明令禁止** `imgui|ImVec|ImDraw` 出现在 `src/` 里：

```powershell
$legacyRe = [regex]'(?i)imgui|ImVec|ImDraw|VisNodeSys|ImAnim|VisualNode'
```

重构第一步就得把它改成「`src/` 内不得出现 Qt 头」，否则自己拦自己。

### 4.3 视觉 1:1 有两处注定做不到

| webui 效果 | 为什么做不到 | 降级策略 |
|---|---|---|
| `backdrop-filter: blur()` 8 档（`shell.css:27`、`views.css:192/213`、`ui.css:599/643/936`、`shell.css:480`） | ImGui 直绘，没有背景合成 | 用 `--glass` 实色叠加；顶栏/浮动面板/菜单三处最显眼，单独核对 |
| `color-mix(in srgb, …)` 7%–35% 着色（`ui.css:139`、`ui.css:851`、`views.css:496`、`views.css:658`、`shell.css:647`） | ImGui 无合成助手 | 主题生成时**预混算出约 15 个派生色**写进主题表 |

---

## 5. 里程碑总览

| 阶段 | 名称 | 交付物 | 风险 |
|---|---|---|---|
| **P0** | 底座与门禁 | ImGui 库落地、能开一个空窗口、门禁反向 | 低 |
| **P1** | 运行时骨架 | Win32 宿主 + D3D11 + `AttachDevice` + 帧循环 + 布局持久化 | 中 |
| **P2** | 主题与字体 | 31 token → ImGuiStyle、5 套主题、中文能显示 | 中 |
| **P3** | 组件套件 | 20 个 webui 组件的 ImGui 实现 | 中 |
| **P4** | 应用外壳 | 顶栏/导航/侧栏/检查器/底栏/状态栏/命令面板/浮层 | 中 |
| **P5** | 六个工作区 | 总控→小说→资产→分镜→出图→出片 | **高** |
| **P6** | 验收取证 | 取证 harness 重建在 D3D11 上 | 中 |
| **P7** | 收尾 | 删 Qt / 删 QML / 门禁定型 / 打包脚本换名 | 低 |

详细步骤见 [`phases.md`](phases.md)。

---

## 6. 文档地图

| 文件 | 内容 |
|---|---|
| [`architecture.md`](architecture.md) | 现状分层 → 目标分层、模块映射表、生命周期与线程契约 |
| [`design-spec.md`](design-spec.md) | webui 1:1 视觉规范：token 表、组件表、图标表、每页布局、降级策略 |
| [`phases.md`](phases.md) | P0–P7 每步的目标 / 改动 / 验收判据 |
| [`PROGRESS.md`](PROGRESS.md) | 进度看板（每阶段勾选 + 验证记录） |

---

## 7. 验证顺序（每个阶段都适用）

1. `powershell -File tools\check-layers.ps1` → 期望 PASS
2. `cmake --build build -j 8 --target ShineTVStudio` → 期望 0 error
3. 实际启动 `ShineTVStudio.exe` 走对应工作区，肉眼比对 `webui/` 同页面
4. 截图取证走 `scripts/capture_window.ps1`

**像素级回归不作判据**：离屏 software 场景图跨进程有噪声（同一份源码两次构建可差 2.8% 像素）。
判定"内容有没有丢"用同一次运行内的对照页 + 无文字纯色区域的精确相同率。
