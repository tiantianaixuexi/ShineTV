#pragma once
// shine::imguiverify::RunReview —— SHINE_IMGUI_REVIEW 总回归入口（P6.3）
//
// 一次跑完 N 个页面 × 5 套主题并抓图，是 ImGui 前端的**唯一**总回归入口。
// 判据与纪律见 Capture.h。
#pragma once

#include "ui/imgui/host/Host.h"
#include "ui/imgui/pages/Shell.h"

#include <filesystem>

namespace shine::imguiverify {

struct ReviewResult {
    int captured = 0;
    int failed = 0;
    // 粘性滚动请求**已登记但没到位**。与 failed 分开：图片照样写出来了（manifest
    // 记 saved），但那张图拍到的不是它承诺的状态 —— 判据绿、结论错。
    // 归到 failed 里会掩盖原因，所以单列一个标志并参与 overall 判定。
    bool scrollFailed = false;
    // overall 判定结果，**必须**由调用方用它决定退出码。
    //
    // ⚠️ 早先 ReviewResult 里没有这个字段，AppEntry 只能用 `failed == 0` 推退出码
    //    —— 而 failed 只数「图片没写出来」。判据红（hover 探针没过 / 快照 TIMEOUT）
    //    时图片照样全部 saved，failed 仍是 0，于是 manifest 写 overall=FAIL、
    //    进程却退 0：读退出码的人会得出「这轮是绿的」。
    //    `run_reviews.ps1` 只能把它报成 agree=False（"PASS↔0/FAIL↔1" 不自洽），
    //    分不清是判据红还是判据自身坏了 —— 而这两种都要查。
    //    判据的结论必须能**原样**传出去，不能让调用方二次推算。
    bool pass = false;
};

// pages 由 Shell 提供；每张图之前显式设置主题与工作区（不依赖"默认恰好是我要的"）。
ReviewResult RunReview(imguiapp::Host& host, pages::Shell& shell,
                       const std::filesystem::path& outputDir);

} // namespace shine::imguiverify
