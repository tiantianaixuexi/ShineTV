---
id: root.agents
kind: instruction
status: current
scope: repository
source_of_truth:
  - CMakeLists.txt
  - Build.bat
  - src/
  - tools/
  - refactor/
last_verified: 2026-09-30
---

# ShineTV Studio AI 协作规则

> **现状（2026-09-30）**：前端已是 **Dear ImGui**（OpenGL 3.3 / WGL），配过与构建**零 Qt 依赖**。
> 旧的 Qt（QWidget + QML）前端已整体删除。
>
> **UI 方向（2026-11-23 用户定调，覆盖旧纪律）**：**不做像素裁量**。
> 界面一律**优先用 Dear ImGui 自带组件**（`ImGui::Button` / `Selectable` / `TreeNodeEx` /
> `BeginTable` / `InputText` / `Combo` / `Checkbox` / `CollapsingHeader` / `TabBar` …），
> 样式贴近主题 token 即可，不做逐像素复刻、不做「逐位等价」证明。
> `webui/` 与 `refactor/design-spec.md` **降为布局与文案参考**，不再是像素验收基准；
> 历史「1:1 复刻」纪律就此作废（史料在 `refactor/轮次记录.md`）。
> 像素计数类判据只作**回归烟测**，数字变化不算回归；功能性判据（hover / overlay /
> 滚动 / 反向矩形 / 真图链 / 快捷键）照旧要绿。

## 开始工作前

1. 读 [`refactor/README.md`](refactor/README.md)（**文件地图：要改什么改哪个文件**）与本文件。
   动界面前按需查 [`refactor/design-spec.md`](refactor/design-spec.md)（布局/文案/配色**参考**，
   不是像素验收基准）与 [`refactor/进度.md`](refactor/进度.md)。
2. 先检查工作区已有改动。unexpected changes 属于用户，不能回滚、覆盖或顺手整理。
3. 用源码符号和路径定位事实。文档只做导航；源码、CMake 和可执行检查优先。
   **不要把推断写成通过** —— 没跑的验证写「未执行」。
4. 改任何视觉前先确认**归因在哪一层**（页面层 / 主题层 / 字体层 / 宿主层）。
   同样的现象在错层上改，会改成另一种错。

## 双端架构（唯一的心智模型）

| 端 | 位置 | 地位 |
|---|---|---|
| Web 设计稿 | `webui/`（React + CSS） | 布局与文案**参考**（2026-11-23 起不再是像素基准） |
| C++ 实现 | `src/ui/imgui/`（Dear ImGui） | 真正的界面；**优先用 ImGui 自带组件**实现 |
| 业务层 | `src/` 其余（`shine_core`） | 零 Qt、零 ImGui，**一行不改** |

⚠️ **`webui/` 不在版本控制里**（`.gitignore` 最后一行就是 `webui/`）。
新 clone 拿不到它 ⇒ `refactor/design-spec.md` §2 内联了全部 31×5 色值，
那份表才是可复现的记录。要核对 CSS 原式前，先确认 `webui/` 还在本机。

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
- `Build.bat` 先检查 `SHINE_MINGW\bin\g++.exe`（默认 `C:\msys64\mingw64`，可用环境变量覆盖），
  找不到就 `exit 2`。让 CMake 自己挑编译器会挑出一个「看起来能编过、但不是你以为的那个」的二进制。
- 手动等价命令：

  ```powershell
  cmake -S . -B build\release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
        -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe
  cmake --build build\release --parallel
  ```

- ⚠️ **不要在同一个 build 目录上并发 `cmake --build`** —— 会撞 exe 锁。
- ⚠️ **替换 `third/` 里的第三方源码后，第一轮必须 `--clean` 全量构建**（2026-11-23 实测）：
  增量构建曾把新旧两版 imgui 的编译产物混链，表现为**确定性崩溃**（`RefreshTexts`
  0xC0000005，`--clean` 后消失、之后几十轮再未复现）——增量构建出看不懂的崩溃，
  先怀疑混链、别先怀疑代码。
- ⚠️ **构建失败后，不要读任何截图或日志** —— 它们描述的是上一个二进制。

## 验证顺序（改完 C++ 必走）

