# ShineTV Studio — AI 执行规则（开工前必读）

> 本文件是**规则**，不是任务清单（任务见 `Plan/PLAN.md`）。与 `Plan/任务/G-图片库.md` §10 同源。
> 章节号沿用拆分前 `PLAN.md` 的编号（§10 项目 Skill / §11 AI 执行规则）。

---

## 10. 项目 Skill

结构与约定见 `.mimocode/skills/`：

| Skill ID | 用途 |
|----------|------|
| `shinetv-structure` | 源码目录与模块职责总览 |
| `shinetv-build` | GCC 16.1 构建与 CMake 约定 |
| `shinetv-comfy` | ComfyCore API / 线程模型 / 事件 |
| `shinetv-graph` | VNS GraphHost / 节点工厂 / 存盘 |
| `shinetv-ui-layout` | 七区布局与 ImGui 性能约定 |
| `shinetv-thirdparty` | third 库接入方式与禁止事项 |
| `shinetv-db` | `src/db` 数据层用法（Redis 池 / 编译期命令 / 反射 Hash / SQLite） |
| `shinetv-garnet` | Garnet（Redis 兼容）命令支持清单 |

---


## 11. AI 执行规则（与 `Plan/任务/G-图片库.md` §10 同源）

1. **一次只做一个任务**：完成 P3.1 → 编译 → 运行 → 逐条对验收 → 确认后再进 P3.2。禁止一次性生成多任务代码。
2. **每个任务必须可编译可运行**：`cmake configure` → `cmake --build` → 启动 exe → 走该任务验收。有问题先修，不进下一步。
3. **新增 `.cpp` 必须手工加进 `CMakeLists.txt` 的 `add_executable` 列表**（本项目源文件逐个列举，不会自动搜集）。
4. **禁止跨模块泄漏依赖**：libpng 只出现在图片解码/编码适配层；libhv 只在 `comfy/` 与 `mcp/`；VNS 只在 `graph/` 与 UI 绘制处。用 `grep` 自检。
5. **JSON 分工**：Comfy / 设置 / 工程存盘用 `yyjson`；VNS 图存盘用其自带 `jsoncpp`。不要混用，也不要把 jsoncpp 引入非 graph 模块。
6. **线程纪律**：扫描 / 解码 / 网络 / 写盘只在 worker；**UI 线程独占**图、工程、画布、缓存等模型；WS 回调在 WS 线程触发，必须 `PostToUi` 后再改状态。
7. **不重复造基础设施**：线程池用 `shine::async`（4 线程，够了，不要扩），日志用 `shine::log`，设置用 `AppSettings`，HTTP 用 `src/comfy/ComfyHttp`。
8. **不擅自改 `third/VisualNodeSystem`**：VNS 的能力边界见 `Doc/BASELINE.md` §4，若确实不够，先记录缺口并说明最小改法，得到确认再动。
9. **不过度设计**：不实现 `Doc/BASELINE.md` §8 的任何一项；不为"将来可能的需求"提前搭抽象层。
10. **中文 UI**：所有面向用户的文案用中文，取色走 `theme::Current()`，不使用 ImGui 默认拉丁字体渲染中文。
11. **绝不用"同步 HTTP 探活"判断服务是否可用**：探活必须走 worker、超时 ≥ 10s；且**执行中的服务不回探活 ≠ 卡死**，要先读忙碌状态（`BusyState`）。UI 线程禁止任何同步 HTTP。
12. **错误只能从协议里取**：WS `execution_error` → `/history` 的 `status.messages` → `/prompt` 400 的 `node_errors`，三条都要接；**禁止用"没反应/超时"推断错误内容**。
13. **日志必须能回答"现在在干什么"**：每个 WS 事件一行（type / promptId / nodeId / 进度）；错误多行（异常类型 + 消息 + 回溯前 3 行 + 中文 hint）；忙碌时的探活失败必须写成 `service busy, probe timeout ignored`。
14. **禁止把忙碌判为卡死**：`Stalled` 只在「WS 已连接 + 有任务在 `queue_running` + 90s 无任何 WS 帧 + `/queue` 未变化」四个条件同时成立时才标记，文案必须是"疑似卡住（仍在队列中）"，不能写"连接失败"。
15. **自检脚本/人工验证时不要用短超时"测连接"**：跑一次性任务时，若 ComfyUI 正在执行上一次任务，先看 `HealthSummary()`（"已连接 · 执行中 · 节点 X"）再决定是等待还是排查。
16. **语言特性按 `Doc/AGENTS.md`「语言特性」与 `Doc/RULES-LANG.md` §13**：新代码用 `std::string_view` / `std::span` / `std::expected` / `std::ranges` / `[[nodiscard]]` / `concepts`；**禁止 `std::format`**（统一 `fmt`）；禁止再写 `const std::string&` + `bool + out 参数` 风格的新接口；历史代码**触碰即升级**，但**不做跨模块大重构**。

---

