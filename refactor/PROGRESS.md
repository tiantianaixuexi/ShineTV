---
id: refactor.progress
kind: status
status: current
source_of_truth:
  - refactor/phases.md
  - refactor/architecture.md
  - refactor/design-spec.md
  - CMakeLists.txt
  - src/ui/imgui/
last_verified: 2026-09-30
---

# 重构进度看板

> **⏹ 重构主干已完成（51/51）。本文现在的用途是「施工参照 + 护栏」，不是进度表。**
> **当前施工对象 = §「没做的」→「视觉 1:1 符合性审计」那张偏差表**（`design-spec.md` §9.3 会指过来）。
> 那张表每条都标了**归因层**（页面层 / 主题层 / 字体层）—— 改之前先看清是哪一层，
> 同样的现象在错层上改只会改成另一种错。
>
> ⚠️ 读本文的方式：**先读结论，再按它给的去处读源码注释**。机制的原文都在源码里，
> 本文只是索引；只读本文会得到「对但不够下刀」的信息。

> 最后更新：**2026-09-30**（HEAD `091ea9c`）。步骤定义见 [`phases.md`](phases.md)，每完成一步就勾上并补验证记录。
> **本文只留四样东西**：当前状态 · 下一步 · 没做的 · 容易再踩的坑。
> 逐轮的长篇过程已进 `git log`（`git log -p -- refactor/PROGRESS.md` 可查历史全文）；
> 结论背后的机制大部分写在**源码注释**里，下文每条都给了去处 —— 改代码前先读那处注释，别只读本文。

> ⚠️ **与 phases.md 的两处偏离**（用户明确要求，phases.md 未同步）：
> 1. 渲染后端用 **OpenGL**（`imgui_impl_opengl3` + `opengl32`），不是 D3D11。
>    `shine::gpu::AttachDevice` 注入的是自己解析的 GL 函数表（`host/Host.cpp`）。
> 2. P6.1 的抓图走 `glReadPixels`，不是 D3D11 后备缓冲。

图例：`[x]` 已完成并通过验收 · `[ ]` 未做（全部列在「没做的」里）

---

## 总览

| 阶段 | 名称 | 状态 | 进度 | 关键验收 |
|---|---|---|---|---|
| **P0** | 底座与门禁 | `[x]` | 4/4 | 能开一个 ImGui 空窗口 |
| **P1** | 运行时骨架 | `[x]` | 6/6 | `gpu::AttachDevice` 生效 |
| **P2** | 主题与字体 | `[x]` | 7/7 | **中文无豆腐块** ✅ |
| **P3** | 组件套件 | `[x]` | 7/7 | 组件画廊 5 主题对齐 |
| **P4** | 应用外壳 | `[x]` | 11/11 | 外壳逐项对齐 |
| **P5** | 六个工作区 | `[x]` | 6/6 | 6 页 × 5 主题对齐 |
| **P6** | 验收取证 | `[x]` | 3/3 | 总回归入口可跑 ✅ |
| **P7** | 收尾 | `[x]` | 7/7 | **不装 Qt 也能构建** ✅ |

**整体：51 / 51 步完成。** 重构主干已结束；剩下的是**产品缺口**，见「下一步」与「没做的」。

### 当前取证结论（2026-09-30 第四轮，实测）

```
# shots: 80  failed: 0  scroll-failed: 0  identical-theme-pairs: 0  identical-driven-pairs: 0
  inverted-rects: 0  duplicate-hits: 0  hover-probes: 11/11  shortcuts: 7/7  overlay-clicks: 7/7
  report-scan: converged  book-snapshot: converged  asset-snapshot: converged  artifact-snapshot: converged
overall=PASS          （manifest 的 overall ↔ 退出码一致：PASS↔0）
```

门禁四条全绿：`check-layers PASS(0)` · `check-colors PASS(24 acknowledged)` · `check-theme PASS(5)` ·
`check-i18n PASS`。三个扫描器各报 0（`find-silent-truncation` / `find-rect-wh-misuse` /
`find-draw-arg-order`），且 `-SelfTest` 五个全过。

⚠️ **`check-colors` 的 `7 → 24 acknowledged` 是第四轮关掉盲区的直接后果**（不是新写坏的）：
判定面变宽后，17 处以前看不见的字面量浮出来，逐条看过并各自给了理由或修掉。
拿第一二三轮的 `7 acknowledged` 当基线会以为退化了 —— 详情见「门禁自身的盲区」。

⚠️ **跑出这个 PASS 的前提是 `novel.db` fixture 在位**（见「视觉 1:1 符合性审计」末尾）。
本轮之前它被删掉了，于是同一份代码只能跑出 `9/11 + FAIL` ——
**判据的成立条件丢了，看起来和「产品退化了」一模一样。**

#### ⚠️ 这套截图**不可跨轮做逐字节比对**（2026-09-30 实测）

同一个二进制连跑两轮，80 张里有 **48 张**字节不同（时钟/动画相位/日志时间戳这些天然会变）。
所以：

- ❌ 「改完再截图、和上一轮 diff」**不是**一个可用判据 —— 48 张噪声会淹没真正的 1px 变化。
- ✅ 能用的是**同轮内的重样判据**（`identical-theme-pairs` / `identical-driven-pairs`）：
  同一轮里两张本该不同的图撞了就说明有一张没拍到它承诺的状态。
- ✅ 另一个可用技巧：取「两轮之间**稳定**的那 32 张」做子集，再跨轮比 —— 但只适合
  无损重构（本轮用它在纯重构阶段验到 **32/32 未变**），不适合像素本来就该变的改动。
- 要真正做跨轮像素回归，得先把不稳定的来源（时钟/日志）钉死，那是另一件事。

---

## 基线（重构开始前已实测）

| 项 | 值 |
|---|---|
| 基线 exe 体积 | **392,330,166 B**（Qt 前端） |
| 工具链 | MSYS2 MinGW64（`C:/msys64/mingw64`），**GCC 16.2.0**（`mingw-w64-x86_64-gcc 16.2.0-4`，2026-09-30 从 16.1.0-2 升上来），`CMAKE_CXX_STANDARD 26` |
| ImGui 版本 | 1.93.0 WIP（`IMGUI_VERSION_NUM 19297`） |
| 已删的 Qt 树 | `src/ui/{app,kit,layout,pages,qml,verify}` 共 **198 个文件** |

---

## 关键指标

| 指标 | 基线 | 目标 | 实测 |
|---|---|---|---|
| exe 体积（未裁剪） | 392,330,166 B | — | **290,667,311 B**（2026-09-30，GCC 16.2 全量重建实测；P7.7 当时 252,530,560 B，16.1 时代复测 290,689,357 B） |
| **exe 体积（strip 后）** | — | **< 30 MB** | **13,406,208 B = 12.8 MB** ✅（2026-09-30 对 16.2 二进制复测；P7.7 当时 12,435,968 B） |
| `src/` 中 Qt 头引用 | 165 个 UI 文件 | **0** | **0**（门禁零豁免仍 PASS） |
| QML 文件 | 52 | **0** | **0** |
| `src/ui/` 下的前端树 | 6 棵 | **1 棵** | **1 棵（`imgui/`）** |
| 构建是否需要 Qt | 是 | **否** | **否**（全新 `build-p7` 目录零 Qt 配过 + 构建 + 取证通过） |
| `check-layers` | PASS | PASS | **PASS (0 violations)** |
| 字体图集 | — | < 64 MB（**自定红线**，不是显存） | **7.8 MB**（`RasterizerDensity=2.0` 后；统计读 `GetSizeInBytes()`） |
| `src/ui/imgui` 文件数 | 45 | — | **132**（72 `.cpp` / 55 `.h`），最大 `.cpp` **818 行** |
| 取证图组数 | 17 | 35+ | **79–80**（见「判据清单」里的最新一轮读数） |

---

## 风险状态

| # | 风险 | 状态 | 备注 |
|---|---|---|---|
| R1 | 中文字体 / TTC / 图集 | **已解决** | 1.92 按需加载 + `RasterizerDensity=2.0`；图集 7.8 MB，无豆腐块 |
| R2 | 门禁自拦 | **已解决** | PASS (0 violations) |
| R3 | `FlowCanvas` | **已解决** | 见 P3.5 三处踩坑 |
| R4 | 毛玻璃无等价 | **已接受降级** | 不追求像素一致 |
| R5 | box-shadow 无等价 | **已解决** | 2026-09-30 已按 CSS 逐条接投影档（`ShadowTier` + `kShadowSpecs`） |
| R6 | `color-mix` 无合成 | **已解决** | `Derived` 预混 + `MixSrgb` 实色混合 |
| R7 | 取证代码全废 | **已解决** | 79–80 张 / 判据齐 |
| R8 | 纹理异步未就绪 | **界面侧已接**（2026-09-30） | 新增 `kit/Image.{h,cpp}`：路径 → 纹理 → 画进矩形，走的是**已有的** `gallery::ThumbnailService`（worker 解码 + CPU/GPU 两级 LRU）。接入时挖出两个真缺陷，见下节 |

#### R8 接线时挖出的两个真缺陷（都属「脚本说通了、其实没接」）

| # | 缺陷 | 为什么一直没人发现 |
|---|---|---|
| 1 | **`gallery::RegisterBuiltinDecoders()` 全仓无人调用** | 它自己的头文件（`media/ImageLoader.h:33`）写着「启动时调用一次（幂等）」，而应用从来没调。于是解码器登记表是空的，`ImageLoader::Load` 对**每一个文件**都返回 `DecodeError::Unsupported` —— 界面上是「不支持的图片格式」。**那句错话误导性极强：不是某个格式不支持，是整条图片链压根没接通。** 没被发现的原因：设计稿里的「图」全是 `kit::Art` 程序化占位（`kit/Views.h:18-19` 明说「不需要任何图片资源」），界面侧从来没有请求过任何文件 ⇒ 这张空表谁也没撞上。已修：`AppEntry.cpp` 在 `gallery::Init()` 之前注册 |
| 2 | **fixture 里的 `output/*.png` 是文本桩**（后缀 `.png`、内容 `"not-a-real-png-fixture"`），而 `visual_assets.sheet_rel_path` 指向的两张图**根本不存在** | 于是「界面能不能显示真实设定图」这条链在取证里**从来没有输入** —— 它报红还是报绿，都跟链子通没通无关。已修：fixture 用工程自带的 `paint::EncodePng` 造两张**纯色真 PNG**（`#FF2D55` / `#7CFF00`，刻意选在五套主题调色板之外），取证的像素计数可直接判 |
| 3 | **`RequestImage` 里写成了「只在 `State == NotLoaded` 时才 `Request`」** | 头文件第 1 条约束**明写着要每帧无条件调**，并写清了违反它的后果（「纹理一旦被淘汰这张图就**永远不再出现**」）；`.cpp` 里紧邻的另一段注释也写着「下一帧的 `Request` 会把 GPU 侧重新补上」—— 而那个 gate 让下一帧**根本不会调**。**头文件、相邻注释、代码三方互相矛盾，而我信了那个 gate 自己编的理由**（「比一个本地 bool 更抗 Reset」）。后果：纹理被 GPU LRU 淘汰 / 设备丢失后，`State` 仍是 `Ready` 而 `Texture()` 空句柄 ⇒ 界面**「图闪过一次就没了」**，不报错、不崩、静息截图照样 saved。已修：去掉 gate，每帧无条件 `Request`（幂等：GPU 还在就一次哈希查找；不在就从 CPU 缩略图缓存重传；再不在才重新解码。还白拿「命中刷新 GPU LRU」） |

**缺陷 3 的判据与反证**（`sheet-recover`，进 `overall`）：

```
修好之后：  sheet-images=settled requested=2 pending=0 pixels=33848
            sheet-recover=ok pixels=33848 before=33848          overall=PASS
把 gate 装回去（反证）：sheet-recover=LOST pixels=0 before=33848  overall=FAIL
```