```powershell
powershell -File tools\check-layers.ps1      # 分层门禁
powershell -File tools\check-colors.ps1      # 硬编码颜色
powershell -File tools\check-theme.ps1       # 主题表完整性
Build.bat --release
.\build\release\ShineTVStudio.exe            # 实际启动，肉眼比对 webui 同页面
```

自动化 1:1 回归（**必须带 fixture**，否则判据的成立条件不成立）：

```powershell
out\_make_fixture.ps1                        # 首次 / fixture 丢了之后跑一次
powershell -File out\_run_review.ps1 -OutDir "$PWD\build\shots" -Theme deepspace
```

产物 = 每张 PNG + `shots-manifest.txt`。**manifest 结尾的 `overall=PASS|FAIL`
必须与进程退出码一致**（PASS↔0 / FAIL↔1）。判据说明见 `design-spec.md` §9。

⚠️ **截图不可跨轮逐字节比对**：同一个二进制连跑两轮，80 张里有 48 张字节不同
（时钟 / 动画相位 / 日志时间戳）。要验证**无损重构**时，取两轮之间稳定的那 32 张做子集比
（实测「32/32 未变」即足够）；想做真正的跨轮像素回归，得先把不稳定来源钉死。

两个最容易骗自己的假信号：

| 假信号 | 真相 |
|---|---|
| **缺 `novel.db` fixture** | `SeedReportFixture()` 不建它。少了它侧栏树 / 资产树退化成诚实空态，`hover-tree-node` / `hover-asset-card` 永远命中不到 —— 同一份代码缺库 `9/11+FAIL`、有库 `11/11+PASS`。「图照常 saved、判据照常红」就是它 |
| **默认跑了一个旧产物** | `Build.bat` 的产物在 `build\debug\|release\`。`scripts\run_reviews.ps1` 曾把 `-Exe` 写死在 `build\`（那里躺着一个更旧的二进制，而它照样报 PASS）—— **现已修成按 `build/release → debug → build` 解析并打印时间戳**。纪律不变：核对日志里那行 `exe:`（**编译失败时脚本仍会跑起来**，那两轮数字全是旧二进制的） |
| **以为「异步的图」拍得到** | 真设定图是 worker 解码 → `PostToUi` → 上传 GPU 的**异步**链，只泵一两帧抓到的必然是占位画 —— 而「链断了」与「拍得太早」在截图上完全一样。**等待放在 `Shoot`（唯一入口）**，见 `ReviewPixel.cpp`：按调用点补等待时漏一处就是一处随机失败（实测漏过 `assets-leaf-selected` —— `SelectAsset` 之后直接抓图，新实体的图那一刻才第一次被请求，同一二进制两轮一轮拍到一轮没有）。判据 `sheet-images=settled requested=N pending=0` 写进 manifest 且**进 overall** —— 两半缺一不可，`pending==0` 单独看与「一张都没被请求过」同形 |
| **以为「判据红了就是产品坏了」** | 判据**自己的前提**也会坏。实测：`viewer-full` 报「大图一次都没请求过」而那张大图**明明在屏上**，连跑 5 轮 5/5 —— 根因是 `kit::Image` 两档 key 撞车（见「号段必须先清后置」），而**撞不撞取决于整条路径字符串的 FNV，路径里含 `SHINE_IMGUI_REVIEW` 的输出目录** ⇒ 同一份代码换个 `-OutDir` 就从 PASS 变 FAIL。**「换个目录名结果就翻」是判据前提坏了的指纹**，不是随机。前提要自己带一条自检并进 `overall`（现在是 `image-keys`） |

**数某个具体颜色的像素**（`out\_colorcount.ps1`）是这一族唯一可靠的取证手段：截图跨轮不可复现
（80 张里 48 张会变），而**数某个色值免疫**。真设定图在 fixture 里是两张纯色 PNG
（`#FF2D55` / `#7CFF00`，刻意选在五套主题调色板之外），上没上屏一数便知。
⚠️ 缓冲是 **RGBA**（`R` 在偏移 0）—— 权威是 `Capture.cpp` 里那句「已经是 RGBA8，不要再交换通道」，
**不是**形参名（它曾叫 `bgraPixels`，已订正）。按名字当成 BGRA 会恒数到 0，而 0 恰好也是
「图没上屏」的读数 ⇒ 一次就伪装成产品缺陷。

