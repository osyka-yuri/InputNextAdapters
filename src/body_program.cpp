#include <inputnext_adapters/body_program.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <tuple>

namespace inputnext::adapters {
namespace {
auto voice_identity_key(const BodyVoice& voice) noexcept {
    return std::tie(voice.wave,
        voice.started_at,
        voice.stable_id,
        voice.sequence,
        voice.source_id,
        voice.priority,
        voice.scale,
        voice.hd_scale,
        voice.duration_scale,
        voice.position_gain,
        voice.channel_mask,
        voice.held);
}

bool same_voice_identity(const BodyVoice& left, const BodyVoice& right) noexcept {
    if (voice_identity_key(left) != voice_identity_key(right)) {
        return false;
    }

    // Held voices can renew their duration without changing identity.
    if (left.held) {
        return true;
    }

    return left.duration_ms == right.duration_ms;
}

bool voice_active(const BodyVoice& voice, MonoTime now) noexcept {
    return voice.started_at <= now &&
           now - voice.started_at < std::chrono::milliseconds{voice.duration_ms};
}

} // namespace

bool same_body_program_identity(const BodyProgram& left, const BodyProgram& right) noexcept {
    for (std::size_t index = 0; index < left.voices.size(); ++index) {
        const auto& left_voice = left.voices[index];
        const auto& right_voice = right.voices[index];
        if (left_voice.has_value() != right_voice.has_value() ||
            (left_voice && !same_voice_identity(*left_voice, *right_voice))) {
            return false;
        }
    }
    return true;
}

bool body_program_active(const BodyProgram& program, MonoTime now) noexcept {
    return std::ranges::any_of(program.voices,
        [now](const auto& voice) noexcept { return voice && voice_active(*voice, now); });
}

bool body_program_has_held(const BodyProgram& program, MonoTime now) noexcept {
    return std::ranges::any_of(program.voices, [now](const auto& voice) noexcept {
        return voice && voice->held && voice_active(*voice, now);
    });
}

} // namespace inputnext::adapters
