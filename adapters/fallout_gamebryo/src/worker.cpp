#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/settings.hpp>
#include <fallout_gamebryo/worker.hpp>
#include <inputnext_adapters/output.hpp>
#include <inputnext_adapters/output_session.hpp>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <span>
#include <thread>

namespace fallout_gamebryo {
namespace {
using inputnext::adapters::DeliveryDiagnostics;
using inputnext::adapters::TickClock;
using std::chrono::steady_clock;

constexpr auto watchdog = std::chrono::milliseconds{20};
constexpr auto cleanup_window = std::chrono::milliseconds{500};

bool same_delivery(const DeliveryDiagnostics& left, const DeliveryDiagnostics& right) noexcept {
    return left.last_status == right.last_status &&
           left.body_owner_present == right.body_owner_present &&
           left.accepted_body_route == right.accepted_body_route &&
           left.body_fallback_reason == right.body_fallback_reason;
}
} // namespace

EffectWorker::EffectWorker(inputnext::adapters::OutputPort& output,
    const Settings& settings,
    WorkerCallbacks callbacks)
    : session_(output, settings),
      callbacks_(callbacks) {
}

EffectWorker::~EffectWorker() {
    request_stop();
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool EffectWorker::start() noexcept {
    const std::scoped_lock lock(mutex_);
    if (stopping_) {
        return false;
    }
    if (thread_.joinable()) {
        return true;
    }
    try {
        thread_ = std::thread(&EffectWorker::run, this);
        return true;
    } catch (...) {
        return false;
    }
}

void EffectWorker::publish(const Observation& observation, std::span<const Event> events) noexcept {
    session_.publish(observation, events);
    wake();
}

void EffectWorker::invalidate(MonoTime now) noexcept {
    session_.invalidate(now);
    wake();
}

void EffectWorker::request_stop() noexcept {
    {
        const std::scoped_lock lock(mutex_);
        if (stopping_) {
            return;
        }
        session_.stop(steady_clock::now());
        stopping_ = true;
    }
    cv_.notify_one();
}

void EffectWorker::wake() noexcept {
    {
        const std::scoped_lock lock(mutex_);
        pending_ = true;
    }
    cv_.notify_one();
}

void EffectWorker::run() noexcept {
    const TickClock clock{.supplied = {}, .use_steady_clock = true};
    DeliveryDiagnostics previous{};
    try {
        std::unique_lock lock(mutex_);
        while (!stopping_) {
            pending_ = false;
            lock.unlock();
            if (callbacks_.foreground != nullptr) {
                session_.set_foreground(callbacks_.foreground(), clock.now());
            }
            session_.tick(clock);
            const auto current = session_.diagnostics();
            if (callbacks_.delivery_changed != nullptr && !same_delivery(previous, current)) {
                callbacks_.delivery_changed(current);
            }
            previous = current;
            lock.lock();
            cv_.wait_for(lock, watchdog, [this] { return pending_ || stopping_; });
        }
    } catch (...) {
        session_.stop(clock.now());
    }

    const auto deadline = clock.now() + cleanup_window;
    do {
        if (session_.drain(clock)) {
            return;
        }
        std::this_thread::sleep_until(std::min(clock.now() + watchdog, deadline));
    } while (clock.now() < deadline);
    if (callbacks_.cleanup_incomplete != nullptr) {
        callbacks_.cleanup_incomplete();
    }
}
} // namespace fallout_gamebryo