**真图这一族的判据都进 `overall`**（都在 `ReviewScene.cpp` 写、`ReviewVerdict.cpp` 判）：

| 判据 | 成立条件 | 它挡的是 |
|---|---|---|
| `sheet-images=settled` | `requested>0 && pending==0` | 抓图太早（图还在 worker 手里）**与**「调用点压根没请求过」—— 后者单独看 `pending==0` 时与前者同形 |
| `sheet-recover=ok` | 清空 GPU 纹理缓存后真图像素数**与清空前相等** | `kit/Image.h` 约束 1 被写坏（只在 `State==NotLoaded` 时请求）⇒ **「图闪过一次就没了」**，不报错、静息截图照样 saved |
| `viewer-full=settled` | 大图档 `requested>0 && pending==0`**且**两屏（图片舞台 / 联系表）纯色像素 `>0` | 查看器那条**第二档尺寸**（`kit::RequestImageFull`，2048）—— 它**只在查看器开着的那一帧**才被请求，所以「没请求过」「请求了没落地」「画的是 `Art` 占位」「打开的不是预期那一页」四种原因在截图上完全一样（都画占位画） |
| `image-keys=ok` | 两档 key 的号段自检 0 撞车（256 条就地探针） | 见下面「号段必须先清后置」——**上面那条判据的成立前提**，它一坏 `viewer-full` 就假红 |
| `imageflow-result=ok` | 出图页「结果」页签里**当前选中镜**的成图纯色像素 `>0` | 那一格画的是 `Art(seed=6)`，而页签名就叫「结果」（最容易被读成出图结果的一处） |
| `film-strip=ok` | 出片页「成片条」第 1/2 镜两种纯色**各 >0**（一张图数两色） | 格子画占位画；或只接通了第一格（一个色的判据在「只画了第一张」时照旧绿） |
| `recent-products=ok` | 总控「最近产物」：**非 shot 来源**与 shot 来源两种纯色各 `>0` | 只列 T 链账本（执行体未接入 ⇒ 恒空）；或那条查询退化成 `source_kind='shot'` |

⚠️ **「两处都在同一张卡/条上」的判据要一次抓图、数两个色**（`Session::GrabCountTwoColors`）：
抓两次是两张逐字节相同的图，第二张还必须 `registerDriven=false` 才不会撞上「受控图不许重样」。

## ✅ 第二十三轮（2026-10-02）：页面层裸文字 / 字面量字号清零 + 两次拆分

`find-raw-text` 基线 **232 条 / 22 文件 → 0**（棘轮基线文件已清空，只剩注释头）。
`theme::font` 补 9 档（`kNano/kFine/kSmall/kCardTitle/kModal/kPageTitle/kViewTitle/
kChapter/kHubBrand`，每档注释写「设计稿在哪一处用它」）；`kit::Text` 补
`TextClipped` / `TextClippedWidth` / `TextWidth` / `CenterY` 四族**逐位转发**。
`Shell_Sidebar.cpp` 735 → 342 + `Shell_Sidebar_Tree.cpp` 298；
`Shell_Chrome.cpp` 653 → 458 + `Shell_TopBar.cpp` 271。**pages/ 最大文件现 608 行。**
90 张 `overall=PASS` · 7 门禁 + 5 自检全绿 · 9 行像素计数判据跨轮逐位相同 ·
**最好一次读数：基线旧二进制 → 本轮新二进制，90 张截图逐字节 0 张不同**。

### 这一轮新踩的三个坑（都写在这里，别再踩）

