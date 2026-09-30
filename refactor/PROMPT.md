---
id: refactor.prompt
kind: runbook
status: current
source_of_truth:
  - README.md
  - AGENTS.md
  - refactor/README.md
  - refactor/design-spec.md
  - refactor/PROGRESS.md
last_verified: 2026-09-30
---

# 会话恢复提示词

> 新会话开工前粘一次，用来接上上下文。
> ⚠️ 旧的「Qt → ImGui 重构执行提示词」已作废 —— Qt 前端早删了，
> 那份提示词里的 `git checkout aa61046 -- third/imgui`、`SHINE_UI_QT` 双前端共存、
> D3D11 后端全部是过去式，照做会破坏现状。历史全文在 git 里。

---

## 目标

`E:\c++\ShineTV` 的前端已是 **Dear ImGui（OpenGL 3.3 / WGL）**，
它是 `webui/`（React + CSS 设计稿）的 **1:1 复刻**。当前任务 = 把复刻里**还不准的地方改准**，
以及把**设计稿里有、实现里还没有**的块补上。

`webui/` **只做效果、不管功能** —— 功能以 `src/` 的 `shine_core` 为准。

## 先读这四篇

| 文件 | 用途 |
|---|---|
| `AGENTS.md` | 协作规则、构建、门禁、地雷 —— **必读** |
| `refactor/README.md` | 分层 + **文件地图：要改什么 → 改哪个文件** |
| `refactor/design-spec.md` | 1:1 判据：几何/字号、31×5 色表、图标、外壳尺寸、22 组件、逐页布局、降级清单、验收方法 |
| `refactor/PROGRESS.md` | **施工清单在 §「视觉 1:1 符合性审计」**；另有跨轮硬事实与已修缺陷 |

## 执行契约

- 按 `PROGRESS.md` 的偏差表逐条推进。**每条先独立核实归因**（默认假设是「我错了」），
  再动手；改错层的成本高于不改。
- 改一处就 `check-layers` + `check-colors` + `check-theme` → `Build.bat` → 抓图，
  **不要攒一大批再验**。
- **不要中途回来问我**：文档没覆盖的实现细节自己定，把决策记进源码注释或 `PROGRESS.md`。
  只有两种情况停下来：① 需要动工作区里已有的用户改动 ② 需要装系统软件 / 用账号 / 花钱。
- 没跑的验证写「未执行」，**不要把推断写成通过**。

## 硬约束（违反即返工）

- **不改 `shine_core`**（`src/core util net db llm comfy gpu media flow visual novel paint mcp project pipeline`）。
  宿主只需要接 `gpu::AttachDevice(GlApi)` 这一处注入。
  ⚠️ 「分镜缩略图」那条缺口**只能在业务层补**（见下「仍未做」），别在 UI 侧编文件名约定去凑。
- **颜色只能从 `theme/` 来**。调用点写 `ImVec4(...)` / `IM_COL32(...)` 字面量会被 `check-colors.ps1` 拦。
  「不画」用 `kit::ColorTransparent()`。纹理染色的恒等值用 `IM_COL32_WHITE`（不是主题色，别换成 `ColorText()`）。
- 构建目录单写，**不要并发 `cmake --build`**（撞 exe 锁）。
- 改了 C++ 之后按 `AGENTS.md` 的顺序验证：门禁 → `Build.bat` → 实际启动 → 抓图。

---

## 当前进度（截至 2026-09-30，第七轮）

主干（51/51 步）早完成。最近三轮做的是**产品缺口**，每一轮都以 `overall=PASS` 收尾：

| 轮次 | 做了什么 |
|---|---|
| 第五轮 | **R8 界面侧接上真实图片**：新增 `kit/Image.{h,cpp}`（路径 → 纹理 → 画进矩形，走已有的 `gallery::ThumbnailService`）。接线时挖出两个真缺陷：`RegisterBuiltinDecoders()` **全仓零调用**（⇒ 每个文件都报「不支持的图片格式」）、fixture 里那几个 `.png` 是文本桩。已接：资产网格卡缩略图、详情页左列基线图（顺带补齐**整块缺失的两列网格**） |
| 第六轮 | **数据面铺开**（`out/_fixture_seed.sql`）：17 实体 / 13 种 kind / 12 章 / 3 卷 / 19 镜 / 8-8 种资产状态 / 10 条 `visual_artifacts` 行（原来 **0 行**）/ 12 张真 PNG + 坏文件 + 缺文件 + 中文文件名。`_make_fixture.ps1` 的硬门禁加了**枚举覆盖度** |
| 第七轮 | **侧栏树可滚**（`Tree` 改成报**完整内容高度**；两棵树套 `ScrollRegion`；提成 `Shell::OverlayBlocksMouse()`）、**V0 派生链**（逐层真图 + 层状态点，`visual_artifacts` 一路投影到视图层）、**分镜 `.tthumb` 逐镜成片图**（`generated_images` 的 `source_kind='shot'`，三分支：真图 / `Art` / 「待出图」） |

