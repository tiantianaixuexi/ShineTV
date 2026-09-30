---
id: root.agents
kind: instruction
status: current
scope: repository
source_of_truth:
  - README.md
  - Build.bat
  - CMakeLists.txt
  - src/
  - tools/
last_verified: 2026-09-30
---

# ShineTV Studio AI 协作规则

> **现状（2026-09-30）**：Qt → Dear ImGui 前端重构已完成并全量验收，配过与构建全程零 Qt 依赖。
> 过程文档（`refactor/`）、Qt 时代文档树（`docs/`）、设计草稿（`design/`）与取证临时脚本（`out/`）
> 已随一次仓库瘦身删除（提交 `ddab612`）。**历史只在 git 里**：`git show fa4f5d4:refactor/PROGRESS.md`。
> 本文件描述的是**现状**，不是待办。

## 开始工作前

1. 读根目录 `README.md` 与本文件。不要从历史计划推断当前实现。
2. 先检查工作区已有改动。unexpected changes 属于用户，不能回滚、覆盖或顺手整理。
3. 用源码符号和路径定位事实。文档只做导航；源码、CMake 和可执行检查优先。
4. 本仓库**不维护**开发计划、进度表或阶段交接文档。决策写进源码注释或提交信息，
   不要新造计划壳 —— 计划壳会变成下一次「照着计划改已经对了的代码」的源头。
5. 已知这份规则里**没有**的东西：1:1 视觉规范与 webui 对照表。`webui/` 只存在于本地、
   **不在版本控制里**，新 clone 拿不到；重做视觉比对前先自己确认基线从哪来。
6. **已知仓库内还有一批失效引用**（删 `docs/` 与 `refactor/` 的遗留，还没清）：
   `src/**` 注释里 76 处 / 56 个文件指向已删的 `docs/**` 与 `refactor/*`，
   `.mimocode/skills/` 下 8 个技能的开篇「先读：…」全部指向 `docs/**`，
   `README.md` 16 处（且仍在讲 P7 已删掉的 `shine_kit`）。
   它们多数是「**规则见** 某文档」这种指向权威的引用 —— 删掉指针会连理由一起删掉，
   所以要清就得先决定那些规则搬到哪儿，**不要顺手批量删注释**。
   规则原文仍在 git 里（删除前的最后状态是 `ddab612^`）。

## 构建

- **一键**：根目录 `Build.bat`

  | 命令 | 配置 | 产物 |
  |---|---|---|
  | `Build.bat` | Debug（默认） | `build\debug\ShineTVStudio.exe` |
  | `Build.bat --release` | Release | `build\release\ShineTVStudio.exe` |
  | `Build.bat --clean` | 先删该 build 目录再编 | 同上 |
  | `Build.bat --help` | 用法 | — |

- 两个配置**各用独立 build 目录**：`CMAKE_BUILD_TYPE` 是按 build 目录缓存的，
  塞同一个目录会导致每次切换配置把所有目标重编一遍。
- `Build.bat` 先检查 `SHINE_MINGW\bin\g++.exe`（默认 `C:\msys64\mingw64`，可用环境变量
  覆盖），找不到就 `exit 2`；非法参数也 `exit 2`。让 CMake 自己挑编译器会挑出一个
  「看起来能编过、但不是你以为的那个」的二进制。
- 手动等价命令：

  ```powershell
  cmake -S . -B build\release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
        -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe
  cmake --build build\release --parallel 8 --target ShineTVStudio
  ```

- 工具链：MSYS2 MinGW64 · **GCC 16.2.0** · `CMAKE_CXX_STANDARD 26`。
  ⚠️ **换编译器版本要清掉对应 build 目录**：`CMAKE_CXX_COMPILER_VERSION` 是缓存值，
  编译器路径没变时 CMake 不会重新探测，缓存会一直记着旧版本号；而 Makefile 生成器
  也不会因为换编译器就重编目标文件 —— 结果是一个「构建成功、但对象文件还是旧的」的产物。
- 实测体积：Release 约 **18.1 MB**，Debug 约 **255 MB**（含调试信息）。
- 打包：`scripts\package-imgui.ps1`。
- `Build.bat` 是**纯 ASCII** 的：`cmd.exe` 按控制台代码页解码 `.bat`，中文会变乱码，
  某些字节组合还会把后面的代码吃掉。改它时保持纯 ASCII。

## 修改边界

- `shine_core`（`src/core`、`src/util`、`src/net`、`src/db`、`src/llm`、`src/comfy`、
  `src/media`、`src/flow`、`src/visual`、`src/novel`、`src/paint`、`src/mcp`、`src/project`、
  `src/pipeline`、`src/gpu`）**不得包含 Qt 头，也不得包含 ImGui 头**；
  `tools/check-layers.ps1` 是门禁。
