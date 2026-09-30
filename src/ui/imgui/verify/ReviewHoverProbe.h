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

// 一整轮悬停探针的三个计数。**三个都进 manifest**（unstable / broken 仅在非 0 时
// 追加后缀），但只有 ok 参与 overall 判定 —— 见 Session::hoverProbesPassed。
struct HoverTally {
    int ok = 0;
    int unstable = 0;
    int broken = 0;
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
