# third/VisualNodeSystem 的本地补丁

这份记录只为一件事服务：**让被 vendored 进来的上游源码能在本工程的工具链上编译**。
每条都写清楚「上游原样是什么 / 为什么必须改 / 改的行为是否等价 / 怎么撤销」。

本工程工具链是 **MSYS2 MinGW64 / GCC**（`C:/msys64/mingw64`），而
VisualNodeSystem 上游以 **MSVC** 为主。有些在 MSVC 下合法的写法，GCC 直接判错。

改动纪律：
- **只允许改到「不改就编译不过」的程度**，且必须是语义等价的重命名 / 补头路径。
- 每处改动在源码里留 `⚠️ 本地补丁（见本文件第 N 条）` 的注释，指向这里。
- 不在 `third/` 里加本工程自己的功能代码。
- 升级上游版本时，逐条对照本文件确认这些补丁是否还需要。

---

## 1. `VisualNodeSystem.cpp` — 变量遮蔽，GCC 判 redeclaration

**位置**：`NodeSystem::UnlinkNodeAreas` 第二个循环（`VisualNodeSystem.cpp:1206-1223`）

**上游原样**：

```cpp
for (const auto& RecordID : RecordIDsToRemove)
{
    auto LinkRecordIterator = NodeAreaLinkRecords.find(RecordID);
    ...
    NodeAreaLinkRecord Record = LinkRecordIterator->second;
    std::string RecordID = LinkRecordIterator->first;   // ← 与循环变量同名
    ...
    DeleteLinkRecord(RecordID);
}
```

**为什么必须改**：GCC 对 range-based `for` 的循环变量与循环体里同名的变量报
`error: redeclaration of ...`。MSVC 接受。本工程用 GCC，所以不改就是
**整个 `shine_vns` 目标编译失败**，`ShineTVStudio` 连链都链不上。

实测报错：

```
VisualNodeSystem.cpp:1214:29: error: redeclaration of 'std::string RecordID'
VisualNodeSystem.cpp:1206:26: note: 'const std::__cxx11::basic_string<char>& RecordID' previously declared here
```

**改成**：内层那个改名为 `RecordIDCopy`，调用点同步改。

**语义是否等价**：等价。那行本来只是把 `map` 的 key（`const std::string&`）
拷一份**可修改**的 `std::string` 出来，因为 `DeleteLinkRecord(RecordID)` 要按值
接收。改名不影响读取的 key、不影响 `Record`、不影响删除顺序。

**怎么撤销**：把 `RecordIDCopy` 改回 `RecordID`（在 MSVC 下能编过）。

---

## 2. jsoncpp 头文件搜索路径（**不改源码，只改 CMake**）

**位置**：根 `CMakeLists.txt` 的 `shine_vns` 目标。

**上游原样**：`third/VisualNodeSystem/ThirdParty/jsoncpp/json_tool.h:10` 写的是
`#include <json/config.h>`（尖括号、带 `json/` 前缀），而 VNS 自己的源码写的是
`#include "jsoncpp/json/json.h"`（从 `ThirdParty/` 起算）。**两种基准不一样。**

**为什么必须改**：`target_include_directories` 少了
`ThirdParty/jsoncpp` 这一条，`json_reader.cpp` 就报
`fatal error: json/config.h: No such file or directory`。

**改成**：`shine_vns` 的 include 路径同时给出

- `ThirdParty/`（给 `"jsoncpp/json/json.h"`）
- `ThirdParty/jsoncpp`（给 `<json/config.h>`）
- `ThirdParty/glm`

**语义是否等价**：只加搜索路径，没有一行源码改动。

**怎么撤销**：不需要，这是本工程 CMake 侧的配置。

---

## 已知但**故意不修**的东西

| 现象 | 处置 |
|---|---|
| `VisualNodeCore.h:11` 重新定义了 `IMGUI_DEFINE_MATH_OPERATORS`，而这个宏也由 `shine_imgui_third` 通过 PUBLIC 传进来，编译时有一条 macro redefinition **警告** | 保留。两个定义的内容相同、且 VisualNodeCore.h 自带 `#define`，去掉任何一边都可能让别的 TU 少算运算符。警告无害，不为消警告而改上游。 |
| `third/VisualNodeSystem/build/` 下有一份上游自带的 Windows 构建产物（`.vcxproj` / `CMakeCache.txt` 等） | 不参与本工程构建。上游 `.gitignore` 已覆盖 `CMakeFiles/`、`*.vcxproj`、`CMakeCache.txt`，不会被提交。 |
