#include "db/redis/Redis.h"

#include <mimalloc.h>

#include <atomic>
#include <cstring>

#include "core/Log.h"
#include "db/redis/RedisCmd.h"

#include <hiredis.h>

namespace shine::db::redis {
namespace {

std::atomic<bool> g_allocatorsInstalled{false};
Pool g_pool;

void* MiMalloc(size_t n) {
    return mi_malloc(n);
}
void* MiCalloc(size_t n, size_t s) {
    if (s != 0 && n > static_cast<size_t>(-1) / s) {
        return nullptr;
    }
    return mi_calloc(n, s);
}
void* MiRealloc(void* p, size_t n) {
    return mi_realloc(p, n);
}
void MiFree(void* p) {
    mi_free(p);
}
char* MiStrdup(const char* s) {
    if (!s) {
        return nullptr;
    }
    const size_t n = std::strlen(s) + 1;
    auto* out = static_cast<char*>(mi_malloc(n));
    if (!out) {
        return nullptr;
    }
    std::memcpy(out, s, n);
    return out;
}

void InstallAllocatorsOnce() {
    if (g_allocatorsInstalled.exchange(true)) {
        return;
    }
    hiredisAllocFuncs fns{};
    fns.mallocFn = MiMalloc;
    fns.callocFn = MiCalloc;
    fns.reallocFn = MiRealloc;
    fns.strdupFn = MiStrdup;
    fns.freeFn = MiFree;
    hiredisSetAllocators(&fns);
    log::Info("redis allocators=mimalloc");
}

} // namespace

std::expected<void, RedisError> Init(const PoolOptions& opt) {
    InstallAllocatorsOnce();
    // Pool::Init 内部已打日志，这里只负责把错误原样上抛（不重复记录）。
    if (auto r = g_pool.Init(opt); !r) {
        return std::unexpected(r.error());
    }
    return {};
}

void Shutdown() {
    g_pool.Shutdown();
    log::Info("redis shutdown");
}

bool ready() noexcept {
    return g_pool.ready();
}

PoolStats poolStats() {
    return g_pool.stats();
}

Client* DefaultClient() noexcept {
    if (!ready()) {
        return nullptr;
    }
    return nullptr; // use Acquire()/Lease
}

std::mutex& DefaultMutex() {
    static std::mutex m;
    return m;
}

std::expected<Lease, RedisError> Acquire() {
    return g_pool.Acquire();
}

namespace {
[[maybe_unused]] void CmdTemplateSmoke() {
    (void)BuildArgv(CmdPing{});
    (void)BuildArgv(CmdGet{.key = "k"});
    (void)BuildArgv(CmdSet{.key = "k", .value = "v"});
}
} // namespace

} // namespace shine::db::redis