- ⚠️⚠️ **取证输出目录名会被画进像素**。工作区页面把**工程根路径**印在
  `ViewHeader` 的副标题上，而路径里含 `<OutDir>\_review_reports_project\`
  （`_run_review.ps1` 把 `novel.db` 拷进去）。⇒ **基线与本轮必须跑进同名同父的目录**，
  否则凡是印了路径的图全变：实测「`shots-r23-base` vs `shots-r23b`」报 **41/90 张不同**，
  同名目录下只剩 0/90。**这是「换个目录名结果就翻」那一族的新变体**，
  且症状（大片图变）与真回归一模一样。裁片证据：`build/evidence-idconflict-{dialog,clean}.png`。
  `out\_verify_r23.ps1` 的参数注释里已把这条要求写死。
- ⚠️ **`AddText(…, "—", "—")` 是空区间，直译会多画一个字**。`text_begin == text_end`
  时 ImGui 早退、**什么都不画**；换成 `kit::Text(draw, pos, "—", …)` 就是 3 字节、
  破折号真的出现 —— 一次「看起来像顺手修好的」像素改动。
  `OverviewTables.cpp` 的降级列就是这一处，收编时写成 `std::string_view("—", 0)` 并注明。
  **收编裸 `AddText` 时必须逐个核对 `begin/end`：不等长就是有意为之，不能当笔误「修好」。**
- 🐛 **（已定位，本轮未修）`kit::Tree` 的行 id 跨父节点撞车**（✅ **2026-11-23 已修**：
  行 id 改先序路径键 `<父行键>##<同父下标>`，折叠箭头 `<行键>#t`，零分配；原格式
  不带父节点身份的撞车形态见下）：
  `Widget_Tree.cpp:68` 的 `"<id>#<depth>##<i>"` 里 `i` 只算**同一父节点内**的下标，
  父节点下标不在 id 里 ⇒ 小说侧栏树「第二卷第 0 章」与「第一卷第 0 章」都得到
  `side-tree#1##0`。两行同时可见时 ImGui 报
  `Programmer error: 2 visible items with conflicting ID!` 并框红，**后注册那行点不动**。
  ⚠️ 本仓的 `duplicate-hits` 判据**抓不到它**（那条查「同帧重叠热区」，这里是「同 id 不同矩形」）。
  复现是间歇的（取决于滚动位置与抓帧），所以取证里时有时无 ——
  别把「某两张图这轮变了」直接读成回归。修法与注意事项见 `refactor/轮次记录.md` 第二十三轮块。

### 这一轮立的等值性判据（比改动本身更值得复用）

`out\_verify_r23.ps1` 一条命令跑完四件事：七门禁 + 五自检 → `overall=PASS` →
**像素计数 verdict 逐行相同**（等值性主证据）→ **截图差异集 ⊆ 同二进制自己的波动集**。
最后一条是关键的第二条腿：逐字节相同会漏掉动画相位 / 主题过渡这类正常波动；
只看数量又会漏掉「变的恰好是噪声里那几张」的巧合。**两边都比，并把集合差列出来。**
配套两个小工具：`out\_shotdiff.ps1`（两目录逐字节差集）、`out\_diffmap.ps1`
（差异像素按 32×32 聚类，回答「改的是**哪一块**」而不是「改了多少」）。

⚠️ 后两条都是「按现用档位的 pending」判的（`PendingImageFullCount()`），**不要**拿总的 `PendingImageCount()`：
注册表是进程级、只增不减，某张图一旦不再被任何可见位点绘制（换视图 / 网格切详情），
被 GPU 淘汰后就永远停在「Ready 但没有纹理」= pending ⇒ 等待永远不会归零，判据以「链断了」的样子假红。

## 门禁与扫描器

| 工具 | 抓什么 | 自检 |
|---|---|---|
| `tools\check-layers.ps1` | 分层越界（`src/` 出现 Qt 头、页面层直连底层） | — |
| `tools\check-colors.ps1` | 硬编码颜色字面量 | — |
| `tools\check-theme.ps1` | 主题表 token 数与顺序 | — |
| `tools\check-i18n.ps1` | 文案规范 | — |
| `tools\find-silent-truncation.ps1` | 列表按容器高度静默截断 | `-SelfTest` |
| `tools\find-draw-arg-order.ps1` | 绘制原语的**颜色 / 圆角**参数错位 | `-SelfTest` |
| `tools\find-rect-wh-misuse.ps1` | `kit::Rect` 四参当成 `(x,y,w,h)` | `-SelfTest`（9 真 + 6 假，第四轮补上；**第四轮之前**带上这个参数会被静默忽略后照常报 `0`）；另可用 `-Root <样本目录>` |
| `tools\find-raw-text.ps1` | 页面层裸 `AddText` 与 `FontAt/FontBoldAt/MonoAt(数字字面量)`（只扫 `src/ui/imgui/pages/`） | `-SelfTest`（8 真 + 8 假）；**棘轮基线** `tools\find-raw-text.baseline.txt`（存量 249 条/23 文件，收编后 `-WriteBaseline` 收紧，只降不升；基线外新增 ⇒ exit 1） |