- 所有 UI 收敛在唯一根目录 `src/ui/`，现在**只有 `imgui/` 一棵树**：
  - `imgui/host/` 宿主与入口 · `imgui/theme/` 主题与字体 · `imgui/kit/` 组件套件 ·
    `imgui/pages/` 业务页 · `imgui/verify/` 取证 · `imgui/app/main.cpp` 属于 exe 不进静态库
  - `shine_imgui` 是 `GLOB_RECURSE CONFIGURE_DEPENDS "src/ui/imgui/*.cpp"`，
    **新增 .cpp 不用改 CMake**
  - 不要再引用 `SHINE_UI_QT`、`shine_kit`、`shine_qml` 或 `scripts/package-qt.ps1` —— 这些都没了
- **任何情况下不改 `shine_core`**：业务层零 Qt 已成立，需要接的只有 `gpu::AttachDevice()`
  这一处宿主注入。
- 网络、文件扫描、图片解码、LLM 请求、ComfyUI 提交/下载和生成编译放 worker；
  通过 `async::PostToUi` 投递结果。**UI 线程不做同步 IO / 同步 HTTP。**
- 复用现有基础设施：`shine::async`、`shine::log`、`AppSettings`、`net::HttpClient`、
  `util::Reflect`、SQLite/Comfy 适配层。新增第二份线程池、HTTP 客户端或 JSON 约定前
  先证明没有现成实现。
- 外部 C/C++ API（libhv、SQLite、Win32、yyjson）在边界转换一次；业务层接口遵循项目现有
  C++ 约定，不为风格统一改动无关调用点。

### 源码编码

- `src/ui/imgui/**` 全树是**无 BOM 的 UTF-8**。读写它只用 `read` / `edit` / `write` 工具。
- **不要**用 PowerShell 的 `Set-Content` / `Out-File` / `[System.IO.File]::WriteAllText` 改源码：
  它们按系统 ANSI 码页（中文 Windows = GBK/936）重编码，会静默把 `⚠️`、`→` 这类 GBK 编不了的
  字符换成 `?`，编译照过、中文照在，但注释里的标记没了。必须用脚本改字节时，全程只走
  `ReadAllBytes` / `WriteAllBytes` 的纯字节替换，不经过任何编码器。
- 输出缓冲与输入游标要**独立推进**（读到输入的每一段就 append 到输出）。共用游标时，
  「替换完把游标跳过差值」一旦算错就会吞掉紧跟标识符的那一个字节。
- `.ps1` 一律带 UTF-8 BOM：PowerShell 5.1 对无 BOM 文件按 ANSI 码页解码，中文注释被解成乱码，
  某些字节序列恰好凑出引号 / 括号，报 `ParserError` 而**行号与真实原因毫无关系**。
  判据：`[System.Management.Automation.Language.Parser]::ParseInput([IO.File]::ReadAllText($f), …)`
  报零错而 `powershell -File $f` 报 ParseError ⇒ 就是 BOM 问题。
- 判别编码别看「高位字节里 0xC2-0xDF 多还是 0x81-0xFE 多」——GBK 汉字首字节本来就落在
  0xB0-0xF7，这个方法会把 UTF-8 文件误判成 GBK。用严格解码试一次：

  ```powershell
  $s = New-Object System.Text.UTF8Encoding($false,$true)
  try { $null = $s.GetString([System.IO.File]::ReadAllBytes($f)); "$f UTF-8" } catch { "$f 非 UTF-8" }
  ```

## 验证顺序

1. **构建失败就别接着跑取证**。改了头文件成员名之类的东西（`ImVector::Size` 在新版是字段
   不是方法）会让编译挂掉，而取证脚本照样跑出一份**旧二进制**的图 —— 数字看着正常，结论全错。
   判据只认「构建成功**且**取证 PASS」，构建那一步的退出码要单独判。
2. C++ 变更：先跑相关门禁/扫描器，再 `Build.bat`（或 `--release`），最后按变更路径启动程序。
3. UI 变更：实际启动 exe 走对应工作区，截图验收用 `scripts\capture_window.ps1`，
   **不要只看编译结果**。
4. 外部服务变更：先做离线 mock/协议自检，再在 ComfyUI 或 LLM 可用时联调；
   报告未执行的部分，**不把推断写成通过**。
5. 取证（多图）：跑完**先排 md5** —— 任意两张逐字节相同 = 有一张没拍到它承诺的状态。
   等待结果要写进 manifest，别丢返回值；每张图的前置动作显式写全，别依赖
   「默认状态恰好是我要的」。
6. 像素差**不是**内容指标：离屏渲染跨进程约 2000px 噪声、跨构建约 40000px。小于它不必当回事，
   **大于它也不能**直接判成回归。判「内容有没有丢」用同一次运行内的对照页 +
   无文字纯色区域的精确相同率。同一二进制连跑两轮，79 张里通常只有 14~15 张 md5 相同
   ⇒ **md5 逐张比对在跨运行场景下不是可用信号**，别拿它当等值证明。
7. 门禁与扫描器**改完先跑自检**。一个「改完报 0」的扫描器和一个坏掉的没区别 —— 门禁报 PASS 与
   「没有违规」在结果里长得一模一样，而假绿比没有门禁更坏。
8. 判据**自己也会骗人**：「报通过的原因」若与「它本该抓的缺陷」同形，它比没有判据更坏。
   改完门禁要做**反向验证**：故意把修复关掉重跑，那条判据必须变红。
