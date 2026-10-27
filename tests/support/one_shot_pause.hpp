#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace inputnext::test {

class OneShotPause final {
public:
    void pause_once() {
        std::unique_lock lock{mutex_};
        if (used_) {
            return;
        }
        used_ = true;
        entered_ = true;
        changed_.notify_all();
        changed_.wait(lock, [this] { return released_; });
    }

    [[nodiscard]] bool wait_until_entered(std::chrono::milliseconds timeout) {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, timeout, [this] { return entered_; });
    }

    void release() {
        {
            std::scoped_lock lock{mutex_};
            released_ = true;
        }
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool used_{};
    bool entered_{};
    bool released_{};
};

} // namespace inputnext::test
