#pragma once

#include "fake_output.hpp"
#include "support/test_runner.hpp"

#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/output_frame.hpp>
#include <inputnext_adapters/output_session.hpp>

#include <input_next_effects.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace inputnext::test::output_session {
using namespace std::chrono_literals;
using namespace inputnext::adapters;

[[noreturn]] inline void fail(const char* message) {
    throw std::runtime_error(message);
}

inline void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

template <typename T>
const T& require_value(const std::optional<T>& opt, const char* message) {
    if (!opt.has_value()) {
        fail(message);
    }
    return *opt;
}

template <typename T>
T& require_value(std::optional<T>& opt, const char* message) {
    if (!opt.has_value()) {
        fail(message);
    }
    return *opt;
}

template <typename T>
const T& require_value(std::optional<T>&&, const char*) = delete;

template <typename T>
const T& require_value(const std::optional<T>&&, const char*) = delete;

template <typename Opt>
concept CanRequireValue = requires(Opt&& opt) { require_value(static_cast<Opt&&>(opt), "test"); };

static_assert(CanRequireValue<std::optional<int>&>);
static_assert(CanRequireValue<const std::optional<int>&>);
static_assert(!CanRequireValue<std::optional<int>>);
static_assert(!CanRequireValue<std::optional<int>&&>);
static_assert(!CanRequireValue<const std::optional<int>&&>);
static_assert(
    std::is_same_v<decltype(require_value(std::declval<std::optional<int>&>(), "test")), int&>);
static_assert(
    std::is_same_v<decltype(require_value(std::declval<const std::optional<int>&>(), "test")),
        const int&>);

inline void advertise_hd(FakeOutput& output, std::uint64_t audio_instance_id) noexcept {
    output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    output.capabilities.audio_instance_id = audio_instance_id;
}

inline void enable_leds(FakeOutput& output) noexcept {
    output.capabilities.supported_channels |= INPUT_NEXT_EFFECTS_LEDS;
    output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_LIGHTBAR;
}

class FakeObserver final : public DeliveryObserver {
public:
    bool ready{true};
    MonoTime fresh_after;
    std::uint32_t lost_count{};
    std::uint32_t binding_count{};
    std::uint32_t body_rebound_count{};
    std::uint32_t expired_count{};
    mutable FrameRestriction restriction{FrameRestriction::none};
    mutable std::uint32_t restriction_queries{};
    mutable std::uint32_t neutral_on_restriction_query{};
    mutable bool restore_after_neutral_read{};
    mutable std::function<void(std::uint32_t)> restriction_checkpoint;

    [[nodiscard]] bool admitted(MonoTime now) const noexcept override {
        return ready && now > fresh_after;
    }

    [[nodiscard]] FrameRestriction frame_restriction(
        [[maybe_unused]] MonoTime now) const noexcept override {
        ++restriction_queries;
        if (neutral_on_restriction_query != 0 &&
            restriction_queries == neutral_on_restriction_query) {
            restriction = FrameRestriction::neutral;
            neutral_on_restriction_query = 0;
        }
        const auto result = restriction;
        if (result == FrameRestriction::neutral && restore_after_neutral_read) {
            restriction = FrameRestriction::none;
            restore_after_neutral_read = false;
        }
        if (restriction_checkpoint) {
            static_cast<void>(inputnext::test::run_noexcept("FakeObserver restriction checkpoint",
                [&] { restriction_checkpoint(restriction_queries); }));
        }
        return result;
    }

    void binding_lost([[maybe_unused]] input_next_status status) noexcept override {
        ++lost_count;
        ready = false;
    }

    void binding_changed(MonoTime started_at) noexcept override {
        ++binding_count;
        fresh_after = started_at;
        ready = false;
    }

    void body_rebound(MonoTime baseline_at) noexcept override {
        ++body_rebound_count;
        fresh_after = baseline_at;
        ready = false;
    }

    void admission_expired() noexcept override {
        ++expired_count;
        ready = false;
    }

    void publish(MonoTime sampled_at) noexcept {
        if (sampled_at > fresh_after) {
            ready = true;
        }
    }
};

inline void
bind_with_fresh_sample(OutputSession& session, FakeObserver& observer, TickClock& clock) {
    require(!session.reconcile(std::nullopt, clock),
        "first target query did not establish a new binding");
    require(session.bound() && observer.binding_count == 1, "new target binding was not reported");
    clock.supplied += 1ms;
    observer.publish(clock.now());
    require(session.reconcile(clock.now(), clock), "fresh post-bind observation was not admitted");
}

inline BodyProgram body_program(MonoTime started_at,
    std::uint64_t stable_id,
    std::chrono::milliseconds duration = 500ms,
    bool held = false) {
    BodyVoice voice{};
    voice.wave.frequency = 82.0F;
    voice.wave.second_frequency = 117.0F;
    voice.wave.second_mix = 0.25F;
    voice.wave.level = 0.7F;
    voice.wave.attack_ms = 3.0F;
    voice.wave.release_ms = 5.0F;
    voice.started_at = started_at;
    voice.duration_ms = static_cast<std::uint32_t>(duration.count());
    voice.stable_id = stable_id;
    voice.sequence = 1;
    voice.source_id = 42;
    voice.priority = 4;
    voice.scale = 0.8F;
    voice.hd_scale = 1.0F;
    voice.duration_scale = 1.0F;
    voice.position_gain = 0.6F;
    voice.channel_mask = 0x3U;
    voice.held = held;

    BodyProgram result{};
    result.voices[4] = voice;
    return result;
}

inline TriggerIntent trigger(std::uint32_t ttl_ms = 100) {
    TriggerIntent result{};
    result.payload.mode = INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK;
    result.payload.start = 0.15F;
    result.payload.end = 0.75F;
    result.payload.strength = 0.65F;
    result.payload.frequency = 74.0F;
    result.ttl_ms = ttl_ms;
    return result;
}

inline LedIntent led(MonoTime source_at, std::uint32_t ttl_ms = 200) {
    LedIntent result{};
    result.payload.lightbar_rgb = UINT32_C(0x00123456);
    result.payload.player_index = 2;
    result.payload.microphone_led = 1;
    result.source_at = source_at;
    result.ttl_ms = ttl_ms;
    return result;
}

inline OutputFrame body_frame(MonoTime started_at,
    std::uint64_t stable_id,
    std::chrono::milliseconds duration = 500ms,
    bool held = false) {
    OutputFrame result{};
    result.body = body_program(started_at, stable_id, duration, held);
    return result;
}

} // namespace inputnext::test::output_session
