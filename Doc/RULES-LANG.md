# ShineTV Studio — 语言特性与代码风格约定（C++26，强制）

> 精简版同时写在 `Doc/AGENTS.md`「语言特性（C++26，硬性）」；本文件是完整版（含能力探测结论、API 边界例外、反射序列化与 `util` 约定）。
> 章节号沿用拆分前 `PLAN.md` 的编号（本文件 §13）。

---

## 13. 语言特性与代码风格约定（C++26，强制）

> 本项目 `CMAKE_CXX_STANDARD 26`，**新代码一律使用 C++20/23/26 设施**。
> 本节的精简版同时写在 `Doc/AGENTS.md` 的「语言特性（C++26，硬性）」里，改代码前先看那份。

### 13.1 现状与目标

**已核实**：仓库现有代码（`src/comfy/`、`src/graph/`、`src/theme/`）**完全没有使用任何 C++20+ 设施**——
`std::string_view` / `std::span` / `std::expected` / `std::optional` / `std::format` / `std::ranges` / `[[nodiscard]]` 命中数**均为 0**，
而 `const std::string&` 出现 50+ 处。所以：

- **新写的模块与函数**（P3 之后的所有任务）：按 §13.3 的现代签名写，**不允许**再产出 `const std::string&` + `bool + out 参数` 这种老式接口。
- **历史代码**：**一次性全量现代化**，见 `Plan/PLAN.md` §5.0 **P2.9**（实测 75 处 / 15 个文件，分 4 批，零行为变更）。
  P2.9 完成后，本文其余任务直接写现代签名，**不再新旧混搭**。超出改造范围、属于 API 边界的部分见 §13.5。

### 13.2 开工前的能力探测（P3.0 之前花 10 分钟，必须做）

写一个临时文件 `src/core/ProbeLang.cpp`（探测完即删，且**不要加进 CMakeLists 的正式列表**——用一次性的 `-c` 编译试探），逐项确认并在本节勾选记录：

| # | 探测项 | 结论 | 需要的编译选项 | 对计划的影响 |
|---|--------|------|----------------|--------------|
| 1 | 静态反射 `<meta>` + `^^T` | ✅ **可用** | **必须** `-freflection`（已加入 `CMakeLists.txt`，仅对 C++ 生效；否则 `std::meta has not been declared`） | 字段枚举与序列化**优先走反射**，写法见 §13.6。**两个硬性约束**：反射结果必须用 `std::define_static_array` 物化；只能用 `template for (constexpr auto …)` 展开（运行时 `for` 迭代报 `consteval-only variable … not declared constexpr`） |
| 2 | `std::expected` | ✅ 可用（`__cpp_lib_expected 202211L`） | — | 可失败 API 的返回类型（§13.3 已按此写） |
| 3 | `std::span` / `std::mdspan` | ✅ 可用（`202311L` / `202406L`） | — | 像素/缓冲视图；二维像素访问用 `mdspan` |
| 4 | `std::ranges` | ✅ 可用（`__cpp_lib_ranges 202406L`） | — | 遍历/查找统一风格 |
| 5 | `std::from_chars` / `to_chars` | ✅ 可用（`__cpp_lib_to_chars 202306L`） | — | 替代 `atoi` / `sprintf` |
| 6 | `std::stacktrace` | ✅ 可用（实测输出 `stacktrace frames=5`） | **必须链接 `-lstdc++exp`**；`-lstdc++_libbacktrace` 在本机不存在 | P8.4 可打印调用栈，但要在 `CMakeLists.txt` 加链接项 |

**实测环境记录（2026-09-16）**

```text
g++ (Rev2, Built by MSYS2 project) 16.1.0
__cplusplus                = 202400
__cpp_lib_expected         = 202211    __cpp_lib_span   = 202311    __cpp_lib_mdspan = 202406
__cpp_lib_ranges           = 202406    __cpp_lib_to_chars = 202306  __cpp_lib_chrono = 202306
__cpp_lib_string_view      = 202403    __cpp_concepts   = 202002
probe-lib: OK  expected=42/0 img=4/16 ranges=3 to_chars=1
```

