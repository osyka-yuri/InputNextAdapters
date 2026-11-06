#include "native_observation/native_observation_test_cases.hpp"
#include "native_observation/native_observation_test_support.hpp"

#include "native/content_observation.hpp"
#include "native/observation.hpp"
#include "native/shout.hpp"
#include "skyrim_adapter/model.hpp"

#include <RE/T/TESObjectWEAP.h>
#include <RE/T/TESShout.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace skyrim_adapter::native_observation_tests {

void shout_capture_requires_a_matching_player_voice_event_and_exact_variation() {
    using VariationID = RE::TESShout::VariationID;
    using native::detail::is_player_voice_fire;
    using native::detail::shout_level_for_source;

    constexpr FormId player_id = 0x14;
    constexpr FormId shout_id = 0x100;
    require(is_player_voice_fire(EventKind::voice_fire, player_id, player_id, shout_id),
        "player voice-fire event with a source was not admitted");
    require(!is_player_voice_fire(EventKind::voice_cast, player_id, player_id, shout_id),
        "voice cast was admitted as a released shout");
    require(!is_player_voice_fire(EventKind::voice_fire, player_id + 1, player_id, shout_id),
        "an NPC voice-fire event was admitted as the player's shout");
    require(!is_player_voice_fire(EventKind::voice_fire, 0, player_id, shout_id) &&
                !is_player_voice_fire(EventKind::voice_fire, player_id, 0, shout_id) &&
                !is_player_voice_fire(EventKind::voice_fire, player_id, player_id, 0),
        "missing player identity or source was admitted");

    require(shout_level_for_source(shout_id, shout_id, VariationID::kOne) == ShoutLevel::one_word,
        "one-word shout variation was not mapped exactly");
    require(shout_level_for_source(shout_id, shout_id, VariationID::kTwo) == ShoutLevel::two_words,
        "two-word shout variation was not mapped exactly");
    require(shout_level_for_source(shout_id, shout_id, VariationID::kThree) ==
                ShoutLevel::three_words,
        "three-word shout variation was not mapped exactly");
    require(shout_level_for_source(shout_id, shout_id, VariationID::kNone) == ShoutLevel::unknown &&
                shout_level_for_source(shout_id, shout_id, VariationID::kTotal) ==
                    ShoutLevel::unknown &&
                shout_level_for_source(shout_id, shout_id, static_cast<VariationID>(0xFF)) ==
                    ShoutLevel::unknown,
        "none, sentinel or invalid shout variations were guessed");
    require(shout_level_for_source(0, shout_id, VariationID::kOne) == ShoutLevel::unknown &&
                shout_level_for_source(shout_id, 0, VariationID::kOne) == ShoutLevel::unknown,
        "a missing event source or current shout was assigned a level");
    require(shout_level_for_source(shout_id, shout_id + 1, VariationID::kThree) ==
                ShoutLevel::unknown,
        "a stale current shout was assigned to a different event source");
}

void survival_stage_observation_uses_bounded_native_values() {
    using native::detail::need_stage;
    using native::detail::SurvivalNeed;

    constexpr std::array cold{50.0F, 120.0F, 300.0F, 500.0F, 800.0F};
    constexpr std::array hunger{80.0F, 160.0F, 340.0F, 520.0F, 770.0F};
    constexpr std::array exhaustion{80.0F, 160.0F, 340.0F, 560.0F, 800.0F};
    constexpr std::array<std::array<float, 5>, 3> thresholds{cold, hunger, exhaustion};
    constexpr std::array<SurvivalNeed, 3> needs{SurvivalNeed::cold,
        SurvivalNeed::hunger,
        SurvivalNeed::exhaustion};

    for (std::size_t need_index = 0; need_index < needs.size(); ++need_index) {
        require(need_stage(needs[need_index], 0.0F) == 0U,
            "zero native Survival value maps to stage zero");
        for (std::size_t index = 0; index < thresholds[need_index].size(); ++index) {
            const auto boundary = thresholds[need_index][index];
            require(need_stage(needs[need_index], boundary) == index + 1,
                "exact native Survival threshold maps to its elevated stage");
            require(need_stage(needs[need_index], boundary - 0.01F) == index,
                "native Survival value below a threshold remains in the lower stage");
        }
    }

    require(!need_stage(SurvivalNeed::cold, -1.0F),
        "negative native Survival values remain unavailable");
    require(!need_stage(SurvivalNeed::cold, std::numeric_limits<float>::quiet_NaN()),
        "NaN native Survival values remain unavailable");
    require(!need_stage(SurvivalNeed::cold, std::numeric_limits<float>::infinity()),
        "nonfinite native Survival values remain unavailable");
    require(need_stage(SurvivalNeed::cold, 1000.0F) == 5U,
        "cold maximum maps to final native stage");
    require(!need_stage(SurvivalNeed::cold, 1000.01F),
        "out-of-range cold native values remain unavailable");
    require(need_stage(SurvivalNeed::exhaustion, 960.0F) == 5U,
        "exhaustion maximum maps to final native stage");
    require(!need_stage(SurvivalNeed::exhaustion, 960.01F),
        "out-of-range exhaustion native values remain unavailable");
}