判据的做法是**直接在 GPU 侧清空纹理缓存**（等价于内存压力下的 LRU 淘汰），而**不动服务侧的
条目状态** —— 那正是「`State == Ready` 而 `Texture()` 空句柄」的形态；然后数 fixture 那两张纯色
图的像素数，取「**淘汰前后相等**」而不是含糊的「大于 0」。反证那组数字把缺陷形态说得很清楚：
**图一开始在屏上（33848），擦掉纹理之后再也不回来（0）。**

⚠️ 服务侧的 `OnDeviceLost()`（它会把 `Ready` 改回 `NotLoaded`）**不能**用来测这条约束 ——
那条路径靠 `State` 判断也能自愈，测它等于测了个假。差别就在「动不动状态」。

⚠️ 这一族只能用**像素计数**验，不能比两张图：截图跨轮不可复现（同一二进制连跑两轮，
80 张里 48 张字节不同）。

**接线后的效果与它自己的判据**（`assets-grid-all` 单张图，逐像素计数）：

```
                          接线前   接线后
#FF2D55（林昭设定图）          0     33848
#7CFF00（周姨设定图）          0     33848
manifest: sheet-images=settled requested=2 pending=0 after 2 frames
```

⚠️ **`sheet-images` 是判据、不是日志**：`requested>0 && pending==0` 才成立，两半缺一不可 ——
`pending==0` 单独看有二义（「全落地了」与「一张都没被请求过」同形），而后者正是最该报出来的
那一支。它已进 `overall`（初值 false = 「这一段没跑到」，与其余几族同一条规矩）。

⚠️ **这个判据是被一个真实的 race 逼出来的**，不是预防性设计：第一版把等图循环放在
`SetAssetsOverview(true)` 之后、**清 kind 筛选之前**，而那一帧的网格被筛成只剩「物品」那一条
（fixture 里它没有 `visual_assets`）⇒ 一张图都不会被请求 ⇒ 循环 0 帧「通过」⇒ 后面的抓图
仍在和异步上屏抢时间。实测**同一二进制连跑两轮，一轮数到 33848 像素、另一轮 0**。
挪到清筛选之后 + 加上 `requested>0` 这一半之后连跑三轮一致。

**已接的位点**（都用 `kit::RequestImage` → 就绪画真图 / 否则画设计稿的 `Art`）：

| 位点 | 设计稿几何 | 数据来源 |
|---|---|---|
| 资产**总览**网格卡缩略图 | `.asset-grid .thumb { height:150px }` + r-sm + cover（views.css:713-715） | `visual_assets.sheet_rel_path` |
| 资产**详情**左列基线图 | `.art { width:100%; height:220 }` + r-sm + cover，下面一行 `.tiny dim`（Assets.jsx:186-203） | 同上 |
| （详情页顺带补齐的两列网格） | `grid-template-columns: minmax(240px,320px) minmax(280px,1fr)`；gap 16；`align-items:start`（Assets.jsx:184） | — |

⚠️ **详情页原来是单列**：KV 铺满 640px，而左列那块「基线图」**整块没画** ——
它是这一页最大的一张图。两列都要一起改（网格的列宽决定图的宽度）。

#### 顺带订正一个**已经骗过人**的过期名字（`Capture.h`）

`SavePng` / `GrabAndSave` 的形参叫 `bgraPixels`，注释写「后备缓冲是 BGRA，PNG 要 RGBA ——
这里做通道交换」；而 `Capture.cpp` 里那句一直是对的：**「输入已经是 RGBA8，直接编码 ——
不要再交换通道」**（并记着 D3D11 时代真交换过一次，每张证据图 R/B 对调、「偏暖发黄」，
差点被当成主题映射错了去改主题）。名字没跟着改。

后果是实测踩到的：`CountColorPixels` 按 BGRA 读偏移 2 ⇒ **恒数到 0**，
而 0 恰好也是「图没上屏」的读数 ⇒ **一次就伪装成了产品缺陷**，白查一轮。
已订正形参名与注释。**教训：形参名也是文档，改名要跟着改。**

**仍未接的图片位点**（四处，几何都已就位、只差把路径接上）：
分镜 `.tthumb`、出图结果面板、出片胶片格、查看器大图 / 联系表、总控「最近产物」缩略图。

⚠️ 其中**分镜那处没有诚实的映射**：设计稿按 `s.status === 'done'` 二选一，而
`stage_artifacts` 表**没有路径列**（只有 `payload_json`，见 `out/_fixture_schema.sql:748-757`），
`BookShotView` 也就没有图路径字段 ⇒ 「这一镜的图在哪」在数据层目前**没有答案**，
不能靠 `output/ch001_S001.png` 这种文件名约定去凑（那是发明约定，不是复刻）。
**资产详情页的 V0 派生链**则有真数据：`visual_artifacts.rel_path`（`NovelVisual.h:43`
的注释就写着「产出图（相对工程根）」），只是还没投影到 `BookAssetView`。
| R9 | cover 缩放锚点 | **已解决** | 满幅画布页改用可视高度 |
| R10 | 启动初始化顺序 | **已解决** | `AppEntry` 分派 + APPDATA 沙箱 |
| R11 | ImGui 叠放 item 抢 hover | **已解决** | 同一矩形只能 `HitTest` 一次；遮罩只画不注册 |
| R12 | `Runner` 无「跑一个 T 阶段」的服务接口 | **未解决** | 阶段**记账**真实，阶段**本体**是桩；归因见「下一步」第一行 |
| R13 | `StopPolicy` 只判 5 组 | **界面侧已解决** | UI 改画 `shine::novelcore` 的 **S1–S12 真实规则表**（`StopCodeCondition` / `StopCodeHint`），不再借用 `pipeline/StopPolicy` 的编号；`StopPolicy` 那 5 组仍在但界面不用它。唯一限制：数字阈值只能拿到**文本**、逐条状态本页算不出 ⇒ 卡上**只列规则不标状态** |
| R14 | `Ledger` 无章节维度 | **一半已接** | `T1–T17` 链确实没章节轴（`LedgerEntry` 只有 4 字段，做不了）；`V1–V7` 走 `stage_artifacts` 的**章节 × 阶段矩阵已渲染** |
| R15 | 校验报告无查询源 | **已解决** | 真值是 `work/ch<NNN>/v08_continuity.json`，列表按章列三态、点行走模态看逐项 C1–C12 |
| R16 | 底栏「校验报告」曾长期是诚实空态 | **已解决** | 同 R15 |

---

## P0 底座与门禁

- [x] **P0.1** `third/imgui`（`IMGUI_VERSION_NUM == 19297`）
- [x] **P0.2** CMake 前端开关（`SHINE_UI_IMGUI` / `SHINE_UI_QT`）+ `shine_imgui` 目标
  - `shine_imgui` 是 `GLOB_RECURSE CONFIGURE_DEPENDS "src/ui/imgui/*.cpp"`，**新增 .cpp 不用改 CMake**
  - 默认 `SHINE_UI_IMGUI=ON` / `SHINE_UI_QT=OFF`
- [x] **P0.3** `tools/check-layers.ps1` 三条规则反向 —— PASS (0 violations)
- [x] **P0.4** 最小宿主：Win32 窗口 + **OpenGL** + ImGui 一帧（37 张 PNG 全部 saved）

---

## P1 运行时骨架

- [x] **P1.1** `Host.{h,cpp}`：窗口 / GL 上下文 / 深度缓冲 / `WM_SIZE`
  - MSAA 关掉，否则 `glReadPixels` 抓的图边缘发虚
- [x] **P1.2** `shine::gpu::AttachDevice(api)` ⚠️ 关键动作
  - 注入自己 `GetProcAddress` 解析的 GL 函数表；缺函数时 `log::Error`，不静默
- [x] **P1.3** 帧循环 + `async::DrainUiQueue()` / `gallery::Tick()`
- [x] **P1.4** CLI 分派 + `ConfigureAppDataSandbox()` + `SHINE_*` 自检
  - 取证模式下 APPDATA 重定向到 `<OutDir>/_appdata`
- [x] **P1.5** 布局持久化（`layout.dat`，magic `shinetv-layout-1`）
- [x] **P1.6** DPI 感知（`ImGui_ImplWin32_EnableDpiAwareness()` 在建窗**前**调用）
  - framebuffer 固定 1:1，**不**按 DPI 放大

---

## P2 主题与字体

> ✅ **本阶段最硬的判据已达成**：5 套主题名与全部业务中文逐字可读。
> 字体相关的硬事实另见「跨轮硬事实 · 字体与图集」。

- [x] **P2.1** `Tokens.h` 搬运（5 个 JSON 原地复用，格式未改）
- [x] **P2.2** `ColorToken` 31 项映射，一次写全
- [x] **P2.3** 派生色预混（`Derived`，12 组）
- [x] **P2.4** `ImGuiStyle` 生成
- [x] **P2.5** ⚠️ 字体图集 + CJK
  - 根目录从 **exe 路径回溯**，不是 CWD（程序常从 `build/` 启动）
  - 等宽字体（`Cascadia/Consolas` 只有拉丁）挂雅黑回落链（`MergeMode`），中文副行不再变 `?`
  - `kUiSizes` 补齐 14.5 / 17 / 26 三档，`kMonoSizes` 补 11 / 12
  - `Shell::SetTheme` 用 `atlasIsSerif_` 守卫：**只有字体族真变了才重建图集**
    （只判「切到水墨」会漏切离水墨的回退；不记状态会连续重烘 5 次）
  - 依据见 `kit/Fonts.cpp` 顶部注释
- [x] **P2.6** 主题切换 + 持久化 + `SHINE_THEME_SET` 自检
- [x] **P2.7** 减少动效开关（从顶栏设置键挪进**主题菜单**，见 P4.2）

---

## P3 组件套件

- [x] **P3.1** 绘制基建：圆角矩形 / 渐变 / 文本截断 / 阴影 / `AutoGridCols`
- [x] **P3.2** 基础控件：Button(4×3) / IconBtn / Input / TextArea / Select / Switch / Checkbox / Field
- [x] **P3.3** 数据展示：Tag(7 色调×2) / StatusDot / Kbd / KV / Progress
- [x] **P3.4** 容器与浮层：Card / Segmented / Tabs / Spinner / Tooltip / Divider / DataTable / Tree / Menu /
  `Overlays.{h,cpp}`（Scrim / Modal / Drawer / Toast）
  - `Card` 的 `hoverable` / `glow` 早先是死代码，已改成真 `HitTest` + hover 边色 + 上浮 2px
  - `IconButton` 的 `tip` 原走 `ImGui::SetTooltip`（错背景 / 错延迟 / 无法按设计稿右侧锚定），改走新 Tooltip
  - `Empty` 的正文框原写死 320px 宽且忽略传入 bounds，在 240px 侧栏里横穿面板
  - **刻意不做** Skeleton / Avatar / Badge：全 `webui/src` 里没有这三个原语，凭空造尺寸等于凭空造设计
- [x] **P3.5** 画布与图像 ⚠️：`FlowCanvas` / `Art` / `ArtInk`
  - `FlowCanvas`（`Views.{h,cpp}`）：150×76 节点、5 态、9px 端口、24 段折线逼近三次贝塞尔、
    滚轮光标锚点缩放（0.35–2.0）、拖节点 / 拖背景平移、适应视图、右上玻璃工具条
  - 三处踩坑：fitInset 重复扣宽、居中未扣内边距、**满幅画布页误套外壳 2400px 撑高**
    （实测 `canvas=644x2400 → y=1112`，节点全被顶出视口）
- [x] **P3.6** 图标字体（`Icon*.{h,cpp}`，与 `webui/src/components/Icon.jsx` 46 个一致）
- [x] **P3.7** 组件画廊页

---

## P4 应用外壳

- [x] **P4.1** 窗口框架布局（46/56/240/280/190/26/34，档位收在 `Shell_Layout.h`）
- [x] **P4.2** 顶栏
  - 运行 / 停止**二选一**（`webui/src/shell/Shell.jsx:44-48`）
  - 「主题」是**幽灵按钮 + 文字**，弹**主题菜单**（5 套 + 色块 + 勾 + 减少动效）
  - 品牌标 / 项目胶囊 / 搜索框**都可点**（分别回项目中心 / 开命令面板）
  - Comfy 状态项读 `ComfySession` 真值，不是恒定 `Idle` 裸点