**反射实测（`-freflection`，2026-09-16 复核通过）**

```text
static_assert(kMembers.size() == 4);                       // define_static_array(nonstatic_data_members_of(^^Settings))
fieldNames<Settings>() -> themeId / comfyBaseUrl / showDemoWindow / thumbSize   // 模板里生成字段名表
define_static_array(enumerators_of(^^Color)).size() == 3    // 枚举值枚举
template for + splice 输出 -> {"themeId": "dark", "thumbSize": 512, "showDemoWindow": false}
运行时 for (auto m : kMembers) -> error: consteval-only variable '__for_range' not declared 'constexpr'
```

探测文件（`build/probe/` 下的 `p_lib.cpp`、`p_reflect*.cpp`、`p_stacktrace.cpp`、`p_refl_{a,b,c}.cpp`）已在探测后删除；
`CMakeLists.txt` 保留 `$<$<COMPILE_LANGUAGE:CXX>:-freflection>`。

命令：`C:/msys64/mingw64/bin/g++.exe -std=c++26 -fsyntax-only src/core/ProbeLang.cpp`
**把六项结果（通过 / 不通过 + 需要的编译选项）写回本表**，然后删掉探测文件。

### 13.3 硬性写法（写不出这些就是不合格代码）

| 场景 | 必须用 | 禁止 |
|------|--------|------|
| 只读字符串参数 | `std::string_view` | `const std::string&`、`const char*`（除非对接 C API） |
| 需要持有的字符串 | 成员存 `std::string`；返回按值 | 返回内部成员引用、把 `string_view` 存成成员（悬垂） |
| 只读缓冲 | `std::span<const std::byte>` / `std::span<const std::uint8_t>` | `const void*` + `size_t`、只为读而传 `const std::vector<T>&` |
| 可失败 | `std::expected<T, E>`；无附加信息用 `std::optional<T>` | `bool` + 出参、整数错误码、`throw` 做常规流程控制 |
| 查询/纯函数 | `[[nodiscard]]`；不会抛的加 `noexcept` | — |
| 枚举 | `enum class`（必要时 `using enum`）；位标志用显式掩码类型 | 裸 `enum` 当标志位 |
| 时间 | `std::chrono::steady_clock` / `seconds` / `milliseconds` | `clock()`、`GetTickCount` 裸算 |
| 字符串 ↔ 数值 | `std::from_chars` / `std::to_chars` | `atoi` / `atof` / `sprintf` |
| 格式化 | `fmt`（`log::Info("{}", x)`） | **`std::format`**（与已链入的 `FMT_HEADER_ONLY` + `SPDLOG_FMT_EXTERNAL` 冲突） |
| 遍历/查找 | `std::ranges`；索引确有必要时注明原因 | 无脑 `for (size_t i …)` |
| 编译期常量 | `constexpr` / `consteval` / `constinit` / `std::to_underlying` | `#define` 定义常量 |
| 模板约束 | `concepts` | SFINAE |
| 热路径回调 | 模板参数 / 函数指针；异步业务回调用 **`std::move_only_function`**（GCC 16 可用） | `std::function` 做热路径；禁止把已迁 `move_only` 的 `*Cb` 改回 `std::function` |
| 其他可用 | 结构化绑定、指定初始化 `Foo{.x=1}`、`if consteval`、`std::bit_cast`、`std::unreachable`、`[[likely]]`、`std::source_location`（日志封装） | `using namespace std;` |

**跨模块改造边界**：只现代化"本任务触碰到的文件"；调用点 > 5 处时，先加 `std::string_view` 重载或调用处写 `std::string(sv)` 显式转换，**不为了风格统一去改别人的模块**。