三个扫描器抓的典型形态：

| 形态 | 症状 |
|---|---|
| `if (y + 24.0f > body.max.y) break;`（含 `> body.max.y - 20.0f` 这种「给页脚留位」的变体） | 列表按容器高度静默截断，用户看不到「还有第 N+1 条」 |
| `Rect{x, y, cardW, 192.0f}` ⇒ `max.x < min.x` | 控件**整块不画也不可点**，编译不报错 |
| `AddRectFilled(a, b, radius + grow, col)` | 颜色位收到几何量，**全透明黑、画了等于没画**，编译不报错也不崩 |

⚠️ 门禁自身的盲区：`refactor/记忆.md` §「门禁自身的盲区」记了**已修的三处**与修法：
`find-rect-wh-misuse.ps1` 第四轮已改成**按结尾字符**判（`^\w*[wh]$`，不再按名字枚举，
裸 `w`/`h` 与 `colW`/`thumbH` 现在都抓）、`check-colors.ps1` 第四轮已补上 `ImU32(0x…)` 规则
—— **更早的文档里那句「这些全漏网」已经过期**，别照着它放弃检查。
**扫描器报 0 不等于没有。** 改门禁会让当轮「绿」失去可比基线，
应单独一轮做，且改完必须先跑它自己的自检证明仍能抓。
⚠️ 第四轮那笔「关掉盲区」的账：`check-colors` 的 `7 → 24 acknowledged` —— **判定面变宽，不是退化**，
别拿更早的 7 当基线。

## ✅ 已修（2026-10-01 第十七轮）：字形只有请求字号的 0.76 倍

ImGui 的「字号」是把 `hhea.ascent − descent` 这个**跨度**映射成那么多像素
（`imstb_truetype.h:2665` 的定义 + `imgui_draw.cpp:4801` 的 `scale_for_layout`），
而 CSS 的 `font-size` 是 **em 盒**；msyh 的 A−D ≈ 1.32 em ⇒ 画的字是请求值的 **0.7577 倍**。
**已修**：`kit/Fonts.cpp` 的 `FaceExtraSizeScale()` 从字体文件现读 `head.unitsPerEm` 与
`hhea` 跨度，逐字面设 `ImFontConfig::ExtraSizeScale = (A−D)/upem ≈ 1.32` ——
⚠️ **第十一轮记的候选公式 `upem/(A−D)` 是反的**（那是缩小）：`:4747` 把 E 乘进
ScaleFactor，:4801 字形位图与 AdvanceX 按 `ScaleFactor × Size` 算（不再除回）⇒ 字形 ∝ E；
:4780 的行高度量除回 E **不变** ⇒ 基线体系不动，只有墨迹与步进 +32%。
推论（仍然成立）：**凡「按 CSS 行盒算位置」的自绘文字，基线用 `kit::AscentOf`（0.8017）
照旧对齐** —— 校正后 baked->Ascent 没变，不要因为字变大就去改基线常量。

要设计稿的实数时**打探针页**（`webui/dist` 能跑 + `chrome --headless=new --dump-dom`），
别靠读 CSS 猜；Chrome 无头写不了文件，量墨迹要用 canvas 的 `actualBoundingBoxAscent`。

## 地雷（都已踩过，别再踩）

- **颜色**：「不画」用 `kit::ColorTransparent()`，别写 `IM_COL32(0,0,0,0)` ——
  它是硬编码颜色里最常见的一种，豁免一旦多了门禁就等于没有。
- **`kit::WithAlpha` 是调制（原 alpha × 参数），覆盖 alpha 要用 `WithAlphaSet`**。用错时
  不报错、只是淡到看不见（实测把 KPI 柔光的 alpha 做成了 6e-5，而卡片文字截图全正常）。
- **`0xRRGGBB`（取证/`CountColorPixels` 要的）与 ImGui 的 `ImU32` 是两个字节序空间**：
  默认下 R 在**最低**字节。把前者的结果喂回 `ImGui::ColorConvertU32ToFloat4` 会静默通道错位
  （实测把 `#1C333D` 算成 `#373323`）。
- **「数某个色的像素」的判据必须有上下界**：色一旦退化成一块面（卡底色 35 万像素），
  只判下界的判据会**判绿**（实测读到 350271 判 ok）。上界的依据要写进注释。
