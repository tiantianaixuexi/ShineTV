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
};

// pages 由 Shell 提供；每张图之前显式设置主题与工作区（不依赖"默认恰好是我要的"）。
ReviewResult RunReview(imguiapp::Host& host, pages::Shell& shell,
                       const std::filesystem::path& outputDir);

} // namespace shine::imguiverify