当前实测（可复现）：

```
# shots: 82  failed: 0  scroll-failed: 0  identical-theme-pairs: 0  identical-driven-pairs: 0
  inverted-rects: 0  duplicate-hits: 0  hover-probes: 11/11  shortcuts: 7/7  overlay-clicks: 7/7
  report-scan/book-snapshot/asset-snapshot/artifact-snapshot 全 converged
  sheet-images=settled requested=7 pending=0 pixels=33848
  sheet-recover=ok pixels=33848 before=33848          ← 纹理被清空后会自己回来
  asset-tree-scroll 可滚 自报内容高=840.0 可视高=428.0 上限=412.0
overall=PASS ↔ 退出码 0；四门禁全绿
```

## 下一步：查看着器（viewer）—— 模型层齐全，UI 层为零

**一次能收掉四项**：派生链节点的「查看该层大图」、详情页的「查看大图」按钮、
查看器大图本身、以及「导出整版」的联系表（设计稿 `type:'sheet'` 的网格）。

已经有的是**模型层**（别重写）：

- `src/media/Viewer.h`：`State{id, open, loading, zoom, panX, panY, index, total, imgW, imgH, name, loadGen}`、
  `Open/Close/IsOpen/Get/Tick`、`Step(dir)`（跳过 Failed）、`Fit()`、`ActualPixels(fitScale)`、
  `OnWheel(...)`、`OnDrag(dx,dy)`、`ClampPan(...)`、`FullTexture(id)`（异步原图 + 只留 4 张）、
  `FullCacheCount/FullLoads/DiscardedLoads`（**验收：连切不应无界上涨**）。
- `gallery::OpenViewer(id) / CloseViewer() / ViewerOpen() / ViewerImageId()`，且
  `gallery::Tick()`（`Host::PumpUi()` 每帧调）**已经**驱动 `viewer::Tick()`。

**没有的是 UI**：`app/media/GalleryView.cpp` 已随 Qt 前端一起删除（`search_file` 找不到），
`src/ui/**` 里对 `gallery::viewer::*` 与 `OpenViewer` **零调用**（只有 `kit/Image.cpp` 一句注释提到它）。

⚠️ 动手前必须先定两个设计决策（都写进源码注释）：

1. **原图从哪来**：`viewer::FullTexture` 收的是 **`ImageId`**（图库扫描出来的 id），
   而界面侧 `kit::Image` 的 key 是**路径哈希**（`kUiImageBit | FNV(path)`）——
   资产设定图 / 派生链那些**工程内文件**不在图库 Model 里，拿不到 `ImageId`。
   两条路：① 把路径注册进图库 Model；② 给 `kit::Image` 加**第二档尺寸**
   （**另取一个号段位**，例如 bit 61，`RequestImageFull(path)`）。
   ⚠️ **不能**复用同一个 key 换尺寸：服务侧按 `ImageId` 存 `targetSize`，换档会
   `Erase` + **重新解码**，两个调用点用不同尺寸请求同一文件会**每帧重解码**（`kit/Image.h` 约束 2）。
2. **它是浮层** ⇒ 必须进 `OverlayBlocksMouse()`、`chromeInteractive_` 闸门，
   并给 `overlay-clicks` 那组探针**加一个**（那是唯一能发现「浮层里按钮点不动」的判据 ——
   静息截图零覆盖）。遮罩**只画不注册**会让点击落到下层（地雷表里有）。

设计稿出处：`webui/src/views/Assets.jsx` 的 `setViewer({name, type:'image'|'sheet', seed, meta, gallery})`
（image 见 150-202、sheet 见 177-181/195-198；画廊左右切换、`meta` 说明行）。

## 仍未做

- **出图结果面板 / 出片胶片格 / 查看器大图与联系表** 这几处图片位点。
  前两者很可能与分镜**同族**（都从 `generated_images` 取）。
  ⚠️ **先按下面这条教训把那张表的列全列一遍、并 grep 一遍谁在写它，再下结论。**
- 主题层 #3（`line.*`/`fill.*` 的 alpha 摊平）、页面层若干细则：见 `PROGRESS.md` 的审计表，
  每条都标了归因在哪一层。

### ⚠️ 一条必须带走的教训（真栽过）

第七轮我写下了「分镜 `.tthumb` 的堵点在**数据层**：`generated_images` 只连 `scene_id`、
没有 `shot_id` ⇒『这一镜的图』确定不出来」，并据此把它记成「可以跳过」。
**那是错的**：那张表里有一对 **`source_kind` / `source_id`**（多态来源），
业务层提交单镜出图时就写着 `source_kind='shot', source_id=shots.id`
（`src/novel/NovelImageStore.cpp`），表上还专门建了 `idx_gimg_source(source_kind, source_id)`，
`src/visual/ReferenceChain.cpp:137` 也早在用它。**我只看了 `scene_id` 就下了结论。**

