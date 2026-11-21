#pragma once

#include "effect_model.hpp"
#include "effect_settings.hpp"

#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace oblivion_adapter {

[[nodiscard]] std::uint32_t base_effect_duration_ms(EffectId effect) noexcept;

struct CueOrigin {
    MonoTime at;
    std::uint64_t frame{};
    std::uint64_t sequence{};
    std::uint32_t source_id{};
    std::uint32_t target_id{};
};

struct HeldBodyEffect {
    EffectId effect{EffectId::count};
    MonoTime started_at;
    std::uint32_t source_id{};
};

enum class CuePriority : std::uint8_t { profile, impact };

// Owns finite cue history, profile preparation, and the independent HD and classic mixes.
class BodyEffects final {
public:
    explicit BodyEffects(const EffectSettings& settings) noexcept;

    void reset() noexcept;
    void expire(MonoTime now) noexcept;
    void cancel(EffectId effect) noexcept;
    [[nodiscard]] bool profile_enabled(EffectId effect) const noexcept;
    void play(EffectId effect,
        CueOrigin origin,
        float strength = 1.0F,
        CuePriority priority = CuePriority::profile) noexcept;
    void render(inputnext::adapters::OutputFrame& frame,
        std::span<const HeldBodyEffect> held,
        MonoTime now) const noexcept;

private:
    struct Profile {
        inputnext::adapters::WaveRecipe wave{};
        float held_gain{1.0F};
        float held_second_mix{};
        float hd_scale{};
        float duration_scale{};
        float classic_low{};
        float classic_high{};
        std::uint32_t duration_ms{};
        std::uint32_t first_beat_end_ms{};
        std::uint32_t second_beat_start_ms{};
        int priority{};
        bool enabled{};
    };

    struct Cue {
        EffectId effect{EffectId::count};
        CueOrigin origin{};
        std::uint64_t stable_id{};
        float strength{};
        int priority{};
    };

    struct RankedVoice {
        const Profile* profile{};
        CueOrigin origin{};
        std::uint64_t stable_id{};
        float strength{};
        int priority{};
        bool held{};
    };

    struct RenderState;

    [[nodiscard]] bool cue_elapsed_ms(const Cue& cue,
        bool sustained,
        MonoTime now,
        std::uint32_t& elapsed_ms) const noexcept;
    [[nodiscard]] bool
    held_has_onset(const HeldBodyEffect& hold, const Profile& profile, MonoTime now) const noexcept;
    [[nodiscard]] static Cue held_cue(const HeldBodyEffect& hold, const Profile& profile) noexcept;
    void mix_cue(RenderState& state,
        inputnext::adapters::OutputFrame& frame,
        const Cue& cue,
        bool sustained,
        MonoTime now) const noexcept;
    static void add_hd_voice(RenderState& state,
        const Cue& cue,
        const Profile& profile,
        bool sustained) noexcept;
    void add_classic_voice(RenderState& state,
        inputnext::adapters::OutputFrame& frame,
        const Cue& cue,
        const Profile& profile,
        bool sustained,
        std::uint32_t elapsed_ms) const noexcept;
    void render_voice(inputnext::adapters::OutputFrame& frame,
        const RankedVoice& ranked,
        std::size_t index) const noexcept;

    float master_{};
    std::array<Profile, effect_sections.size()> profiles_{};
    std::array<Cue, effect_inbox_capacity> cues_{};
};

} // namespace oblivion_adapter