- [x] **P4.3** 导航栏 6 工作区 + 3 面板开关 + 画廊 + accent 选中条
  - 面板开关**无常驻高亮**：设计稿的 `.rail-btn.active` 在整个 CSS 里**没有规则**（死类）
  - ⚠️ 修掉一个致命交互 bug：曾先 `Hovered(idA)` 再 `Clicked(idB)`，两个 InvisibleButton
    落同一矩形，ImGui 只让先注册者拿 `HoveredId` → **整个导航栏点不动**
- [x] **P4.4** 侧栏 + 折叠把手（写死的假章节名已换成诚实空态）
- [x] **P4.5** 检查器（3 段可折叠；写死的假属性已换成诚实空态）
- [x] **P4.6** 底栏 4 标签
  - 任务队列 ← `comfy::QueueModel` 真实快照（早先是写死的 `Progress(42%, run=true)`）
  - 日志 ← 运行期真实事件（早先是四条写死的 "T3 生成图 · …"）
  - 产物 ← 扫 `<project>/output`，**IO 在 worker**，按路径 + 3s 节流重扫
  - 校验报告 ← `v08_continuity.json`（原为诚实空态，现已接真数据 + 报告模态）
- [x] **P4.7** 状态栏（Comfy / LLM / 队列读真值；`T{n}/17` 三态，未接入时如实显示「阶段执行体未接入」）
- [x] **P4.8** 面包屑（第三段改成 `Shell::DerivedViewLabel()` 按工作区现算）
- [x] **P4.9** 命令面板（`Ctrl+K`；另注册 `Ctrl+N` / `Ctrl+O` / `Ctrl+T` / `Ctrl+Enter`）
- [x] **P4.10** 浮层全套：主题菜单 / 设置模态 / 校验报告模态 / `Esc` 逐层关闭
  - ⚠️ 全屏 scrim **不能**用 `kit::HitTest`：它先拿 `HoveredId`，把模态内部按钮变成死键。
    改用只画不注册的 `kit::ScrimPaint` + 手算 `Rect::contains`
- [x] **P4.11** 项目中心页（`DrawProjectHub` 早先**定义了但零调用**，已接成整屏替身 `hubOpen_`）

---

## P5 六个工作区

- [x] **P5.1** 总控（pipeline）— 接 `Runner::{Usage, LedgerLog, CurrentStage, RunNext}` + `Budget` +
  `Ledger::Entries` + `StopPolicy::Evaluate` + `Checkpoint::Load` + `AppSettings`
  - 删掉全部伪造 KPI / 哈希 / 停止条件
  - ⚠️ `BindOverviewProject` 曾落在**匿名 namespace** 里 → 外部链接不到
- [x] **P5.2** 小说（novel）— 8 模式 · `NovelGraph::ListChapters / ListEntities` 真实返回值
- [x] **P5.3** 资产（assets）— `NovelVisual::FindAssetByEntity / ListArtifacts` + kind 筛选树
- [x] **P5.4** 分镜（storyboard）— `ListShotsByChapter / ListStageArtifacts` + `RunContinuityChecks`
  - `unverified` 显式报成「无不一致，但有数据不足项」，不报成「通过」
- [x] **P5.5** 出图（imageflow）— 含 `FlowCanvas` + 4 个面板页签
- [x] **P5.6** 出片（videoflow）— 含 `FlowCanvas`
  - 三页共用一份工程快照（`BindNovelProject` 一处即可），IO 在 worker，generation 丢弃过期结果

---

## P6 验收取证

- [x] **P6.1** 后备缓冲 → PNG 抓图 + manifest（`shots-manifest.txt`，对齐 `scripts/run_reviews.ps1`）
  - `glReadPixels` 原点在左下，PNG 从左上起 → **逐行倒着写**
- [x] **P6.2** 保留全部 `SHINE_*` 环境变量名与分派点
- [x] **P6.3** `SHINE_IMGUI_REVIEW`：6 工作区 × 5 主题 + 全工作区基线
  - 覆盖面 17 → 37 张（后续轮次加到 79–80）
  - manifest 含 `overall=PASS|FAIL` 行；退出码**只读** `ReviewResult::pass`（见「跨轮硬事实」）
  - ⚠️ 补了**判据二**：同一工作区在 5 套主题下的像素必须两两不同。旧门禁只判「PNG 写出来了」是**假绿**
    —— 实测把 exe 放到没有 `themes/` 的目录里跑，37 张只有 13 张唯一、五套主题逐字节相同，manifest 依旧 PASS

---

## P7 收尾

- [x] **P7.1** 删 `src/ui/{app,kit,layout,pages,qml,verify}` —— **198 个文件**
  - 唯一还被用到的 `AppEnvironment.{h,cpp}` 搬到 `src/ui/imgui/host/`
- [x] **P7.2** 删 `shine_kit` / `shine_qml` 目标与 `SHINE_UI_QT`
- [x] **P7.3** 移除 `find_package(Qt6)` / `AUTOMOC` / `AUTORCC`
  - `check-layers.ps1` 的「Qt 前端豁免目录」一并删掉 —— 门禁现在对全 `src/` 零豁免
- [x] **P7.4** 门禁定型：`check-layers` / `check-theme` / `check-colors` 的主题路径改指 `src/ui/imgui/theme/Themes`
- [x] **P7.5** `package-qt.ps1` → `scripts/package-imgui.ps1`
  - 旧脚本传 `-DSHINE_QT_UI=ON` —— 这个开关**在 CMakeLists 里从来不存在**，
    所以它一直在静默配出一个 ImGui 版，再叫 `windeployqt` 去部署 Qt 运行库
- [x] **P7.6** 更新 `AGENTS.md`：删掉「双前端共存」描述，补上 `src/ui/imgui/**` 的 UTF-8 编码事实
- [x] **P7.7** 实测产物体积并填表（见「关键指标」，2026-09-30 已复测）
  - 差距全在调试信息：`.debug_info` 单节曾测得 ≈146 MB

---

## 判据清单

### 取证指标（`SHINE_IMGUI_REVIEW=1`，入口 `out/_run_review.ps1`）

| 指标 | 读的是什么 | 抓什么 |
|---|---|---|
| `# shots` / `failed` | PNG 写出数 / 失败数 | 编码、落盘失败 |
| `identical-theme-pairs` | 同工作区 5 主题两两像素比对 | `themes/` 没落地 ⇒ 五套主题逐字节相同 |
| `identical-driven-pairs` | 受控图两两比对 | 「证明性」截图没拍到它承诺的状态 |
| `inverted-rects` | 运行时反向矩形计数 | `kit::Rect` 四参当 (x,y,w,h) ⇒ **整块不画也不可点** |
| `duplicate-hits` | 同帧重叠热区计数 | 画得出但**点不动**（先注册者独占 `HoveredId`） |
| `hover-probes` | 悬停三拍（静息 A / 静息 B / 目标 C）+ 命中自检 | hover 链路断；分 `unstable` / `broken` |
| `shortcuts` | 7 个快捷键逐个注入 | 空动作；按 `Layout`/`Palette`/`Theme`/`Hub` 四类分别读产品自己的状态 |
| `overlay-clicks` | 浮层 × / 点遮罩 / 不穿透 / 同帧自毁守卫 | 浮层 item 提交在根窗口而 hover 恒在 child |
| `scroll-failed` | 粘性滚动请求有没有被核销 | 区域压根不能滚 |
| `*-snapshot` | 收敛 / TIMEOUT | fixture 没落地 ⇒ 页面走空态，分支从未被执行 |
| `overall` + 退出码 | 判据的 `pass` **原样**传出 | — |

**最新一轮（`build/rv23-gcc162/`，GCC 16.2 全量重建后）**：
80/80 saved · failed 0 · `scroll-failed 0` · `identical-theme-pairs 0` · `identical-driven-pairs 0` ·
`inverted-rects 0` · `hover-probes 11/11` · `shortcuts 7/7` · `overlay-clicks 7/7` ·
**四个 snapshot 全部 converged**（`chapters=3 shots=5` / `assets=3` / `artifact rows=6`）·
**`duplicate-hits: 815`** → **`overall=FAIL`**。

⚠️ **这个红项怎么读（2026-09-30 实测盘上每一份 manifest 得出）**：`duplicate-hits` 在此前**每一轮**
都是 0，但那些轮次（`rv14` / `rv16`~`rv22` / `rv15-baseline`）的 `book-snapshot` 与 `asset-snapshot`
**全是 TIMEOUT** ⇒ 侧栏树 / 检查器 / 资产树全退化成诚实空态，**这个指标一直在测空气**。
rv23 是第一轮**页面里有真实数据**的取证，所以 815 是**首次可观测值**，不是「从 0 涨到 815」的回归。
它**不是** GCC 16.2 引入的（同一份源码，编译器不改命中注册）。究竟是「嵌套热区被设计性命中」
（整卡 `HitTest` + 卡内 footer 按钮 / 滚动区自身热区）还是真缺陷，**尚未判定**，见「没做的」。

**基线对照**（`build/rv15-baseline-31b56a0/`，提交 `31b56a0`，manifest 在盘上可复核）：
`overall=FAIL`，成因同样是 `book-snapshot` / `asset-snapshot=TIMEOUT`（那个年代 fixture 没落地），
hover 8/11 / overlay 4/4。证据目录 `build/rv14` ~ `build/rv23-gcc162` **全部保留在盘上，不要当 build 垃圾清掉**。
⚠️ 反过来说：验证记录里 r74 / r88 / r92 那几轮「四个 snapshot converged · `overall=PASS`」的
manifest **已不在盘上**，所以它们现在只能当二手结论引用；要看数字就得重跑。

### 硬门禁（失败退出码 1）

| 脚本 | 抓什么 | 自检 |
|---|---|---|
| `tools/check-layers.ps1` | 分层：`shine_core` 不得含 Qt 头 / ImGui 头 | `-SelfTest`（6/6） |
| `tools/check-theme.ps1` | 主题 token 与 JSON 一致 | 无 |
| `tools/check-colors.ps1` | 硬编码颜色字面量（豁免必须写 `// theme-ok <理由>`） | `-SelfTest`（11/11） |
| `tools/check-i18n.ps1` | 文案 i18n 覆盖 | 无 |

### 人工复核扫描器（只打印疑似，不改退出码）

| 脚本 | 抓什么 | 自检 |
|---|---|---|
| `tools/find-silent-truncation.ps1` | 列表按容器高度静默截断（含 `> body.max.y - 20.0f` 变体） | `-SelfTest`（6/6） |
| `tools/find-draw-arg-order.ps1` | 绘制原语的**颜色 / 圆角**参数错位 | `-SelfTest`（10/10） |
| `tools/find-rect-wh-misuse.ps1` | `kit::Rect` 四参当成 (x,y,w,h) | **无 `-SelfTest`**（带上会被静默忽略后照常报 `0`，与真通过字面相同）；用 `-Root <样本目录>` |

**一条纪律**：改完扫描器**先跑它自己的自检**。一个「改完报 0」的扫描器和一个坏掉的没区别，
而假绿比没有门禁更坏。

---

## 验证记录

> 每轮记「日期 / 做了什么 / 观测到什么 / 是否通过」。没跑的写「未执行」，**不要把推断写成通过**。
> 逐轮长篇过程在 `git log -p -- refactor/PROGRESS.md`；下表只留结论。

### 分轮结论

