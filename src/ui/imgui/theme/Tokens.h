#pragma once
// shine::theme —— 设计系统 Token（ImGui 前端）
//
// 由 src/ui/kit/theme/Token.h 平移而来（refactor/phases.md P2.1）：那份是纯 std、
// 零 Qt，可近乎原样搬。唯一的改动是删掉 QSS 用的字体族串（ImGui 侧走字体图集，
// 不用 CSS font-family），以及本文件现在是 5 套主题 JSON 的唯一真值来源。
//
// 权威：webui/src/styles/tokens.css 的 :root 块（--r-* / --sp-* / --dur-* / --font-*）
// 与 5 套主题 JSON。点分名与 ColorToken 字段顺序必须逐位一致 —— ApplyTheme()
// 靠 kColorTokenNames 做一次性映射，插入会让全表错位。
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace shine::theme {

// 5 套主题（深空为默认；下标 = ThemeId 枚举值，不得重排）
enum class ThemeId {
    DeepSpace /*深空*/, Dusk /*薄暮*/, PaperInk /*纸墨*/, InkWash /*水墨*/, PolarNight /*极夜*/,
};

inline constexpr std::array<ThemeId, 5> kAllThemes = {
    ThemeId::DeepSpace, ThemeId::Dusk, ThemeId::PaperInk, ThemeId::InkWash, ThemeId::PolarNight};

[[nodiscard]] std::string_view ThemeIdKey(ThemeId id);   // 持久化用的 ASCII id
[[nodiscard]] std::string_view ThemeDisplayName(ThemeId id); // UI 显示名（含中文）
[[nodiscard]] bool ThemeIdFromKey(std::string_view key, ThemeId& out);

// ---- §2.1 + §2.2 颜色 token（字段顺序 = kColorTokenNames）----
//
// ⚠️⚠️ **`line.*` 与 `fill.*` 存的是 CSS 原文的 rgba，可能是半透明的 ——
//      不要把它们当成不透明实色去参与运算。**
//
// 2026-09-30 之前不是这样：themes/*.json 里这几个字段存的是「CSS 的 rgba **叠在
// `--bg-surface` 上**」压平出来的实色（例：深空的 `line.subtle` 存 `#202734`）。
// 那个做法自洽、也确实是 CSS 在 `bg-surface` 上的答案，但**叠在别的底上就错**：
//   `.card` 自己的底是 `--bg-panel`，它的 1px 边框是画在**自己的底**上的
//   ⇒ 深空卡片边应为 `#29303E`，而压平值给的是 `#202734`（暗 9/9/10）。
// 这属于「一个数据层决定了一个不该由数据层决定的事」：底色是**画的时候**才知道的。
//
// 现在存原文 rgba（如 `#EAF0F712` = rgba(234,240,247,0.07)），渲染器在绘制时按
// 实际已画内容做 source-over 混合，于是**每处自动拿到正确的合成色**：
//   `FlattenOver(raw, bg-surface)` ≡ `#202734`（与旧值逐通道相等）
//   `FlattenOver(raw, bg-panel)`   ≡ `#29303E`（设计稿要的那个）
// 后者正是 `design-spec.md` §2.0.3 记了很久的那个偏差，一次性解决，
// 且**不需要**给 `DrawRoundRect` 加底色参数、也不需要按底色准备多套变体。
//
// 判据：`line.*` 五套主题的 CSS 原文**全是 rgba**（tokens.css:39-41 / 81-83 /
// 122-124 / 163-165 / 216-218）；`fill.*` 只有**纸墨 / 水墨**是 rgba
// （:140-142 / :181-183），深空 / 薄暮 / 极夜本来就是实色 hex（:57-59 / 99-101 /
// 234-236）—— 所以后三套的 `fill.*` 没有变化，改动只落在浅色两套。
//
// 连带影响（已核对）：`MixSrgb(tone, fillMuted, N)` 在浅色两套下的结果会**带 alpha**
// —— 那是 CSS `color-mix` 的正确答案（输出 alpha = 0.1×1 + 0.9×0.04 ≈ 0.14）。
// 而 `ImGuiCol_*` 拿到半透明值也没问题：ImGui 的绘制本来就是混合的。
// 唯一**不能**再做的事：拿这些字段当「不透明基色」去算一个必然不透明的结果。
struct ColorToken {
    // §2.1 中性色阶
    std::uint32_t bgVoid = 0;
    std::uint32_t bgSurface = 0;
    std::uint32_t bgPanel = 0;
    std::uint32_t bgElevated = 0;
    std::uint32_t bgOverlay = 0;
    // ↓ 三者在五套主题下都是 rgba（可能是半透明），见上面那段
    std::uint32_t lineSubtle = 0;
    std::uint32_t lineNormal = 0;
    std::uint32_t lineStrong = 0;
    std::uint32_t textPrimary = 0;
    std::uint32_t textSecondary = 0;
    std::uint32_t textMuted = 0;
    std::uint32_t textInverse = 0;
    // §2.2 语义强调色
    std::uint32_t accentPrimary = 0;
    std::uint32_t accentPrimaryHover = 0;
    std::uint32_t accentPrimaryFg = 0;
    std::uint32_t accentSecondary = 0;
    std::uint32_t accentInfo = 0;
    std::uint32_t statusOk = 0;
    std::uint32_t statusWarn = 0;
    std::uint32_t statusDanger = 0;
    std::uint32_t statusBusy = 0;
    std::uint32_t statusIdle = 0;
    // §2.4 交互态专用色 + 焦点环 + 遮罩 + 阴影
    std::uint32_t statusPending = 0;
    // ↓ 纸墨 / 水墨 是 rgba，其余三套是实色
    std::uint32_t fillHover = 0;
    std::uint32_t fillSelected = 0;
    std::uint32_t fillMuted = 0;
    // ↓ line.focus 五套都是实色 hex，不用当半透明看待
    std::uint32_t lineFocus = 0;
    std::uint32_t shadowScrim = 0;
    std::uint32_t shadow1 = 0;
    std::uint32_t shadow2 = 0;
    std::uint32_t shadowAccent = 0;

    bool operator==(const ColorToken&) const = default;
};

// 颜色 token 点分名。顺序 == ColorToken 字段顺序 == ApplyTheme 映射顺序。
inline constexpr std::array<std::string_view, 31> kColorTokenNames = {
    "bg.void",  "bg.surface",  "bg.panel",  "bg.elevated",  "bg.overlay",
    "line.subtle",  "line.normal",  "line.strong",
    "text.primary",  "text.secondary",  "text.muted",  "text.inverse",
    "accent.primary",  "accent.primary.hover",  "accent.primary.fg",  "accent.secondary",  "accent.info",
    "status.ok",  "status.warn",  "status.danger",  "status.busy",  "status.idle",
    "status.pending",  "fill.hover",  "fill.selected",  "fill.muted",  "line.focus",  "shadow.scrim",
    "shadow.1",  "shadow.2",  "shadow.accent",
};

inline constexpr std::size_t kColorTokenCount = kColorTokenNames.size();

// ---- §2.3 形状 / 间距 / 字体 / 描边 / 动效（与主题无关）----
namespace radius {
inline constexpr int kXs = 4;   // 分段项 / 勾选框 / kbd / 卡片强调条
inline constexpr int kSm = 6;   // 按钮 / 输入框 / 菜单项 / 树节点 / tooltip
inline constexpr int kMd = 10;  // 卡片 / 分段控件容器 / 提示条 / 浮层面板
inline constexpr int kLg = 14;  // 弹窗 / 空态字形 / 浮动面板
inline constexpr int kXl = 18;  // 预留大容器档
inline constexpr int kPill = 999; // 胶囊 / 进度条 / 滚动条滑块（>半边长时 ImGui 自钳）
} // namespace radius

namespace space {
// ⚠️ 下标语义不可动：既有代码按 kSteps[i] 取值，插入会让全部既有取值漂移。
// tokens.css 的 --sp-1…--sp-7（4/8/12/16/24/32/48）对应下标 2…8。
inline constexpr std::array<int, 9> kSteps = {0, 2, 4, 8, 12, 16, 24, 32, 48};
inline constexpr int kXs = 6;   // 细间距：图标与文字、紧凑分组内
inline constexpr int kXl = 20;  // 大间距：区块之间
inline constexpr int kXxl = 40; // 超大间距：页面级留白
} // namespace space

namespace font {
// --font-ui 的族序：Segoe UI 在前（拉丁字形更中性），雅黑接管中文。
inline constexpr std::string_view kFamily = "Segoe UI, Microsoft YaHei UI, PingFang SC, system-ui";
// tokens.css:195 [data-theme="inkwash"] 覆盖的 --font-ui（衬线族）。只有水墨换。
inline constexpr std::string_view kFamilySerif = "Segoe UI, Noto Serif SC, Source Han Serif SC, SimSun, serif";
inline constexpr std::string_view kMonoFamily = "Cascadia Code, JetBrains Mono, Consolas, monospace";

// ---- 字号 token（float，px）----
//
// R3 起**页面只传 token，不写数字字面量**（记忆.md（原 component-reuse.md） §5.2：字号可调整
// 结构上成立的条件 —— 全局缩放进 Fonts.cpp 的 scale，token 值永远不动）。
// ⚠️ token 值 = 设计稿的 CSS 像素，**保持半点档**（不要 snap 成整数：整数字号
//    会让半点设计档被向上取档画大，本仓踩过这个静默偏差）。
// R3 只定义 PageKpi / Shell_Chrome 两个收编文件用到的档；每收编一批文件把它们
// 用到的档补进来 —— 判据是「有没有调用点」，与 kit/Fonts.cpp 的 kUiSizes 同一把尺，
// 不是把 CSS 里出现过的字号全铺一遍。
//
// ⚠️ **每一档的注释写的是「设计稿在哪一档用它」，不是「它大概多大」** —— 后者
//    读起来像参数说明，前者能回答「改这一档会不会连带改坏别处」。design-spec §1.1
//    是 CSS 实测刻度表，下面逐档对得上它；两处对不上的（kNano）单独标出来。
//
// ---- 最细三档（< 11）：右栏注记族 ----
inline constexpr float kNano = 9.0f;        // ⚠️ design-spec §1.1 刻度表**没有 9**，且
                                            //    `Fonts.cpp` 的 kUiSizes/kMonoSizes 也没有 ⇒
                                            //    `MonoAt(9.0f)` 取到 9.5f 那档光栅化，而
                                            //    传给 AddText 的字号实参仍是 9.0。唯一调用点
                                            //    是总控右栏「材质」小块的等宽字段值。
inline constexpr float kFine = 9.5f;        // 同上小块的分组小标题（700）与等宽字段值
inline constexpr float kSmall = 10.5f;      // kbd / tag.sm / chip.cnt / 甘特格 / 表头标签
                                            // （design-spec §1.1）+ 侧栏 .plabel（shell.css:583-588）
// ---- 主力档 ----
inline constexpr float kMicro = 11.0f;      // 最细一行：查看器脚注 / 整版说明（FileViewer.jsx:49 的 .tiny.dim 11px 档）/ .cmdk-foot
inline constexpr float kLabel = 11.5f;      // 小标签：状态栏 / KPI 标签与脚注
inline constexpr float kTiny = 12.0f;       // .tiny（base.css:118-120）：单位 / 面包屑
inline constexpr float kHint = 12.5f;       // 输入提示 / 副标题（.vw-sub）/ .sheet-bar 标题
inline constexpr float kBody = 13.0f;       // 控件正文：.btn/.tabs/.input/.table 声明最多
inline constexpr float kBodyLarge = 13.5f;  // base.css body / 顶栏品牌字
inline constexpr float kTitle = 14.0f;      // 面板标题档：查看器头部等宽文件名（FileViewer.jsx:158 内联 14）/ .drawer-h
// ---- 标题族（> 14）----
inline constexpr float kCardTitle = 14.5f;  // 项目卡名（design-spec §1.1；项目中心卡 / 新建向导卡）
inline constexpr float kModal = 15.0f;      // 弹窗标题 / 命令面板输入
inline constexpr float kPageTitle = 16.0f;  // 小说页模式标题（两行版式里那一行最大的字）
inline constexpr float kViewTitle = 18.0f;  // 区块 h3 / 视图大标题（design-spec §1.1 的 17/18 档）
inline constexpr float kChapter = 20.0f;    // 章节标题
inline constexpr float kValue = 24.0f;      // KPI 数值（views.css:444）
inline constexpr float kHubBrand = 34.0f;   // 项目中心品牌字（第十七轮定的版式，不是刻度表那一档）
inline constexpr float kBase = kBody;       // 默认字体档（Fonts.cpp 的 io.FontDefault）
// 字重：msyh 只有 400/700 两档，600/800 落 Bold（kit/Fonts.h 顶部）。
inline constexpr int kRegular = 400;
inline constexpr int kSemibold = 600;
} // namespace font

// ---- 阴影 ----
//
// ⚠️ 这里原来是一组 `struct Shadow { x, y, blur, color }` 加三个常量
//    `shadow::kSm/kMd/kLg`，全树**零引用**，而且值与设计稿对不上（三个 alpha 全错，
//    少了 CSS 里的第二层，命名把 --shadow-accent 写成了 --shadow-3）。**它从来没
//    生效过** —— 当时 `DrawShadowed()` 也没画阴影，只画了矩形加一条 1px 上沿高光。
//
// 现在改成**描述**而不是预乘好的常量：CSS 的 `box-shadow` 是「几何 + 半透明色」，
// 两者都随主题变（纸墨的阴影是暖色低 alpha，极夜是黑色高 alpha；模糊半径也不同：
// 深空 16px 而纸墨 14px），所以几何必须逐主题，见 Theme.cpp 的 kShadowSpecs。
struct ShadowLayer {
    float dx = 0.0f;
    float dy = 0.0f;
    // CSS 的 blur **半径**（不是直径）。ImGui 没有 blur API，渲染侧用它当
    // 「可见扩散范围」，画若干层同心圆角矩形逼近高斯。见 Draw.cpp 的 DrawShadow。
    float blur = 0.0f;
    // 该层 alpha 占该档基准色的比例。CSS 同一档里两层的 alpha 本来就不同
    // （深空 --shadow-1 是 0.35 与 0.3），而主题 JSON 里的 shadow.1 只有一个值，
    // 所以逐层比例记在这里，颜色（色相）仍然取 JSON 的 shadow.1 / 2 / accent。
    float alpha = 1.0f;
};

// 哪一档。决定去 JSON 取哪个色值。
//
// ⚠️ `None` 是**默认档**，而且它才是对的：设计稿的 `.card` 基线规则
// （ui.css:175）只有 background / border / radius / transition，**没有 box-shadow** ——
// 投影是 `.card.hoverable:hover`（ui.css:196-199）才加的，还配了 translateY(-2px)。
// 所以「卡片 = 有投影」是一个想当然的读法；照着读会给 28 个调用点无脑加上设计稿
// 在静止态根本没有的投影，比不做还偏离。
//
// 真正**常驻**有投影的是这些（逐条对过 CSS）：
//   ui.css:947   .modal          --shadow-2
//   ui.css:998   .toast          --shadow-2
//   ui.css:655   .drawer         --shadow-2
//   shell.css:101 .menu-pop      --shadow-2
//   shell.css:494 .cmdk          --shadow-2
//   views.css:216 .float-panel   --shadow-2
//   views.css:1035 .fnode        --shadow-1   ← 流式画布节点，**常驻**（不是 hover）
//   views.css:195 .float-toolbar --shadow-1
//   views.css:261 .float-strip   --shadow-1
// 而 hover 才有的是：.card.hoverable:hover(ui.css:198) / .proj-card:hover(views.css:119)
//   / .tl-card:hover(views.css:969) / .gantt .gcell:hover(views.css:494)
//   / .derive .dnode:hover(views.css:869) / .jump-btn:hover(shell.css:648)。
enum class ShadowTier {
    None = 0,   // 不画投影
    Card = 1,   // --shadow-1（两层）
    Overlay = 2,// --shadow-2（一层）
    Accent = 3, // --shadow-accent（一层）
    kCount = 4
};

// 一个 box-shadow 档位（最多两层，CSS 的 --shadow-1 就是两层）。
struct ShadowSpec {
    ShadowLayer layers[2]{};
    int count = 0;
};

namespace shadow {
inline constexpr int kMaxLayers = 2;
} // namespace shadow


namespace border {
inline constexpr int kNormal = 1;
inline constexpr int kFocus = 2;
} // namespace border

// ---- 半透明档位（alpha premultiplication steps）----
// tokens.css 本身**没有** color-mix；它把半透明写成裸 rgba(...)。真正用
// color-mix(in srgb, X N%, transparent) 的是 ui.css / views.css / shell.css，
// 那里出现的 N% 就是这 13 档。每一档都对应一行具名 CSS，不是随手取的数：
//
//   P07  views.css:660 .gates .gate.fail  背景  color-mix(danger 7%, fill-muted)
//   P09  views.css:11   工作区顶部 info 光晕
//   P10  ui.css:852/869 .stageflow .snode  背景（与 fill-muted 实色预混，非纯 alpha）
//   P12  ui.css:139-144 .tag.*  背景
//   P14  views.css:498 .gantt .gcell.stop 背景
//   P18  views.css:496 .gantt .gcell.done 背景 / :705 .chip.on .cnt / :1055 .fnode.run 光环
//   P22  shell.css:647 .jump-btn:hover 背景
//   P25  views.css:658 .gates .gate.pass  边框
//   P30  views.css:496 .gantt .gcell.done 边框（Storyboard.jsx:131 同值）
//   P35  ui.css:139-144 .tag.*  边框 / views.css:660 .gates .gate.fail 边框
//   P40  ui.css:851  .stageflow .snode.done 边框
//   P45  ui.css:868  .stageflow .snode.fail 边框 / views.css:1058 .fnode.done 边框
//   P50  views.css:1061 .fnode.fail 边框
//
// 28%（--shadow-accent）不在表里：它是 themes/*.json 的 shadow.accent 显式字段。
// 13 档的 alpha 字节 = lround(N*2.55)，实测值见 kBytes（探针验过）。
namespace alpha {
enum class Step : std::uint8_t { P07 = 0, P09, P10, P12, P14, P18, P22, P25, P30, P35, P40, P45, P50 };
inline constexpr std::size_t kStepCount = 13;
inline constexpr std::array<float, kStepCount> kPercents = {7.0f,  9.0f,  10.0f, 12.0f, 14.0f, 18.0f, 22.0f,
                                                            25.0f, 30.0f, 35.0f, 40.0f, 45.0f, 50.0f};
// lround(N*2.55)：18 23 26 31 36 46 56 64 77 89 102 115 128
inline constexpr std::array<std::uint8_t, kStepCount> kBytes = {18, 23, 26, 31, 36, 46, 56,
                                                                64, 77, 89, 102, 115, 128};
} // namespace alpha

namespace motion {
inline constexpr int kDurFastMs = 120;
inline constexpr int kDurBaseMs = 200;
inline constexpr int kDurSlowMs = 320;
struct Ease {
    float x1;
    float y1;
    float x2;
    float y2;
};
inline constexpr Ease kStandard{0.2f, 0.0f, 0.0f, 1.0f};     // --ease
// ⚠️ 订正：这里原来写的是 `kEmphasized{0.3, 0.05, 0.15, 1}`，**设计稿里根本没有这条**。
//    tokens.css:28 只定义了 `--ease-out: cubic-bezier(0.16, 1, 0.3, 1)`，而
//    `fade-up var(--dur-2) var(--ease-out)`（shell.css:369/380 等）用的就是它。
//    照抄 CSS 才是 1:1，凭印象写一个「看起来也像缓动」的曲线不是。
inline constexpr Ease kEaseOut{0.16f, 1.0f, 0.3f, 1.0f};      // --ease-out
// ⚠️ 原来还有一个 `kExit{0.4, 0, 1, 1}` —— tokens.css 里**没有** `--ease-exit`，
//    全树零引用。已删：编一个设计稿里不存在的缓动，比不编更糟 —— 它看起来像是
//    「有依据的」，下一个人会直接拿去用。
//
// 这三条目前零消费点；接过渡时（kit/Anim.h）从这里取，不要另写数字。
} // namespace motion

} // namespace shine::theme