### 13.4 本文各任务接口的现代化状态

下列任务的接口已在本文中按 §13.3 改写（可直接照抄）：

| 任务 | 已现代化的关键签名 |
|------|-------------------|
| P3.0 | `OnMessage(std::string_view)`、`NormalizeBaseUrl/BuildApiUrl/BuildWebSocketUrl(std::string_view)`、`[[nodiscard]] SecondsSinceLastEvent()` |
| P3.1 | `[[nodiscard]] ParseNodeDefs(std::string_view)`、`FindNodeDef(std::string_view)` |
| P3.2 | `WidgetValue(std::string_view)`、`ClassType()` noexcept |
| P3.3 | `RegisterComfyNodes(std::span<const NodeTypeDef>)`、`IsRegistered(std::string_view)` |
| P3.4 | `[[nodiscard]] CompileToApiJson()`、`EnumerateLinks()` 用 `std::span` |
| P4.1 | `Upload(..., std::span<const std::byte>)` 返回 `std::expected<GpuTextureHandle, GpuError>`、`HttpDownloadBinary(std::string_view)` 返回 `std::expected<std::string, HttpError>` |
| P4.2 | `Refresh(std::size_t)`、`Items()` 返回 `std::span<const MediaItem>`、`ClassifyMediaKind(std::string_view)` |
| P5.2 | `Resolve(const ResolveRequest&)`、请求内只读数组用 `std::span<const std::string>` |
| P6.1 / P6.3 | `BasePixels()/MaskPixels()` 返回 `std::span`；`EncodePng/DecodePng` 返回 `std::expected` |
| P7.1 / P7.2 | `Call(std::string_view, std::string_view)` 返回 `std::expected`；`Start(...)` 返回 `std::expected<void, std::string>` |

> 其余任务（P3.5、P3.6、P4.3–P4.5、P5.1、P5.3–P5.7、P6.2、P6.4、P7.3–P7.5、P8.x）的签名同样按 §13.3 改写后再落地，
> 文档里没逐条重写的地方**以本节为准**。

### 13.5 例外清单（API 边界，保持原样；P2.9 只在这里"不改"）

这些位置**不是风格问题，而是被外部 API 的签名决定的**。处理方式是"边界处显式转换一次"，**不要为它们造包装层，也不要改 `third/`**：

