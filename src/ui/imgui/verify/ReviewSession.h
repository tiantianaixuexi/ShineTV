#pragma once
// shine::imguiverify::detail —— 总回归各族判据共享的常量与一次会话的状态
//
// 这个头只放**跨文件共享**的东西，别往里塞实现：
//   * kReportWaitFrameCap / kThemedWorkspaces / DockTabSlug —— 三段覆盖与命名的常量；
//   * Session —— 一次 RunReview 全程累积的东西：manifest 路径、抓图计数、各族
//     判据的计数、以及两张查重表（主题覆盖 / 受控图）。
//
// ⚠️ Session 里带计数的那几个字段**全部**参与 overall 判定，写法是「先给一个
//    **该族没跑到**的初值，再由跑那一族的文件填」。判据报告绿与「判据压根没跑」
//    在结果里长得一模一样（都要写 -1 或一个明显偏小的数），所以初值不能填成
//    「已通过」：`-1 < kHoverProbeTotal` 恒成立，判红是默认行为而不是例外分支。
//
// ⚠️ 换名说明（拆分 Review.cpp 时做的唯一一处符号改名）：局部量 kHoverProbeTotal /
//    kShortcutTotal / kOverlayClickTotal 是**可变的**（悬停按目标数回填、浮层按
//    条数自增），搬进结构体后去掉 k 前缀，免得下一个读代码的人顺手加 const 就把
//    判据改坏。值与初值一个没动。

#include "ui/imgui/verify/Review.h"

#include "ui/imgui/verify/ReviewPixel.h"

#include <cstdint>
#include <filesystem>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace shine::imguiverify::detail {

// 等报告扫描回投的帧数上限。PumpFrames 是紧循环、不 sleep，光推帧未必给 worker
// 调度机会，所以调用方每帧还要真的让出一会儿。
constexpr int kReportWaitFrameCap = 400;

// 工作区序号。场景各段与悬停目标表都要用，统一提到这里。
//
// ⚠️ 历史教训，保留在这儿的理由：出图 / 出片这两个常量原先声明在「资产那一段」
//    里面 —— 因为悬停探针（第六段）排在第七段**之前**却也要用它们，声明在各自
//    用到的那一段里，后一段就看不见，编译直接报「未声明」。第一版就是这么写的。
//    拆成文件之后函数作用域不再帮忙，共用常量只能统一提到最早的公共位置。
inline constexpr int kWsOverview = static_cast<int>(pages::Workspace::Overview);
inline constexpr int kWsNovel = static_cast<int>(pages::Workspace::Novel);
inline constexpr int kWsAssets = static_cast<int>(pages::Workspace::Assets);
inline constexpr int kWsStoryboard = static_cast<int>(pages::Workspace::Storyboard);
inline constexpr int kWsImageFlow = static_cast<int>(pages::Workspace::ImageFlow);
inline constexpr int kWsVideoFlow = static_cast<int>(pages::Workspace::VideoFlow);

// 主题轮：画廊 + 资产两页最密（覆盖全部控件与 7 色调），足以暴露主题映射漏项。
// 6 工作区全跑 × 5 主题 = 30 张，判读成本远高于收益；这里用「全页 × 当前主题」
// 加「密页 × 5 主题」两段覆盖，30 张的覆盖面一分不少。
// 主题轮覆盖哪些页。P6.3 要求「6 工作区 × 5 主题 = 30 组」（phases.md:356），
// 早先只挑了画廊 + 资产两页，于是总控/小说/分镜/出图/出片在 4 套非默认主题下
// **从未被截过** —— 主题映射在这些页上漏项不会被任何证据图发现。
// 现在全量覆盖（P5.7 新增工作流后是 7 页 × 5 主题 = 35 组），判读成本靠文件名
// 分组与 md5 排重来控制。
constexpr int kThemedWorkspaces[] = {
    static_cast<int>(pages::Workspace::Overview),
    static_cast<int>(pages::Workspace::Novel),
    static_cast<int>(pages::Workspace::Assets),
    static_cast<int>(pages::Workspace::Storyboard),
    static_cast<int>(pages::Workspace::ImageFlow),
    static_cast<int>(pages::Workspace::VideoFlow),
    static_cast<int>(pages::Workspace::Workflow),
};

// 底栏页签名 → 文件名片段。与 `Shell.cpp` 的 `tabs[]` 同序（任务队列/日志/产物/校验报告）。
// 写在取证里而不是去读外壳那份数组：那张表是私有局部量，跨不过 TU，抄一份反而
// 多一个可能失同步的副本 —— 所以用**下标查表**而不是再维护一份平行定义。
inline const char* DockTabSlug(int tab) {
    static const char* kSlugs[] = {"queue", "logs", "artifacts", "reports"};
    return (tab >= 0 && tab < static_cast<int>(std::size(kSlugs))) ? kSlugs[tab] : "tab";
}

