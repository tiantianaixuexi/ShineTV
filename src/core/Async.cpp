#include "core/Async.h"
#include "core/Log.h"

#include <exec/start_detached.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include <exception>
#include <memory>
#include <mutex>
#include <vector>

namespace shine::async {
namespace {

std::unique_ptr<exec::static_thread_pool> g_pool;
std::mutex g_uiMutex;
std::vector<std::move_only_function<void()>> g_uiQueue;

} // namespace

void Init() {
    if (!g_pool) {
        g_pool = std::make_unique<exec::static_thread_pool>(4);
    }
}

void Shutdown() {
    {
        std::lock_guard lock(g_uiMutex);
        g_uiQueue.clear();
    }
    if (g_pool) {
        // ⚠️ **故意泄漏线程池，不再 join**（2026-09-17 修"关窗后进程不消失"）：
        // worker 可能正卡在 libhv 的 HTTP connect（10s 级），甚至卡在 libhv 惰性 logger 的
        // `atexit` 死锁上 —— 实测 gdb 栈：
        //   main:   `async::Shutdown` → `static_thread_pool::~` → `std::thread::join` → `pthread_join`
        //   worker: `http_client_connect` → `hv_default_logger`(hlog.c:537) → `msvcrt!atexit`
        //           → `msvcrt!_onexit` → `RtlSleepConditionVariableCS`（**永久等待**）
        // 此时 `g_pool.reset()` 会**永远等不到**，用户看到的就是"窗口关了、进程还在"。
        // 进程马上要退出，交给 OS 回收这些线程；我们只保证：① 不再接受新任务；② 信箱已清空。
        (void)g_pool.release();
    }
}

void RunOnWorker(std::move_only_function<void()> fn) {
    if (!g_pool || !fn) {
        return;
    }
    auto work = stdexec::schedule(g_pool->get_scheduler()) | stdexec::then([fn = std::move(fn)]() mutable {
                    // P8.4 S3：worker 兜底 —— 异常转日志，避免 std::terminate
                    try {
                        fn();
                    } catch (const std::exception& e) {
                        log::Error("worker 异常：{}", e.what());
                    } catch (...) {
                        log::Error("worker 异常：未知类型");
                    }
                });
    exec::start_detached(std::move(work));
}

void PostToUi(std::move_only_function<void()> fn) {
    if (!fn) {
        return;
    }
    std::lock_guard lock(g_uiMutex);
    g_uiQueue.push_back(std::move(fn));
}

void DrainUiQueue() {
    std::vector<std::move_only_function<void()>> batch;
    {
        std::lock_guard lock(g_uiMutex);
        batch.swap(g_uiQueue);
    }
    for (auto& fn : batch) {
        try {
            fn();
        } catch (const std::exception& e) {
            log::Error("UI 信箱任务异常：{}", e.what());
        } catch (...) {
            log::Error("UI 信箱任务异常：未知类型");
        }
    }
}

} // namespace shine::async