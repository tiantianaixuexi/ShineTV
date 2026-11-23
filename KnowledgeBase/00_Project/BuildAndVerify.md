# 构建与验证速查（Build & Verify）

> **本页回答**：怎么编、改完 C++ 按什么顺序验、1:1 回归怎么跑、像素取证怎么取。
> **可信度**：✅〔A〕= 出自 [`AGENTS.md`](../../AGENTS.md)（2026-09-30 核验）｜✅〔源〕= 本次读 `Build.bat` / 目录实测｜△ INFERRED｜❓ UNKNOWN。

## 1. 构建（`Build.bat`）✅〔A〕

| 命令 | 配置 | 产物 |
|---|---|---|
| `Build.bat` | Debug（默认） | `build\debug\ShineTVStudio.exe` |
| `Build.bat --release` | Release | `build\release\ShineTVStudio.exe` |
| `Build.bat --clean` | 先删该 build 目录再编 | 同上 |
| `Build.bat --help` | 用法 | — |

- **独立 build 目录的原因**：`CMAKE_BUILD_TYPE` 按 build 目录缓存，塞同一个目录 ⇒ 每次切换配置全量重编。
- 工具链：`SHINE_MINGW\bin\g++.exe`（默认 `C:\msys64\mingw64`，环境变量可覆盖）；找不到 `exit 2`——不让 CMake 自己挑编译器。
- `Build.bat` 实际还带 `--target ShineTVStudio`，并把 `compile_commands.json` 回拷到仓库根供 clangd。✅〔源〕

手动等价 ✅〔A〕：

```powershell
cmake -S . -B build\release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
      -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe
cmake --build build\release --parallel
```

## 2. 改完 C++ 的验证顺序（必走）✅〔A〕

```powershell
powershell -File tools\check-layers.ps1      # 1. 分层门禁
powershell -File tools\check-colors.ps1      # 2. 硬编码颜色
powershell -File tools\check-theme.ps1       # 3. 主题表完整性
Build.bat --release                          # 4. 构建
.\build\release\ShineTVStudio.exe            # 5. 实际启动，肉眼比对 webui 同页面
```

（`tools\check-i18n.ps1` 文案规范门禁也在，可一并跑；各门禁抓什么见 [GatesAndJudgments.md](GatesAndJudgments.md)。）

## 3. 1:1 回归（自动化）✅〔A〕

```powershell
out\_make_fixture.ps1                                             # 首次 / fixture 丢了之后跑一次
powershell -File out\_run_review.ps1 -OutDir "$PWD\build\shots" -Theme deepspace
```

- **产物** = 每张 PNG + `shots-manifest.txt`；manifest 结尾 `overall=PASS|FAIL` **必须与退出码一致**（PASS↔0 / FAIL↔1）。
- **必须带 `novel.db` fixture**——否则判据的成立条件不成立（缺库 9/11+FAIL、有库 11/11+PASS）。
- fixture 纪律：`_fixture_{schema,data}.sql` 是 UTF-8 留档**不要动**；改动只**追加** `out\_fixture_seed.sql`（100+ id 号段）；改完**必须重跑 `_make_fixture.ps1`**（`_run_review.ps1` 只拷不重建，忘了 ⇒ 判据以「NO-IMAGE」的样子红）。硬门禁 = 行数 + 枚举覆盖度（8 资产状态 / 4 层状态 / 4 层名 / ≥1 降级 / `generated_images` ≥4 且 `source_kind` ≥2 种）。
- 备选：`scripts\run_reviews.ps1`（⚠️ 核对日志首行 `exe:` 路径与时间戳——历史上 `-Exe` 曾写死 `build\` 旧二进制照样报 PASS）。

## 4. 颜色像素计数取证（`out\_colorcount.ps1`）✅〔A〕

- 这一族**唯一可靠**的取证手段：截图跨轮不可复现（同二进制两轮 80 张里 48 张会变），而**数某个色值免疫**。fixture 真设定图是两张纯色 PNG（`#FF2D55` / `#7CFF00`，刻意在五套主题调色板之外），上没上屏一数便知。
- ⚠️ **RGBA 通道坑**：缓冲是 **RGBA**（`R` 在偏移 0），权威是 `Capture.cpp`「已经是 RGBA8，不要再交换通道」——按形参旧名当 BGRA 会恒数到 0，而 0 恰是「图没上屏」的读数。
- 数色判据**必须有上下界**（只判下界时色退化成一块面也判绿）；`0xRRGGBB` 与 ImGui `ImU32` 是两个字节序空间。

## 5. 纪律红线 ✅〔A〕

1. 不要在同一个 build 目录上并发 `cmake --build`（撞 exe 锁）。
2. **构建失败后不要读任何截图或日志**——描述的是上一个二进制。
3. 截图**不可跨轮逐字节比对**：无损重构取稳定的 32 张子集比；等值性一条命令 `out\_verify_r23.ps1`（配 `out\_shotdiff.ps1` 差集 / `out\_diffmap.ps1` 聚类）。
4. **取证输出目录名会被画进像素**（`ViewHeader` 副标题印工程根路径）⇒ 基线与本轮必须跑进**同名同父**的目录（实测不同名时 41/90 张不同）。

---

详见 [`AGENTS.md`](../../AGENTS.md)『构建』『验证顺序』（假信号表、判据族、fixture 纪律原文）与
[`refactor/README.md`](../../refactor/README.md)『5. 构建 / 运行 / 门禁 / 验收』（环境变量表）；
验收方法与判据定义见 [`refactor/design-spec.md`](../../refactor/design-spec.md)『9. 1:1 验收方法』。