// 一次 RunReview 的会话。各族判据与场景脚本共享它，因此**不要**把只服务一族的
// 临时量放进来 —— 那属于那个族自己的函数。
struct Session {
    Session(imguiapp::Host& h, pages::Shell& s, std::filesystem::path dir)
        : host(h), shell(s), outputDir(std::move(dir)) {}

    imguiapp::Host& host;
    pages::Shell& shell;
    std::filesystem::path outputDir;
    // shots-manifest.txt 的绝对路径。建完输出目录后由入口填。
    std::filesystem::path manifest;
    // 判据开始前用户的主题。所有 base 主题的图、悬停探针、SHINE_SCAN 都用它；
    // 主题轮跑完也用它把主题还原回去。构造时取一次 —— 那正是原来取它的时机。
    const shine::theme::ThemeId base = shine::theme::CurrentThemeId();
    // 抓图计数与 overall 结论。**调用方必须用 result.pass 决定退出码**，
    // 不许拿 captured / failed 二次推算（见 Review.h 里记的那次假绿）。
    ReviewResult result;

    // 主题轮：每个工作区在 5 套主题下的像素指纹。判据二查重样。
    std::map<int, std::vector<std::uint64_t>> themedHashes;
    // 受控图的像素哈希与图名。第三/四段的每张都是**显式驱动**出来的（开工程 / 选页签 /
    // 选条目 / 开浮层），彼此应该两两不同 —— 出现重样就说明其中一张没拍到它承诺的状态。
    // ⚠️ 只对这批做重样判据，不对全树做：`ws-*`（当前主题）与 `theme-<base>-*`
    //    拍的是同一个工作区同一套主题，只因累积的界面状态不同才没有撞上 ——
    //    拿它们互相比较，判据就成了碰运气。
    std::vector<std::uint64_t> drivenHashes;
    std::vector<std::string> drivenNames;

    // worker 回投的等待结论：既进 manifest，也进 overall。没拍成就是没拍成，
    // 不能因为「其它图都写出来了」就整轮报绿。
    bool reportScanConverged = false;
    bool bookSnapshotConverged = false;
    bool assetSnapshotConverged = false;
    bool artifactsConverged = false;

    // 悬停探针：几个目标里几个真的产生了像素变化。-1 = 这一族压根没跑到。
    int hoverProbesPassed = -1;
    // 本轮探针总数（targets 数组长度）。写死 4 的话加探针时会忘了改判据，
    // 判据跟着目标数走。
    int hoverProbeTotal = 4;
    // 快捷键动作判据：几个快捷键按下去真的改变了界面状态。-1 = 没跑到。
    int shortcutsPassed = -1;
    int shortcutTotal = 7;
    // 真设定图的异步链有没有落地（`kit::PendingImageCount()` 归零）。
    //
    // ⚠️ 与 reportScanConverged / bookSnapshotConverged 同级，**必须进 overall**：
    //    图片是异步上屏的（worker 解码 → PostToUi → 上传 GPU），而「摆好状态就抓图」
    //    只泵一两帧 —— 那一两帧里图还没到。若只把它写成一行日志，
    //    「图片链断了」与「拍得太早」在截图上完全一样，而整轮照样 PASS。
    //    初值 false = 「这一段没跑到」，与其余几族同一条规矩。
    bool sheetImagesSettled = false;

    // 「纹理被淘汰后会自己回来」有没有被验到（见 `kit/Image.h` 的第 1 条约束）。
    //
    // 同样**必须进 overall**：这条约束一旦被写坏，症状是「图闪过一次就没了」——
    // 不报错、不崩、截图上也只在淘汰之后才看得出来。判据取「清空 GPU 纹理缓存之后
    // 真图颜色还在不在」，读数来自**像素计数**（截图跨轮不可复现，不能比图）。
    bool sheetImagesRecovered = false;

    // 浮层点击探针的结果。**必须与 hover / shortcuts 同级**：它的效果不在像素上，
    // 只打日志而 overall 仍 PASS 就是假绿（见 ReviewVerdict.cpp 的 overall 那行）。
    int overlayClicksPassed = 0;
    int overlayClickTotal = 4;

