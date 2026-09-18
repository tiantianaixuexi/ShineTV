# ShineTV 项目规则

## 文档地图（先看这里）

| 文档 | 作用 | 何时读 |
|------|------|--------|
| `AGENTS.md`（本文件，位于 `Doc/`） | 工具链 / 字体 / 布局 / 技术栈 / 语言特性 —— **硬性规则总入口** | 每次开工前 |
| `Doc/RULES-AI.md` | AI 执行规则（一次一步、验收纪律、线程纪律）+ 项目 Skill 索引 | 每次开工前 |
| `Doc/RULES-LANG.md` | C++26 语言特性与代码风格、能力探测结论、API 边界例外、反射序列化与 `util` 用法 | 写代码时 |
| `Doc/RULES-COMFY.md` | ComfyUI 错误捕获与连接健康（WS 事件、"忙碌 ≠ 卡死"判定、日志模板） | 动 Comfy 或排查时 |
| `Doc/STYLE-UI.md` | 默认视觉方向（色板 token，禁止硬编码颜色） | 改 UI 时 |
| `Plan/PLAN.md` | **总纲 + 文档地图 + 大类/小类总表** | 领任务时 |
| `Plan/任务/<大类>.md` | **施工图**：每个小类的 S 做什么 / 判据（P3–P8、G，共 7 个文件） | 开工前 |
| `Plan/PROGRESS.md` | **唯一勾选入口**：小任务 `[ ]`/`[x]` 与状态 | 每做完一个 S |
| `Plan/证据.md` | 实测证据（每个小类汇总 + 每个 S 的 ✅ 记录） | 追溯/复核时 |
| `Plan/坑与手法.md` | 踩过的坑、事故、验收手法（截图/离线/无头） | 卡住时 |
| `Plan/归档-已完成.md` | 已完成的旧线（P0/P1/P2/P2.9/R） | 查历史结论 |
| `Plan/HANDOFF.md` | 新会话交接（开场白 + 现状 + 基础设施 + 本机环境） | 新会话第一眼 |
| `Doc/BASELINE.md` | 现状盘点、技术栈与目录、已完成基线事实、明确不做清单 | 需要背景时 |
| `Doc/BUILD.md` | 构建方式（工具链、configure / build 命令） | 需要编译时 |

> 规则/约束/参考材料**不再写进 `Plan/PLAN.md`**（已拆分到上表），`Plan/PLAN.md` 只放"做什么、怎么算做完"。

## 工具链（硬性）

- **必须使用 GCC 16.1.0**（MSYS2 MinGW64），不要用 Clang / MSVC
- 编译器路径：`C:/msys64/mingw64/bin`
  - `C:/msys64/mingw64/bin/gcc.exe`
  - `C:/msys64/mingw64/bin/g++.exe`
- 构建工具：同目录 `mingw32-make.exe`
- CMake 配置示例：

```bash
cmake -S . -B build -G "MinGW Makefiles" ^
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe ^
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe
cmake --build build -j
```

## UI / 字体（硬性）

- **界面必须使用中文字体**（禁止只用 ImGui 默认拉丁字体导致中文方框）
- 默认字体：**微软雅黑** `C:/Windows/Fonts/msyh.ttc`（找不到则 `simhei.ttf` / `msyhbd.ttc`）
- Glyph Range：至少 `GetGlyphRangesChineseSimplifiedCommon()`；完整中文可用 `GetGlyphRangesChineseFull()`
- 字体加载集中在 `src/app/Fonts.cpp`，启动时调用
- 所有 UI 文案默认中文；代码标识符可用英文

## 布局（硬性）

VS Code 风格（见 `Plan/PLAN.md` §1、skill `shinetv-ui-layout`）：

1. **顶栏** 菜单 + 连接状态
2. **活动栏** 最左，切换侧栏（资/节/流/C/设置）
3. **侧栏** 内容随活动栏切换
4. **中央** 节点图（VNS，`src/graph`）
5. **右栏** 属性（选中节点）/ 预览
6. **底栏** 队列 / 日志 / 输出
7. **状态栏** 连接/队列/图/缩放/选中/主题

大列表必须 `ImGuiListClipper`。快捷键：`Ctrl+B` 侧栏，`Ctrl+S` 保存图。

## 技术栈（硬性）

