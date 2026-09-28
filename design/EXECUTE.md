---
id: design.execute
kind: instruction
status: current
scope: ui-redesign
source_of_truth:
  - design/README.md
  - design/00-audit.md
  - design/01-tokens-color.md
  - design/02-type-icons.md
  - design/03-scaffold.md
  - design/04-navigation.md
  - design/05-panels.md
  - design/06-data-state.md
  - design/07-feedback-motion.md
  - design/08-gallery-gates.md
  - AGENTS.md
last_verified: 2026-09-28
---

# 施工协议 · 给执行本方案集的 AI

本目录 8 份方案由 AI 逐个实施，用户逐个验收。**本文件是执行规则**；每份方案文件是那一轮的施工图。

**当前进度**：全部 8 步待实施（以 git 历史为准：`git log --oneline -- design/` 里每次方案提交对应一步）。

## 铁律

1. **一次只做一个方案。** 做完 → 验证 → 交付说明 → **停下等用户看截图**。不要连做下一个。
2. **用户是验收方，不是你。** 截图和自检输出给用户看，由用户决定要不要继续。不要自己判定"视觉达标"就往下走。
3. **方案与源码冲突时以源码为准**，并当场改方案文件（更新 `文件:行号`），不要凭方案里的旧行号硬改。
4. **`src/ui/verify/` 是取证资产，不是历史包袱。** 改外壳或控件树结构时，同步改探针，并重跑对应的 `SHINE_P0x_REVIEW`。
5. **AGENTS.md 的红线继续生效**：core 不碰 Qt；颜色只走 Token（`tools/check-colors.ps1`）；UI 线程不做 IO（走 `shine::async::RunOnWorker` + `PostToUi`）；`src/ui/kit` 不依赖业务模块类型。
6. **每个方案结束提交到 `main`**（用户明确要求：改动落在 main，不要停在功能分支）。提交后 fast-forward 合回 main 并删临时分支。

## 每个方案的执行循环

```
1. 读 design/NN-xxx.md 的「现状证据」，逐条核对源码行号
2. 按「方案」改码；按「改动清单」逐文件走，漏一项都算没做完
3. 静态门禁（方案里列了哪几条就跑哪几条，通常至少三条）
4. cmake --build build -j 8 --target ShineTVStudio
5. 跑方案「验收」里的取证命令，把截图路径记下来
6. 逐条核对「判据」：能自动判的自动判（退出码、自检输出），
   只能肉眼判的（如"卡片和画布能区分"）明确告诉用户"这一条需要你看图"
7. 写交付说明：改了什么 / 门禁退出码 / 截图路径 / 哪几条需要用户肉眼确认 / 遗留项
8. 提交到 main
9. 停下
```

## 顺序与依赖

| 序 | 文件 | 前置 | 备注 |
|---|---|---|---|
| 01 | `01-tokens-color.md` | — | 基础，先做 |
| 02 | `02-type-icons.md` | 01 | 依赖 01 调好的 space/radius |
| 03 | `03-scaffold.md` | 02 | 依赖 `Typography`、`Glyph` |
| 04 | `04-navigation.md` | 03 | 依赖 `PageScaffold` 的页头 |
| 05 | `05-panels.md` | 03, 04 | 依赖右栏折叠策略 |
| 06 | `06-data-state.md` | 01, 03 | **必须同步改 `P09Review` / `P10Checks`** |
| 07 | `07-feedback-motion.md` | 03 | |
| 08 | `08-gallery-gates.md` | 全部 | 收尾：门禁 + 文档回写 |

跳做会被迫返工。01 → 08 是最短路径。

## 通用验证命令

```powershell
pwsh -File tools/check-layers.ps1
pwsh -File tools/check-colors.ps1
pwsh -File tools/check-theme.ps1
pwsh -File tools/check-emoji.ps1      # 02 之后存在
pwsh -File tools/check-ui.ps1        # 08 之后存在

cmake --build build -j 8 --target ShineTVStudio

# 设计检视台（每个方案都应在这里能看到自己的产物）
$env:SHINE_GALLERY_SHOTS = "$PWD\build\gallery"
& .\build\ShineTVStudio.exe --widget-gallery

# 主窗口截图（真实窗口，DX11 必须 CopyFromScreen，脚本已处理）
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\capture_window.ps1 -Out build\_shot.png
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\crop_zoom.ps1 -In build\_shot.png -X 0 -Y 0 -W 900 -H 520 -Out build\_crop.png
```

## 已知坑（不要重新踩一遍）

| 坑 | 出处 |
|---|---|
| DX11 交换链 `PrintWindow` 全黑，只能置前 + `CopyFromScreen` | `scripts/capture_window.ps1:9-14` |
| PowerShell 脚本必须纯 ASCII（5.1 读无 BOM UTF-8 会语法错） | `scripts/capture_window.ps1:14` |
| QSS 里插入 token 会让 `%N` 全错位 → **只在 `ColorToken` 尾部追加** | `QssBuilder.cpp:447-448` |
| Qt 的 QSS `palette(...)` 不走 Token，深色主题下会渲染成系统黑字 | `docs/10-modules/ui-kit.md:46` |
| QSS 选择器分组会吞掉后续选择器 → 五态规则必须各写一条 | `QssBuilder.cpp:179-180` |
| 中文路径必须走 `util::PathFromUtf8`（`path{std::string}` 按 ANSI 解释） | `Theme.cpp:112-113` |
| `shine::verify` 探针不做 objectName 匹配，但用 `findChild<QTabWidget*>` / `dynamic_cast` / 文本断言 | `P09Review.cpp:41`、`P06Review.cpp:51`、`P04Review.cpp:112` |

## 明确不做

- 不合并多个方案成一次提交；
- 不删除 `src/ui/verify/` 下的任何探针来"让检查通过"；
- 不引入新依赖（当前只有 `Qt6::Widgets`，`CMakeLists.txt:17`）；
- 不写开发计划/进度表文档（`AGENTS.md` 禁止）；执行状态写在本文件的方案表里，或直接体现在 git 历史里；
- 不改 `shine_core` 的任何文件（除非用户明确要求）。

## 方案完成后的收尾（全部 8 个做完后）

1. 按 `08-gallery-gates.md` 的 8.4 回写 `docs/`（`ui-kit.md` / `checks.md` / `product.md`）；
2. 在 `design/README.md` 顶部标注实施结果（哪几步已落地、哪几步被用户否决）；
3. 对照 `00-audit.md` 的 15 条问题逐条标注"已解决 / 不改 / 暂缓"。
