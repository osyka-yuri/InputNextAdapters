#include "native_observation/native_observation_test_cases.hpp"
#include "native_observation/native_observation_test_support.hpp"

#include "native/observation.hpp"
#include "native/spell_keywords.hpp"
#include "skyrim_adapter/model.hpp"

#include <RE/A/ActorValues.h>
#include <RE/M/MagicCaster.h>
#include <RE/M/MagicSystem.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

namespace skyrim_adapter::native_observation_tests {

namespace {

struct FakeSpellKeyword {
    struct EditorId {
        std::string_view value;

        [[nodiscard]] constexpr std::size_t length() const noexcept { return value.length(); }

        [[nodiscard]] bool operator==(std::string_view expected) const noexcept {
            return std::ranges::equal(value, expected, [](char left, char right) {
                return std::tolower(static_cast<unsigned char>(left)) ==
                       std::tolower(static_cast<unsigned char>(right));
            });
        }
    } formEditorID;
};

static_assert(!std::is_convertible_v<FakeSpellKeyword::EditorId, std::string_view>);

template <std::size_t N>
[[nodiscard]] bool reference_has_keyword(const std::array<const FakeSpellKeyword*, N>& keywords,
    std::string_view editor_id) {
    return std::ranges::any_of(keywords,
        [editor_id](const auto* keyword) { return keyword && keyword->formEditorID == editor_id; });
}

template <std::size_t N>
[[nodiscard]] native::detail::SpellKeywordFlags reference_spell_keywords(
    const std::array<const FakeSpellKeyword*, N>& keywords) {
    return {
        .ward = reference_has_keyword(keywords, "MagicWard"),
        .fire = reference_has_keyword(keywords, "MagicDamageFire"),
        .frost = reference_has_keyword(keywords, "MagicDamageFrost"),
        .shock = reference_has_keyword(keywords, "MagicDamageShock"),
        .restore_health = reference_has_keyword(keywords, "MagicRestoreHealth"),
    };
}

} // namespace

void caster_modes_and_phases_are_exact() {
    using RE::MagicCaster;
    using skyrim_adapter::native::detail::cast_mode;
    using skyrim_adapter::native::detail::cast_phase;

    require(cast_phase(MagicCaster::State::kReady) == CastPhase::ready,
        "ready caster state was not mapped");
    require(cast_phase(MagicCaster::State::kCharging) == CastPhase::charging,
        "charging caster state was not mapped");
    require(cast_phase(MagicCaster::State::kCasting) == CastPhase::casting,
        "casting caster state was not mapped");
    require(cast_phase(MagicCaster::State::kUnk08) == CastPhase::unknown,
        "unverified caster state was guessed");
    require(cast_mode(RE::MagicSystem::CastingType::kFireAndForget) == CastMode::fire_and_forget,
        "fire-and-forget mode was not mapped");
    require(cast_mode(RE::MagicSystem::CastingType::kConcentration) == CastMode::concentration,
        "concentration mode was not mapped");
    require(cast_mode(RE::MagicSystem::CastingType::kScroll) == CastMode::unknown,
        "unverified scroll mode was guessed");
    require(cast_mode(RE::MagicSystem::CastingType::kScroll, true) == CastMode::fire_and_forget,
        "verified scroll mode was not normalized to one-shot");
    require(cast_mode(RE::MagicSystem::CastingType::kConstantEffect) == CastMode::unknown,
        "constant-effect mode was guessed");
}

void spell_classification_requires_matching_evidence() {
    using RE::ActorValue;
    using skyrim_adapter::native::detail::spell_kind;
    using skyrim_adapter::native::detail::SpellEvidence;

    SpellEvidence evidence{};
    evidence.resist_av = ActorValue::kResistFire;
    require(spell_kind(evidence) == SpellKind::fire,
        "fire resistance evidence did not identify fire");

    evidence = {};
    evidence.frost_keyword = true;
    require(spell_kind(evidence) == SpellKind::frost, "frost keyword did not identify frost");

    evidence = {};
    evidence.shock_keyword = true;
    require(spell_kind(evidence) == SpellKind::shock, "shock keyword did not identify shock");

    evidence = {};
    evidence.ward_keyword = true;
    require(spell_kind(evidence) == SpellKind::ward, "ward keyword did not identify a ward");

    evidence = {};
    evidence.primary_av = ActorValue::kHealth;
    require(spell_kind(evidence) == SpellKind::generic,
        "health damage without recovery evidence was mislabeled as healing");
    evidence.recovers = true;
    require(spell_kind(evidence) == SpellKind::healing,
        "health recovery was not identified as healing");

    evidence = {};
    require(spell_kind(evidence) == SpellKind::generic,
        "unknown magic did not remain neutral generic");
    evidence.fire_keyword = evidence.shock_keyword = true;
    require(spell_kind(evidence) == SpellKind::generic,
        "conflicting elements were falsely labeled");
}

void spell_keyword_scan_matches_reference_lookup() {
    using native::detail::scan_spell_keywords;

    constexpr FakeSpellKeyword ward{"MagicWard"};
    constexpr FakeSpellKeyword fire{"MagicDamageFire"};
    constexpr FakeSpellKeyword frost{"MagicDamageFrost"};
    constexpr FakeSpellKeyword shock{"MagicDamageShock"};
    constexpr FakeSpellKeyword healing{"MagicRestoreHealth"};
    constexpr FakeSpellKeyword wrong_case{"magicdamagefire"};
    constexpr FakeSpellKeyword mixed_case_fire{"mAgIcDaMaGeFiRe"};
    constexpr FakeSpellKeyword lookalike{"MagicDamageFireExtra"};
    constexpr FakeSpellKeyword unrelated{"OtherKeyword"};
    const auto equivalent = [](const auto& keywords) {
        const auto actual = scan_spell_keywords(keywords);
        const auto expected = reference_spell_keywords(keywords);
        require(actual.ward == expected.ward && actual.fire == expected.fire &&
                    actual.frost == expected.frost && actual.shock == expected.shock &&
                    actual.restore_health == expected.restore_health,
            "single-pass spell EditorID flags differ from reference keyword lookups");
    };

    const std::array<const FakeSpellKeyword*, 11> all_keywords{
        &unrelated,
        nullptr,
        &healing,
        &wrong_case,
        &fire,
        &lookalike,
        &ward,
        &fire,
        &frost,
        &shock,
        nullptr,
    };
    equivalent(all_keywords);
    const auto flags = scan_spell_keywords(all_keywords);
    require(flags.ward && flags.fire && flags.frost && flags.shock && flags.restore_health,
        "recognized spell EditorIDs, including duplicates, were not collected");
    const std::array mixed_case_keywords{&mixed_case_fire};
    const auto mixed_case_flags = scan_spell_keywords(mixed_case_keywords);
    const auto mixed_case_reference = reference_spell_keywords(mixed_case_keywords);
    require(mixed_case_flags.fire && mixed_case_reference.fire,
        "native EditorID comparison must recognize an equal-length case variant");
    const std::array wrong_length_keywords{&lookalike};
    const auto wrong_length_flags = scan_spell_keywords(wrong_length_keywords);
    require(!wrong_length_flags.fire && !reference_spell_keywords(wrong_length_keywords).fire,
        "native EditorID comparison must reject a wrong-length prefix lookalike");
    const std::array<const FakeSpellKeyword*, 0> empty{};
    equivalent(empty);

    FakeSpellKeyword mutable_keyword{"MagicDamageFire"};
    const std::array<const FakeSpellKeyword*, 1> mutable_keywords{&mutable_keyword};
    equivalent(mutable_keywords);
    require(scan_spell_keywords(mutable_keywords).fire,
        "the initial fresh scan missed an in-place keyword");
    mutable_keyword.formEditorID.value = "MagicDamageFrost";
    equivalent(mutable_keywords);
    require(!scan_spell_keywords(mutable_keywords).fire &&
                scan_spell_keywords(mutable_keywords).frost,
        "a subsequent scan did not observe the same keyword object after mutation");

    const std::array base_keywords{&ward, &shock, &healing};
    const auto base = scan_spell_keywords(base_keywords);
    const auto base_reference = reference_spell_keywords(base_keywords);
    require(base.ward == base_reference.ward && base.shock == base_reference.shock &&
                base.restore_health == base_reference.restore_health,
        "base-effect spell keywords differ from the reference keyword lookup");

    const auto spell_kind_for = [](const auto& item_keywords, const auto& effect_keywords) {
        const auto item = scan_spell_keywords(item_keywords);
        const auto effect = scan_spell_keywords(effect_keywords);
        const auto reference_item = reference_spell_keywords(item_keywords);
        const auto reference_effect = reference_spell_keywords(effect_keywords);
        native::detail::SpellEvidence actual{};
        actual.ward_keyword = item.ward || effect.ward;
        actual.fire_keyword = item.fire || effect.fire;
        actual.frost_keyword = item.frost || effect.frost;
        actual.shock_keyword = item.shock || effect.shock;
        actual.restore_health = item.restore_health || effect.restore_health;
        auto expected = actual;
        expected.ward_keyword = reference_item.ward || reference_effect.ward;
        expected.fire_keyword = reference_item.fire || reference_effect.fire;
        expected.frost_keyword = reference_item.frost || reference_effect.frost;
        expected.shock_keyword = reference_item.shock || reference_effect.shock;
        expected.restore_health = reference_item.restore_health || reference_effect.restore_health;
        return std::pair{native::detail::spell_kind(actual), native::detail::spell_kind(expected)};
    };
    const std::array conflicting_item{&fire, &shock};
    const std::array ward_effect{&ward};
    const auto [actual_kind, expected_kind] = spell_kind_for(conflicting_item, ward_effect);
    require(actual_kind == expected_kind,
        "item/base-effect keyword union changed spell priority behavior");
    const std::array healing_item{&healing};
    const std::array<const FakeSpellKeyword*, 0> no_effect{};
    const auto [healing_kind, expected_healing_kind] = spell_kind_for(healing_item, no_effect);
    require(healing_kind == expected_healing_kind,
        "spell keyword priority changed the healing fallback");
}

void caster_metadata_must_match_equipped_source() {
    using skyrim_adapter::native::detail::caster_matches_equipped;

    require(caster_matches_equipped(EquipmentKind::spell, 20, 20),
        "current spell matching equipped spell was rejected");
    require(caster_matches_equipped(EquipmentKind::scroll, 21, 21),
        "current spell matching equipped scroll was rejected");
    require(!caster_matches_equipped(EquipmentKind::spell, 20, 22),
        "stale current spell leaked into a different equipped spell");
    require(!caster_matches_equipped(EquipmentKind::weapon, 30, 22),
        "stale current spell leaked into a melee weapon");
    require(caster_matches_equipped(EquipmentKind::staff, 31, 32, 32),
        "matching staff enchantment metadata was rejected");
    require(!caster_matches_equipped(EquipmentKind::staff, 31, 33, 32),
        "unrelated caster metadata was accepted for a staff");
    require(!caster_matches_equipped(EquipmentKind::staff, 31, 32),
        "staff caster metadata without an enchantment match was accepted");
}

void dual_cast_concentration_requires_flag_and_matching_spells() {
    Observation observation{};
    observation.weapon_drawn = true;
    observation.dual_cast = true;
    observation.hands[0] = {
        .equipped_id = 40,
        .caster_source_id = 40,
        .equipment = EquipmentKind::spell,
        .weapon = WeaponClass::none,
        .spell = SpellKind::fire,
        .cast_mode = CastMode::concentration,
        .cast_phase = CastPhase::casting,
        .two_handed = false,
    };
    observation.hands[1] = observation.hands[0];
    require(confirmed_dual_spell(observation, CastMode::concentration),
        "native dual flag and matching active spell hands confirm concentration");

    observation.dual_cast = false;
    require(!confirmed_dual_spell(observation, CastMode::concentration),
        "matching independent spells do not imply native dual casting");
    observation.dual_cast = true;
    observation.hands[1].spell = SpellKind::frost;
    require(!confirmed_dual_spell(observation, CastMode::concentration),
        "different spells do not confirm a coherent dual cast");
    observation.hands[1] = observation.hands[0];

    observation.hands[0].equipment = EquipmentKind::staff;
    require(!confirmed_dual_spell(observation, CastMode::concentration),
        "staff casting remains outside typed dual-spell evidence");
    observation.hands[0].equipment = EquipmentKind::scroll;
    require(!confirmed_dual_spell(observation, CastMode::concentration),
        "scroll casting remains outside typed dual-spell evidence");
    observation.hands[0] = observation.hands[1];

    observation.hands[0].caster_source_id = 41;
    require(!confirmed_dual_spell(observation, CastMode::concentration),
        "caster source must exactly match the equipped spell");
    observation.hands[0] = observation.hands[1];
    observation.hands[0].spell = SpellKind::unknown;
    require(!confirmed_dual_spell(observation, CastMode::concentration),
        "unknown spell kind cannot confirm a coherent dual cast");
    observation.hands[0] = observation.hands[1];
    observation.hands[0].cast_phase = CastPhase::ready;
    require(!confirmed_dual_spell(observation, CastMode::concentration),
        "ready caster phase does not confirm an active concentration");
    observation.hands[0].cast_phase = CastPhase::unknown;
    require(!confirmed_dual_spell(observation, CastMode::concentration),
        "unknown caster phase does not confirm an active concentration");
    observation.hands[0] = observation.hands[1];
    observation.weapon_drawn = false;
    require(!confirmed_dual_spell(observation, CastMode::concentration),
        "sheathed state cannot confirm a dual concentration cue");
}

void dual_cast_fire_and_forget_release_requires_active_hands() {
    Observation observation{};
    observation.weapon_drawn = true;
    observation.dual_cast = true;
    observation.hands[0] = {
        .equipped_id = 40,
        .caster_source_id = 40,
        .equipment = EquipmentKind::spell,
        .weapon = WeaponClass::none,
        .spell = SpellKind::fire,
        .cast_mode = CastMode::fire_and_forget,
        .cast_phase = CastPhase::charging,
        .two_handed = false,
    };
    observation.hands[1] = observation.hands[0];
    observation.hands[1].cast_phase = CastPhase::casting;
    require(confirmed_dual_spell(observation, CastMode::fire_and_forget, 40, SpellKind::fire),
        "matching charging or casting hands confirm a native fire-and-forget release");
    require(!confirmed_dual_spell(observation, CastMode::fire_and_forget, 41, SpellKind::fire) &&
                !confirmed_dual_spell(observation, CastMode::fire_and_forget, 40, SpellKind::frost),
        "actual release attribution must match the event source and known spell kind");
    observation.hands[0].cast_phase = CastPhase::ready;
    require(!confirmed_dual_spell(observation, CastMode::fire_and_forget),
        "ready hand phase cannot authorize bilateral release attribution");
    observation.hands[0].cast_phase = CastPhase::charging;
    observation.hands[1].cast_phase = CastPhase::none;
    require(!confirmed_dual_spell(observation, CastMode::fire_and_forget),
        "inactive hand phase cannot authorize bilateral release attribution");
    observation.hands[1].cast_phase = CastPhase::unknown;
    require(!confirmed_dual_spell(observation, CastMode::fire_and_forget),
        "unknown hand phase cannot authorize bilateral release attribution");
}

void staff_actions_use_only_the_exact_source_enchantment() {
    using skyrim_adapter::native::detail::staff_action_matches_source;

    require(staff_action_matches_source(31, 31, 32),
        "exact staff source and enchantment were rejected");
    require(!staff_action_matches_source(31, 33, 32),
        "a different staff was accepted as the event source");
    require(!staff_action_matches_source(31, 31, 0), "a staff without an enchantment was accepted");
    require(!staff_action_matches_source(0, 0, 32), "an absent event source was accepted");
}

} // namespace skyrim_adapter::native_observation_tests