void official_survival_global_bindings_match_installed_metadata() {
    using native::detail::global_binding;
    using native::detail::global_binding_metadata_matches;
    using native::detail::GlobalValueType;
    using native::detail::SurvivalGlobal;

    constexpr std::array globals{
        SurvivalGlobal::mode_enabled,
        SurvivalGlobal::cold_need,
        SurvivalGlobal::hunger_need,
        SurvivalGlobal::exhaustion_need,
    };
    constexpr std::array<std::uint32_t, 4> local_ids{0x826, 0x81B, 0x81A, 0x816};
    constexpr std::array<std::string_view, 4> editor_ids{
        "Survival_ModeEnabled",
        "Survival_ColdNeedValue",
        "Survival_HungerNeedValue",
        "Survival_ExhaustionNeedValue",
    };
    constexpr std::array<GlobalValueType, 4> types{
        GlobalValueType::short_integer,
        GlobalValueType::floating,
        GlobalValueType::floating,
        GlobalValueType::floating,
    };
    constexpr std::string_view plugin{"ccQDRSSE001-SurvivalMode.esl"};

    for (std::size_t index = 0; index < globals.size(); ++index) {
        const auto binding = global_binding(globals[index]);
        require(binding.local_form_id == local_ids[index] && binding.plugin == plugin &&
                    binding.editor_id == editor_ids[index] && binding.value_type == types[index],
            "Survival Mode binding matches local form, plugin, EDID, and value type");
        require(global_binding_metadata_matches(binding,
                    editor_ids[index],
                    static_cast<char>(types[index])),
            "official Survival editor ID and global type are accepted");
        const auto wrong_type = types[index] == GlobalValueType::floating
                                    ? GlobalValueType::short_integer
                                    : GlobalValueType::floating;
        require(!global_binding_metadata_matches(binding,
                    editor_ids[index],
                    static_cast<char>(wrong_type)),
            "a Survival global with the wrong value type is rejected");
        require(!global_binding_metadata_matches(binding,
                    "WrongSurvivalGlobal",
                    static_cast<char>(types[index])),
            "a Survival global with a different EDID is rejected");
    }
}

void weapon_types_are_typed_without_guesses() {
    using RE::WeaponTypes;
    using skyrim_adapter::native::detail::melee_weapon;
    using skyrim_adapter::native::detail::weapon_class;

    require(weapon_class(WeaponTypes::kHandToHandMelee) == WeaponClass::unarmed,
        "hand-to-hand weapon type was not mapped");
    require(weapon_class(WeaponTypes::kOneHandSword) == WeaponClass::sword,
        "sword type was not mapped");
    require(weapon_class(WeaponTypes::kOneHandDagger) == WeaponClass::dagger,
        "dagger type was not mapped");
    require(weapon_class(WeaponTypes::kOneHandAxe) == WeaponClass::axe, "axe type was not mapped");
    require(weapon_class(WeaponTypes::kOneHandMace) == WeaponClass::mace,
        "mace type was not mapped");
    require(weapon_class(WeaponTypes::kTwoHandSword) == WeaponClass::two_handed,
        "two-handed sword type was not mapped");
    require(weapon_class(WeaponTypes::kTwoHandAxe) == WeaponClass::two_handed,
        "two-handed axe type was not mapped");
    require(weapon_class(WeaponTypes::kBow) == WeaponClass::bow, "bow type was not mapped");
    require(weapon_class(WeaponTypes::kCrossbow) == WeaponClass::crossbow,
        "crossbow type was not mapped");
    require(weapon_class(WeaponTypes::kStaff) == WeaponClass::none,
        "staff was reported as a weapon class");
    require(weapon_class(static_cast<RE::WEAPON_TYPE>(0xFF)) == WeaponClass::unknown,
        "unrecognized weapon type was guessed");
    require(melee_weapon(weapon_class(WeaponTypes::kOneHandSword)),
        "sword was not eligible for melee hits");
    require(!melee_weapon(weapon_class(WeaponTypes::kBow)), "bow was eligible for melee hits");
    require(!melee_weapon(weapon_class(WeaponTypes::kStaff)), "staff was eligible for melee hits");
}

} // namespace skyrim_adapter::native_observation_tests
