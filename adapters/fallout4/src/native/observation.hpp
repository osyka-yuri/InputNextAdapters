#pragma once

#include <fallout4_adapter/model.hpp>
#include <inputnext_adapters/led_policy.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace fallout4_adapter::native {
inline constexpr std::uint8_t no_context_priority = 0;
inline constexpr std::size_t max_context_entries = 64;
inline constexpr std::size_t max_menu_entries = 64;
inline constexpr std::size_t max_mapped_entries = 256;
inline constexpr std::uint32_t invalid_mapped_key = 0xFFFFFFFFU;

// CommonLib's signed four-bit gunState sign-extends state 8 to -8 on read.
[[nodiscard]] constexpr std::uint32_t gun_state_bits(std::int32_t stored_value) noexcept {
    return static_cast<std::uint32_t>(stored_value) & 0x0FU;
}

[[nodiscard]] constexpr bool ui_generation_matches(std::uint64_t expected,
    std::uint64_t actual) noexcept {
    return expected != 0 && expected == actual;
}

[[nodiscard]] constexpr bool ui_provenance_matches(std::uint64_t expected_epoch,
    std::uint64_t actual_epoch,
    std::uint64_t expected_generation,
    std::uint64_t actual_generation,
    std::uint64_t current_generation) noexcept {
    return expected_epoch != 0 && expected_epoch == actual_epoch &&
           ui_generation_matches(expected_generation, actual_generation) &&
           ui_generation_matches(expected_generation, current_generation);
}

[[nodiscard]] inline std::optional<float> actor_value_maximum(float current,
    float damage) noexcept {
    if (!std::isfinite(current) || !std::isfinite(damage)) {
        return std::nullopt;
    }

    const auto maximum = current - damage;
    if (!std::isfinite(maximum) || maximum <= 0.0F) {
        return std::nullopt;
    }
    return maximum;
}

[[nodiscard]] inline std::optional<float> normalized_actor_value(float current,
    float damage) noexcept {
    const auto maximum = actor_value_maximum(current, damage);
    if (!maximum) {
        return std::nullopt;
    }
    return std::clamp(std::max(current, 0.0F) / *maximum, 0.0F, 1.0F);
}

[[nodiscard]] inline std::optional<inputnext::adapters::LedHealthSample>
absolute_health_sample(float current, float damage) noexcept {
    const auto maximum = actor_value_maximum(current, damage);
    if (!maximum) {
        return std::nullopt;
    }
    return inputnext::adapters::LedHealthSample{.current = current, .maximum = *maximum};
}

struct ContextFacts {
    bool available{};
    bool alive{};
    bool focused{};
    bool blocking_menu{};
    bool selection_context{};
    bool playback_context{};
    bool native_playback{};
    bool context_stack_valid{};
    std::uint8_t selection_priority{};
    std::uint8_t playback_priority{};
};

struct UiMenuScalar {
    std::uint32_t flags{};
    std::int32_t input_context{};
    bool vats_menu_name{};
};

struct UiScalarSnapshot {
    std::uint64_t generation{};
    std::array<std::int32_t, max_context_entries> contexts{};
    std::array<UiMenuScalar, max_menu_entries> menus{};
    std::uint8_t context_count{};
    std::uint8_t menu_count{};
    std::uint32_t native_vats_mode{};
    std::uint32_t attack_key{invalid_mapped_key};
    std::uint32_t aim_key{invalid_mapped_key};
    bool valid{};
};

struct UiProjection {
    ContextFacts context{};
    std::int32_t top_context{};
    bool valid{};
    bool native_mode_valid{};
};

[[nodiscard]] constexpr ContextFacts resolve_context_stack(std::span<const std::int32_t> contexts,
    bool native_playback) noexcept;