| # | 边界 | 保持原样的东西 | 正确做法 |
|---|------|----------------|----------|
| 1 | **libhv HTTP client** | `http_client_send(req, resp)`、`HttpRequest::url`、`HttpResponse::body`（C++ 封装接受 `std::string`） | 入参用 `string_view`，调 libhv 前 `req.url = std::string(sv)`；响应用 `.body` 取出后按 `string_view` 传递 |
| 2 | **libhv WebSocketClient** | `hv::WebSocketClient::onmessage` 回调签名 `void(const std::string&)`、`SetPingInterval` 等 | 回调内部立刻转 `OnMessage(std::string_view{msg})`，不改 libhv |
| 3 | **libhv HTTP Server**（P7.2 新增） | `http_server_t` / `hloop` / 路由回调的 C 风格签名 | 在我们的 `HttpServer::Route` 里做一次转换，`Request::path/query` 用 `string_view` 视图指向 libhv 的缓冲 |
| 4 | **VNS**（VisualNodeSystem） | `NodeFactory::RegisterNodeType(const std::string&, …)`、`Node::SetName(std::string)`、`NodeArea::*`、`NodeSocket` 构造 | 调用处写 `std::string(sv)`；**不修改 `third/VisualNodeSystem`** |
| 5 | **ImGui** | `ImGui::Begin(const char*)`、`InputText`、`MenuItem` 等 C 字符串接口 | 我们的 UI 层文案保持字面量或 `std::string` + `.c_str()` |
| 6 | **Win32 / DX11 / COM** | `CreateWindowW`、`ShellExecuteW`、`IFileOpenDialog`、`D3D11CreateDevice`、`IStream` | `W` 系列保持 UTF-16 传参；路径用 `std::filesystem::path::native()`／已有的宽窄转换工具 |
| 6b | **Win32 A 版 API 的返回文本**（`FormatMessageA`、CRT `strerror`、第三方库内部用 A 版取的错误串） | 返回 `const char*` 的调用本身 | ⚠️ **它的编码是"当前 ANSI 代码页"（本机 `GetACP()==936`/GBK），不是 UTF-8**！ 必须 `util::AcpToUtf8()` 转一次再进 `std::string`，或改用 **W 版 + `util::Utf16ToUtf8()`**（`util::WinErrorMessage()` 已封装）。**实测事故**：`http_client_strerror()`→`socket_strerror()`→`FormatMessageA` 的 GBK 字节被塞进 UTF-8 串，ImGui 把每个坏字节渲染成一个 `?`，UI 上显示「网络错误: ?????????」（实为 GBK「函数不正确。 」9 字节） |
| 6c | **文件路径与文件打开** | `std::filesystem::path` 本身（它是 UTF-16，是对的） | **路径一律用 `path` 传递**：UTF-8 字符串 ↔ 路径走 `util::PathFromUtf8 / PathToUtf8`（❌ 别写 `path{std::string}`、别用 `path.string()`）；打开文件走 `util::OpenInput/OpenOutput/ReadFileBytes/WriteFileBytes`（❌ 别用 `std::ifstream(std::string)` / `fopen`）。**实测事故**：`std::string(w.begin(), w.end())` 拼设置路径 + `path.string()` 探针 → 中文目录下设置路径报废、图库一张图都扫不出来。第三方库只给窄路径 API 时（如 VNS 的 `LoadFromFile`），改成"我们自己用 `util` 读字节 → 调它的 `LoadFromJson`/`SaveToJson`" |
| 7 | **yyjson** | `yyjson_read(const char*, size_t)`、`yyjson_mut_obj_add_strcpy(…, const char*)` | 传 `sv.data(), sv.size()`；写回时 `.c_str()` |
| 8 | **imgui_impl_dx11** | `ImTextureID`（= `ImU64`）与 `ImTextureRef` 的强转 | 我们的 `GpuTexture::imgui_id()` 内部转换，对外仍是 `ImTextureID` |

**判定规则**：如果某个 `const std::string&` 出现在"我们的代码直接对接上述外部 API"的那一层，**它就算合规**，只要：
① 该函数用 `[[nodiscard]]`/`noexcept` 标注到位；② 在其上一层（业务层）已经用 `string_view`。

**P2.9 完成时的收尾动作**：把仍然存在的 `const std::string&` 逐条列出来（文件:行 + 属于上面哪一条例外），写回本节末尾作为记录。
如果某条无法归入任何例外，就说明**没改干净**，必须改掉。

**P2.9 收尾记录（2026-09-16，逐条对照）**

| 文件:行 | 内容 | 归属例外 |
|---------|------|----------|
| `src/comfy/ComfySession.h:22` / `.cpp:82` | `const std::string& BaseUrl() const` | 例外 5（下游是 ImGui `%s`/`.c_str()`，需 NUL 结尾） |
| `src/comfy/ComfySession.h:23` / `.cpp:83` | `const std::string& ClientId() const` | 例外 5（同上） |
| `src/comfy/ComfySocket.cpp:152` | `client->onmessage = [this](const std::string& msg) { OnMessage(msg); };` | 例外 2（libhv `WebSocketClient::onmessage` 固定签名；内部已转 `OnMessage(string_view)`） |
| `src/comfy/ComfySession.h:21` | 注释文字（非代码） | — |

合计 **5 处代码**，全部可归入例外，无"没改干净"的项。

**例外 9：存量结果结构体的 `ok/error` 契约（不动形状）**

