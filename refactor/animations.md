---
id: refactor.animations
kind: design
status: current
source_of_truth:
  - src/ui/imgui/kit/Anim.h
  - src/ui/imgui/kit/Anim.cpp
  - third/ImAnim/im_anim.h
last_verified: 2026-11-23
---

> **本页回答**：ShineTV 的 UI 动画体系怎么组织——引擎选型、API 与通道约定、哪些地方有什么动画、取证与「减少动效」的确定性语义、以及 imgui/ImAnim 升级记录。

# 动画体系（ImAnim + kit/Anim）

> **一句话现状**：动画引擎是 `third/ImAnim`（`shine_imanim` 目标），业务侧只用
> `kit/Anim.h` 的三个补间封装；**所有动画的终值与设计稿逐值相同**，动画只是「过程」——
> 取证钉住时钟时补间直接落终值，像素判据不受影响。

## 1. 体系结构

| 层 | 落点 | 职责 |
|---|---|---|
| 引擎 | `third/ImAnim`（`iam_tween_float/color`，C 接口） | 按 (id, channel) 维护补间池；不依赖任何 ImGui item —— 本工程控件全自绘，这正是选它的原因 |
| 封装 | `src/ui/imgui/kit/Anim.h/.cpp` | CSS `transition` 语义：`TransitionTo`（0..1 进度）/ `TransitionColorTo`（颜色含 alpha）/ `TransitionFloatTo`（位移/缩放/透明度/滑块） |
| 时钟 | `src/ui/imgui/kit/Draw.h`（`Now()` / `LastFrameDelta()` / `AnimationPinned` / `ReduceMotion`） | 连续动画（脉冲/呼吸/微光/转圈）走 `Now()`；补间走 `FrameDelta()` |
| 泵 | `Draw.cpp` 每帧 `iam_update_begin_frame()`；`Shell.cpp` 首帧 `ReserveTweenPool(512,128,256,64,512)` | 钉住时钟也要泵（否则在飞的补间不推进）；钉/解钉时 `iam_pool_clear()` 防半路值残留 |

**通道约定**（同一控件的 `(hit.tid, channel)` 对每帧必须稳定；组内不重号）：

| 通道 | 用途 | 典型时长 |
|---|---|---|
| 0 | hover 主底 / 主色 | `kDurFast` 0.12s |
| 1 | 按下 / 位移 / lift | `kDurFast`~`kDurBase` |
| 2 | 描边 / 辉光 | `kDurBase` 0.2s |
| 3 | 文字 / 次色 | `kDurFast` |
| 4+ | 指示器 / 结构滑动（tab 下划线、开关滑块、展开高度） | `kDurBase` 0.2s |

时长三档 = 设计稿 `tokens.css` 的 `--dur-1/2/3`（120/200/320ms）；缓动 = CSS 的两条
cubic-bezier（`theme::motion::kStandard` / `kEaseOut`）原样搬入。

## 2. 确定性语义（动效不许破坏取证 —— 全体系最重要的一条）

1. **pinned 期间补间直接落终值**（`Anim.cpp` 各封装第一句）：`kit::PinAnimation()` 钉住时钟后，
   所有 Transition* 返回 target 本值 ⇒ 静息截图逐字节确定、hover 探针照常可测（终态色 ≠ 静息态色）。
2. **reduce-motion**（Shell_Palette 的「减少动效」开关）：过渡时长压到 1ms 落地（不是 0 ——
   0 会让 crossfade 策略每帧重启补间）；**连续动画不冻结**（脉冲/进度仍要走完，只是时长压缩）。
3. **终值恒等**：每个补间的 settle 值与接入前的硬切换**逐值相同**（按压缩放 0.97、卡上浮 -2px
   等几何常量都没动）——「有没有动画」不改变「画的是什么」。
4. **tid=0 不补间**：纯展示控件（无命中测试）不接补间 —— id=0 会跨控件撞车，表现为两条补间互相拉扯。
5. 连续动画（Progress 微光、StatusDot 呼吸、Spinner、Toast 淡出）本来就在，走 `Now()` 秒表；
   钉住时钟时冻住相位，与补间家族互不干扰。
6. ⚠️ **引擎 lazy-init 坑（2026-11-23 实测，接线时必查）**：`iam_tween_*` 默认开懒初始化，
   「通道不存在且 target≈init_value」时**直接返回 target 且不建通道**（`im_anim.cpp:944`）——
   而本模块恒传 `init_value == target`（pinned 重建语义）⇒ 不关它则补间**永不启动、静默
   退化成硬切换**（编译过、像素终值全对，只有运行时看得出来）。已在 `ReserveTweenPool`
   里 `iam_set_lazy_init(false)` 根治：缺通道以 init 建（首帧落 target 不跳变），翻转即补间。
   **教训：接线动画后要实测「运行时真的在动」，编译过 ≠ 动画在跑。**

## 3. 动画清单（哪里有什么动画）