[[nodiscard]] constexpr UiProjection project_ui_snapshot(
    const UiScalarSnapshot& snapshot) noexcept {
    UiProjection result{};
    if (!snapshot.valid || snapshot.context_count == 0 ||
        snapshot.context_count > max_context_entries || snapshot.menu_count > max_menu_entries) {
        return result;
    }

    constexpr std::uint32_t on_stack = 1U << 6U;
    constexpr std::uint32_t blocking_menu_flags =
        (1U << 0U) | (1U << 3U) | (1U << 4U) | (1U << 5U) | (1U << 24U);
    const bool native_playback = snapshot.native_vats_mode == 1U;
    result.native_mode_valid = snapshot.native_vats_mode <= 1U;
    result.context = resolve_context_stack(
        std::span{snapshot.contexts.data(), static_cast<std::size_t>(snapshot.context_count)},
        native_playback);
    result.top_context = snapshot.contexts[snapshot.context_count - 1U];
    if (!result.context.context_stack_valid || result.top_context < 0 ||
        result.top_context >= 0x21) {
        return result;
    }

    bool vats_menu_without_context{};
    for (std::size_t index = 0; index < snapshot.menu_count; ++index) {
        const auto& menu = snapshot.menus[index];
        if ((menu.flags & on_stack) == 0) {
            continue;
        }
        const bool vats_context = menu.input_context == 0x0D || menu.input_context == 0x0E;
        const bool vats_menu = vats_context || menu.vats_menu_name;
        if (!vats_menu && (menu.flags & blocking_menu_flags) != 0) {
            result.context.blocking_menu = true;
        }
        if (!vats_menu) {
            continue;
        }
        if ((menu.input_context == 0x0E && !result.context.playback_context) ||
            (menu.input_context == 0x0D && !result.context.selection_context &&
                !result.context.playback_context) ||
            (!result.context.selection_context && !result.context.playback_context)) {
            vats_menu_without_context = true;
        }
    }

    result.context.context_stack_valid = result.context.context_stack_valid &&
                                         result.native_mode_valid && !vats_menu_without_context;
    result.valid = result.context.context_stack_valid;
    return result;
}

[[nodiscard]] constexpr ContextFacts resolve_context_stack(std::span<const std::int32_t> contexts,
    bool native_playback) noexcept {
    ContextFacts facts{};
    facts.native_playback = native_playback;
    facts.context_stack_valid = !contexts.empty() && contexts.size() <= max_context_entries;
    if (!facts.context_stack_valid) {
        return facts;
    }

    for (std::size_t index = 0; index < contexts.size(); ++index) {
        const auto context = contexts[index];
        if (context < 0 || context >= 0x21) {
            facts.context_stack_valid = false;
            facts.selection_priority = no_context_priority;
            facts.playback_priority = no_context_priority;
            return facts;
        }

        const auto priority = static_cast<std::uint8_t>(index + 1);
        if (context == 0x0D) {
            facts.selection_context = true;
            facts.selection_priority = priority;
        } else if (context == 0x0E) {
            facts.playback_context = true;
            facts.playback_priority = priority;
        }
    }
    return facts;
}

[[nodiscard]] constexpr Mode classify_mode(ContextFacts facts) noexcept {
    if (!facts.available || !facts.alive || !facts.focused) {
        return Mode::suspended;
    }
    if (facts.blocking_menu) {
        return Mode::menu;
    }

    if (!facts.context_stack_valid ||
        facts.selection_context != (facts.selection_priority != no_context_priority) ||
        facts.playback_context != (facts.playback_priority != no_context_priority)) {
        return Mode::transition;
    }

    if (!facts.selection_context && !facts.playback_context) {
        return facts.native_playback ? Mode::transition : Mode::gameplay;
    }

    if (facts.selection_context && facts.playback_context) {
        if (facts.selection_priority == facts.playback_priority) {
            return Mode::transition;
        }

        const bool playback_has_priority = facts.playback_priority > facts.selection_priority;
        if (facts.native_playback != playback_has_priority) {
            return Mode::transition;
        }
        return playback_has_priority ? Mode::vats_playback : Mode::vats_selection;
    }

    if (facts.selection_context) {
        return facts.native_playback ? Mode::transition : Mode::vats_selection;
    }
    return facts.native_playback ? Mode::vats_playback : Mode::transition;
}

[[nodiscard]] constexpr TriggerSide trigger_side_from_key(std::uint32_t key) noexcept {
    switch (key) {
    // GetMappedKey returns per-device codes (0x09/0x0A); combined codes
    // are accepted for callers that already normalized the gamepad map.
    case 0x0009U:
    case 0x10009U:
        return TriggerSide::left;
    case 0x000AU:
    case 0x1000AU:
        return TriggerSide::right;
    default:
        return TriggerSide::none;
    }
}

struct TriggerAssignments {
    TriggerSide attack{TriggerSide::none};
    TriggerSide aim{TriggerSide::none};
};

[[nodiscard]] constexpr TriggerAssignments trigger_assignments(std::uint32_t attack_key,
    std::uint32_t aim_key) noexcept {
    const auto attack = trigger_side_from_key(attack_key);
    const auto aim = trigger_side_from_key(aim_key);
    if (attack != TriggerSide::none && attack == aim) {
        return {};
    }
    return {.attack = attack, .aim = aim};
}