- 代价：整块功能被误判成「业务层问题」而永久留成占位（`Page_Storyboard.cpp` 里
  那段注释当年也是这么写的：「`BookShotView` 没有 status 字段 ⇒ 数据层给不出这个判断」——
  判断与图**是同一个查询**，那对列一直就在库里）。
- 纪律：**说「数据层没有答案」之前，先把那张表的列全列一遍 + `search_content` 一遍谁在写它。**
  归因写错层，比不改还贵。
- 补一条：UI 侧**可以**读库 —— `BookQuery.cpp` 里既有做法（同一函数读 `volumes` 就是裸 SQL，
  那张表同样没有 `novelcore` 访问器），`check-layers.ps1` 只禁「业务层 include ImGui 头」
  与「UI 里写颜色字面量」。所以**这类活通常不需要动 `shine_core`**。

---

## 地雷（详见 `AGENTS.md` §地雷 与 `PROGRESS.md` 的跨轮硬事实）

- `kit::Rect` 四参是 `(minX,minY,maxX,maxY)`，**不是** `(x,y,w,h)`；写错不报编译错，
  控件整块不画也不可点。宽高式用 `kit::RectAt()`。
- 同一帧同一矩形只能 `HitTest` 一次（先注册者独占 `HoveredId`）。⚠️ 这条判据（`duplicate-hits`）
  **只在同一个 draw list 内比** —— 跨窗口/跨 draw list 的重叠抢不走 hover
  （`ItemHoverable` → `IsClippedEx`），把合法滚动内容算进去会假红。
- `kit::ScrollRegion` 不调 `setContentHeight()` 就是**滚不动**；它内部是 `BeginChild`，
  **每个 child 都要按 `OverlayBlocksMouse()` 退出命中测试**，否则根窗口里的浮层按钮点不动。
- ⚠️ **`RequestImage` 必须每帧无条件调**（服务侧的 `Request` 幂等；只在 `State==NotLoaded`
  时调 ⇒ 纹理被 LRU 淘汰后**图永远不再出现**，「图闪过一次就没了」，且不报错）。
- ⚠️ **截图不可跨轮逐字节比对**：同一二进制连跑两轮，80 张里 48 张字节不同（时钟/动画/日志）。
  能用的是**同轮内的重样判据**，以及**数某个纯色的像素数**（`out\_colorcount.ps1`）——
  而抓到的缓冲是 **RGBA**（`R` 在偏移 0），权威是 `Capture.cpp` 里那句
  「已经是 RGBA8，不要再交换通道」，**不是**形参名。
- ⚠️ **判据要打在那个「必然会触发」的对象上**：侧栏有**两棵**树，小说树放得下、
  资产树必然溢出 —— 打在小说树上会报假红；给它拍「滚到底」那张图更会与未滚那张逐字节相同。
- 取证抓图会**自动等异步图落地**（`Shoot` 里，唯一入口）—— 别在每个调用点各补一个等待。
- **像素差不作判据**（跨进程约 2000px、跨构建约 40000px 噪声）。
- `ImGui::IsMouseHoveringRect` 在本工程会 `0xC0000005`，用 `Rect::contains(io.MousePos)`。
- 取证注入必须早于 `NewFrame`（`Host::SetFrame*Override`）。
- 两处必须接受的降级：`backdrop-filter` 毛玻璃、`box-shadow`；另加一条：本版 ImGui 没有
  push clip path ⇒ 铺满矩形的过程画（`Art` / 真图）**裁不掉圆角**。
- ⚠️ `webui/` **不在版本控制里**；核对 CSS 原式前先确认它还在本机。
  ⚠️ **JSX 内联样式会覆盖 CSS**，两处都要看（`.dnode` 宽 92 不是 108、`.dthumb` 高 50 不是 64）。

## 开工第一件事

```powershell
git status --short                 # 工作区改动可能是用户的，别覆盖
powershell -File tools\check-layers.ps1
powershell -File tools\check-colors.ps1
Build.bat --release
out\_make_fixture.ps1                                # 取证判据的成立条件，别跳
powershell -File out\_run_review.ps1 -OutDir "$PWD\build\shots" -Theme deepspace
```

**先量基线，再动手。** 上一轮的「PASS / 11-11 探针 / 几张图」到底是多少，
只有自己跑出来才算数 —— 文档里的数字会过期，而**缺 fixture、跑了旧产物、与产品退化
在日志上长得一样**。

⚠️ 改 fixture 的规矩：`out\_fixture_{schema,data}.sql` 是 **UTF-8 留档，不要动**
（PowerShell 的 `Set-Content/Out-File` 会按系统 ANSI 码页重编码，中文静默变乱码且脚本照跑成功）——
改动一律**追加**到 `out\_fixture_seed.sql`，新行用 **100+ id 号段**，
并同步 `_make_fixture.ps1` 的硬门禁（行数 + **枚举覆盖度**）。
**枚举没铺满 = 对应分支永远没人验**，而整轮照样 PASS。
