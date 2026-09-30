---
id: refactor.prompt
kind: runbook
status: current
source_of_truth:
  - README.md
  - AGENTS.md
  - refactor/README.md
  - refactor/design-spec.md
  - refactor/PROGRESS.md
last_verified: 2026-09-30
---

# 会话恢复提示词

> 新会话开工前粘一次，用来接上上下文。
> ⚠️ 旧的「Qt → ImGui 重构执行提示词」已作废 —— Qt 前端早删了，
> 那份提示词里的 `git checkout aa61046 -- third/imgui`、`SHINE_UI_QT` 双前端共存、
> D3D11 后端全部是过去式，照做会破坏现状。历史全文在 git 里。

---

## 目标

`E:\c++\ShineTV` 的前端已是 **Dear ImGui（OpenGL 3.3 / WGL）**，
它是 `webui/`（React + CSS 设计稿）的 **1:1 复刻**。当前任务 = 把复刻里**还不准的地方改准**。

`webui/` **只做效果、不管功能** —— 功能以 `src/` 的 `shine_core` 为准。

## 先读这四篇

| 文件 | 用途 |
|---|---|
| `AGENTS.md` | 协作规则、构建、门禁、地雷 —— **必读** |
| `refactor/README.md` | 分层 + **文件地图：要改什么 → 改哪个文件** |
| `refactor/design-spec.md` | 1:1 判据：几何/字号、31×5 色表、图标、外壳尺寸、22 组件、逐页布局、降级清单、验收方法 |
| `refactor/PROGRESS.md` | **施工清单就在 §「视觉 1:1 符合性审计」**；另有跨轮硬事实与已修缺陷 |

## 执行契约

- 按 `PROGRESS.md` 的偏差表逐条推进。**每条先独立核实归因**（默认假设是「我错了」），
  再动手；改错层的成本高于不改。
- 改一处就 `check-layers` + `check-colors` + `check-theme` → `Build.bat` → 抓图，
  **不要攒一大批再验**。
- **不要中途回来问我**：文档没覆盖的实现细节自己定，把决策记进源码注释或 `PROGRESS.md`。
  只有两种情况停下来：① 需要动工作区里已有的用户改动 ② 需要装系统软件 / 用账号 / 花钱。
- 没跑的验证写「未执行」，**不要把推断写成通过**。

## 硬约束（违反即返工）

- **不改 `shine_core`**（`src/core util net db llm comfy gpu media flow visual novel paint mcp project pipeline`）。
  宿主只需要接 `gpu::AttachDevice(GlApi)` 这一处注入。
- **颜色只能从 `theme/` 来**。调用点写 `ImVec4(...)` / `IM_COL32(...)` 字面量会被 `check-colors.ps1` 拦。
  「不画」用 `kit::ColorTransparent()`。
- 构建目录单写，**不要并发 `cmake --build`**（撞 exe 锁）。
- 改了 C++ 之后按 `AGENTS.md` 的顺序验证：门禁 → `Build.bat` → 实际启动 → 抓图。

## 已知地雷（详见 `AGENTS.md` §地雷 与 `PROGRESS.md` §跨轮硬事实）

- `kit::Rect` 四参是 `(minX,minY,maxX,maxY)`，**不是** `(x,y,w,h)`；写错不报编译错，
  控件整块不画也不可点。宽高式用 `kit::RectAt()`。
- 同一帧同一矩形只能 `HitTest` 一次（先注册者独占 `HoveredId`）；浮层遮罩**只画不注册**
  （`kit::ScrimPaint`），否则面板里的控件点不动，且哪一处坏取决于绘制顺序。
- `ImGui::IsMouseHoveringRect` 在本工程会 `0xC0000005`，用 `Rect::contains(io.MousePos)`。
- `kit::ScrollRegion` 不调 `setContentHeight()` 就是**滚不动**。
- 取证注入必须早于 `NewFrame`（`Host::SetFrame*Override`）。
- **像素差不作判据**（跨进程约 2000px、跨构建约 40000px 噪声）。
- 两处必须接受的降级：`backdrop-filter` 毛玻璃、`box-shadow`。不要为它们死磕像素。
- ⚠️ `webui/` **不在版本控制里**；核对 CSS 原式前先确认它还在本机。

## 开工第一件事

```powershell
git status --short                 # 工作区改动可能是用户的，别覆盖
powershell -File tools\check-layers.ps1
powershell -File tools\check-colors.ps1
Build.bat --release
out\_make_fixture.ps1                                # 取证判据的成立条件，别跳
powershell -File out\_run_review.ps1 -OutDir "$PWD\build\shots" -Theme deepspace
```

**先量基线，再动手。** 上一轮的「PASS / 9-11 探针 / 几张图」到底是多少，
只有自己跑出来才算数 —— 文档里的数字会过期，而**缺 fixture 与产品退化在日志上长得一样**。
