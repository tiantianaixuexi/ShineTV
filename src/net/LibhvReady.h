#pragma once
// shine::net —— libhv 进程级就绪（唯一入口）
//
// 必须在**任何** libhv 调用之前、由**主线程**调用一次（App::Init / 各 HTTP 模块兜底）。
// 原因见 .cpp：libhv 默认 logger 惰性 + atexit，并发首次使用会死锁。
// 业务侧请 include 本头，不要再依赖 comfy::ComfyHttp.h 才拿到 EnsureLibhvReady。
namespace shine::net {

void EnsureLibhvReady();

} // namespace shine::net