| 轮次 | 结论 | 关键读数 |
|---|---|---|
| P0–P7 收尾 | ✅ | 37 张 · `overall=PASS` · 退出码 0 |
| 报告页 / 侧栏树 / 资产树三轮 | ✅ | 45 → 52 → 64 张，md5 全唯一 |
| 悬停 + 动作判据 | ✅ | 77 张 · `hover-probes 11/11` · `shortcuts 7/7` |
| 章节 × V 矩阵 + 滚动修复 | ✅ | 79 张 · `scroll-failed 0` |
| 8 处截断 + 滚动条 + 镜码（r74） | ✅ | 80 张 · 四个 snapshot converged · `overall=PASS` |
| 投影落地（r88） | ✅ | 80 张 · `overlay-clicks 4/4` · `overall=PASS`（不靠 diff、靠读像素剖面证明） |
| 鼠标注入修好后（r92） | ✅ | 80 张 · `hover-probes 11/11` · `overall=PASS` |
| kit 收编（rv13/14） | ⚠️ | `overall=FAIL`，成因 = fixture 两个 snapshot TIMEOUT；已与基线 `31b56a0` 对照（同样红） |
| 大文件拆分（rv20–22） | ⚠️ | 同上；manifest 的 `# shots:` 指标行与拆分前**逐字节相同** ⇒ 行为等值 |
| **第一轮（恢复基线 + 12 处 CSS 偏差）** | ✅ | 80 张 · `overall=PASS` · 探针 11/11；⚠️ 修之前**先量了基线**：同一份代码缺 fixture 只有 9/11 |
| **第二轮（字体 / 页面层 / kit 收编）** | ✅ | 80 张 · `overall=PASS`；纯重构部分用「两轮间稳定的 32 张」比对，0/32 变 |
| **第三轮（#3 alpha / #9 时间线结构）** | ✅ | 80 张 · `overall=PASS`；像素计数实证（`#202734` 4902→38、`#29313E` 7→3987） |
| **第四轮（横向滚动 + 门禁盲区 + 判据自检）** | ✅ | 80 张 · `overall=PASS`；三个扫描器各 0、五个 `-SelfTest` 全过、`check-colors` 7→24 acknowledged（**判定面变宽，不是退化**） |

⚠️ **一次未复现的失败，如实记**：r85 在第 47 张图（`overlay-palette`）之后窗口塌成 0×0
（`capture: bad frame 0x0`），剩 36 张全 FAILED，且崩溃前有几帧报出 x 翻转的反向矩形。
r86/r87/r88 连续三轮同参数**均未复现**，无残留进程。**原因未定论，不写成「已修复」。**

### 已修的真缺陷（按类，供「为什么有这条门禁」用）

**假数据 / 死代码**（静息截图上看着最正常的一类）
- 底栏「任务队列」写死 `Progress(42%, run=true)`、日志四条写死事件 → 接 `comfy::QueueModel` 真实快照
- 侧栏假章节名、检查器假属性（`{代码:S012, 动作:转身}`）、出片「视频任务」4 条算术级数进度 → 诚实空态 / 真值源
- 校验报告页写死「诚实空态」而真值就在 `v08_continuity.json` → 接上；`DrawReportModal` 的驱动字段
  `reportDetail_` 全代码只有 `= -1` 初始化 ⇒ 模态一次都没开过
- toast 是空函数（`DrawOverlays` 是 `(void)draw;`）→ 补 `.toast` 原语（fixed right 16 / bottom 40 / 左 3px 色调条 / 0.4s 淡出）
- 面包屑第三段恒为「总览」（`SetWorkspace` 无条件写死）→ `DerivedViewLabel()` 按工作区现算
- `loading` 标志在派发瞬间就翻、视图没跟上 ⇒ 调用方「等 !loading」一帧都不等（上一轮据此把上一章画面当成新章拍了）
- `runStageIndex_` / `runPercent_` 全文件只有「写 0」与「读出来显示」⇒ 状态栏的 0% 进度条**结构上不可能前进**
- `pipeline::Runner` 注入空 lambda（`return true`）⇒ 17 阶段完成 / ¥0.17 / 100% 全是**真数据通路算出的假结果**；
  `Runner::RunNext` 先扣预算（`:40`）后调执行体（`:46`），所以「返回 false」也留真·假账 ⇒ UI 侧改成压根不调 `RunNext`
- 图评审页 5 行假复选框：返回值丢弃 + `on` 传字面量 `i < 3` + 五行同一个 label
- 组件画廊的 `Switch` / `Checkbox` / `Segmented` / `Tabs` 返回值直接丢弃（kit 的 `bool on` 是**按值**传）⇒ 画廊里按不动的控件
- 「3 秒节流」是死计时器（`*RefreshAt_` 只被赋 `0.0f`）⇒ 除了换工程没有第二个触发点
- 「批量出图」是丢了返回值的 primary 主按钮 → 点了弹 toast 说明为什么没接
- `Art()` 程序化占位在三处漏标注 → 抽出全仓共用常量 + 可见标注（`Shell.cpp` 早先已标，其余三处漏了）
- 两个浮动面板头的「运行中 / 出片中」是字面量 → 只在 `ComfyHasRunning()` 为真时画
- `IsMouseHoveringRect` 在本工程 **0xC0000005**，共 7 处在用 → 全部改 `Rect::contains(io.MousePos)` 手算

**命中与叠放**（画得出、点不动）
- 同一矩形注册两次 InvisibleButton（`Hovered(idA)` + `Clicked(idB)`）⇒ ImGui 先注册者独占 `HoveredId`，**整个导航栏点不动**
- 四个浮层全被工作区盖住：都用 `GetWindowDrawList()`，而 child 在父窗口那条 list **之后**渲染 → 全改 `GetForegroundDrawList()`
- 浮层的 item 提交在**根窗口**，`FindHoveredWindowEx` 从 `g.Windows` 末尾往前扫 ⇒ `g.HoveredWindow` 恒是工作区 child，
  而 `ItemHoverable` 第一句就是 `if (g.HoveredWindow != window) return false;` ⇒ **根窗口里所有按钮恒死键**
  （80 张静息绿图零覆盖：它们不点击；hover 探针也抓不到：它们只打 chrome 与 child 内部控件）
- 模态遮罩注册成全屏热区（`kit::Scrim`）⇒ 报告模态「外壳先画」时遮罩先拿 `HoveredId`、**全部按钮按不动**；
  项目中心「对话框最后画」时**不会坏**。危害方向取决于绘制顺序，两种顺序结论相反
- 项目中心卡片先注册整卡热区再注册页脚 4 个按钮 ⇒ 按钮死键，点「…」会直接打开项目（等价于 webui 的 `stopPropagation`）
- 检查器三段折叠箭头是纯装饰（每帧新建的 `const` 局部数组，没人写回展开态）⇒「关联」段永远打不开
- 浮层开着时外壳 chrome 照常提交 item ⇒ 吃掉「点遮罩关闭」那次点击（症状：工作区被切走、浮层还开着）
- 「点面板外关闭」把刚打开的对话框当场关掉（同一帧 `IsMouseClicked()` 仍为真、鼠标仍在工具条上）⇒ `HubState::dismissArmed`
- 设置模态与主题菜单可以同开（互斥只写在主题那一侧）→ 两处入口都走 `Shell::ToggleSettingsModal`
- `SettingsCloseRect()` 是一份独立的 `560×452` 复算 ⇒ 改高度就分叉（× 停在老位置，判据只会报「没关掉」）

**滚动、裁剪、视口**（自绘控件没有 DOM 兜底）
- 自绘控件只往 `ImDrawList` 加 draw call，**全程没给 ImGui 提交过任何 item** ⇒ 三个 `ScrollRegion` 全部从来不能滚
- `ScrollRegion` 不调 `setContentHeight()` = **滚不动**（不是「滚不顺」）；上报值恰好等于视口高也等于滚不动
- 嵌套 child 的裁剪矩形只写进**它自己那条** list 的 `CmdBuffer` ⇒ 画到外层 list 的 draw call 完全不受裁剪
  （症状不是「卡片消失」而是**盖住上层**：总控页右栏滚上去后「停止条件」浮到 y=93 压住 KPI 与页头按钮）
- 页面把外壳给的 2400px **布局区**当视口 ⇒ 右栏高 1978 而内容只有 654；一张 **1750px 高只有 6 行**的账本卡
- `SetScrollHereY` 在 child 构造期（BeginChild 之后、内容之前）拿不到 item（读到的是上一帧残留）⇒ 实测**滚不动**，
  改用 `SetScrollY(自报内容高 − 可视高)`
- 10 处列表按容器高度 `break`（含 `> body.max.y - 20.0f` 这种「给页脚留位」的变体）⇒ 50 个任务只画前 6 个、无任何提示
- 滚动区带 `ImGuiWindowFlags_NoScrollbar` ⇒ 内容能滚了之后用户看着**与不能滚时完全一样**
- 页面要自建视口必须知道「真正能看见多少」⇒ `pages::WorkspaceViewportHeight()`（外壳每帧写入）/ `SetPageContentHeight()`（页面自报）
- 命令面板 16 条目 + 3 组标题 = 546px，输入框下只有 358px 且无裁剪 ⇒ 末尾几行画到面板外压在工作区上

**布局与度量**
- `kit::Rect` 四参是 **(minX, minY, maxX, maxY)**；当 (x,y,w,h) 用 ⇒ `max < min` ⇒ 整块不画也不可点，编译不报错。
  两处（资产总览网格卡、底栏页签下划线画到屏幕顶栏）+ 一处小说页「设定」网格整张不画
- `AddRectFilled(p_min, p_max, col, rounding)` —— **颜色在圆角前面**。写反则 col 收到半径变成全透明黑、
  rounding 位收到颜色被截成巨大值、半径为 0 那次 col 恰为 0 被 `if (col == 0) return` 整块丢掉。
  编译过、不崩、同一帧别的图元照常出图（见 `kit/Draw.cpp` 的 ⚠️⚠️ 注释与 `find-draw-arg-order.ps1`）
- 整个 ImGui 前端**一个投影都没画**：28 个 `DrawShadowed` 全是平面矩形，`Tokens.h` 的 shadow 常量零引用
  → 引入 `ShadowTier` + `kShadowSpecs`，按 CSS 逐条接 20 处（常驻 9 + hover 7 + accent 3）
- 投影两层 alpha 被乘两次（JSON 里存的就是 CSS 那层 alpha）**且**每层画成互不重叠的窄圈（累计只剩 31%）
  ⇒ 第 i 环要铺满 `[本体边, g_i]` 且只带**增量** alpha；只修前者得到的仍然是错的
- 资产卡 hover 边框用 `accent-glow`，设计稿写的是 `line-strong`（当初为了让 hover「看得出来」自己挑的颜色）
- 检查器段头「偏上 6.2px」是**我自己算错的数**：按正确口径重算是 Δ = −0.75px（在接受带内），
  真毛病是**整段高度差 15.2px**（24 vs 39.2）—— 是布局短，不是字没居中
- 居中偏差最大两处：`kit::Chip()` 低 5.25~6.0px（kit 自身，且被资产侧栏 chip 实际调用）、队列行文字高 5.75~6.0px
  → 统一走 `kit::CenterTextY`（度量口径见「跨轮硬事实」）

**重构（零行为变化，等价性有判据）**
- `WorkspaceB.cpp` 3522 行 → 11 个模块，切在**线程边界**上：`BookQuery.cpp`（worker 开库查询）/
  `BookData.cpp`（UI 侧快照与视图重建）—— 分错就会出现「UI 线程里开着库」这种明令禁止的形状
- `Shell.cpp` 2797 行 → 6 个文件，切在「**外壳的哪一族**」上（Chrome / Panels / Dock / Palette / Layout / 本体）
- `kit/Widgets.cpp` 1578 → 12 个控件族 + 34 行门面 · `verify/Review.cpp` 1533 → 8 个验收族 + 55 行入口 ·
  `pages/WorkspaceA.cpp` 1014 → 7 · `pages/ProjectHub.cpp` 1069 → 5 · `theme/Theme.cpp` 750 → 3 模块 ·
  `kit/Views.cpp` + `kit/Icon.cpp` 1102 → 3 个视图族 + 4 个图标族
- 拆出的共用件：`PageKpi` / `PageModal`（`ModalBox` / `ModalFooterHint`）/ `kit::detail::HitTestItem` /
  `kit::detail::UniqueId` / `BezierAt` / `DrawLink`
