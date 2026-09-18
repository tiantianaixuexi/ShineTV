#include "net/LibhvReady.h"

#include <hlog.h>

#include <mutex>

namespace shine::net {

namespace {
// ⚠️ libhv 默认 logger 惰性 + 非线程安全：首次 `hv_default_logger()` 会 `atexit(...)`，
// 多 worker 并发首次写日志会在 msvcrt `_onexit` 上死锁（见 Doc 与 坑与手法）。
// 修法：第一次碰 libhv 之前 `call_once` 单线程初始化（不改 third/）。
std::once_flag g_libhvLoggerOnce;
} // namespace

void EnsureLibhvReady() {
    std::call_once(g_libhvLoggerOnce, []() { (void)hv_default_logger(); });
}

} // namespace shine::net