- **`AddRectFilled` 的签名是 `(p_min, p_max, col, rounding)`**，颜色在圆角**前面**。
  写反的三个信号全是骗人的：编译过、不崩、同一帧别的图元照常出图。
  判据是 `_VtxCurrentIdx` 有没有推进，或直接读渲染结果。
- **ImGui 里先注册者独占 `HoveredId`**：同一矩形只能 `HitTest` 一次；浮层遮罩
  **只画不注册**（`kit::ScrimPaint`），否则遮罩会跟面板内容抢同一个 hover，
  症状是「面板里某些控件点不动」，而且**哪一处坏取决于绘制顺序**。
- **`kit::Rect` 四参是 `(minX,minY,maxX,maxY)`**。宽高式用 `kit::RectAt(x,y,w,h)`。
- **child 的裁剪与层级按 draw list 生效**，不按代码嵌套：浮层用 `GetForegroundDrawList()`；
  要建视口的区域整块改用容器自己的 `drawList()`。
- **`kit::ScrollRegion` 不调 `setContentHeight()` 就是滚不动**（不是「滚不顺」）；
  上报值恰好等于视口高等于滚不动。
- **`NoMouseInputs` 不向嵌套 child 传播**（ImGui 只跳过自己带这个 flag 的窗口）⇒ 浮层开着时，
  光给工作区那一层加 flag 不够：总览右栏 / 分镜时间线 / 底栏列表这些**嵌套** child 照样抢
  `HoveredWindow`，浮层按钮在**某些工作区**上点不动（症状随工作区、鼠标位置而变）。
  闸门现在是 `kit::OverlayMouseGate()`（外壳每帧写、`ScrollRegion` 缺省读）—— **不要再手传**。
- **注入鼠标键时，释放沿必须在覆盖还开着的那一帧泵出去**（`Host.cpp:352` 只在 Set 时才推事件）：
  先 `Clear` 再泵 = 释放事件从没发生，`io.MouseDown[0]` 永久为 true，而
  `IsMouseClicked` 要求「上一帧是抬起的」（`MouseDownDuration == 0`）⇒ **之后第一条**按下探针静默失效。
- **`ImGui::IsMouseHoveringRect` 在本工程会 `0xC0000005`**，用 `Rect::contains(io.MousePos)` 手算。
- **合成鼠标位置必须走 `Host::SetFrameMouseOverride()` / `io.AddMousePosEvent()`，
  且排在所有后端 `NewFrame` 之前**；直接写 `io.MousePos` 字段活不过 `NewFrame`
  （win32 后端在窗口有焦点时一定塞真实光标），症状是「能不能复现取决于取证窗口当时有没有焦点」。
- **中文发糊是结构性的**（`kit/Fonts.cpp` 顶部注释写了三条根因）：
  `RasterizerDensity` 必须设 2.0，设计稿字号大量是半点（10.5/11.5/12.5/13.5/14.5），
  图集按整数档光栅化后缩到半点必然重采样。
- **两个降级不要死磕**：`backdrop-filter` 毛玻璃、`box-shadow`（见 `design-spec.md` §9.3）。
- ⚠️ **`kit::Image` 两档 key 的号段必须先清后置**：`ImageKey` / `ImageKeyFull` 要写成
  `(FNV(path) & ~(bit62|bit61)) | 自己那一位`。写成 `FNV(path) | 自己那一位` 看着像
  「各占一位、互不干扰」，其实**约 1/4 的路径两档会撞成同一个 key**（FNV 的每一位都任意）：
  登记表 `try_emplace` 不覆盖已有项 ⇒ 大图档登记不进去 ⇒ `RequestedImageFullCount()` 恒 0
  ⇒ `viewer-full` 假红（而那张大图**明明在屏上**）；同时服务侧按 key 存 `targetSize`
  ⇒ 两个调用点每帧换档重解码。自检 `image-keys`（就地 256 条探针，旧公式下报 74/256）已进 `overall`。
- ⚠️ **插进取证脚本的探针要「摆好页面态、拍完还原」**：多插一条探针把工作区留在别处，
  下一段的滚动判据立刻报「请求从没被核销（id="ov-right"）」—— 看着像滚动区坏了，根因是上一条探针。
  同理**判据对象要放在必然可见的那一格**：总控「最近产物」那张卡在 1600×960 下要收起底栏
  （`Shell::ToggleDock`）才拍得全，而卡的最后一行仍会被窗口底边裁掉 ⇒ 判据打在第一/三行。
