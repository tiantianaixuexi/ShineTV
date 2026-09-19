#include "core/Log.h"

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <fmt/format.h>

#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shine::log {
namespace {

std::mutex g_uiMutex;
std::vector<Line> g_lines;
uint64_t g_version = 0;
std::shared_ptr<spdlog::logger> g_logger;
constexpr size_t kMaxLines = 2000;
constexpr size_t kTrimBatch = 500;

template <typename Mutex>
class UiSink final : public spdlog::sinks::base_sink<Mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        this->formatter_->format(msg, formatted);
        std::string text(formatted.data(), formatted.size());
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
            text.pop_back();
        }
        int level = 0;
        switch (msg.level) {
        case spdlog::level::warn: level = 1; break;
        case spdlog::level::err:
        case spdlog::level::critical: level = 2; break;
        default: level = 0; break;
        }
        std::lock_guard lock(g_uiMutex);
        if (g_lines.size() >= kMaxLines) {
            g_lines.erase(g_lines.begin(), g_lines.begin() + static_cast<std::ptrdiff_t>(kTrimBatch));
        }
        g_lines.push_back({level, std::move(text)});
        ++g_version;
    }
    void flush_() override {}
};

} // namespace

void LogText(int level, std::string_view text) {
    if (!g_logger) {
        return;
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    switch (level) {
    case 1: g_logger->warn("{}", text); break;
    case 2: g_logger->error("{}", text); break;
    default: g_logger->info("{}", text); break;
    }
}

void Init() {
    if (g_logger) {
        return;
    }
    auto ui = std::make_shared<UiSink<std::mutex>>();
    // S18：`SHINE_LOG_TO_STDERR=1` → 日志走 stderr 而不是 stdout。
    // 必须的场合：**stdio MCP**（`--mcp-stdio`）—— 协议要求 stdout **只走 JSON-RPC**，
    // 日志混进去会让客户端解析失败（实测：响应里夹着 `[..] [info] mcp Register…` 行）。
    std::shared_ptr<spdlog::sinks::sink> console;
    if (const char* env = std::getenv("SHINE_LOG_TO_STDERR"); env != nullptr && *env != '\0' &&
                                                                     env[0] != '0') {
        console = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    } else {
        console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    }
    std::vector<std::shared_ptr<spdlog::sinks::sink>> sinks{ui, console};
    std::string logFile;
    if (const char* env = std::getenv("SHINE_LOG_FILE"); env != nullptr && *env != '\0') {
        logFile = env;
    } else {
        wchar_t appdata[MAX_PATH];
        const DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            std::filesystem::path dir = std::filesystem::path(appdata) / L"ShineTVStudio" / L"logs";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            const std::filesystem::path file = dir / L"shine.log";
            // spdlog on MinGW: narrow path (APPDATA 用户名若含中文可能失败 → 静默跳过)
            logFile = file.string();
        }
    }
    if (!logFile.empty()) {
        try {
            sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, true));
        } catch (...) {
        }
    }
    g_logger = std::make_shared<spdlog::logger>("shine", sinks.begin(), sinks.end());
    g_logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    g_logger->set_level(spdlog::level::trace);
    spdlog::set_default_logger(g_logger);
    spdlog::flush_on(spdlog::level::info);
}

void Shutdown() {
    if (g_logger) {
        g_logger->flush();
        g_logger.reset();
    }
}

std::vector<Line> LinesSnapshot() {
    std::lock_guard lock(g_uiMutex);
    return g_lines;
}

size_t LineCount() {
    std::lock_guard lock(g_uiMutex);
    return g_lines.size();
}

uint64_t Version() {
    std::lock_guard lock(g_uiMutex);
    return g_version;
}

void Clear() {
    std::lock_guard lock(g_uiMutex);
    g_lines.clear();
    ++g_version;
}

} // namespace shine::log
