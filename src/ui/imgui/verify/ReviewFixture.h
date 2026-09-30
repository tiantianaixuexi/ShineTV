#pragma once
// shine::imguiverify::detail —— 取证流水线的输入 fixture
//
// 这里造的**不是产品数据**，是让取证流水线有东西可拍的输入文件：格式与
// business 层落盘格式完全一致的校验报告 JSON，加上产物页要扫的 output 目录。
//
// ⚠️ 纪律（这一族自己的理由全在 ReviewFixture.cpp 里）：**应用侧绝不造报告** ——
//    DrawDockReports 只读 business 层落盘的文件。fixture 只存在于输出目录下的
//    临时工程里，跑完由调用方把工程根还原回去。
//
// 少了 fixture 就是覆盖洞：页面拍到了、列表分支从来没被执行过，而 manifest 照样
// 记 saved。「图拍到了」与「分支跑到了」是两件事。

#include <filesystem>

namespace shine::imguiverify::detail {

// 在 root 下造一份样例工程。返回 false = 写盘失败（调用方据此跳过那一族取证）。
bool SeedReportFixture(const std::filesystem::path& root);

} // namespace shine::imguiverify::detail
