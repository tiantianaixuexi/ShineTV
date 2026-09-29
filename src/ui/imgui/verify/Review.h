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
};

// pages 由 Shell 提供；每张图之前显式设置主题与工作区（不依赖"默认恰好是我要的"）。
ReviewResult RunReview(imguiapp::Host& host, pages::Shell& shell,
                       const std::filesystem::path& outputDir);

} // namespace shine::imguiverify
