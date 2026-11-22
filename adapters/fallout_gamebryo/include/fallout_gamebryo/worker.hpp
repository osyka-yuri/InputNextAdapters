#pragma once

#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/session.hpp>
#include <fallout_gamebryo/settings.hpp>
#include <inputnext_adapters/output.hpp>
#include <inputnext_adapters/output_session.hpp>

#include <condition_variable>
#include <mutex>
#include <span>
#include <thread>

namespace fallout_gamebryo {
struct WorkerCallbacks {
    bool (*foreground)() noexcept {};
    void (*delivery_changed)(inputnext::adapters::DeliveryDiagnostics) noexcept {};
    void (*cleanup_incomplete)() noexcept {};
};

class EffectWorker final {
public:
    EffectWorker(inputnext::adapters::OutputPort& output,
        const Settings& settings,
        WorkerCallbacks callbacks = {});
    ~EffectWorker();

    EffectWorker(const EffectWorker&) = delete;
    EffectWorker& operator=(const EffectWorker&) = delete;
    EffectWorker(EffectWorker&&) = delete;
    EffectWorker& operator=(EffectWorker&&) = delete;

    [[nodiscard]] bool start() noexcept;
    void publish(const Observation& observation, std::span<const Event> events = {}) noexcept;
    void invalidate(MonoTime now) noexcept;
    void request_stop() noexcept;

private:
    void wake() noexcept;
    void run() noexcept;

    Session session_;
    WorkerCallbacks callbacks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool pending_{};
    bool stopping_{};
    std::thread thread_;
};
} // namespace fallout_gamebryo