- 删死代码：`kit::Modal`（零调用，且头部绘制与 `ModalFrameRect` 逐行重复）、`StageStateFromStatus()`、
  `selectedShot_` / `setSelected`（写的是它、真读的是 `BookState::selectedShot` ⇒ 「换到第 2 张镜」那张图与默认态逐像素相同）
- kit 小组件收编：7 处重复实现 → `kit::ListRow`（**4 处**）+ `kit::ListCard`（**3 处**，**刻意不与 ListRow 合并**：
  hover 一个走填色一个走投影档，选中一个换底一个换描边环）；4 个「已实现、算得对、全树零调用」的组件全部接线
  （`ModalFrameRect` / `Toast` / `Menu` / `DataTable`）；新原语 `kit::ScrimPaint`（只画不注册）+ `kit::OverlayPanel`
- 字形集扫描整段删除（`GlyphRanges` 在 `RendererHasTextures` 后端下只被 `imgui_draw.cpp:3568` 读一次且到不了）

**取证基建**
- fixture 由 `out/_make_fixture.ps1` + `out/_fixture_seed.sql` 播种（`novel.db` + `stage_artifacts` + `visual_assets`
  + 6 个产物文件），缺库/缺自检文件**直接失败**，不再「跑出一轮空图还报 PASS」
- 覆盖度靠枚举得出：底栏 3 个页签、检查器「预览」段、出图/出片第二个面板页签、8 个小说模式都曾从没被拍过
  ⇒ 补 `SetNovelMode` / `SetImageFlowPanel` / `SetImageFlowFolded` / `SetVideoFlowPanel` / `SelectStoryboardShot` 等入口
- `git stash` 基线对照要**留下证据目录**（`build/rv15-baseline-31b56a0/`），
  只写「已对照跑过基线」而把目录清掉 = 结论只在提交信息里，工作区查无实据
- ⚠️ **取证脚本的默认 `-Exe` 曾经指向 `build-p7\ShineTVStudio.exe`**（2026-09-30 修）。那个目录里躺着一个
  两小时前、体积小 26 MB 的旧二进制，而默认跑**照样 PASS** —— 那个 PASS 与当前代码毫无关系。
  **默认跑错产物是「判据通过」最难发现的一种假绿**：判据只认产物，不认产物的出处。
  已改指规范的 `build\ShineTVStudio.exe`；跑之前仍要核对 `-Exe` 与 exe 时间戳。

---

## 下一步

重构主干（51/51）已完成并通过验证。剩下的是**产品缺口**，不是重构缺口：

| 缺口 | 归因 | 说明 |
|---|---|---|
| **阶段执行体是桩**（页面已不再假装） | `shine_core` | **归因成立，且已复核修正三处事实**：① `pipeline::StageExecutor` 是 `std::function<bool(StageId, const std::string&, std::string&)>`（`src/pipeline/Runner.h:23`），第二参是 **`const std::string&`**；② `GenerateOneChapter` 在命名空间 **`shine::novel`**（`src/novel/NovelPipeline.h:45-49`）；③ **`NovelContinuity` 根本不是执行体** —— 它是 V8 连续性校验模块，形状上无法充当 T 阶段执行体。<br>保留这个缺口的理由比「签名不同」更硬：`RunNext` **每个阶段调一次**、共 29 个阶段（T1–T17 + V0–V11），且 `Runner.cpp:40` 先扣预算、`:46` 才调执行体、`:51-55` 成功后还要落 `work/T*.json` 并记账 —— 把「生成一章」包成 29 次调用会稳定产出假账本。两套 V 阶段枚举互不兼容（`pipeline::StageId` 是 V0–V11，`src/novel/NovelVisualStages.h:55,64-65` 的 `VisualStageId` 是 V1–V7），`pipeline/` 全树对 novel 零引用。<br>**界面侧已做完**：`Configure(root, mode, nullptr, nullptr)`、`StartRun` 压根不调 `RunNext`、KPI 全 0 改成「未接入」、状态栏改成「阶段执行体未接入」、两个运行按钮 disabled 且**在按钮下方**写出原因（tooltip 抓不到截图）。`OverviewPipelineWired()` 是**唯一一份**读数。 |
| **侧栏树 / 检查器主体** | 页面层 + 一处越界 | **已实现**：`WorkspacePages.h` 开 `BookSide()` 只读视图，侧栏画「卷 → 章 → 镜」三层树、点选换章换镜，检查器「属性」给九行真实字段，镜码 `ShotCode()` 三处共用一份。资产 kind 筛选树、节点「快捷跳转」、检查器「关联」段（伏笔 / 场 / 镜）均已接真数据。<br>**仍缺**：把「哪一对镜连续性不过」做成**可点的**结构化关联。`ContinuityIssue` 只有人类可读的 `detail`（两个 `ShotRow::id` 在里面），已用 `ShotPairToCodes` 在内存快照里换回镜码显示，但要跳转只能正则解析字符串 —— 应在 `src/novel/` 补 `shotAId/shotBId`，属越界，**本轮不猜**。 |

已闭环的旧缺口（原归因多为「要动 `shine_core`」，复核后都不需要）：

| 旧缺口 | 结论 |
|---|---|
| ~~S1–S12 停止规则表~~ | **已接上，零 `shine_core` 改动**。`src/novel/NovelRunLoop.h:43-48` 的 `StopCode`(S1…S12) + `StopCodeName`/`StopCodeCondition`/`StopCodeHint`，`NovelRunLoop.cpp:102-138` 逐条落地。旧界面照 `pipeline::StopPolicy` 的编号把「LLM 调用 0/1000」标成 S1 —— **编号撞名**，已拆成「停止条件 · S1–S12」（规则表）与「流水线停止规则」（预算/前置，**去掉 S 编号**）两张卡 |
| ~~侧栏树的卷层级~~ | **已接上**。`ChapterRow::volume_id` 早就有，`ListChapters` 的 SELECT 也带了它 —— 缺的只是 UI 侧**两行没搬的字段**（`BookChapter` 少 `volume_id`、搬运循环漏了），加一条 `SELECT id,title FROM volumes` |
| ~~章节 × 阶段双轴甘特（V 链）~~ | **已接上并渲染**。`T1–T17` 链确实没章节轴（做不了）；`V1–V7` 走 `stage_artifacts`(chapter_id + stage + 镜序 + created) 已有只读接口，矩阵逐格与 fixture 一致 |
| ~~小说页「设定」模式的实体卡不可点~~ | **已接上**。点实体就是换 `BookState::selectedAsset`（右栏与资产总览网格读的是同一个），设计稿里没有「点完右栏显示什么」的歧义 |

另有一条**已接受降级**：`Derived::gateBg` / `checkRowBg` 当年按纯 alpha 算，与设计稿的实色差一个底。
字段按「只加不减」冻结，要 1:1 请用新增的 `stageDoneBg` / `stageFailBg` / `gateFailBg`。

---

## 没做的

### 视觉 1:1 符合性审计（2026-09-30 起）

投影已按 CSS 接完。这一轮留下一批**已核实、已排序、尚未动**的 token 偏差（归因一律先独立核实，默认假设是「我错了」）。

> **▶ 2026-09-30（第二轮）现状：`overall=PASS`**（80 张 · `hover-probes 11/11` · `duplicate-hits 0` ·
> `identical-*-pairs 0` · `inverted-rects 0` · 四族收敛）。下面标 ✅ 的是本轮改掉的；
> 仍标 ⚠️ 的按原样留着。**改之前先跑一次基线**（方法见本节末尾）。

| # | 偏差 | 归因 | 说明 |
|---|---|---|---|
| 1 | ~~`--dur-1/2/3` 零读点~~ | — | ✅ **已接**：`kit/Anim.{h,cpp}`（third/ImAnim）+ `TransitionTo` / `TransitionColorTo`。另订正 `kEaseOut` 对齐 `webui/src/styles/tokens.css:28`、删掉凭印象编的 `kExit`。CSS 里实际是 43 处 `transition`（不是 60+） |
| 2 | ~~面包屑整条底色 + 下边框缺失~~ | — | ✅ **已接**：底色 + 下边框 + padding 24→16 + 字号 12.5→12.0（12.5 不在 `kSizes` 里 ⇒ 实际渲染 13px）+ flex `gap:7px`。✅ 第二轮补上：第一段从写死的「项目」改成**真实工程名**（回退串 `无项目`，与顶栏胶囊的 `未打开项目` 是**两个**串，别统一），并接上 `Shell.jsx:128` 的「第 2 段 == 第 3 段就只画两段」折叠规则 |
| 8 | ~~面包屑左内边距、顶栏 padding、活动栏行距与选中底、dock 左右内边距、检查器段头、底栏页签字号~~ | 页面层 | ✅ **第二轮已全部接完**（逐条对着 `shell.css:19-30 / 157-211 / 382-403`）：顶栏 `padding 0 12`（左右都是 12，不是 16）、`.brand` gap 8、顶栏子项 gap 10、搜索框边是 `line-subtle`（**不是** `line-normal`）+ 内边距 12/图标 14/gap 8/Kbd 右贴 12、`.rail` 行距 44（40 按钮 + 4 gap，原 48）、行盒 40×40（原 44×44）、**补上缺失的 `.rail-btn:hover` 底色**、`.dock-tabs` 页签 12px / pad 5-12-7 / gap 2 / 下划线满宽 / 容器 1px 下边框 + 关闭钮 22×22 与 `收起 · Ctrl+J` tooltip、dock-body `padding 10 14` |
| 3 | ~~15 个 `line.*` / `fill.*` 的 alpha 被压平成不透明实色~~ | 主题层 | ✅ **已修（第三轮）**。原记录说「口径已写成 design-spec §2.0.3」——**口径本身就该改**：JSON 存的是「CSS 的 rgba 叠在 `--bg-surface` 上」的实色，而底色是**画的时候**才知道的事，压在数据层等于让一个未知量提前失参。改法只有一步：**JSON 原样存 CSS 的 rgba**，由渲染器在绘制时按实际已画内容做 source-over 混合。于是不需要给 `DrawRoundRect` 加底色参数、也不需要按底色准备多套 `Derived` 变体。⚠️ 记录里的 `#29303E` 是按标称 0.07 手算的；实测是 **`#29313E`**（alpha 存成字节后是 18/255，G 通道高 1/255）—— 见 `design-spec.md` §2.0.3 的实测表 |
| 4 | ~~15 处字号取不到档~~ | 字体层 | ✅ **已接**。⚠️ 记录本身过期了一半：`kUiSizes` 早已含 12.5 / 14.5 / 17 / 26，真正缺的只有 **9.5** 与 **16.0** 两档，而这两档都**有调用点**（`OverviewRightRail.cpp` 的矩阵表头与行首、`Page_Novel.cpp` 的「设定集」标题），缺档时被向上取成 10.5 / 17。**判据是「有没有调用点」，不是「CSS 里写过没有」** —— 已补进 `kUiSizes` / `kMonoSizes` |
| 5 | ~~`font-weight: 500` 无字面~~ | 字体层 | ✅ **已判断清楚，且不需要新字面**。CSS 字重匹配规则：请求 **500 时先找 ≤500 的档**，只有 400/700 的字体族上落到 **400**。微软雅黑没有 Medium，浏览器画出来就是常规字重 ⇒ ImGui 侧**该用 `FontAt`**。真正错的是 `KeyValues` 的**值列**用了 `FontBoldAt`（等于比键列重两档，检查器/报告卡/章节详情共用这条路径）。⚠️ **不要**为了「更像 500」去加载一档 Medium：字体里没有，浏览器也不会合成 |
| 6 | ~~`.card.hoverable:hover` 的 `translateY(-2px)` 未接~~ | 页面层 | ✅ **已接**（第二轮）。`kit::Card()` 早就接了；缺的是**没走 `kit::Card()` 的页面级卡片**。已抽出 `kit::CardHoverLift(hovered, hoverable)` 并把两处**有 hover 的**手写卡接上：资产总览网格卡（`Page_Assets.cpp`）、项目中心项目卡（`HubCard.cpp`）。另两处（项目中心空态卡、向导汇总卡）**静止态无 hover**，设计稿也没有 `:hover` 规则 ⇒ 不接（接了反而是发明的效果） |
| 7 | ~~`[data-theme="inkwash"]` 下 `.brand .mark` / `.hub-logo` 的 `box-shadow: none` 未接~~ | 页面层 | ✅ **已接**。新增 `theme::ThemeLogoHasShadow(ThemeId)`（与 `ThemeUsesSerif` 同一处），并把两处 logo 合并成 `kit::LogoMark()`。⚠️ 那条 CSS 覆盖的是**两件事**（`background: var(--accent)` 实底 + `box-shadow: none`），不是只去掉投影 |
| 9 | ~~`.tl-card` 宽 96→128、圆角 8→10；项目中心卡片名 17.0→14.5px~~ | 页面层 | ✅ **已接**（`Page_Storyboard.cpp` / `HubWizard.cpp`）。如实补齐了记录里没写全的三处：**步进 104→138**（宽 128 + `.timeline` gap 10）、选中态边框从「accent 1.5px」改成「**accent-glow** 1px」、选中环从「2px/半径+2」改成 **`0 0 0 3px accent-dim` 的 3px 扩展环**（Storyboard.jsx:154 的 inline style）。⚠️ 该卡的**高度模型**仍未接，单独记在下面 |
| 10 | 本轮新发现（**审计里没记**） | 各层 | 见下面「第二轮新发现」表 |