`QueueResult` / `ObjectInfoResult` / `HistoryResult` / `SystemStatsResult` / `PromptSubmitResult` / `OperationResult` 里的 `bool ok; std::string error;`
是跨模块公开契约（`App.cpp`、`ComfyQueueModel`、`ComfySession` 都在读 `r.ok`），
且 5 个 `Parse*Json(json, out)` 的返回值与 `out.ok` 语义重叠。
把它们改成 `std::expected<T, E>` 会同时牵动 6 个结构体与全部读取点 —— 属于"为风格统一而大改"，**不做**。

`std::expected` 的落点因此在**新 API**（P3.0 起）：`DecodeError` / `GpuError` / `HttpError` / `CompileError` 等新错误类型一律用 `expected` 返回。
`std::span` 的落点：P4.1 的 `GpuTextureManager::Upload`、GALLERY S1 的 `Image` 像素视图、P6.3 的 PNG 编解码缓冲。

---

### 13.6 反射序列化写法（GCC 16.1.0 实测可用，照抄这个骨架）

> **已落地成现成实现：`src/util/Reflect.h`** —— 直接用，不要各模块再抄一遍：
> `util::reflect::ToJsonString(v)` / `FromJsonString(text, v)` / `WriteObject` / `ReadObject` /
> `FieldNames<T>()`（编译期字段名表）/ `FieldCount<T>()` / `TypeName<T>()`。
> 支持类型：`std::string`、`std::string_view`、`bool`、整型、浮点、`enum class`（存整数）、顺序容器（`vector<string>` 等 → JSON 数组）；
> **不支持的类型会在编译期 `static_assert` 报错**，不会静默丢字段。
> 已在用：`AppSettings`（`src/core/Settings.cpp` 的 Load/Save 已改为反射，删掉 ~40 行手写 yyjson 样板）。
> 下面的骨架保留为原理说明与"要自己扩展时"的参考。

**前提**：`CMakeLists.txt` 已加 `$<$<COMPILE_LANGUAGE:CXX>:-freflection>`；用到反射的源文件要 `#include <meta>`。

**实测能力边界**

| 用法 | 结果 |
|------|------|
| `std::define_static_array(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current()))` | ✅ 编译期常量数组（`static_assert` 通过） |
| `std::define_static_array(std::meta::enumerators_of(^^E))` | ✅ 枚举值数组 |
| `std::meta::identifier_of(m)` | ✅ constexpr `std::string_view` 字段名 |
| `obj.[: m :]`（splice） | ✅ 读取成员值 |
| `template for (constexpr auto m : …)` + `if constexpr` 分派 | ✅ 实测产出合法 JSON |
| 运行时 `for (auto m : kMembers)` | ❌ `consteval-only variable … not declared constexpr` |

**为什么"枚举必须编译期展开"**

`std::meta::info` 是 **consteval-only 类型**：它只允许存在于编译期求值过程中。
`for (auto m : kMembers)` 会被编译器展开成大致这样：

```cpp
auto&& __for_range = kMembers;   // 运行时变量，要在运行时"持有" info
auto __for_begin = std::ranges::begin(__for_range);
auto __for_end   = std::ranges::end(__for_range);
for (; __for_begin != __for_end; ++__for_begin) { auto m = *__for_begin; /* ... */ }
```

`__for_range` / `__for_begin` / `__for_end` / `m` 全是**运行时变量**，都需要在运行时存储 `info`，
而"第 2 个成员"这种编译期实体在运行时并不存在 —— 所以编译器直接拒绝。
（报错里那句 `add 'constexpr'` 只是机械提示，你不可能真加，因为你要的正是运行时循环。）