enum class WeaponKind : std::uint8_t {
    unarmed,
    melee,
    firearm,
    explosive,
    unsupported,
    two_handed_melee
};

enum class WeaponKeywordGroup : std::uint8_t {
    rotary,
    flame,
    cryo,
    launcher,
    shotgun,
    heavy,
    energy,
    pistol,
    rifle,
};

template <class KeywordForm>
[[nodiscard]] constexpr const KeywordForm* select_weapon_keyword_form(const KeywordForm* base_form,
    const KeywordForm* instance_form) noexcept {
    return instance_form ? instance_form : base_form;
}

template <class HasKeywordGroup>
[[nodiscard]] constexpr WeaponFamily classify_weapon_family_lazy(WeaponKind kind,
    HasKeywordGroup has_keyword_group,
    std::int8_t
        projectile_count) noexcept(noexcept(has_keyword_group(WeaponKeywordGroup::rotary))) {
    switch (kind) {
    case WeaponKind::unarmed:
        return WeaponFamily::unarmed;
    case WeaponKind::melee:
        return WeaponFamily::melee;
    case WeaponKind::two_handed_melee:
        return WeaponFamily::two_handed_melee;
    case WeaponKind::firearm:
        if (has_keyword_group(WeaponKeywordGroup::rotary)) {
            return WeaponFamily::rotary;
        }
        if (has_keyword_group(WeaponKeywordGroup::flame)) {
            return WeaponFamily::flame;
        }
        if (has_keyword_group(WeaponKeywordGroup::cryo)) {
            return WeaponFamily::cryo;
        }
        if (has_keyword_group(WeaponKeywordGroup::launcher)) {
            return WeaponFamily::launcher;
        }
        if (has_keyword_group(WeaponKeywordGroup::shotgun)) {
            return WeaponFamily::shotgun;
        }
        if (has_keyword_group(WeaponKeywordGroup::heavy)) {
            return WeaponFamily::heavy;
        }
        if (has_keyword_group(WeaponKeywordGroup::energy)) {
            return WeaponFamily::energy;
        }
        if (has_keyword_group(WeaponKeywordGroup::pistol)) {
            return WeaponFamily::pistol;
        }
        if (has_keyword_group(WeaponKeywordGroup::rifle)) {
            return WeaponFamily::rifle;
        }
        if (projectile_count > 1) {
            return WeaponFamily::shotgun;
        }
        return WeaponFamily::generic_firearm;
    case WeaponKind::explosive:
    case WeaponKind::unsupported:
        return WeaponFamily::none;
    }
    return WeaponFamily::none;
}

struct WeaponReadinessFacts {
    WeaponKind kind{WeaponKind::unsupported};
    bool identified{};
    bool fully_drawn{};
    bool reloading{};
    bool throwing{};
    bool blocked{};
};

[[nodiscard]] constexpr bool weapon_ready(WeaponReadinessFacts facts) noexcept {
    if (!facts.identified || !facts.fully_drawn || facts.reloading || facts.throwing) {
        return false;
    }

    switch (facts.kind) {
    case WeaponKind::unarmed:
    case WeaponKind::melee:
    case WeaponKind::two_handed_melee:
        return true;
    case WeaponKind::firearm:
        return !facts.blocked;
    case WeaponKind::explosive:
    case WeaponKind::unsupported:
        return false;
    }
    return false;
}

[[nodiscard]] constexpr std::uint64_t weapon_identity(std::uint32_t form_id,
    std::uintptr_t instance_data) noexcept {
    if (form_id == 0) {
        return 0;
    }

    const auto mix = [](std::uint64_t value) constexpr noexcept {
        value += 0x9E3779B97F4A7C15ULL;
        value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
        return value ^ (value >> 31U);
    };

    const auto form_hash = mix(form_id);
    const auto instance_hash = mix(static_cast<std::uint64_t>(instance_data));
    const auto identity = form_hash ^ std::rotl(instance_hash, 23);
    return identity == 0 ? 1 : identity;
}

struct NativeObservation {
    Observation value{};
    std::uint32_t player_handle{};
    std::uintptr_t player_address{};
    std::uint32_t player_form_id{};
    std::uint64_t ui_generation{};
};

// Call only from the admitted Main phase with the pending request's generation.
// The address is a callback-provenance value and must not be dereferenced after this function returns.
[[nodiscard]] NativeObservation read_observation(MonoTime now,
    bool focused,
    std::uint64_t expected_ui_generation,
    const inputnext::adapters::LedSettings& led_settings);
} // namespace fallout4_adapter::native
