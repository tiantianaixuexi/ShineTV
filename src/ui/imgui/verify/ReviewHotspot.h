#pragma once
// shine::imguiverify::detail —— 热区扫描：把「这个控件现在在哪」**量**出来
//
// 两个扫描函数 + 一个环境变量入口，同一族用途：
//   * ScanHotspots —— 沿一片网格把鼠标挨个挪过去，收集出现过的**全部**控件 id。
//   * FindHotspot —— 同样地扫，返回**第一个**命中某个 id 的坐标。
//   * RunHotspotScanEnv —— `SHINE_SCAN=<ws>:<x0>:<y0>:<x1>:<y1>:<step>` 的入口。
//
// 为什么需要它们：探针报「点空了」时，坐标必须**量**出来而不是猜。猜出来的坐标
// （`ImVec2(120, 24)` 之类）在布局一改就静默失效，判据却仍然报「通过」。
//
// ⚠️ 两者都**不参与** pass/fail：它们只打 id / 坐标，是诊断工具。

#include "ui/imgui/host/Host.h"
#include "ui/imgui/pages/Shell.h"

#include <set>
#include <string>

namespace shine::imguiverify::detail {

std::set<std::string> ScanHotspots(imguiapp::Host& host, pages::Shell& shell, float x0, float y0,
                                   float x1, float y1, float step, bool verbose = true);

bool FindHotspot(imguiapp::Host& host, pages::Shell& shell, const char* id, ImVec2& out,
                 float step = 12.0f, float x0 = 0.0f, float y0 = 0.0f, float x1 = -1.0f,
                 float y1 = -1.0f);

// 环境变量入口。不设 SHINE_SCAN 时什么也不做。
void RunHotspotScanEnv(imguiapp::Host& host, pages::Shell& shell, shine::theme::ThemeId base);

} // namespace shine::imguiverify::detail
