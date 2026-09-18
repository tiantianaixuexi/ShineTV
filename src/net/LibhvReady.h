#pragma once
// shine::net —— libhv 进程级就绪（唯一入口）
//
// 必须在**任何** libhv 调用之前、由**主线程**调用一次（App::Init / 各 HTTP 模块兜底）。
// 初始化内容：
//  1) `call_once` 创建 `hv_default_logger`（避免并发首次 + atexit 死锁）；
//  2) 将 handler 换成 **shine::log（spdlog）**，libhv 日志进应用日志面板/控制台，
//     不再写 `libhv*.log`。业务侧 include 本头，不要再依赖 comfy::ComfyHttp.h。
namespace shine::net {

void EnsureLibhvReady();

} // namespace shine::net
