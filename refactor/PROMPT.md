---
id: refactor.prompt
kind: runbook
status: current
source_of_truth:
  - refactor/README.md
  - refactor/phases.md
  - refactor/PROGRESS.md
  - AGENTS.md
last_verified: 2026-09-30
---

# 重构执行提示词

> 粘到新会话里直接用。这篇是**恢复入口**：任何会话开始前粘一次即可接上进度。

---

## 目标

把 `E:\c++\ShineTV` 的 Qt 前端整体重构成 Dear ImGui，**视觉 1:1 对齐 `webui/` 设计稿**，
然后自行推进到底，不要中途回来问我。

## 先读这五篇，它们是唯一权威

- `refactor/README.md` —— 现状基线、两条权威线、三个风险、P0–P7 总览
- `refactor/architecture.md` —— 目标分层、模块映射、生命周期与线程契约
- `refactor/design-spec.md` —— webui 1:1 判据（token 表 / 22 组件 / 46 图标 / 6 页逐页布局）
- `refactor/phases.md` —— P0–P7 共 **51 步**，每步的目标 / 改动 / 验收
- `refactor/PROGRESS.md` —— 进度看板，每完成一步就更新

`AGENTS.md` 已指向 `refactor/`。仓库里的 `docs/` 是 Qt 时代旧文档 —— **不要读、不要引用、不要更新**。

## 执行契约

- 按 `phases.md` 的 P0→P7 顺序推进，**一步验收通过再进下一步**；不要跳步，也不要并行改重叠文件。
- **不要中途回来问我。** 文档没覆盖的实现细节自己定，把决策记进 `PROGRESS.md` 的验证记录表。
- 只有两种情况停下来问我：① 需要动工作区里已有的用户改动　② 需要装系统软件 / 用账号 / 花钱。
- 每完成一步：跑验收 → 勾上 `PROGRESS.md` 复选框 → 在验证记录表追加一行
  （日期 / 阶段 / 内容 / 观测 / 通过与否）。没跑的写「未执行」，**不要把推断写成通过**。
- 改完 C++ 按 `AGENTS.md` 的验证顺序走：`check-layers.ps1` → `cmake --build` → 实际启动程序 → 截图。

## 硬约束（违反即返工）

- **不改 `shine_core`**（`src/core util net db llm comfy gpu media flow visual novel paint mcp project pipeline`）。
  业务层零 Qt 已经成立，宿主只需要接 `gpu::AttachDevice()` 这一处注入。
- 工作区里 `Plugins/**` 的删除改动**属于用户**，一个字都不要碰 —— 不回滚、不覆盖、不「顺手清理」。
- 迁移期保留 Qt 前端可回退：CMake 开关 `SHINE_UI_IMGUI`（默认 ON）/ `SHINE_UI_QT`，Qt 路径到 P7 才删。
- ImGui **只能从磁盘恢复**：`git checkout aa61046 -- third/imgui`。不要去网上下。
- 构建目录单写，不要并发 `cmake --build`（会撞 exe 锁）。
- 每阶段完成就 commit，然后 **fast-forward 合进 `main` 并删掉临时分支**，不要停在功能分支上等下一轮指令。

## 已知地雷（都已写进 `phases.md`，别重新踩一遍）

- **P2.5 中文字体** —— `msyh.ttc` 是 TrueType Collection，必须设 `ImFontConfig::FontNo`；
  字形范围用 `GetGlyphRangesChineseSimplifiedCommon()`。不解决就是满屏豆腐块**且不报错**。
  **P2 不做完不要进 P3**（字体没解决时所有截图判据作废）。
- **P0.3 门禁** —— `tools/check-layers.ps1:29` 现在明令禁止 `imgui|ImVec|ImDraw` 出现在 `src/`，
  必须先反转为「`src/` 零 Qt 头」，否则自己拦自己。
- **P2.2 token** —— `Token.h:69` 是 **31** 项不是 28（`tokens.css:3` 的注释和 `Shell.jsx:791-798` 都过时）。
  `ApplyTheme()` 一次写全 31 项映射，不许用循环凑 —— 漏一项不报错，只是某个控件在某主题下颜色错。
- **P5.5 cover 缩放** —— 按左上角推 `uv`/`src`，**不要用中心锚**（绕中心会让整幅画左上平移 `w/2*(k-1)`，只画出一角）。
- **取证** —— 多图跑完**先排 md5**：任意两张逐字节相同 = 有一张没拍到它该拍的状态。
  等待结果要写进 manifest，别丢返回值。
- **像素差不是内容指标** —— 离屏渲染跨进程约 2000px、跨构建约 40000px 噪声。
  大于它也**不能**直接判成回归；判「内容有没有丢」用同一次运行内的对照页。
- **两处必须接受的降级** —— `backdrop-filter` 毛玻璃、`box-shadow`。不要为它们做像素级死磕。

## 完成的判据

- `cmake -S . -B build-imgui` **不装 Qt 也能配置通过**
- `tools/check-layers.ps1` PASS
- 6 个工作区 × 5 套主题全部可走通
- `src/` 里 Qt 头引用 = **0**，QML 文件 = **0**
- `PROGRESS.md` 关键指标表填上实测值（含 exe 体积对比基线 **392,330,166 B**）

## 开工第一件事

```powershell
git checkout aa61046 -- third/imgui
```

预期 `#define IMGUI_VERSION_NUM   19297`（1.93.0 WIP，268 个文件，含 `imgui_impl_win32` + `imgui_impl_dx11`）。