9. 「覆盖了 N 个工作区」不等于「覆盖了每个视图面」；fixture 缺数据也会造成覆盖洞 ——
   **页面拍到了，分支没跑到**。

### ⚠️ 取证基线已经不完整

`novel.db` fixture 的播种链（`out/_make_fixture.ps1` + `out/_fixture_seed.sql`）随 `out/`
一起删除了。没有它，novel.db 快照判据会 TIMEOUT，侧栏 / 检查器 / 资产树全部退化成诚实空态，
而只看「图写出来了」的门禁照样 PASS —— 2026-09-30 实测过：某轮 `duplicate-hits` 恒为 0
正是因为页面是空的，**那个指标一直在测空气**。重建取证基线前先把 fixture 补回来。

## 门禁与静默失效扫描器

硬门禁（失败退出码 1）：

| 脚本 | 抓什么 | 自检 |
|---|---|---|
| `tools\check-layers.ps1` | 分层：`shine_core` 不得含 Qt / ImGui 头 | `-SelfTest` |
| `tools\check-theme.ps1` | 主题 token 与 JSON 一致 | 无 |
| `tools\check-colors.ps1` | 硬编码颜色字面量（豁免必须写 `// theme-ok <理由>`） | `-SelfTest` |
| `tools\check-i18n.ps1` | 文案 i18n 覆盖 | 无 |

人工复核（只打印疑似，**不改退出码**）：

| 脚本 | 抓什么 | 自检 |
|---|---|---|
| `tools\find-silent-truncation.ps1` | 列表按容器高度静默截断 | `-SelfTest` |
| `tools\find-draw-arg-order.ps1` | 绘制原语的**颜色 / 圆角**参数错位 | `-SelfTest` |
| `tools\find-rect-wh-misuse.ps1` | `kit::Rect` 四参当成 (x,y,w,h) | **无 `-SelfTest`**（带上会被静默忽略后照常报 `0`）；用 `-Root <样本目录>` |

三个扫描器抓的典型形态：

| 形态 | 症状 |
|---|---|
| `if (y + 24.0f > body.max.y) break;`（含 `> body.max.y - 20.0f` 这种「给页脚留位」的变体） | 列表按容器高度静默截断，用户看不到「还有第 N+1 条」 |
| `Rect{x, y, cardW, 192.0f}` ⇒ `max.x < min.x` | 控件**整块不画也不可点**，编译不报错 |
| `AddRectFilled(a, b, radius + grow, col)` | 颜色位收到几何量，**全透明黑、画了等于没画**，编译不报错也不崩 |

- 「不画」用 `kit::ColorTransparent()`，别写 `IM_COL32(0,0,0,0)` —— 它是硬编码颜色里最常见的一种，
  豁免一旦多了门禁就等于没有。
- `AddRectFilled` 的签名是 `(p_min, p_max, col, rounding)`，**颜色在圆角前面**。写反的三个信号
  全是骗人的：编译过、不崩、同一帧别的图元照常出图。判据是 `_VtxCurrentIdx` 有没有推进，
  或直接读渲染结果。
- ImGui 里**先注册者独占** `HoveredId`：同一矩形只能 `HitTest` 一次，浮层遮罩**只画不注册**
  （`kit::ScrimPaint`），否则遮罩会跟面板内容抢同一个 hover，症状是「面板里某些控件点不动」，
  而且**哪一处坏取决于绘制顺序**。
- child 的裁剪与层级按 **draw list** 生效，不按代码嵌套：浮层用 `GetForegroundDrawList()`；
  要建视口的区域整块改用容器自己的 `drawList()`。
- `kit::ScrollRegion` 不调 `setContentHeight()` 就是**滚不动**（不是「滚不顺」）；上报值恰好等于
  视口高等于滚不动。
- `ImGui::IsMouseHoveringRect` 在本工程会 **0xC0000005**，用 `Rect::contains(io.MousePos)` 手算。
- 合成鼠标位置必须走 `io.AddMousePosEvent()` 且排在**所有后端 `NewFrame` 之后**；直接写
  `io.MousePos` 字段活不过 `NewFrame`（win32 后端在窗口有焦点时一定塞真实光标），
  症状是「能不能复现取决于取证窗口当时有没有焦点」。

## 常用入口

- 构建：`Build.bat`（Debug 默认）/ `Build.bat --release`
- 门禁：`powershell -File tools\check-layers.ps1` / `check-colors.ps1` / `check-theme.ps1` / `check-i18n.ps1`
- 静默失效扫描：`powershell -File tools\find-silent-truncation.ps1` / `find-draw-arg-order.ps1` / `find-rect-wh-misuse.ps1`
- 截图取证：`scripts\capture_window.ps1` · 回归入口 `scripts\run_reviews.ps1`
- 运行辅助：`scripts\studio.ps1` · `scripts\comfy.ps1`
- 第三方库：`third/`（`imgui` `ImAnim` `VisualNodeSystem` `libhv` `sqlite` `yyjson` `spdlog` `glaze` `mimalloc` `stdexec` 等）
- 运行时数据：`runtime/`（**不入库**）