| 领域 | 必须使用 | 禁止 / 备注 |
|------|----------|-------------|
| **语言反射** | **C++26 静态反射**（`-freflection` 已加入 `CMakeLists.txt`，仅对 C++ 生效） | 字段枚举/序列化**优先反射**：`std::define_static_array` + `template for (constexpr auto m : …)` + splice `obj.[:m:]`；**禁止**用运行时 `for` 迭代反射结果；完整可用写法见 `Doc/RULES-LANG.md` §13.6 |
| **内存分配** | **mimalloc** | 启动 `mi_process_init()`；热路径/大块缓冲优先 mi |
| **日志** | **spdlog** | 禁止 `printf`/`std::cout` 做正式日志 |
| **格式化** | **fmt** | `log::Info("{}", x)`，禁止 `vsnprintf` |
| **异步任务流** | **stdexec** | Comfy/图片/解码一律 sender/receiver |
| **JSON** | **yyjson** | Comfy API / 设置；VNS 内部可用其 jsoncpp |
| **网络** | **libhv** | HTTP client + WebSocketClient |

### 异步任务约定（stdexec）

```text
just(path) | then(读文件) | then(解码) | then(UI 投递)
```

- IO/解码在 worker；**仅结果回 UI**（`async::PostToUi` + `DrainUiQueue`）
- 错误落到 spdlog，不要静默失败

### 库接入状态（P1 已全部链入）

mimalloc / spdlog+fmt / stdexec / libhv / yyjson 均在根 `CMakeLists.txt`。细节见 skill `shinetv-thirdparty`。

## 语言特性（C++26，硬性）

**本项目 `CMAKE_CXX_STANDARD 26`，新代码一律使用 C++20/23/26 设施，禁止 C++03 风格签名。**
完整约定、对照表与改造边界见 `Doc/RULES-LANG.md` §13。

| 场景 | 必须用 | 禁止 / 说明 |
|------|--------|-------------|
| 只读字符串参数 | `std::string_view` | 禁止 `const std::string&`（除非确实需要以 `std::string` 生命周期为契约） |
| 需要持有/返回字符串 | `std::string`（按值返回，靠 move） | 禁止返回 `const std::string&` 指内部成员，除非调用方明确需要零拷贝且不跨帧持有 |
| 只读字节/像素缓冲 | `std::span<const std::byte>` / `std::span<const std::uint8_t>` | 禁止 `const void* + size`、禁止 `const std::vector<T>&` 仅仅为了读 |
| 可失败返回 | `std::expected<T, E>`（C++23），无信息失败用 `std::optional<T>` | 禁止 `bool + out 参数`、禁止返回错误码整数 |
| 路径 | `std::filesystem::path`（持有）；参数可用 `std::string_view` / `const path&` | 禁止裸 `const char*` 路径 |
| 时间/耗时 | `std::chrono`（`steady_clock` / `milliseconds`） | 禁止 `clock()` / `GetTickCount` 直接算耗时 |
| 数值 ↔ 字符串 | `std::from_chars` / `std::to_chars` | 禁止 `atoi` / `atof` / `sprintf` |
| 遍历/查找 | `std::ranges`（`std::ranges::find_if` / `views::filter` / `views::transform`） | 手写 `for (size_t i…)` 仅限需要索引或性能敏感处，并注明原因 |
| 编译期常量 | `constexpr` / `consteval` / `constinit` / `std::to_underlying` | 禁止 `#define` 定义常量（宏仅用于条件编译） |
| 返回值标注 | `[[nodiscard]]`（所有纯查询与可失败函数）、`noexcept`（不会抛的函数） | — |
| 格式化 | `fmt`（`fmt::format` / `log::Info("{}", x)`） | **禁止 `std::format`**（与已链入的 `FMT_HEADER_ONLY` + `SPDLOG_FMT_EXTERNAL` 冲突）；禁止 `vsnprintf` |
| 枚举 | `enum class` + `using enum`（需要时） | 禁止裸 `enum` 作标志位（用 `enum class` + `\|\|`/`&` 或位掩码包装） |
| 回调 | **异步 `*Cb`：`std::move_only_function`**（GCC 16 可用）；WS 监听：`fu2::function`；其余可用 `std::function` | 热路径（每帧/每像素）禁止 `std::function` |
| 模板约束 | `concepts` | 禁止 SFINAE 技巧 |
| 结构化绑定 / 指定初始化 | `auto [a, b] = …`；`Foo{.x = 1, .y = 2}` | — |
| 反射 | **C++26 静态反射**（字段枚举 / 序列化）—— 已实测可用，`-freflection` 已加入 CMake | 必须 `define_static_array` + `template for` 展开；**禁止**运行时 `for` 迭代反射结果；骨架见 `Doc/RULES-LANG.md` §13.6 |
| 调用栈（诊断） | `std::stacktrace`（GCC 13+，MinGW 需探测） | 探测不通就不用，不要为此引入新依赖 |

