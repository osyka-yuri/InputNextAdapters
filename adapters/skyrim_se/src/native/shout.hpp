#pragma once

#include "skyrim_adapter/model.hpp"

#include <RE/T/TESShout.h>

namespace skyrim_adapter::native::detail {

[[nodiscard]] constexpr bool
is_player_voice_fire(EventKind kind, FormId actor_id, FormId player_id, FormId source_id) noexcept {
    return kind == EventKind::voice_fire && actor_id != 0 && player_id != 0 &&
           actor_id == player_id && source_id != 0;
}

[[nodiscard]] constexpr ShoutLevel shout_level_for_source(FormId event_source_id,
    FormId current_shout_id,
    RE::TESShout::VariationID variation) noexcept {
    if (event_source_id == 0 || current_shout_id == 0 || event_source_id != current_shout_id) {
        return ShoutLevel::unknown;
    }

    switch (variation) {
    case RE::TESShout::VariationID::kOne:
        return ShoutLevel::one_word;
    case RE::TESShout::VariationID::kTwo:
        return ShoutLevel::two_words;
    case RE::TESShout::VariationID::kThree:
        return ShoutLevel::three_words;
    case RE::TESShout::VariationID::kNone:
    case RE::TESShout::VariationID::kTotal:
        return ShoutLevel::unknown;
    }
    return ShoutLevel::unknown;
}

} // namespace skyrim_adapter::native::detail