    // 查看器里那两屏（大图那一层 + 「导出整版」的联系表）有没有真的把图打上屏。
    //
    // ⚠️ 与 sheetImagesSettled 同级、也必须进 overall，而且它挡的更窄更专：
    //    大图档（`kit::RequestImageFull`，2048）**只在查看器开着的那一帧**才被请求，
    //    所以「压根没请求」与「请求了没落地」在截图上完全一样（都画 `Art` 占位）。
    //    结论 = 大图档 requested>0 && pending==0 && **两屏各自的纯色像素 > 0**。
    bool viewerFullSettled = false;

    // 逐镜成图（`generated_images` 的 `source_kind='shot'`）在两个位点上有没有真的上屏：
    //   * `filmStripImages`     —— 出片页「成片条」那几格（第 1/2 镜的两种纯色各 > 0）；
    //   * `imageFlowResultImage`—— 出图页「结果」页签那张成图（第 1 镜的纯色 > 0）。
    //
    // ⚠️ 两者都必须进 overall，理由与 `sheetImagesSettled` 完全一样：真图是**异步**上屏的，
    //    「链断了」与「拍得太早」在截图上都是占位画 —— 这两处以前画的正是 `Art`
    //    （所以占位画与「还没出图」在像素上根本分不开）。判据用**数纯色**（截图跨轮不可复现）。
    bool filmStripImages = false;
    bool imageFlowResultImage = false;
    // 出图页「结果」页签那两行 KV（`Prompt` / `生成参数`）的**数据投影**对不对：
    // 第 1 镜成图那一行的 `prompt/model/steps/width/height` 是否等于 fixture 契约
    // （`ReviewFixture.h` 的 `kShot1*`）。
    //
    // ⚠️ 这一条判的**不是像素**，与上面那几条不同，理由是这两行**没有异步窗口** ——
    //    同一帧里从 `BookShot` 读出来、当帧画完，所以没有「链断了」与「拍早了」同形
    //    的那种二义，数色值在这里没有对象可数。它挡的是：① 查询没把那几列 SELECT
    //    出来；② 列序读串（steps 当 width）；③ 行被「全空」判掉 ⇒ 界面上那两行
    //    凭空少掉，而其余判据全绿。
    // ⚠️ 它**挡不住**「画在屏外」—— 那一段由 `Page_ImageFlow.cpp` 里 kvHeight 跟着
    //    行数走的算式保证（面板 560 高、body 里放得下 6 行）。别把这条当成像素判据读。
    bool imageFlowParams = false;

    // 总控 KPI 卡右上角那团柔光的**像素**判据：屏上真有足够多「合成后的 accent-dim」。
    //
    // ⚠️ 为什么非要数像素：这一团是 `PushClipRect` 裁到 padding box 之后画的，而
    //    「裁剪写反/写小」的症状是**整团柔光消失**，卡还在、字还在、静息截图照样 saved。
    //    只读代码或只比「颜色算对没有」都抓不到它。
    // ⚠️ 判据要有**上下界**（都写在 ReviewScene.cpp）：下界 8000 挡「只接上一张卡」
    //    （一张 ≈3200），上界 40000 挡「颜色退化成一块面」。只判下界实测骗过一次：
    //    页面 alpha 链写错 ⇒ 报出来的色是**卡底色** ⇒ 读到 35 万像素，判绿。
    //    这团光是 5 个 90px 圆被裁掉一角，必然只有万把像素的量级。
    // ⚠️ 颜色来自页面自己的只读出口（`pages::KpiGlowRgb()`）：柔光色是**逐主题**的
    //    （accent-dim 每套不同），写死在取证里会让这条判据只在默认主题下成立。
    bool kpiGlow = false;
    // 总控「最近产物」那一行**非 shot 来源**的出图产物（`generated_images`）有没有上屏。
    // 同一个理由：这一卡以前只有 T 链账本那一族数据，而账本在执行体接入之前**恒为空**
    // ⇒ 卡上永远只有空态。判据用数纯色（`assets/gen_recent_ref.png` = kRecentAssetColor）。
    bool recentProductsImage = false;

    // `kit::ImageKey` 与 `kit::ImageKeyFull` 的号段是不是真的分裂了
    // （`kit::ImageKeyCollisionCount() == 0`）。
    //
    // ⚠️ 这一条**必须进 overall**：它是 `viewerFullSettled` 那个判据的**成立前提** ——
    //    两档 key 一旦撞上，大图档就登记不进登记表，`RequestedImageFullCount()` 恒 0，
    //    界面上却照样画得出图 ⇒ 判据报红而产品没坏（假红）。
    //    而且它只在**某些路径字符串**下撞（路径含 `SHINE_IMGUI_REVIEW` 的输出目录）
    //    ⇒ 「这轮红了」会被当成偶发而放过去。初值 false = 这一段没跑到。
    bool imageKeysDistinct = false;