<!-- INVENTORY:START -->
| 控件/位置 | 动画 | 通道/时长 | 备注 |
|---|---|---|---|
| Button 四变体 | hover 底/边/辉光交叉渐变；按压缩放 0.97 平滑进退 | ch0/2/3 色 kDurFast；ch1 float kDurFast | 辉光 alpha 跟 hover01 起落（ch2 float） |
| IconButton | hover 底 + 字色渐变 | ch0/ch3 kDurFast | active 态保持静态 |
| Card（hoverable/glow） | 上浮 -2px 平滑进退 + 边框色渐变 | ch1 float kDurBase；ch0 色 kDurBase | 投影档保持离散；tid=0 纯展示卡不补间 |
| Seg / Chip / Ntab | hover/选中文字色、底、描边渐变；Chip 计数底渐变 | ch0/2/3/4 色 kDurFast | 设计稿「hover 只改字色不换底」等终值全部未动 |
| Tabs 指示下划线 | 选中切换时 x0/x1 **滑动** | ch4/ch5 float kDurBase | 键挂在第 0 项 tid（帧稳定）；病态多选时其余画静态几何 |
| Switch | 滑块**滑动** + 底/描边/滑块色渐变 | ch4 float kDurBase；ch0/2/3 色 kDurFast | |
| Input | 聚焦环 ↔ hover 边框渐变（含 alpha） | ch0/ch2 kDurFast | 无 Hit 处用 `GetItemID()` 作补间键 |
| Checkbox 标签 | hover 字色渐变 | ch3 kDurFast | 勾选框本体保持离散 |
| DataTable 行 | hover 填色渐变 | ch0 kDurFast | 选中行分支未动 |
| Tree 节点 | hover 填色 + 字色渐变 | ch0/ch3 kDurFast | tid=0（不可见行）不补间；展开/折叠高度与箭头旋转保持离散（递归布局无独立目标高度） |
| ListRow / ListCard | 行底三态渐变（含淡出到透明）；卡边框色渐变 | ch0/ch2 色 kDurFast | 投影档保持离散 |
| 菜单行 | hover/选中底色与字色渐变 | ch0/ch3 kDurFast | disabled 后处理未动 |
| Tooltip | 出现淡入 + .94→1 缩放（接入 kit 补间） | ch0 float kDurFast（id=`kit-tooltip`） | 原有 0→120ms 手算淡入换成补间；200ms 延迟与帧号断档逻辑未动；重悬停把补间驱回 0 |
| 浮层壳层（Scrim/OverlayPanel/Modal/Drawer） | 打开淡入（遮罩/底板/投影/头尾乘 fade） | ch0 float kDurBase（id=`overlay-scrim`/`overlay-panel`） | 「本帧在画 = 开着」为 target；正文/页脚由调用画画，淡入期间只壳层参与；**关闭动画无驱动点做不了**（关闭帧起函数不再被调用），重开不从 0 起——边界注释在 `Overlays.cpp` |

**结构性跳过（有理由的不做）**：Segmented 选中胶囊 + 圆点 + 文字位移（`DrawRoundRect` 顶高光不看 fill，渐隐会残留白线）；Tree 展开高度滑动（见上表）；投影档交叉渐变；浮层整体 0.98→1 缩放（返回矩形供布局/命中用，缩放会动几何）。
<!-- INVENTORY:END -->

**刻意不动的**：投影档（ShadowTier）切换保持离散（档间交叉渐变要动 DrawShadowed 的分层 alpha，
收益低风险高）；`webui` 设计稿的 43 处 CSS transition 是**终值**依据，动效过程不进 1:1 判据。

## 4. vendor 升级记录（2026-11-23）

- **imgui**：`third/imgui` 核心 14 文件从 1.93.0 WIP(**19297**) 升到 1.93.1 WIP(**19303**)
  （来源 `F:\c++\imgui`，无项目定制、原版覆盖）。**API 破损一处**：`ImGuiCol_TabActive /
  TabUnfocused / TabUnfocusedActive` 在此版删了 1.90.9 的旧别名 → `Theme.cpp` 改用
  `TabSelected / TabDimmed / TabDimmedSelected`，并补两个新槽 `Tab{,Dimmed}SelectedOverline`
  （映射见 `Theme.cpp` 注释）。构建全绿（`shine_imgui` + exe 链接通过）。
- ⚠️ **backends 仍是 19297**：`F:\c++\imgui` 只含核心文件，`imgui_impl_win32/opengl3` 与
  `misc/cpp/imgui_stdlib` 未随升（实测 19297 backends 编过 19303 核心）。下次拿到完整
  上游时同步 backends，别忘。
- **ImAnim**：`F:\c++\imgui_third\ImAnim` 与 `third/ImAnim` **逐字节相同**（SHA256 六文件全同），
  无需复制 —— 仓内已是该版。
- 注：文档里引用 `imstb_truetype.h` / `imgui_draw.cpp` 的行号（如 `imstb_truetype.h:2665`、
  `imgui_draw.cpp:4801`）按 19297 核验，升级后**行号已漂移**，符号名仍然有效。

## 5. 证据索引（要改动效时从这里进）

| 事实 | 落点 |
|---|---|
| 补间 API / 通道约定 / 取证语义 | `src/ui/imgui/kit/Anim.h`（契约注释）+ `Anim.cpp` |
| hit.tid 的生成（与 InvisibleButton 同 ID） | `src/ui/imgui/kit/Widget_Core.cpp` `HitTestItemId` |
| 钉住/解钉、每帧泵、reduce-motion 时长压缩 | `src/ui/imgui/kit/Draw.cpp`（`iam_update_begin_frame` / `iam_pool_clear`） |
| 补间池预分配 | `src/ui/imgui/pages/Shell.cpp`（`ReserveTweenPool`） |
| 按钮全套（hover 色 + 按压缩放 + 辉光 alpha） | `src/ui/imgui/kit/Widget_Button.cpp` |
| 卡片（lift float + 边框色） | `src/ui/imgui/kit/Widget_Card.cpp` |
| lazy-init 修复（补间能跑的前提） | `src/ui/imgui/kit/Anim.cpp` `ReserveTweenPool` |
| 浮层开合边界（壳层淡入 / 关闭无驱动点） | `src/ui/imgui/kit/Overlays.cpp`「开合动画的边界」注释 |
| ImAnim 引擎 | `third/ImAnim/im_anim.h`（`iam_tween_*` / `iam_set_lazy_init`）+ `docs/`（上游文档） |