`template for` 是**展开语句**：循环体在编译期被摊开成 N 段独立代码，每个迭代各有自己的 `constexpr auto m`，
运行时不存在任何持有 `info` 的变量 —— 因此 `identifier_of(m)` 与 splice `obj.[:m:]` 都能用。
另外 `define_static_array` 的作用是把 consteval 返回的 **range**（`std::vector<meta::info>`）物化成静态数组，
元素仍是 consteval-only，所以它能在 `static_assert` / `template for` 里用，却依旧不能在运行时循环里用。

**要在运行时循环就给个"桥"**

把 `info` 换成**普通数据**再带出去：

```cpp
// ① 编译期生成字段名表（元素是普通的 std::string_view），运行时随便循环
template <class T>
consteval auto FieldNames() {
    constexpr auto ms = FieldInfos<T>();
    std::array<std::string_view, ms.size()> out{};
    for (std::size_t i = 0; i < ms.size(); ++i) out[i] = std::meta::identifier_of(ms[i]);
    return out;
}

// ② 编译期展开成运行时容器（序列化 / 表格 / 调试面板最实用）
template <class T>
[[nodiscard]] std::vector<std::pair<std::string_view, std::string>> FieldRows(const T& obj) {
    std::vector<std::pair<std::string_view, std::string>> rows;
    rows.reserve(FieldInfos<T>().size());
    template for (constexpr auto m : FieldInfos<T>()) {
        rows.emplace_back(std::meta::identifier_of(m), ToDisplay(obj.[: m :])); // ToDisplay 自己写类型分派
    }
    return rows;
}
```

**一句话记忆**：`info` 只能活在编译期（`consteval` 函数 / `static_assert` / `template for`）；
只有从 `info` 里提取出来的**普通数据**（`string_view`、数值）才能带到运行时。

**标准骨架**

```cpp
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>
#include <yyjson.h>

namespace shine::reflect {

// 字段表：编译期生成，可放进模板 / static_assert
template <class T>
consteval auto FieldInfos() {
    return std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current()));
}

// 写一个对象的字段到 yyjson 对象；新类型在这里加 if constexpr 分支
template <class T>
void WriteObject(yyjson_mut_doc* doc, yyjson_mut_val* obj, const T& v) {
    template for (constexpr auto m : FieldInfos<T>()) {
        constexpr std::string_view key = std::meta::identifier_of(m);
        const auto& value = v.[: m :];
        using V = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<V, std::string>) {
            yyjson_mut_obj_add_strncpy(doc, obj, key.data(), value.c_str(), key.size());
        } else if constexpr (std::is_same_v<V, bool>) {
            yyjson_mut_obj_add_bool(doc, obj, key.data(), value);
        } else if constexpr (std::is_arithmetic_v<V>) {
            yyjson_mut_obj_add_real(doc, obj, key.data(), static_cast<double>(value));
        }
        // 枚举 / vector / 嵌套结构：继续加分支，不要指望递归魔法
    }
}

// 读回：同样的骨架 + yyjson_obj_get(root, key.data())
template <class T>
void ReadObject(yyjson_val* root, T& v) {
    template for (constexpr auto m : FieldInfos<T>()) {
        constexpr std::string_view key = std::meta::identifier_of(m);
        auto& value = v.[: m :];
        using V = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<V, std::string>) {
            if (auto* j = yyjson_obj_get(root, key.data()); j && yyjson_is_str(j)) value = yyjson_get_str(j);
        } else if constexpr (std::is_arithmetic_v<V>) {
            if (auto* j = yyjson_obj_get(root, key.data()); j && yyjson_is_num(j)) value = static_cast<V>(yyjson_get_num(j));
        }
    }
}
} // namespace shine::reflect
```

**适用对象**：`AppSettings`（P8.1）、`comfy::NodeTypeDef`（P3.1）、`video::Shot` / `video::VideoProject`（P5.1）。
**注意**

- 反射只产出**字段名与类型**，不负责 JSON 结构；嵌套 / `vector` / `optional` 要显式写分支。
- 成员顺序 = 声明顺序（稳定），但**不要**把它当对外格式契约。
- 运行时迭代不可用 → 不要写"运行时遍历字段表增删"的代码。
- 字段改名会同时改 JSON 键名 → 存盘格式要版本字段（`"schemaVersion"`）兜底。

