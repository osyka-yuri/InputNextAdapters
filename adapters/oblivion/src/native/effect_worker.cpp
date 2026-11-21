#include "effect_worker.hpp"

#include "audio_haptics.hpp"
#include "config.hpp"
#include "effect_model.hpp"
#include "effect_settings.hpp"
#include "inputnext_adapters/output_session.hpp"
#include "native/diagnostics.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

namespace oblivion_adapter::native {
namespace {

constexpr auto watchdog = std::chrono::milliseconds{20};
constexpr auto cleanup_retry_window = std::chrono::milliseconds{500};

template <std::size_t BufferSize, typename... Args>
void write_formatted(std::array<char, BufferSize>& buffer,
    const char* format,
    Args... args) noexcept {
    const int length = std::snprintf(buffer.data(), buffer.size(), format, args...);
    if (length < 0 || static_cast<std::size_t>(length) >= buffer.size()) {
        diagnostics::write("failed to format native diagnostic");
        return;
    }
    diagnostics::write(buffer.data());
}

void log_config_error(const ConfigError& error) noexcept {
    std::array<char, 256> message{};
    const auto size = static_cast<int>(std::min(error.message.size(), std::size_t{160}));
    write_formatted(message,
        "InputNextOblivion.ini line %zu: %.*s",
        error.line,
        size,
        size > 0 ? error.message.data() : "");
}

} // namespace

EffectWorker::EffectWorker(const EffectSettings& settings,
    bool (*game_has_focus)() noexcept) noexcept
    : session_(runtime_, settings),
      game_has_focus_(game_has_focus),
      stealth_enabled_(settings.led.enabled && settings.led.stealth_enabled) {
}

std::unique_ptr<EffectWorker> EffectWorker::prepare(const std::filesystem::path& game_dir,
    bool pause_api_available,
    bool (*game_has_focus)() noexcept) {
    if (!game_dir.is_absolute()) {
        return {};
    }
    const auto config_path = game_dir / L"Data" / L"OBSE" / L"Plugins" / L"InputNextOblivion.ini";
    EffectSettings settings{};
    ConfigError error{};
    switch (read_config_file(config_path, settings, &error)) {
    case ConfigStatus::invalid:
        log_config_error(error);
        return {};
    case ConfigStatus::io_error:
        diagnostics::write("cannot read InputNextOblivion.ini");
        return {};
    case ConfigStatus::loaded:
    case ConfigStatus::missing:
        break;
    }

    const auto audio = load_audio_haptics(config_path, settings);
    if (settings.audio_haptics_enabled) {
        std::array<char, 160> message{};
        write_formatted(message,
            "audio profiles=%zu bank status=%u",
            audio.profiles,
            static_cast<unsigned>(audio.status));
    }

    auto worker = std::unique_ptr<EffectWorker>{new EffectWorker{settings, game_has_focus}};
    const auto runtime_path = game_dir / L"inputnext_runtime.dll";
    const auto result = worker->runtime_.start(runtime_path);
    if (!result) {
        std::array<char, 160> message{};
        write_formatted(message,
            "InputNext runtime ABI bind failed (%u, Windows %lu)",
            static_cast<unsigned>(result.error),
            static_cast<unsigned long>(result.native_error));
        return {};
    }
    if (settings.pause_on_options) {
        if (!pause_api_available) {
            diagnostics::write("Options shortcut unavailable: missing xOBSE console API");
        } else if (!worker->options_.bind(runtime_path, settings.controller_slot)) {
            diagnostics::write("Options shortcut unavailable: missing InputNext Core API");
        }
    }
    worker->thread_ = std::thread([instance = worker.get()] { instance->run(); });
    return worker;
}

EffectWorker::~EffectWorker() {
    stop();
    if (thread_.joinable()) {
        thread_.join();
    }
}

EffectWorker& EffectWorker::activate_for_process(std::unique_ptr<EffectWorker> prepared) noexcept {
    auto& worker = *prepared.release();
    {
        const std::scoped_lock lock(worker.wake_mutex_);
        worker.active_ = true;
    }
    worker.wake_cv_.notify_one();
    return worker;
}

void EffectWorker::wake() noexcept {
    {
        const std::scoped_lock lock(wake_mutex_);
        work_pending_ = true;
    }
    wake_cv_.notify_one();
}

void EffectWorker::stop() noexcept {
    if (!stopped_.exchange(true)) {
        session_.stop(std::chrono::steady_clock::now());
        wake();
    }
}

void EffectWorker::publish(EffectObservation observation) noexcept {
    session_.publish(observation);
    wake();
}

bool EffectWorker::push_event(EffectEvent event) noexcept {
    // TaskHandler publishes the frame's snapshot after recording its callbacks.
    return session_.push_event(event);
}

void EffectWorker::invalidate() noexcept {
    session_.invalidate(std::chrono::steady_clock::now());
    wake();
}

void EffectWorker::run() noexcept {
    {
        std::unique_lock lock(wake_mutex_);
        wake_cv_.wait(lock, [this] { return active_ || stopped_.load(); });
        if (!active_) {
            return;
        }
    }
    using inputnext::adapters::TickClock;
    TickClock clock{.supplied = {}, .use_steady_clock = true};
    auto last = session_.diagnostics();
    while (!stopped_.load()) {
        const bool focused = game_has_focus_();
        session_.set_focus(focused, clock.now());
        options_.poll(focused, clock.now());
        session_.tick(clock);

        const auto current = session_.diagnostics();
        if (current.body_owner_present &&
            (!last.body_owner_present || last.accepted_body_route != current.accepted_body_route ||
                last.body_fallback_reason != current.body_fallback_reason)) {
            std::array<char, 128> message{};
            write_formatted(message,
                "body route=%u fallback=%u",
                current.accepted_body_route,
                current.body_fallback_reason);
        }
        last = current;

        std::unique_lock lock(wake_mutex_);
        wake_cv_.wait_for(lock, watchdog, [this] { return work_pending_ || stopped_.load(); });
        work_pending_ = false;
    }

    drain_on_exit(clock);
    diagnostics::shutdown();
}

void EffectWorker::drain_on_exit(inputnext::adapters::TickClock clock) noexcept {
    // Check the deadline between drain passes. The session retains pending closes
    // and acknowledgements until process exit.
    const auto deadline = clock.now() + cleanup_retry_window;
    do {
        if (session_.drain(clock)) {
            return;
        }
        const auto next_retry = clock.now() + watchdog;
        if (next_retry >= deadline) {
            break;
        }
        std::this_thread::sleep_until(next_retry);
    } while (clock.now() < deadline);
    diagnostics::write("output cleanup unconfirmed at game exit after 500 ms retry window");
}

} // namespace oblivion_adapter::native
