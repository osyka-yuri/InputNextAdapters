#pragma once

#include "effect_session.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace oblivion_adapter::native {

class EffectWorker final {
public:
    EffectWorker(const EffectWorker&) = delete;
    EffectWorker& operator=(const EffectWorker&) = delete;
    EffectWorker(EffectWorker&&) = delete;
    EffectWorker& operator=(EffectWorker&&) = delete;

    // A prepared worker can be joined on load failure before it starts output.
    [[nodiscard]] static std::unique_ptr<EffectWorker> prepare(
        const std::filesystem::path& game_dir,
        bool (*game_has_focus)() noexcept = foreground_is_game);
    // Keep the active worker alive for xOBSE callbacks until process exit.
    [[nodiscard]] static EffectWorker& activate_for_process(
        std::unique_ptr<EffectWorker> prepared) noexcept;
    void stop() noexcept;
    void invalidate() noexcept;

    [[nodiscard]] std::uint64_t generation() const noexcept { return session_.generation(); }

    [[nodiscard]] static bool foreground_is_game() noexcept;

    [[nodiscard]] bool stealth_enabled() const noexcept { return stealth_enabled_; }

    void publish(EffectObservation observation) noexcept;
    bool push_event(EffectEvent event) noexcept;

private:
    friend struct std::default_delete<EffectWorker>;

    EffectWorker(const EffectSettings& settings, bool (*game_has_focus)() noexcept) noexcept;
    ~EffectWorker();
    void wake() noexcept;
    void run() noexcept;
    void drain_on_exit(inputnext::adapters::TickClock clock) noexcept;

    inputnext::adapters::RuntimeOutput runtime_;
    EffectSession session_;
    bool (*const game_has_focus_)() noexcept;
    const bool stealth_enabled_;
    std::mutex wake_mutex_;
    std::condition_variable wake_cv_;
    bool work_pending_{};
    bool active_{};
    std::atomic<bool> stopped_;
    std::thread thread_;
};

} // namespace oblivion_adapter::native