- ⚠️ **总控「最近产物」的数据来自两族，别只认一族**：T 链账本（`pipeline::Runner::LedgerLog()`）
  在执行体未接入时**恒为空**，真产物在 `generated_images`（且**不限 `source_kind`**——
  只认 `shot` 会漏掉资产设定图那条链）。fixture 必须至少铺两种 `source_kind`，
  否则「查询退化成只认 shot」这种退化不会被任何证据发现（`_make_fixture.ps1` 已加门禁）。

## 文档分工与会话记忆

- **工程事实**（代码在哪、怎么编、模块职责、门禁/判据速查）→ [`KnowledgeBase/INDEX.md`](KnowledgeBase/INDEX.md)
  （薄层唯一入口，长文仍在 `refactor/`，两边只留指针不复制正文）。
- **会话记忆**（何时做了何事 / 为什么这么改 / 踩了什么坑）→ `.memsearch/memory/*.md`，
  按 [memsearch 技能](.agents/skills/memsearch/SKILL.md) 写入与召回。
  **DSH 侧 Stop hook 没挂、不会自动落盘** —— 会话收尾手动补一条（格式照 `.memsearch/memory/` 既有文件）。
- 工程事实不写进 `.memsearch/memory/`，事件记忆不写进 `refactor/`；判据是「下次还需要重新知道这件事吗？」

## 常用入口

- 构建：`Build.bat`（Debug 默认）/ `Build.bat --release`
- 门禁：`powershell -File tools\check-layers.ps1` / `check-colors.ps1` / `check-theme.ps1` / `check-i18n.ps1`
- 静默失效扫描：`powershell -File tools\find-silent-truncation.ps1` / `find-draw-arg-order.ps1` / `find-rect-wh-misuse.ps1` / `find-raw-text.ps1`
- 截图取证：`scripts\capture_window.ps1` · 逐像素差异 `out\_pixdiff.ps1` ·
  **指定颜色的像素计数** `out\_colorcount.ps1`（`_pixdiff` 只说差了多少像素、不说变成了什么色，
  而这个空档正是「算式算出来了、但没人验过真到没到像素」长期混过去的地方）
- **1:1 回归（首选）**：`out\_make_fixture.ps1` → `out\_run_review.ps1`（自带 fixture 步骤）
- 回归（备选）：`scripts\run_reviews.ps1`（⚠️ 默认 `-Exe` 指向 `build\`，见上文假信号表）
- **改 fixture**：`out\_fixture_{schema,data}.sql` 是 **UTF-8 留档，不要动**（PowerShell 的
  `Set-Content/Out-File` 会按系统 ANSI 码页重编码，中文静默变乱码且脚本照跑成功）——
  改动一律**追加**到 `out\_fixture_seed.sql`，新行用 **100+ id 号段**。
  改完必须同步 `out\_make_fixture.ps1` 的硬门禁（行数 + **枚举覆盖度**：8 种资产状态 /
  4 种层状态 / 4 个层名 / ≥1 条降级 / `generated_images` 行数 ≥4 且 **`source_kind` ≥2 种**）。
  **枚举没铺满 = 对应分支永远没人验**。
  ⚠️ 改完 seed **必须重跑 `_make_fixture.ps1`**：`_run_review.ps1` 只**拷**已生成的 `novel.db`，
  它不重建 —— 忘了重建时判据以「NO-IMAGE」的样子红，与产品坏了同形。
- 语义检索：`memsearch search "…"`（记忆库 `shinetv_memory`，默认）·
  知识库文档加 `--collection shinetv_kb` · 文档改动后 `memsearch index <路径>` 增量刷新
- 运行辅助：`scripts\studio.ps1` · `scripts\comfy.ps1`
- 第三方库：`third/`（`imgui` **`ImAnim`** `VisualNodeSystem` `libhv` `sqlite` `yyjson`
  `spdlog` `fmt` `function2` `glaze` `mimalloc` `stdexec` `hiredis` `libpng` `libjpeg-turbo`
  `libwebp` `zlib`）
- 运行时数据：`runtime/`（**不入库**）
