#pragma once
#include <functional>

namespace shine::async {

// stdexec static_thread_pool + UI-thread mailbox.
void Init();
void Shutdown();

// Run fn on the worker pool (thread-safe).
// 用 `std::move_only_function`：允许投递**持有可移动不可拷贝资源**的任务
// （例如 `gallery::Image` 这种 mimalloc 大图），避免为了进 std::function 而多一次拷贝。
void RunOnWorker(std::move_only_function<void()> fn);

// Queue a callback for the next UI frame (thread-safe).
void PostToUi(std::move_only_function<void()> fn);

// Called once per frame on the UI thread.
void DrainUiQueue();

} // namespace shine::async