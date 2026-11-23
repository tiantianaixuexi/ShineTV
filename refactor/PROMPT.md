---
id: refactor.prompt
kind: runbook
status: current
source_of_truth:
  - README.md
  - AGENTS.md
  - refactor/README.md
  - refactor/design-spec.md
  - refactor/进度.md
last_verified: 2026-10-01
---

> **本页回答**：会话恢复时的开工契约：目标、先读什么、执行契约、硬约束、第一件事。现状与下一步不在此复述。

# 会话恢复提示词

> 新会话开工前粘一次，用来接上上下文。
> ⚠️ 旧的「Qt → ImGui 重构执行提示词」已作废 —— Qt 前端早删了，
> 那份提示词里的 `git checkout aa61046 -- third/imgui`、`SHINE_UI_QT` 双前端共存、
> D3D11 后端全部是过去式，照做会破坏现状。历史全文在 git 里。

---

## 目标

`E:\c++\ShineTV` 的前端已是 **Dear ImGui（OpenGL 3.3 / WGL）**，
它是 `webui/`（React + CSS 设计稿）的 **1:1 复刻**。当前任务 = 把复刻里**还不准的地方改准**，
以及把**设计稿里有、实现里还没有**的块补上。

`webui/` **只做效果、不管功能** —— 功能以 `src/` 的 `shine_core` 为准。

## 先读这几篇

| 文件 | 用途 |
|---|---|
| `AGENTS.md` | 协作规则、构建、门禁、地雷 —— **必读** |
| `refactor/README.md` | 分层 + **文件地图：要改什么 → 改哪个文件** |
| `refactor/design-spec.md` | 1:1 判据：几何/字号、31×5 色表、图标、外壳尺寸、22 组件、逐页布局、降级清单、验收方法 |
| `refactor/进度.md` | **现状 + 当前计划（小步清单）+ 判据清单** |
| `refactor/轮次记录.md` | 历史轮次记录（各轮读数与结论，归档） |
| `refactor/记忆.md` | **坑与纪律**：已修缺陷档案、门禁盲区、跨轮硬事实、归因纠正、开发规范 |

## 执行契约

- 按 `进度.md` 的偏差表逐条推进。**每条先独立核实归因**（默认假设是「我错了」），
  再动手；改错层的成本高于不改。
- 改一处就 `check-layers` + `check-colors` + `check-theme` → `Build.bat` → 抓图，
  **不要攒一大批再验**。
- **不要中途回来问我**：文档没覆盖的实现细节自己定，把决策记进源码注释或 `进度.md`。
  只有两种情况停下来：① 需要动工作区里已有的用户改动 ② 需要装系统软件 / 用账号 / 花钱。
- 没跑的验证写「未执行」，**不要把推断写成通过**。

## 硬约束（违反即返工）

- **不改 `shine_core`**（`src/core util net db llm comfy gpu media flow visual novel paint mcp project pipeline`）。
  宿主只需要接 `gpu::AttachDevice(GlApi)` 这一处注入。
  ⚠️ 但**先别急着说「只能改 shine_core」**：过去记在这条下面的三个缺口
  （分镜缩略图、V0 派生链的逐层图、总控「最近产物」）最后都只动了 UI 侧 ——
  前两个用的是 `generated_images` 的 `source_kind/source_id`、`visual_artifacts.rel_path`，
  第三个读的是同一张 `generated_images`（**不限 `source_kind`**）。
  **说「数据层没答案」之前先把那张表的列列全 + grep 谁在写它。**
- **颜色只能从 `theme/` 来**。调用点写 `ImVec4(...)` / `IM_COL32(...)` 字面量会被 `check-colors.ps1` 拦。
  「不画」用 `kit::ColorTransparent()`。纹理染色的恒等值用 `IM_COL32_WHITE`（不是主题色，别换成 `ColorText()`）。
- 构建目录单写，**不要并发 `cmake --build`**（撞 exe 锁）。
- 改了 C++ 之后按 `AGENTS.md` 的顺序验证：门禁 → `Build.bat` → 实际启动 → 抓图。
- ⚠️ 改了 `out/_fixture_seed.sql` **必须重跑 `out\_make_fixture.ps1`**：
  `_run_review.ps1` 只**拷**已生成的 `novel.db`，不重建 —— 忘了重建时判据以
  「NO-IMAGE」的样子红，与产品坏了同形。

---

> 现状与读数以 refactor/进度.md『总览』『关键指标』为准，本文件不复述（会漂移）。

> 下一步以 refactor/进度.md『当前计划（小步）』为准。

---

> 地雷以 AGENTS.md『地雷』与 refactor/记忆.md『跨轮硬事实』为准。

## 开工第一件事

```powershell
git status --short                 # 工作区改动可能是用户的，别覆盖
powershell -File tools\check-layers.ps1
powershell -File tools\check-colors.ps1
Build.bat --release
out\_make_fixture.ps1                                # 取证判据的成立条件，别跳（改过 seed 就必须跑）
powershell -File out\_run_review.ps1 -OutDir "$PWD\build\shots" -Theme deepspace
```

**先量基线，再动手。** 上一轮的「PASS / 87 张 / 13-13 探针」到底是多少，
只有自己跑出来才算数 —— 文档里的数字会过期，而**缺 fixture、跑了旧产物、与产品退化
在日志上长得一样**。第九轮的基线就是**红的**（见上），文档里却记着 PASS；
第十轮量到的基线与文档一致（`overall=PASS` / 87 张），**但别因为「上一次对」就跳过这一步**。

⚠️ 改 fixture 的规矩：`out\_fixture_{schema,data}.sql` 是 **UTF-8 留档，不要动**
（PowerShell 的 `Set-Content/Out-File` 会按系统 ANSI 码页重编码，中文静默变乱码且脚本照跑成功）——
改动一律**追加**到 `out\_fixture_seed.sql`（`INSERT` 用 **100+ id 号段**；**补已有行的列用 `UPDATE`**，
理由见上文那条地雷），并同步 `_make_fixture.ps1` 的硬门禁（行数 + **枚举覆盖度** +
`generated_images` 的行数/`source_kind` 种类/**参数列齐全度与「同列不同值」**）。
**枚举没铺满 = 对应分支永远没人验**，而整轮照样 PASS；**同一列填成同一个值 = 「读串列」验不出来**。

⚠️ 动过 `ReviewFixture.h` 里那几个写死的期望值（`kShot1*` 等）时，**fixture 与期望值必须一起改** ——
两边分叉的症状是判据报 `MISMATCH`，而它看起来与「产品坏了」一模一样（这也是它带实测值与期望值的原因）。