    // 抓一张图并计入 captured / failed。返回像素内容哈希（0 = 没抓到或没写出来，
    // 判据查重样时按「0 跳过」处理，见 ReviewVerdict.cpp）。
    std::uint64_t Grab(const std::string& name, int workspace, shine::theme::ThemeId theme) {
        std::uint64_t hash = 0;
        if (Shoot(host, shell, outputDir, name, workspace, theme, &hash)) {
            result.captured++;
        } else {
            result.failed++;
        }
        return hash;
    }

    // 受控图：抓完之后把指纹与图名记进判据三要查的那张表。
    void GrabDriven(const std::string& name, int workspace, shine::theme::ThemeId theme) {
        drivenHashes.push_back(Grab(name, workspace, theme));
        drivenNames.push_back(name);
    }

    // **初始页**（项目中心）那一张。应用启动就落在它上面（webui `store.jsx:13` 的
    // `useState('hub')`），所以它是「一开机就能看到的那一屏」，而不是某个工作区。
    //
    // ⚠️ 与 `Grab` 的差别只有一处：`hubShot = true`（让 `Shoot` 把项目中心**开**着拍）。
    //    `Grab` 的缺省动作是关掉它 —— 少了这一张，项目中心这一整页在 89 张证据图里
    //    一张都没有，页面上改了什么都不会有任何像素证据。
    //
    // ⚠️ 这张图的**内容依赖本机** `%APPDATA%\ShineTVStudio\projects.json`（项目中心的
    //    最近列表读它，取证不重写用户索引），所以它只当**视觉证据**用：不要拿它的
    //    像素数当判据（换台机器项目数不同，读数就不同）。
    void GrabHubDriven(const std::string& name, shine::theme::ThemeId theme) {
        std::uint64_t hash = 0;
        if (Shoot(host, shell, outputDir, name, kWsOverview, theme, &hash, nullptr,
                  /*hubShot=*/true)) {
            result.captured++;
        } else {
            result.failed++;
        }
        drivenHashes.push_back(hash);
        drivenNames.push_back(name);
    }

    // 抓一张图并数出某个纯色有多少像素（`rgb` = 0xRRGGBB）。
    //
    // `registerDriven` 决定要不要把它登记进「受控图不许逐字节相同」那张表：
    //   * **true** —— 这是一个本该与别的受控状态不同的独立状态（如 `assets-grid-all`）；
    //   * **false** —— 这个探针拍的是**与前面某张相同的状态**（如淘汰试验那张，
    //     它就是要和淘汰前一样）。登记进去等于给判据布置一个「两张图本来就该一样」的
    //     陷阱，而截图本身不可复现 ⇒ 有时真会逐字节相同 ⇒ 间歇性 FAIL。
    //     它要验的东西与「图与图之间是否不同」无关，用**像素计数**验。
    int GrabCountColor(const std::string& name, int workspace, shine::theme::ThemeId theme,
                       std::uint32_t rgb, bool registerDriven) {
        std::vector<std::uint8_t> pixels;
        std::uint64_t hash = 0;
        if (Shoot(host, shell, outputDir, name, workspace, theme, &hash, &pixels)) {
            result.captured++;
        } else {
            result.failed++;
        }
        if (registerDriven) {
            drivenHashes.push_back(hash);
            drivenNames.push_back(name);
        }
        return static_cast<int>(CountColorPixels(pixels, rgb));
    }

    // 一张图里数**两个**纯色。判据要「两处都接上了」时必须用这个，不能用两次
    // `GrabCountColor`：那是**两张逐字节相同**的图（同一个状态拍两遍），既多存一张
    // 证据、又让读的人以为那是两个不同状态（而第二张还必须 `registerDriven=false`
    // 才不会撞上「受控图不许重样」——一处不写就间歇性 FAIL）。
    std::pair<int, int> GrabCountTwoColors(const std::string& name, int workspace,
                                           shine::theme::ThemeId theme, std::uint32_t rgbA,
                                           std::uint32_t rgbB, bool registerDriven) {
        std::vector<std::uint8_t> pixels;
        std::uint64_t hash = 0;
        if (Shoot(host, shell, outputDir, name, workspace, theme, &hash, &pixels)) {
            result.captured++;
        } else {
            result.failed++;
        }
        if (registerDriven) {
            drivenHashes.push_back(hash);
            drivenNames.push_back(name);
        }
        return {static_cast<int>(CountColorPixels(pixels, rgbA)),
                static_cast<int>(CountColorPixels(pixels, rgbB))};
    }
};

} // namespace shine::imguiverify::detail