#### `.tl-card` 的高度/表头模型（第三轮发现、**第三轮已修**）

上面先修了 `.tl-card` 的**尺寸与状态样式**，它所在的**时间线面板**当时还留着一条结构缺口。
那一轮的记录把改动面判断成「本页版式重排，不该零敲碎打」——**那个判断对了一半**：
确实只能一起改，但一起改并不大。逐项与现状：

| 项 | 设计稿 | 改前 | 现在 |
|---|---|---|---|
| 面板本体 | 一张 `<Card title="故事板时间线 · 拖拽重排" icon="film" extra=…>`（Storyboard.jsx:145） | 只有 `DrawShadowed`，**无表头** | 走 `kit::Card`；`extra` 用 `CardHeaderRow` 右对齐画 |
| 面板高度 | 内容撑开：46.6 + 16 + 8 + 卡高 + 12 + 16 | 写死 `contentBottom - 118` | `kTimelinePanelH` = **249.4**，逐项可核 |
| 卡片高 | `tthumb` 72 + `.tinfo`(7 + 17.6 + 2 + 18.4 + 6 + 16.8 + 9) + 上下 1px 边 | 拉伸填满 ⇒ **98** | **150.8**（不再是「填满」） |
| 缩略图 | `.tthumb { height:72px }`、**满幅无内缩无自己的底色**（卡的 `overflow:hidden` 裁圆角） | 内缩 6px、高 60、自带底色与边框 | 满幅 72、底色即卡的 `fill-muted` |
| 占位图 | `image` 20px + gap 4 + `.tiny` 竖排居中 | 只有图标，缺「待出图」三字 | 图标 + 「待出图」都画 |
| 卡内文字 | `.tcode` mono 11/**700** accent / `.taction` 11.5 secondary mt2 / `.tdur` 10.5 muted mt6（`prog thin` 4px + gap 6 + 时长） | 镜码 11.5、无动作行、时长条贴卡底 | 三段逐项按 CSS 排 |
| 时长条 | `<div className="prog thin grow" style="minWidth:30">` + 时长文本 | 写死贴卡底、宽度 `card.width()-12` | 行盒内居中、宽 = 可用宽 − gap − 时长宽、`min-width 30` |

实测复核（`storyboard-shot-1`，逐像素计数）：`fill-muted`（卡底）**106066 → 122810**，
`bg-panel` **426496 → 418945** —— 后者恰好是「面板长高多少、上面两张卡就矮多少」的
净额，两者一起印证了「面板由内容撑开」而不是「写死一个高度」。

**横向滚动**（2026-09-30 第四轮已接，**同轮消掉一处静默截断**）：

`.timeline { overflow-x: auto }` 原先写成「放不下就 break」—— 本章镜多于视口能放下的
数量时，后面的镜**不画也不提示**，正是本仓库最在意的那类缺陷。现在走
`ScrollRegion(horizontalScroll=true)`。

⚠️ **只开那个 flag 会得到更糟的结果**：滚条的行程取自窗口的 `ContentSize.x`，而自绘控件
一个 item 都不提交 ⇒ `ContentSize.x` 恒 0 ⇒ 滚条不出现、内容被裁且**到不了**
（这比原来「至少不画半截」更坏）。所以同时给 `kit::ScrollRegion` 补了
**`setContentWidth()`** —— 与 `setContentHeight()` 同一个道理，两个都声明才滚得动。
未声明宽度的区域行为与补这个方法之前**逐字节相同**（析构仍补 `Dummy(0, height)`）。

**仍未接（都属功能，不属 1:1 版式）**：

- 拖拽重排：`draggable` + `onDragStart/onDragOver/onDrop`（Storyboard.jsx:155-158），
  以及 `.tl-card.dragging { opacity: .45 }`。属功能，按「功能不需要管」的口径留在后面。

#### 第二轮新发现（都是「代码与自己的注释矛盾 / 尺寸算式缺项」一类）

| 位置 | 症状 | 根因 |
|---|---|---|
| `kit/Widget_Button.cpp` `IconButton` | 全树**所有**图标钮（设置 / 收起底栏 / 卡片上的）悬停时是**变绿**而不是**变亮** | `ui.css:98-105` 两条规则的字色不同（`:hover` 是 `text-primary`、`.active` 才是 `accent`），实现把它俩合成了一个 `ColorAccent()` |
| `Shell_Chrome.cpp` `DrawRail` | 选中工作区的**左侧 2.5px accent 竖条一直不可见** | `.rail-btn.on::before{left:-8px}` 的 −8 是相对**40px 按钮盒**，实现按「56px 栏宽」推成 `area.min.x - 8`，落在栏外被裁掉 |
| `Shell_Panels.cpp` `DrawJumpButtons` | 小说侧栏**第二行跳转按钮压住侧栏树第一行** 8px（树顶 8px 点不动） | 占高算式漏了 `plabel`(20.8) 与网格底部外边距(10)：真实占 84.8，调用方按 64 推进。现在占高由函数自己返回 |
| `kit/Widget_Choice.h` `Tabs` | 底栏页签**套了 `.tabs` 的尺寸**（13px / 内缩下划线 / 无 gap），而它该是 `.dock-tabs`（12px / 满宽下划线 / gap 2 / 盒内 2px 边框） | 两个同名不同规格的 CSS 组件，`kit::Tabs` 只实现了前者。已提成 `TabsSpec` 一份实现覆盖 `.tabs` / `.dock-tabs` / `.novel-modes .ntab` 三条规则 |
| `Page_Novel.cpp` 模式页签 | 手写第三份页签条，且逐条与 `views.css:527-554` 不符：宽 +32（应 +24）、字号 12.5（应 13）、基色 text-secondary（应 text-muted）、hover 底 fill-hover（应 fill-muted）、**选中项多铺了一层 fill-selected**（`.on` 根本没有 background） | 同上；已并进 `kit::Tabs` |
| `Page_Novel.cpp` `.novel-body` / `.novel-right` | `.novel-body` 的 `padding 16/18/24` **整条没接**（章选择条与正文贴到视图边缘）；`.novel-right` 内容从 `+60` 起步（应从 `+14`）且**漏了「属性」小标题** | 右栏是网格的另一列，**不随**中间的模式页签下移 |

#### 第三轮新发现（同一类：「有档位却不用 / 算式抄了四份」）

| 位置 | 症状 | 根因 |
|---|---|---|
| `Shell_Chrome.cpp` 品牌标 + `ProjectHub.cpp` hub 标 | 两处 logo 的投影是**一圈描边环**，不是 CSS 的 `--shadow-accent: 0 4px 20px`（**带 offset 的虚化光**） | `theme::ShadowTier::Accent` 档位表里**本来就有**这一档，几何就是 CSS 原文（0/4/20，逐主题核对过），两处却各自手写环来近似 —— 同一个 token 被近似成两个不同形状（±2/半径+2/线宽 2 与 ±4/半径+4/线宽 8），而且都与 blur 无关。已合并成 `kit::LogoMark()`，投影走档位表 |
| `Page_Storyboard.cpp` `.tl-card` | 选中环画成「2px、半径+2」；选中边框画成「accent 1.5px」 | JSX 的 inline style 是 `box-shadow: 0 0 0 3px var(--accent-dim)`（**3px 扩展环**，spread 会把外圆角一并放大 3px）+ `border-color: accent-glow`。且 inline 的 `box-shadow` 会**盖掉** `:hover` 的 shadow-1 ⇒ 选中项 hover 时没有 shadow-1、但依然吃 `transform: translateY(-2px)` |
| 全树 4 处 | 「图标 + gap + 文字」整体居中的算式抄了四份（`Button` / `Tag` / `DrawJumpButtons` / `CenterTextX`） | 代价不是 4 行代码，而是**改对齐规则要找齐四处** —— 上轮把按钮里的图标字号从 14 改成 13 时只有 `Button` 那份跟着动。已抽成 `kit::PlaceIconText()`（居中 / 贴左两个入口），四处共用 |
| `HubCard.cpp` / `Page_Assets.cpp` | 手写卡片的 hover 位移要么没接、要么**接错位置** | 位移必须在 `HitTest` **之后**（`kit::Card` 的既有约定）：热区跟着位移会让鼠标停在卡片下缘 2px 内时 `hovered` 反复推出/落回。`HubCard` 还多一条：`bounds` 同时喂给绘制与整卡热区，所以要**另存位移前的矩形**给热区 |

#### 第三轮：审计 #3（`line.*` / `fill.*` 的 alpha）—— 口径错了，不是算法错了

一条**被自己的记录带偏过**的偏差。原口径是「按 `--bg-surface` 摊平成实色，已知偏差，
按明暗顺序判而不是按像素判」。那个口径把问题定性成「只能二选一」——其实不用选：
**底色是画的时候才知道的，就不该在数据层定死**。

- 改法只有一步：5 套主题 JSON 的 `line.*`（五套都是 CSS 原文的 rgba）与
  `fill.*`（只有纸墨/水墨是 rgba）**原样存 8 位 rgba**，由渲染器按实际已画内容做
  source-over 混合。ImGui 本来就是 alpha 混合的，**什么都不用加**。
- 于是不需要给 `DrawRoundRect` 加底色参数，也不需要按底色准备多套 `Derived` 变体 ——
  原先评估的「三层都要改」其实是「只需要改数据层」。
- 改动前逐主题交叉验算五套：旧 JSON 值**恰好等于** `FlattenOver(raw, bg-surface)`
  （逐通道相等）⇒ 落在 `bg-surface` 上的元素视觉不变，改动只影响非 surface 底。
- 实测复核（`out/_colorcount.ps1`，深空 `ws-grid.png`）：旧边色 `#202734` **4902 → 38**，
  新边色 `#29313E` **7 → 3987**，对照组 `bg-panel` 底色 333107 不变。
- 连带影响已核对：`MixSrgb(tone, fillMuted, N)` 在浅色两套下结果**带 alpha** ——
  那是 CSS `color-mix` 的答案（≈0.14），不是 bug。
- 新增工具 `out/_colorcount.ps1`：`_pixdiff` 只说「差了多少像素」，不说「变成了什么色」，
  而这个空档正是「算式算出来了、但没人验过真到没到像素」长期混过去的地方。

**怎么跑基线（别跳这一步）**：`Build.bat --release` → `out\_run_review.ps1 -Theme deepspace`。
⚠️ **`novel.db` fixture 是必须的输入**：没有它，侧栏树 / 检查器 / 资产树全部退化成诚实空态，
`hover-tree-node`、`hover-asset-card` 两个探针**永远不可能过**（实测：缺库时 9/11，有库 11/11）。
它由 `out\_make_fixture.ps1` 从 `out\_fixture_{schema,data,seed}.sql` 生成，
再由 `out\_run_review.ps1` 拷进 `<输出目录>\_review_reports_project\db\`。
这套输入**曾随一次仓库瘦身被删掉**（`.gitignore` 里明写着它们「是提交的」），本轮已恢复 ——
**删掉它们等于把回归判据的成立条件删掉**，而现象是「图片照常 saved、判据照常红」。
| 3 | 15 个 `line.*` / `fill.*` 的 alpha 被压平成不透明实色 | 主题层 | JSON 存的是 CSS `rgba` 叠在 `--bg-surface` 上的结果，算法自洽，但叠在哪个底上都一样（例：深空卡片边应为 `#29303E`，实现画的是 `#202734`，暗 9/9/10） |
| 4 | 15 处字号取不到档：`FontBoldAt(9.5f)` / `(16.0f)` 等 | 字体层 | 两侧都无此档，向上取档到 10.5 / 17px。设计稿有 9.5 / 12.5 / 14.5 / 16px |
| 5 | `font-weight: 500` 无字面 | 字体层 | `Fonts.cpp` 只加载 400 / 600 两档 |
| 6 | `.card.hoverable:hover` 的 `translateY(-2px)` 未接 | 页面层 | **这条记录已被订正**：`kit::Card()` 早就接了。真正缺的是**没走 `kit::Card()` 的页面级卡片**（资产卡 / 项目卡 / 向导行等手写 `DrawShadowed` 的地方） |
| 7 | `[data-theme="inkwash"]` 下 `.brand .mark` / `.hub-logo` 被 `webui/src/styles/tokens.css:206` 的 `box-shadow: none` 覆盖 | 页面层 | 水墨主题下这两个 logo 应当**没有**投影，ImGui 侧未按主题特判 |
| 9 | `.tl-card` 宽 96→128、圆角 8→10；项目中心卡片名 17.0→14.5px | 页面层 | 同上 |

**未接投影档位、且需要先补 `HitTest` 的两处**（不是漏接，是顺序问题）：
`pages/WorkspaceA.cpp` 总览 KPI 卡（`.card.kpi.hoverable`，`KpiCard` 整个没有命中测试）、
`pages/ProjectHub.cpp` 向导确认卡（`.card.glow`；只读汇总卡，hover 反馈价值低，建议只补 `HitTest` 不接档）。
**`pages/Page_Assets.cpp` 的总览网格卡在设计稿里找不到对应元素**（设计稿该页是表格而非卡片栅格），
其真实原型是 `webui/src/views/Assets.jsx:151`；按纪律不硬套档位，注释里的原型出处待订正。

### kit 小组件覆盖缺口

✅ **#10 已收**：分段页签条原有 2 套实现（`kit::Tabs` + `Page_Novel.cpp` 手写一份），
且 `kit::Tabs` 只有 `.tabs` 一档尺寸、被 dock 误用。现在提成 **`TabsSpec` 一份实现覆盖三条 CSS 规则**
（`.tabs` / `.dock-tabs` / `.novel-modes .ntab`），调用点两处都统一了 —— 见「第二轮新发现」表。

✅ **#9 收到「算式」这一层**（第三轮）。原先记的是「居中胶囊 3 处」，逐处核实后真实情况是：
**真正重复的是算式而不是控件** —— 「图标 + gap + 文字」整体居中这条式子被抄了四份
（`Button` / `Tag` / `DrawJumpButtons` / 纯文字版的 `CenterTextX`）。已抽成
**`kit::PlaceIconText()`**（`Center` / `Left` 两个入口，四处共用）。
顶栏那几个「胶囊」（项目胶囊 / 搜索触发器 / Comfy 状态项 / 状态栏项）**没有**走这条居中算式
（它们都是贴左 `padding` 排布），所以不算这份缺口的实例 —— 把不同对齐方式硬套一个签名，
只会造出一个「参数比用处还多」的组件。

第三轮新增的三个 kit 原语（都属于「一个动作只存一份」）：

| 原语 | 收掉的重复 |
|---|---|
| `kit::PlaceIconText(Rect, iconSize, gap, textW, align, padLeft)` | 居中/贴左的图标+文字落点，4 份 |
| `kit::CardHoverLift(hovered, hoverable)` | `.card.hoverable:hover` 的 `translateY(-2px)`，手写卡片各处一份 |
| `kit::LogoMark(draw, Rect, radius, iconSize)` | 两处品牌标（渐变底 + `--shadow-accent` + 水墨特判），2 份 |

已判定**不该收**：#3 缩略图格（1 处，`hoverable=false` 的 `ListCard` 形态不对，1 处不值得抽）、
#8 甘特 8 列（没有表头带、单元格是色块，套 `DataTable` 会硬造一个设计稿里不存在的表头）。
`kit::Drawer` 至今零调用，但页面上确实没有抽屉式交互，**不算缺口**。

### 门禁自身的盲区（2026-09-30 第四轮，**已修**）

三处盲区一处是「根本抓不住」，一处是「抓一半」，共同点是**文档都写着已覆盖**：

| # | 盲区 | 根因 | 修法 |
|---|---|---|---|
| 1 | `find-rect-wh-misuse.ps1` **没有 `-SelfTest`** | 传进来会被静默忽略、照常扫默认根目录并打印 `0` —— 与真通过字面相同 | 补 `-SelfTest`：造 9 真 + 6 假样本，断言「命中数 == 真样本数」 |
| 2 | 同一脚本漏掉 8 种真误用里的 6 种 | `$sizeLike` **按名字枚举** `width\|height\|cardW\|panelW\|boxW`，而它自己的注释承诺的是「变量名以 W/w/H/h 结尾」⇒ `w` / `h` / `colW` / `cellW` / `iconW` / `thumbH` 全漏 | 改成按**结尾字符**判（`^\w*[wh]$`）。**枚举永远漏，判据要按形态写** |
| 3 | `check-colors.ps1` 抓不到 `ImU32(0x…)` | 没有这条规则；而**裸** `0x…` 那条又是 `\b…{8}\b`，`0x59FFFFFFu` 结尾是字字符 `u` ⇒ 无词边界 ⇒ 两个方向都漏 | 补 `ImU32(0x…)` 规则 + 容忍 `u/U/l/L` 后缀 |

**关掉盲区的直接后果，要如实记**：`check-colors` 的豁免数从 **7 → 24**。
多出来的 17 处全是**以前看不见**的，逐条看过：

- `theme/Theme_Derived.cpp` 14 处 → 与 `Theme.cpp` 同一类（它就是派生调色板的源码），**补进 `$Zones`** 并写明理由。
- `kit/Draw.cpp` 的 `ImU32(0x38FFFFFFu)`（box-shadow 上沿高光的近似值）→ **不做成 token**：
  设计稿里没有这个色，做成 token 会让人去「对齐」一个不存在的设计值。行内 `// theme-ok <理由>`。
- `theme/Theme_Color.cpp` 的 `0xFFFFFF00u` → 它是**位掩码**不是颜色，行内 `// theme-ok <理由>`。
- `kit/Widget_Status.cpp` 的 `ImU32(0x59FFFFFFu)` → **真违规，已修**：这个值早就有 token
  （`Derived::progShimmer` = 白 35%，逐位就是 `0x59FFFFFF`，注释都写着 `ui.css:448`），
  只是这个调用点没接。顺带修掉一个后果：写死的值不跟主题走。

另外两处「文档与代码不一致」也顺手修了：`check-colors` 自检汇总里的样本数**写死过**
（写着 11、实际早不是）→ 改成现算；自检样本补了 `ImU32` 与带后缀两条真样本
（**加规则必须同时加真样本**，否则「规则写错了」没有任何机器看着）。

⚠️ 门禁的「绿」在第四轮前后**不可直接比**：`check-colors` 的判定面变宽了。
以后拿第四轮之后的 manifest 当基线。

### 真缺陷（留给后续）

- ~~**`duplicate-hits: 815` 尚未判定**~~ → ✅ **已判定并归零（2026-09-30 第二轮）**。
  815 次 = **2 个 `.tree` 折叠箭头 × 约 408 帧**，不是 815 处缺陷。判定方法：
  ① 把 `DeclareNestedHit` 只加在 `kit::Tree`（设计性嵌套的那一处）→ 815 掉到 324；
  ② 把诊断信息从「只报自己」扩成「连**对方**矩形一起报」→ 剩下的是
  `side-tree#0##0 与 min=(66,152) max=(168,176) 交叠 102x8`，对方是**小说侧栏第二行跳转按钮**
  （`DrawJumpButtons` 溢出自己的盒子，见「第二轮新发现」）；
  ③ 修掉那处溢出 → **0**。
  结论：`duplicate-hits` 这条判据**本身是有效的**（它抓到了一处真布局缺陷），
  只是原来缺一个「设计性嵌套」的声明通道而把 `.tree` 那一类也计了进去。
  现在 `kit::DeclareNestedHit(childId)` 只豁免**三条件同时成立**的情况
  （同 id / 矩形是包含关系 / 同一帧），任一条不成立照常报 —— 声明没对上时**不许静默放行**。
- ~~`kit::ResetInvertedRectCount()` / `ResetDuplicateHitCount()` **全仓零调用点**~~ → ✅ **已修（第四轮）**：
  `RunReview` 开头显式归零。今天本来也不是缺陷（它是总入口，所有泵帧都在取证内部），但只要将来
  有任何代码在取证前泵过帧，那些帧里的违规会**累加**进取证计数 ⇒ 判据报红而根因在一次早已结束的
  交互会话里。归零的代价是 0 行风险：**判据只对它自己跑出来的东西负责**。
- ~~manifest 的 `overlay-clicks` 只有 `7/7` 一个数，**没有分项也没有原因**~~ → ✅ **已修（第四轮）**：
  与 `hover-probes` 对齐 —— 有失败时该行带 `broken=N` 后缀，并**另起一行**
  `overlay-clicks-failed=<名>：<原因> | …`。只报一个数的坏处是：红了以后读 manifest 的人
  既不知道是哪两条、也不知道为什么，只能去翻 `.stdout.txt`，而脚本读的是 manifest。
- ~~`unstable=N` 二义：同时表示「注入没到位」和「产品真的坏了」，manifest 层面没解。~~ → ✅ **已修（第四轮）**：
  `unstable` 实际混了**四种**成因（注入没到位 / 哨兵失效 / 页面在动 / **探针坐标点空了**），
  按**动作**拆成 `inject-failed` `sentinel-dead` `moving` `off-target` `hover-not-painted` 五档，
  有失败时另起一行 `hover-probes-failed=…` 写明细。聚合值 `unstable=` 保留（脚本按正则读它）。
  这么分的理由很实际：**「去改探针坐标」和「去查宿主后端」和「去改产品 hover 分支」
  是三个完全不同的人干的三件不同的事**，挤在一个数里等于把三句话写成一句。
- `tools/check-colors.ps1` 把 `kit/Views.cpp` 列为**按文件**的豁免区（`$Zones`），且文件不存在时直接 `exit 1`。
  ⇒ **`Views.cpp` 这个文件名是承重结构**：删掉或改名门禁就红（`check-colors.ps1:48` / `:229-232`）。
- 本文早期按 `文件名:行号` 引用旧位置（`Review.cpp:1550`、`WorkspaceA.cpp` / `ProjectHub.cpp` 的半像素表与 ListCard 计数），
  大文件拆分后已失效；本文已改用文件名 + 符号名，历史条目在 `git log` 里。

---

## 跨轮硬事实（别再踩）

### 字体与图集（ImGui 1.92 字体系统）

- **中文发糊是结构性的**，三条根因缺一仍糊，依据全写在 `kit/Fonts.cpp` 顶部注释：
  ① `ImFontConfig::RasterizerDensity` 缺省 1.0，而它是 1.92 起**唯一**能给字形光栅化加分辨率的旋钮（本工程 `DisplayFramebufferScale=(1,1)`、WGL 后端报不上更高密度 ⇒ `CurrentPixelDensity` 恒 1）；
  ② `PixelSnapH=true` 把 `OversampleH` 压成 1，而 `OversampleV` 在 auto 模式下**恒为 1**，且 CJK 糊的正是纵向笔画；
  ③ 本工程字号大量是**半点**（10.5/11.5/12.5/13.5/14.5，来自设计稿 CSS px），stb_truetype 按整数高度出位图，1:1 光栅化后缩到半点必然重采样。
  **修法**：`kRasterizerDensity = 2.0f`，四处 `ImFontConfig` 全部设上。A/B 实测平均边缘梯度 22.55 → **39.26（+74.1%）**、墨迹像素只 +1.9%（目击「变粗」被测量否掉）。
- **刻意不设 `ImFontConfig::GlyphRanges`**：1.92 起那是 legacy 路径，只有不声明 `RendererHasTextures` 的后端才读
  （`imgui_draw.cpp:3514` 跳过、`:3568` 唯一读点）。换回 legacy 后端时**必须重新接上**，否则界面全是 `'?'` —— 那不是回归，是换了一种加载协议。
- **别开 `ImTextureFormat_Alpha8`**：core 支持，但上游 `imgui_impl_opengl3.cpp` 把格式硬编码成 RGBA
  （`:712` / `:733` 以 `GL_RGBA` 上传，`:739` 却按 `BytesPerPixel` 算行跨度）⇒ 开了图集直接烘不出来。**这是 vendored 1.93 WIP 的缺口，不是本侧问题。**
- 图集统计用 `ImTextureData::GetSizeInBytes()`（读 `BytesPerPixel`），**不要**硬编码 `Width*Height*4` —— 旧代码求和后打成 `vram=`，
  标签是错的（`Pixels` 是 CPU 堆缓冲，GPU 侧只有 `TexID`）。64 MB 是项目自定红线、不是显存预算，告警已改成一次性快照 + 单帧增量 + 增长率三档 `Warn`。
- 判定「字体尺寸」时别读反：`ImDrawList::AddText(font, size, …)`（自绘路径，绝大多数调用点）**不取整**；
  `ImGui::PushFont` 走 `GetRoundedFontSize` 取整，会落到另一个 baked 尺寸并触发 on-demand 重烘焙。预烘焙清单 `kUiSizes` 保持半点正是为了配合自绘路径，**不要「顺手」改成整数**。
- `BuildGlyphRanges()` 曾把**局部** `ImVector<ImWchar>` 的指针交出去就返回（悬垂，1.92 之前的老契约）。**该函数已随字形集扫描整段删除**，但形状仍要认：凡是把临时容器的指针交给「后续帧才回读」的 API，就是悬垂。

### 绘制与命中

- `AddRectFilled(p_min, p_max, col, rounding)` —— **颜色在圆角前面**。写反的三种形态全是「画了等于没画」，编译期完全沉默
  （`kit/Draw.cpp` 有 ⚠️⚠️ 注释 + `tools/find-draw-arg-order.ps1` 门禁）。
  **判据是 `_VtxCurrentIdx` 有没有推进**，不是像素 diff：实测 135 次调用一次都没推进，直接坐实「发出但被丢弃」。
- `kit::Rect` 四参是 `(minX, minY, maxX, maxY)`（`kit/Widget_Core.h:20`），当 (x,y,w,h) 用 ⇒ `max < min` ⇒ 整块不画也不可点。
  要 (x,y,w,h) 就用 `RectAt`。
- 遮罩**只画不注册**（`kit::ScrimPaint`，`kit/Overlays.h:21` 有说明）：注册成全屏热区会跟面板内容抢同一个 `HoveredId`，
  而 ImGui 里**先注册者独占**（`imgui.cpp:5161`）⇒ 后果不是「遮罩没生效」而是「面板内某些控件点不动」，且**哪一处坏取决于绘制顺序**。
- **child 的裁剪与层级按 draw list 生效，不按代码嵌套**：要建视口的区域整块改用容器自己的 `drawList()`；
  浮层/模态用 `GetForegroundDrawList()`，否则会被后渲染的 child 盖住。
- 「不画」用 `kit::ColorTransparent()`，别写 `IM_COL32(0,0,0,0)` —— 它是硬编码颜色里最常见的一种，豁免一多门禁等于没有。
- `kit::PinAnimation` 语义（接过渡后的前置条件）：钉住时**连续动画**（脉冲/旋转/呼吸）冻结在给定时刻，
  **过渡**（`TransitionTo` / `TransitionColorTo`）**直接落终值** —— 冻在半路会让静息截图每轮 md5 都变。
  另调一次 `iam_pool_clear()`，避免解绑瞬间闪一下。

### 度量

- **文字垂直居中的口径**（先定死，否则每次量出来的数都不一样）：`ImFont::RenderText` 里 `const float line_height = size;`
  ⇒ **行盒高 = 请求字号**、行盒顶 = `pos.y`，所以 `Δ = (pos.y + fontSize/2) − 容器中心Y`，
  正确写法 `pos.y = 中心Y − fontSize/2`，已封装成 `kit::CenterTextY`（`kit/Draw.h:137`）。
- ⚠️ **两条已踩过、别再走的坑**：① **不要**改成按真实 Ascent/Descent 算 —— 这版 `Descent` 是**负数**
  （size=13 时 asc=11 / desc=−3），照它算会把字往下推，方向与「字偏高」正好相反；
  ② `min.y + (h − size) * 0.5f` 与 `center().y − size * 0.5f` **代数恒等**，曾有一条注释说前者「逐字号各偏各的」，**那句是错的**，已撤回。
- 残留的 −0.45px 光学偏差（CJK 墨迹盒中心在基线上方 0.38em）**已知且接受**。判据：`|Δ| ≤ 0.5` 不动，`|Δ| ≥ 1` 才算缺陷。

### 取证与判据纪律

- **构建失败就别接着跑取证**：改坏一个 API 会让编译挂掉，而取证脚本照样对**旧二进制**跑出一份数字正常的报告。判据只认「构建成功**且**取证 PASS」，构建退出码要单独判。
- **像素差不是内容指标**：离屏渲染跨进程约 2000px 噪声、跨构建约 40000px；小于它不必当回事，大于它**也不能**直接判成回归。
  判「内容有没有丢」用**同一次运行内**的对照页 + 读像素剖面（沿一条线采样 RGB 看渐变）。
  本仓两次「投影差 180px / 0px」的真相都是**界面里印着的工程路径**（`rv77`→`rv78`），与投影无关。
- **md5 逐张比对在跨运行场景下不是可用信号**：同一个二进制连跑两轮，79 张里只有 14~15 张 md5 相同
  （差异是同一个共享元素上一个 175px / 34×10 小块的字形亚像素抗锯齿）。另有 5 张每轮都不同 —— 裁出来读是**输出目录名**。
  拆分类重构的等值证明改用「**manifest 的 `# shots:` 指标行逐字节相同**」。
- **判据自己也会骗人**：「报通过的原因」若与「它本该抓的缺陷」同形，它比没有判据更坏。注释必须写**事实描述**（谁抓谁），不能写意图。
- **反向验证是自检的最低要求**：故意把修复关掉重跑，那条判据必须变红（`overlay-clicks` 关掉修复后 2/4 + `overall=FAIL`，而其余 78 项全绿 ⇒ 证明它是唯一能抓住该缺陷的信号）。
  恢复后仍红 = **判据自己期望写错**，一条永远红的判据和一条坏掉的判据没有区别。
- **一个信号至少对应三种原因**：注入没到位 / 环境塌了（视口 0×0、坐标落在热区外）/ 目标真的没变。
  只看命中数必然改错地方，**两个方向都错**。做法是在探针里记下「系统实际看到什么」，并在没到位时**先于**其余分支短路。
- `ScrollRegion` 判据要打出**期望上限**和 **ImGui 侧上限**（`GetScrollMaxY()`）：两者不等说明根本没修好（粘性请求能到位 ≠ 用户滚轮能滚）。
  判据的多个分支必须**平级**写 —— 把 `maxScrollY > 0` 塞进 `if` 里，「内容压根没超出」和「请求没被核销」会一起静默通过。
- 「覆盖 N 个工作区」不等于「覆盖每个工作区的每个视图面」；fixture 缺数据也会造成覆盖洞（**页面拍到了，分支没跑到**）。
- 「证明性」截图必须**成对**（默认态 + 目标态）：单拍一张时它跟谁都不重样，判据抓不到「这个入口是死的」。
- 判据的产物是**证据不是 build 垃圾**：基线对照目录、取证目录、manifest 都要留在盘上可复核。
  只写「已对照跑过基线」而把目录清掉，等于结论只在提交信息里。

### 工具与环境

- **`.ps1` 必须带 UTF-8 BOM**：PowerShell 5.1 对无 BOM 文件按系统 ANSI 码页（中文 Windows = GBK/936）解码，
  中文注释被解成乱码，某些字节序列恰好凑出引号/括号 ⇒ 报 `ParserError` 而**行号与真实原因毫无关系**。
  判据：`[System.Management.Automation.Language.Parser]::ParseInput([IO.File]::ReadAllText($f), …)` 报零错而 `powershell -File $f` 报 ParseError ⇒ 就是 BOM 问题。
  用 `write` 工具生成的 `.ps1` 没有 BOM，交付前一律前置 `EF BB BF`。
- **改 `src/ui/imgui/**` 只用 `read`/`edit`/`write`**，不要 `Set-Content` / `Out-File` / `[IO.File]::WriteAllText` ——
  它们按 ANSI 重编码，会静默把 `⚠️`、`→` 这类 GBK 编不了的字符换成 `?`，编译照过、中文照在，标记没了。
  必须用脚本改字节时全程只走 `ReadAllBytes` / `WriteAllBytes`，**输出缓冲与输入游标必须独立推进**（共用游标时「替换完跳过差值」一旦算错就会吞掉紧跟标识符的那一个字节）。
  改行尾要归到 CRLF 时先在 4 份合成样本上自检（LF 串 / CRLF 串 / 混合 / 已损坏的 LF CR），否则会写出 `0A 0D` 让 git 把源文件判成二进制。
- 判别文件编码别看「高位字节里 0xC2-0xDF 多还是 0x81-0xFE 多」（GBK 汉字首字节本来就落在 0xB0-0xF7，会把 UTF-8 误判成 GBK）。用严格解码试一次：
  `$s = New-Object System.Text.UTF8Encoding($false,$true); try { $null = $s.GetString([IO.File]::ReadAllBytes($f)); 'UTF-8' } catch { '非 UTF-8' }`
- PowerShell 里**函数名不要用 `W` / `Sl`**（是 `Set-Location` 的内置别名，PowerShell 优先解析别名，报的却是「位置参数无法找到」）；
  变量名不要只靠大小写区分（`$A` / `$a` 不敏感，大写赋值会覆盖小写那个）。
- 「看着加了、实际等于没加」比没加更危险：编译器不报错、像素看不出来、只有真去滚才发现。给被修的量加**只报一次的哨兵** +
  一次**主动探测**，并且哨兵与判据**都要接到最终 verdict**（只写日志而 `overall=PASS` 等于「报错但绿灯」）。

### 三条归因纠正（别再拿旧结论去改代码）

1. **「V 链只画 8 个阶段、真实是 12 个」不成立** —— `pipeline::StageId` 是 V0–V11（连续性链），
   小说侧的真实视觉链是 `VisualStageId` V1–V7（`src/novel/NovelVisualStages.h`），**V8 是另一件事**（连续性校验）。**代码是对的，没改。**
2. **S 编号撞名** —— `pipeline::StopPolicy.cpp:7-11` 的「S1–S4 预算」与 `novelcore::StopCode` 的 S1–S12 是**两套**编号，
   旧界面拿前者当后者标，才需要拆成两张卡。同源撞名还有 `chapter-vstage` 曾经被读成 8 列甘特。
3. **「`ContinuityIssue` 没有 shot id，要改 `src/novel/`」不准确** —— 两个 `ShotRow::id` 就在 `detail` 文本里。
   真正缺的只是**结构化字段**（现在得正则解析人类可读文本，把一条契约绑在字符串格式上）。
