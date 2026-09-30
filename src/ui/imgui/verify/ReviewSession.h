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
// 现在全量覆盖，判读成本靠文件名分组与 md5 排重来控制。
constexpr int kThemedWorkspaces[] = {
    static_cast<int>(pages::Workspace::Overview),
    static_cast<int>(pages::Workspace::Novel),
    static_cast<int>(pages::Workspace::Assets),
    static_cast<int>(pages::Workspace::Storyboard),
    static_cast<int>(pages::Workspace::ImageFlow),
    static_cast<int>(pages::Workspace::VideoFlow),
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
    // 浮层点击探针的结果。**必须与 hover / shortcuts 同级**：它的效果不在像素上，
    // 只打日志而 overall 仍 PASS 就是假绿（见 ReviewVerdict.cpp 的 overall 那行）。
    int overlayClicksPassed = 0;
    int overlayClickTotal = 4;

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
};

} // namespace shine::imguiverify::detail
