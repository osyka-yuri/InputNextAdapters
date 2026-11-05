#pragma once

#include "session/support/session_test_support.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace skyrim_adapter::tests::session::interleaving_support {
using namespace std::chrono_literals;

class BlockingCall {
public:
    void pause_first() {
        std::unique_lock lock{gate_};
        if (used_) {
            return;
        }
        used_ = true;
        entered_ = true;
        changed_.notify_all();
        changed_.wait(lock, [&] { return released_; });
    }

    [[nodiscard]] bool wait_until_entered() {
        std::unique_lock lock{gate_};
        return changed_.wait_for(lock, 3s, [&] { return entered_; });
    }

    [[nodiscard]] bool wait_until(MonoTime deadline) {
        std::unique_lock lock{gate_};
        return changed_.wait_until(lock, deadline, [&] { return released_; });
    }

    void release() {
        {
            std::scoped_lock lock{gate_};
            released_ = true;
        }
        changed_.notify_all();
    }

private:
    std::mutex gate_;
    std::condition_variable changed_;
    bool used_{};
    bool entered_{};
    bool released_{};
};

class Completion {
public:
    void signal() {
        {
            std::scoped_lock lock{gate_};
            complete_ = true;
        }
        changed_.notify_all();
    }

    [[nodiscard]] bool wait_for(std::chrono::milliseconds timeout) {
        std::unique_lock lock{gate_};
        return changed_.wait_for(lock, timeout, [&] { return complete_; });
    }

private:
    std::mutex gate_;
    std::condition_variable changed_;
    bool complete_{};
};

struct InterleavingGuard {
    BlockingCall* blocking{nullptr};
    std::thread* t1{nullptr};
    std::thread* t2{nullptr};

    explicit InterleavingGuard(BlockingCall* blocking_call = nullptr,
        std::thread* thread1 = nullptr,
        std::thread* thread2 = nullptr) noexcept
        : blocking(blocking_call),
          t1(thread1),
          t2(thread2) {}

    InterleavingGuard(const InterleavingGuard&) = delete;
    InterleavingGuard& operator=(const InterleavingGuard&) = delete;
    InterleavingGuard(InterleavingGuard&&) = delete;
    InterleavingGuard& operator=(InterleavingGuard&&) = delete;

    ~InterleavingGuard() {
        if (blocking) {
            blocking->release();
        }
        if (t1 && t1->joinable()) {
            t1->join();
        }
        if (t2 && t2->joinable()) {
            t2->join();
        }
    }
};

inline Action voice_action(MonoTime observed_at) {
    Action action{};
    action.event = raw_event(EventKind::voice_fire, 0x14, observed_at);
    return action;
}
} // namespace skyrim_adapter::tests::session::interleaving_support
