#pragma once
// shine::imguiverify::detail —— 悬停探针
//
// 前 N 张全是**静息态**。设计稿里几乎每个控件都定义了 `:hover`，而悬停恰恰是
// 最容易整条链路断掉的状态：win32 后端接没接上、HitTest 的 item 有没有占位、
// 控件有没有 hover 样式 —— 三者任何一个断了，界面都只是「鼠标划过去没反应」，
// 静息态截图**完全看不出来**。所以单独验，并且把「悬停前后必须像素不同」做成硬判据。
//
// 这个文件是**一族**探针：目标表（每个目标连同它的前置动作）、单目标的三拍与
// 判定、整轮的计数与 manifest 报告行。判定分四支，顺序不能换 ——
// 详见 ProbeHover 里「注入没到位那条排在最前面」的注释。

#include "ui/imgui/verify/ReviewSession.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace shine::imguiverify::detail {

// 悬停探针要打的目标 + **前置动作**。
//
// ⚠️ 前置动作必须显式写全。踩过的坑：资产网格那张卡探针一开始没切视图，
//    上一条 `assets-leaf-selected` 已经把资产页留在**详情**态 —— 那张坐标上根本没卡片，
//    探针报「hover 前后相同」，看上去像卡片 hover 链路断了。症状与真缺陷一模一样，
//    判据本身没错，错在没把状态摆成它承诺的样子。
struct HoverTarget {
    const char* name;
    int workspace;
    float x;
    float y;
    // 资产页要不要先切回总览网格（详情态下主区没有卡片）。
    bool assetsOverview = false;
    // 资产 kind 筛选要不要先清空（筛掉了目标所在的那一类就只剩空态）。
    bool clearKindFilter = false;
    // 底栏页签（0 队列 / 1 日志 / 2 产物 / 3 校验报告）。-1 = 不动。
    int dockTab = -1;
    // 出图页右侧面板页签；-1 = 不动。
    int imageFlowPanel = -1;
    // 小说页模式标签；-1 = 不动。
    int novelMode = -1;
};

// 一整轮悬停探针的计数。**全部进 manifest**（仅在非 0 时追加后缀），
// 但只有 ok 参与 overall 判定 —— 见 Session::hoverProbesPassed。
//
// ⚠️ 原来只有 `unstable` / `broken` 两档，而 `unstable` 把**四种成因完全不同**的情况
//    混成了一个数：注入没到位（宿主/后端层）、哨兵失效、页面在动、**探针坐标点空了**。
//    它们在 manifest 上分不开，就等于把「去改坐标」「去查后端」「去改产品」写成同一句话 ——
//    而这三件事的改法与风险完全不重叠。现在按**动作**分档：
struct HoverTally {
    int ok = 0;
    int injectFailed = 0; // 鼠标覆盖没到位（帧内 ImGui 看到的坐标 != 注入的）→ 宿主/后端层
    int sentinelDead = 0; // 窗外静息点仍命中控件，后续结论不可信
    int moving = 0;       // 静息两帧就不一致 → 页面在动，这一拍测不出 hover 的因果
    int offTarget = 0;    // 注入到位、页面静止，却没有控件命中，需查热区和输入状态
    int broken = 0;       // 命中且静止，hover 前后像素却完全相同 → **产品**的 hover 缺陷
    // 聚合值只为兼容既有的 manifest 读法（脚本按正则读 `unstable=`）。
    [[nodiscard]] int unstable() const { return injectFailed + sentinelDead + moving + offTarget; }
    [[nodiscard]] int failed() const { return unstable() + broken; }
};

// 悬停探针：把鼠标放到 (x, y) 拍一张，**再把鼠标放到窗外拍一张只取哈希**，
// 要求两者像素不同。对照那张不落盘（落盘会与已有静息态图逐字节撞上，把
// 「受控图不许重样」那条判据变成噪声）。
bool ProbeHover(imguiapp::Host& host, pages::Shell& shell, const std::filesystem::path& dir,
                const HoverTarget& target, shine::theme::ThemeId theme,
                std::vector<std::uint64_t>& drivenHashes,
                std::vector<std::string>& drivenNames, HoverTally* tally, int* captured,
                int* failed);

// 跑完整轮悬停探针，并把 `hover-probes=ok/total changed-vs-rest …` 写进 manifest。
// 同时填 Session::hoverProbesPassed / hoverProbeTotal。
void RunHoverProbes(Session& session);

} // namespace shine::imguiverify::detail