### 典型签名对照（写新代码时照此改）

```cpp
// ❌ 旧式（仓库 P1/P2 历史代码常见）
bool ParseObjectInfoJson(const std::string& json, ObjectInfoResult& out);
void RunOnWorker(std::function<void()> fn);
HttpResponse HttpGet(const std::string& url, int timeoutSec);
void Upload(const void* rgba8, size_t bytes);

// ✅ 本项目要求
[[nodiscard]] std::expected<void, ParseError> ParseObjectInfoJson(std::string_view json, ObjectInfoResult& out);
void RunOnWorker(std::function<void()> fn);                       // 回调本身保留 std::function
[[nodiscard]] HttpResponse HttpGet(std::string_view url, std::chrono::seconds timeout);
[[nodiscard]] std::expected<GpuTextureHandle, GpuError> Upload(uint32_t w, uint32_t h,
                                                              std::span<const std::byte> rgba8);
```

**改造策略（已定）**：存量代码**一次性全量现代化**，作为独立任务 **`Plan/PLAN.md` §5.0 P2.9** 执行。
项目规模不大（实测 `const std::string&` 共 **75 处 / 15 个文件**），按模块分 4 批改完、每批编译验收，之后新代码不再与旧风格混搭。

**例外（保持原样，不要为它们造包装层）**：libhv C/C++ API 边界、VNS API 边界、ImGui 的 C 字符串、Win32/DX11/COM、yyjson 的 C API —— 逐条见 `Doc/RULES-LANG.md` §13.5。
在这些边界上，用 `std::string(sv)` / `.c_str()` 显式转换即可，**不要改 `third/` 里的代码**。

**P2.9 之后**写新代码一律按上表，**不允许再回退**到 `const std::string&` + `bool + out 参数` 的旧式签名。

## 目录

- `src/`：应用源码（app / theme / core / util / comfy / graph / media / video / paint / mcp）
- `src/app/`：**全部面板/视图 UI**。shell（顶栏/活动栏/侧栏/停靠/状态栏）、panels（侧栏）、views（中央/右栏/底栏）、dialogs、共享控件；功能域视图平铺：`gallery/` `novel/` `shots/` `output/`（命名空间 `shine::app::*`）
- `src/util/`：**纯函数工具，header-only**（`Random.h` / `Strings.h` / `Time.h` / `Json.h`）—— 复用优先，禁止在各模块重复实现 `Lower` / `RandomHex` / trim / yyjson 样板；约定见 `Doc/RULES-LANG.md` §13.7
- `third/`：第三方库（imgui docking、VNS、libhv…）
- `.mimocode/skills/`：项目 Skill（结构/构建/Comfy/UI/三方库）
- `Plugins/`：原 UE5.8 插件（只读参考）
- `Plan/PLAN.md` / `Doc/BUILD.md`

## 当前阶段（2026-09-19 盘点）

- **P3/P4/P7 ✅**；**P5 余 P5.7**；**P6/P8 ⬜**；**G 线在完成分支 `c14133e` 已做完（勿在 main 重做）**；小说 P1–P8 ✅ + 附加交付（AgentKit/字段/多模型）。
- **下一步**：合并 `refactor/libhv-log-to-shine@f7a0cb8` → **P5.7**（或 P6/P8）。详见 `Plan/PLAN.md` §0 与 `Plan/PROGRESS.md`。

## 项目 Skill

| ID | 何时读 |
|----|--------|
| `shinetv-structure` | 改代码前理解目录/模块 |
| `shinetv-build` | 编译失败或配置 CMake |
| `shinetv-comfy` | 动 `src/comfy/` 或队列/WS |
| `shinetv-graph` | 动中央节点图 / VNS / 存盘 |
| `shinetv-ui-layout` | 改 Dock/状态栏/面板/性能 |
| `shinetv-thirdparty` | 链新库或改 CMake third |
