# 门禁与判据体系速查（Gates & Judgments）

> **本页回答**：门禁 / 扫描器各抓什么、自动化判据的成立条件、判据红了先怀疑什么。
> **可信度**：✅〔A〕= 出自 [`AGENTS.md`](../../AGENTS.md)（2026-09-30 核验）｜✅〔源〕= 目录实测｜△ INFERRED｜❓ UNKNOWN。

## 1. 硬门禁（失败退出码 1）✅〔A〕

| 工具 | 抓什么 | 自检 |
|---|---|---|
| `tools\check-layers.ps1` | 分层越界（`src/` 出现 Qt 头、页面层直连底层） | — |
| `tools\check-colors.ps1` | 硬编码颜色字面量 | — |
| `tools\check-theme.ps1` | 主题表 token 数与顺序 | — |
| `tools\check-i18n.ps1` | 文案规范 | — |

## 2. 静默失效扫描器（`find-*`）✅〔A〕

| 工具 | 抓什么 | 自检 |
|---|---|---|
| `tools\find-silent-truncation.ps1` | 列表按容器高度静默截断 | `-SelfTest` |
| `tools\find-draw-arg-order.ps1` | 绘制原语的**颜色 / 圆角**参数错位 | `-SelfTest` |
| `tools\find-rect-wh-misuse.ps1` | `kit::Rect` 四参当成 `(x,y,w,h)` | `-SelfTest`（9 真 + 6 假；另可 `-Root <样本目录>`） |
| `tools\find-raw-text.ps1` | 页面层裸 `AddText` 与 `FontAt/FontBoldAt/MonoAt(数字字面量)`（只扫 `pages/`） | `-SelfTest`（8 真 + 8 假）；**棘轮基线** `find-raw-text.baseline.txt`（只降不升，基线外新增 ⇒ exit 1） |

- **扫描器报 0 不等于没有**；改门禁当轮「绿」失去可比基线，应单独一轮做，改完先跑它自己的自检。
- 门禁自身盲区的**已修记录与修法**在长文里，别照更早的「全漏网」旧结论放弃检查。
- `tools/` 另有 `find-fixed-width.ps1` / `find-manual-list.ps1`（带基线，未入 AGENTS.md 表）——△抓什么未核。

详见 [`AGENTS.md`](../../AGENTS.md)『门禁与扫描器』（典型形态表、盲区修复记录）；文件级分工见 [`refactor/README.md`](../../refactor/README.md)『3.6 门禁与脚本』。

## 3. 自动化判据族（真图判据，全进 `overall`）✅〔A〕

都在 `ReviewScene.cpp` 写、`ReviewVerdict.cpp` 判；像素取证用 `out\_colorcount.ps1`（RGBA，见 [BuildAndVerify.md](BuildAndVerify.md) §4）。

| 判据 | 成立条件 | 它挡的是 |
|---|---|---|
| `sheet-images=settled` | `requested>0 && pending==0` | 抓图太早 **与** 「调用点压根没请求过」（两者单独看同形） |
| `sheet-recover=ok` | 清 GPU 纹理缓存后真图像素数 = 清空前 | `kit/Image.h` 约束被写坏 ⇒「图闪过一次就没了」 |
| `viewer-full=settled` | 大图档 `requested>0 && pending==0` 且两屏纯色像素 `>0` | 查看器第二档尺寸（2048）没请求 / 没落地 / 画占位 / 开错页（四因同图） |
| `image-keys=ok` | 两档 key 号段自检 0 撞车（256 条探针） | `viewer-full` 的**成立前提**（key 撞车 ⇒ 假红） |
| `imageflow-result=ok` | 出图页「结果」页签当前选中镜成图纯色像素 `>0` | 那格画 `Art` 占位（页签名就叫「结果」，最易误读） |
| `film-strip=ok` | 出片页「成片条」第 1/2 镜两种纯色**各 >0** | 占位画；或只接通了第一格 |
| `recent-products=ok` | 总控「最近产物」非 shot / shot 两种纯色各 `>0` | 只列 T 链账本（恒空）；或查询退化成 `source_kind='shot'` |

⚠️ 两处同卡判据要**一次抓图数两个色**（`Session::GrabCountTwoColors`）；pending 按**现用档位**（`PendingImageFullCount()`）判，别拿总的 `PendingImageCount()`。

详见 [`AGENTS.md`](../../AGENTS.md)『验证顺序』判据表（逐条原文与实现落点）与 [`refactor/design-spec.md`](../../refactor/design-spec.md)『9. 1:1 验收方法』。

## 4. 假信号表（判据红 ≠ 产品坏）✅〔A〕

| 假信号 | 真相 / 指纹 |
|---|---|
| **缺 `novel.db` fixture** | 侧栏树 / 资产树退化成诚实空态，`hover-tree-node` / `hover-asset-card` 永远不命中：同一份代码缺库 `9/11+FAIL`、有库 `11/11+PASS`；「图照常 saved、判据照常红」 |
| **默认跑了一个旧产物** | `build\` 根躺着更旧的二进制照样 PASS；核对日志首行 `exe:`（路径 + 时间戳），编译失败时脚本仍会跑起来 |
| **异步图拍太早** | 占位画与「链断了」在截图上完全一样；等待集中在 `Shoot`（`ReviewPixel.cpp`），`sheet-images` 两半缺一不可（`pending==0` 与「从没请求过」同形） |
| **判据前提坏了** | **「换个目录名结果就翻」是指纹**（`kit::Image` 两档 key 撞车随路径 FNV 而变）；前提要自带自检进 `overall`（现在是 `image-keys`） |

⚠️ 同族新变体：取证输出目录名会被画进像素 ⇒ 基线与本轮必须同名同父目录（见 [BuildAndVerify.md](BuildAndVerify.md) §5）。

详见 [`AGENTS.md`](../../AGENTS.md)『验证顺序』假信号表与『地雷』（key 号段先清后置、探针摆还原、最近产物两族数据源等原文）。