---

### 13.7 工具库 `src/util/` 约定

**放什么**：无状态、无业务语义的纯函数（随机、字符串、时间、JSON 只读）。
**不放什么**：模块状态与单例、Comfy / 图 / 图库 / 视频的业务逻辑、需要注册进 UI 的东西 —— 那些留在各自模块。

| 头文件 | 现成能力 |
|--------|----------|
| `util/Random.h` | `RandomHex(bytes)`、`RandomId32()`（= ComfyUI `client_id`/`prompt_id` 的形式）、`RandomBelow(n)`、`RandomSeed()` |
| `util/Strings.h` | `Trim`、`TrimEnd`、`ToLower`、`ToUpper`、`StartsWith`、`EndsWith`、`EndsWithNoCase`、`Split`、`ToInt` / `ToDouble` / `FromDouble` |
| `util/Time.h` | `NowMillis()`（墙上时间）、`MonotonicMillis()`（单调，用于超时与"忙碌/卡死"判定）、`ElapsedMillis()`、`FormatDurationMs()` |
| `util/Json.h` | `json::Get / GetStr / GetStrCopy / GetI64 / GetInt / GetF64 / GetBool / GetObj / GetArr / HasKey`（yyjson 只读助手，宽容取默认值） |
| `util/Reflect.h` | **反射序列化**：`reflect::ToJsonString(v)` / `FromJsonString(text, v)` / `WriteObject` / `ReadObject` / `FieldNames<T>()` / `FieldCount<T>()` / `TypeName<T>()`（详见 §13.6；需 `-freflection`） |
| `util/Encoding.h` | **编码与路径唯一入口**：`Utf16ToUtf8` / `Utf8ToUtf16` / `AcpToUtf8` / `Utf8ToAcp` / `WinErrorMessage(code)`（**W 版**系统消息）/ **`PathFromUtf8` / `PathToUtf8` / `FileNameToUtf8`**（UTF-8 ⇄ `std::filesystem::path`）。**Win32 A 版 API 的返回文本、所有路径字符串进出都必须过这里**，见 §13.5 例外 6b |
| `util/File.h` | **文件读写（宽字符路径安全）**：`OpenInput(path)` / `OpenOutput(path)` / `ReadFileBytes(path) → optional<string>` / `WriteFileBytes(path, sv)`。**一律传 `std::filesystem::path`**（窄字符串打开在中文路径下必失败） |

**规则**

- 全部 **header-only（`inline`）**：不新增 `.cpp`、不用改 `CMakeLists.txt`；`src` 已在 include 路径里，直接 `#include "util/Strings.h"`。
- **优先复用**：不要在各模块再写一份局部 `Lower` / `RandomHex` / 手写 trim / `yyjson_obj_get` 断言样板。
- 新函数要带 `[[nodiscard]]` / `noexcept`，参数用 `string_view` / `span`，能 `constexpr` 就 `constexpr`。
- 时间一律走 `std::chrono`（已由 `util/Time.h` 封装），禁止 `clock()` / `GetTickCount` 裸算。
- 数值 ↔ 字符串一律用 `from_chars` / `to_chars`（`util/Strings.h` 已封装），禁止 `atoi` / `sprintf`。

**已迁移到 util 的实现（不要再写第二份）**

- `ComfyTypes.cpp`：原局部 `RandomHex` → `util::RandomId32()`；`NormalizeBaseUrl` 的裁剪 → `util::TrimEnd`。
- `ComfyClient.cpp`：原局部 `Lower` → `util::ToLower`；`ReadStr` 的查键改成 `util::json::Get`（保留"数值转字符串"的原有行为）；`ClassifyMediaKind` 的后缀比较 → `util::EndsWith`。


