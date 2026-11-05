#pragma once

#include "content_effects.hpp"
#include "model.hpp"
#include "skyrim_adapter/detail/body_cue_history.hpp"
#include "skyrim_adapter/detail/critical_health.hpp"
#include "skyrim_adapter/detail/effect_mixer.hpp"
#include "world_effects.hpp"

#include <array>
#include <inputnext_adapters/led_policy.hpp>
#include <optional>
#include <span>

namespace skyrim_adapter {
namespace detail {
struct ActionRecipe;
} // namespace detail

class Policy {
public:
    Policy() noexcept;
    explicit Policy(const PolicyConfig& config) noexcept;

    void reset() noexcept;
    void reset_for_menu(std::optional<MonoTime> pause_started_at) noexcept;
    void reset_body_history(MonoTime fresh_after) noexcept;
    [[nodiscard]] EffectFrame
    step(const Observation& observation, std::span<const Action> actions, MonoTime now) noexcept;

private:
    struct EventStamp {
        Action action{};
        bool valid{};
    };

    struct DualReleaseStamp {
        FormId source_id{};
        SpellKind spell{SpellKind::unknown};
        Hand hand{Hand::unknown};
        MonoTime observed_at;
        bool valid{};
    };

    struct StepContext {
        const Observation& observation;
        std::span<const Action> actions;
        MonoTime now;
        EffectFrame frame{};
        detail::EffectRequests effects{};
        bool body_observation_fresh{};
        bool interruption{};
        bool phase_baseline_was_set{};
        bool admitted_bow_observation{};
    };

    void reset_effect_state() noexcept;
    void offer_body_cue(detail::BodyCueSlot slot, BodyCue cue) noexcept;
    [[nodiscard]] bool prepare_step(StepContext& context) noexcept;
    [[nodiscard]] bool has_fresh_interruption(const StepContext& context) const noexcept;
    void handle_interruption(StepContext& context) noexcept;
    [[nodiscard]] bool action_seen(const Action& action) const noexcept;
    void add_world_and_content_cues(StepContext& context) noexcept;
    void add_world_effect_cue(StepContext& context) noexcept;
    void add_content_effect_cues(StepContext& context) noexcept;
    void add_bow_phase_cue(StepContext& context) noexcept;
    void process_actions(StepContext& context) noexcept;
    void process_action(StepContext& context, const Action& action) noexcept;
    void fence_bow_release(const Action& action, const Observation& observation) noexcept;
    static void offer_action_triggers(StepContext& context,
        const Action& action,
        const detail::ActionRecipe& recipe) noexcept;
    [[nodiscard]] bool admit_action_body_recipe(const Action& action,
        const detail::ActionRecipe& recipe,
        bool body_action_fresh,
        MonoTime now,
        BodyCue& cue) noexcept;
    [[nodiscard]] bool record_or_coalesce_dual_release(const Action& action) noexcept;
    void process_held_state(StepContext& context) noexcept;
    void update_led(StepContext& context) noexcept;
    void process_held_magic(StepContext& context) noexcept;
    void process_held_magic_hand(StepContext& context, std::size_t index) noexcept;
    void process_critical_health(StepContext& context) noexcept;
    void finish_step(StepContext& context) noexcept;
    [[nodiscard]] detail::RumbleFallback bow_rumble_fallback(
        const StepContext& context) const noexcept;

    PolicyConfig config_{};
    WorldEffects world_effects_{};
    ContentEffects content_effects_{};
    detail::BodyCueHistory body_history_{};
    detail::EffectMixer effect_mixer_{};
    detail::CriticalHealthHeartbeat critical_health_{};
    inputnext::adapters::LedPolicy led_policy_;
    std::array<EventStamp, inbox_capacity> event_history_{};
    std::array<DualReleaseStamp, 8> dual_release_history_{};
    std::size_t next_event_stamp_{};
    std::size_t next_dual_release_stamp_{};
    bool phase_baseline_set_{};
    BowPhase body_bow_phase_{BowPhase::unknown};
    bool body_phase_baseline_set_{};
    MonoTime body_fresh_after_;
    bool body_fresh_after_set_{};
};
} // namespace skyrim_adapter
