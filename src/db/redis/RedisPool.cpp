#include "db/redis/RedisPool.h"

#include <fmt/format.h>

#include <utility>

#include "core/Log.h"

namespace shine::db::redis {

Lease::Lease(Client* c, void* pool) noexcept : client_(c), pool_(pool) {}

Lease::Lease(Lease&& other) noexcept : client_(other.client_), pool_(other.pool_) {
    other.client_ = nullptr;
    other.pool_ = nullptr;
}

Lease& Lease::operator=(Lease&& other) noexcept {
    if (this != &other) {
        if (client_ && pool_) {
            static_cast<Pool*>(pool_)->Release(client_, false);
        }
        client_ = other.client_;
        pool_ = other.pool_;
        other.client_ = nullptr;
        other.pool_ = nullptr;
    }
    return *this;
}

Lease::~Lease() {
    if (client_ && pool_) {
        static_cast<Pool*>(pool_)->Release(client_, false);
        client_ = nullptr;
        pool_ = nullptr;
    }
}

Pool::~Pool() {
    Shutdown();
}

void Pool::CloseUnlocked(Slot& slot) noexcept {
    if (slot.client) {
        slot.client->Disconnect();
        slot.client.reset();
        ++closed_;
    }
    slot.broken = false;
}

std::expected<std::unique_ptr<Client>, RedisError> Pool::OpenNew() {
    auto c = std::make_unique<Client>();
    if (auto r = c->Connect(opt_.connect); !r) {
        return std::unexpected(r.error());
    }
    return c;
}

std::expected<void, RedisError> Pool::Init(const PoolOptions& opt) {
    Shutdown();

    std::lock_guard lock(mutex_);
    opt_ = opt;
    if (opt_.maxConnections == 0) {
        opt_.maxConnections = 1;
    }
    if (opt_.minConnections > opt_.maxConnections) {
        opt_.minConnections = opt_.maxConnections;
    }

    std::size_t opened = 0;
    RedisError warmupError{};
    bool warmupFailed = false;
    for (std::size_t i = 0; i < opt_.minConnections; ++i) {
        auto c = OpenNew();
        if (!c) {
            warmupError = c.error();
            warmupFailed = true;
            log::Warn("redis pool warmup {}/{} failed: kind={} code={} msg={}", i + 1, opt_.minConnections,
                      ToChar(c.error().kind), c.error().code, c.error().message);
            break;
        }
        idle_.push_back(Slot{.client = std::move(*c), .lastUsed = std::chrono::steady_clock::now()});
        ++created_;
        ++opened;
    }

    // minConnections 是硬要求：预热没凑齐就**不做"假装就绪"**——
    // 清掉半开的连接、保持 not ready、把首个错误原样抛给调用方。
    // 需要"服务器没起也算成功、连接推迟到首次 Acquire"的语义，请把 minConnections 设为 0（纯懒加载，不预热）。
    if (warmupFailed) {
        for (auto& s : idle_) {
            CloseUnlocked(s);
        }
        idle_.clear();
        ready_ = false;
        initError_ = warmupError;
        log::Error("redis pool NOT ready: warmed {}/{} ({}:{}), kind={} msg={}", opened, opt_.minConnections,
                   opt_.connect.host, opt_.connect.port, ToChar(warmupError.kind), warmupError.message);
        return std::unexpected(warmupError);
    }

    ready_ = true;
    initError_ = {};
    log::Info("redis pool ready max={} min={} warmed={} ({}:{})", opt_.maxConnections, opt_.minConnections, opened,
              opt_.connect.host, opt_.connect.port);
    return {};
}

void Pool::Shutdown() noexcept {
    std::unique_lock lock(mutex_);
    for (auto& s : idle_) {
        CloseUnlocked(s);
    }
    idle_.clear();
    ready_ = false;
    initError_ = {};
    cv_.notify_all();
}

bool Pool::ready() const noexcept {
    std::lock_guard lock(mutex_);
    return ready_;
}

PoolStats Pool::stats() const {
    std::lock_guard lock(mutex_);
    return PoolStats{
        .maxConnections = opt_.maxConnections,
        .idle = idle_.size(),
        .leased = leased_,
        .created = created_,
        .closed = closed_,
    };
}

std::expected<Lease, RedisError> Pool::Acquire() {
    const auto deadline = std::chrono::steady_clock::now() + opt_.acquireTimeout;
    std::unique_lock lock(mutex_);

    if (!ready_) {
        // Init 失败过就把真因原样带出去（Connect/Io/Timeout…）；只说 "pool not ready" 没法排查。
        if (initError_.kind != ErrorKind::None) {
            return std::unexpected(initError_);
        }
        return std::unexpected(RedisError{.kind = ErrorKind::Closed, .code = 0, .message = "pool not ready"});
    }

    for (;;) {
        const auto now = std::chrono::steady_clock::now();

        while (!idle_.empty()) {
            Slot slot = std::move(idle_.back());
            idle_.pop_back();
            if (!slot.client) {
                continue;
            }
            if (opt_.idleTtl.count() > 0 && now - slot.lastUsed > opt_.idleTtl) {
                CloseUnlocked(slot);
                continue;
            }
            if (!slot.client->connected()) {
                CloseUnlocked(slot);
                continue;
            }
            Client* raw = slot.client.release();
            ++leased_;
            return Lease(raw, this);
        }

        const std::size_t total = idle_.size() + leased_;
        if (total < opt_.maxConnections) {
            ++leased_;
            lock.unlock();
            auto c = OpenNew();
            lock.lock();
            if (!ready_) {
                --leased_;
                cv_.notify_one();
                return std::unexpected(RedisError{.kind = ErrorKind::Closed, .code = 0, .message = "pool closed"});
            }
            if (!c) {
                --leased_;
                cv_.notify_one();
                if (!idle_.empty()) {
                    continue;
                }
                return std::unexpected(c.error());
            }
            Client* raw = (*c).release();
            ++created_;
            return Lease(raw, this);
        }

        if (now >= deadline) {
            return std::unexpected(RedisError{
                .kind = ErrorKind::Timeout,
                .code = 0,
                .message = fmt::format("pool busy (leased={} max={})", leased_, opt_.maxConnections),
            });
        }
        cv_.wait_until(lock, deadline);
        if (!ready_) {
            return std::unexpected(RedisError{.kind = ErrorKind::Closed, .code = 0, .message = "pool closed"});
        }
    }
}

void Pool::Release(Client* client, bool) noexcept {
    if (!client) {
        return;
    }
    std::unique_ptr<Client> owned(client);
    const bool dead = !owned->connected();
    if (dead) {
        owned->Disconnect();
    }

    std::lock_guard lock(mutex_);
    if (leased_ > 0) {
        --leased_;
    }
    if (!ready_ || dead) {
        ++closed_;
        cv_.notify_one();
        return;
    }
    idle_.push_back(Slot{.client = std::move(owned), .lastUsed = std::chrono::steady_clock::now()});
    cv_.notify_one();
}

} // namespace shine::db::redis
