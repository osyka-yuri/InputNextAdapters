#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#undef FAIL

#include "fallout4_adapter/model.hpp"
#include "native/hit_facts.hpp"

#include <RE/H/HitData.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace {
using fallout4_adapter::EventKind;
using fallout4_adapter::native::HitAddresses;
using fallout4_adapter::native::HitFlags;
using fallout4_adapter::native::normalize_hit_event;
using HitFlag = RE::HitData::Flag;

constexpr std::uintptr_t player_address = 0x1000U;
constexpr std::uintptr_t other_address = 0x2000U;
constexpr std::uintptr_t unrelated_address = 0x3000U;
constexpr std::uint64_t weapon_identity = 0xABCDEFU;

void check(bool condition, std::string_view message) {
    INFO(message);
    CHECK(condition);
}

[[nodiscard]] HitFlags raw_flags(std::uint32_t mask) noexcept {
    return HitFlags{static_cast<HitFlag>(mask)};
}

[[nodiscard]] std::optional<fallout4_adapter::PolicyEvent> normalize(std::uintptr_t player,
    std::uintptr_t target,
    std::uintptr_t cause,
    std::optional<HitFlags> flags = std::nullopt,
    std::optional<float> health_damage = std::nullopt) noexcept {
    return normalize_hit_event(HitAddresses{.player = player, .target = target, .cause = cause},
        flags,
        weapon_identity,
        health_damage);
}

} // namespace

TEST_CASE("fallout4.native_hit.explosion_and_limb_flag_masks", "[unit][fallout4][native_hit]") {
    const auto explode_limb =
        normalize(player_address, player_address, other_address, raw_flags(0x00000080U));
    check(explode_limb && !explode_limb->explosion,
        "raw kExplodeLimb mask is not explosive damage");
    check(explode_limb && explode_limb->crippled_limb, "raw kExplodeLimb mask is a limb outcome");

    const auto explosion =
        normalize(player_address, player_address, other_address, raw_flags(0x00100000U));
    check(explosion && explosion->explosion, "raw kExplosion mask identifies explosive damage");
    check(explosion && !explosion->crippled_limb, "explosive damage alone is not a limb outcome");

    const auto combined =
        normalize(player_address, player_address, other_address, raw_flags(0x00100080U));
    check(combined && combined->explosion, "combined explosion and limb flags retain explosion");
    check(combined && combined->crippled_limb,
        "combined explosion and limb flags retain limb outcome");

    const auto crippled =
        normalize(player_address, player_address, other_address, raw_flags(0x00000100U));
    check(crippled && crippled->crippled_limb, "kCrippleLimb remains a limb outcome");
    const auto dismember =
        normalize(player_address, player_address, other_address, raw_flags(0x00000040U));
    check(dismember && dismember->crippled_limb, "kDismemberLimb remains a limb outcome");
}

TEST_CASE("fallout4.native_hit.predictions_and_missing_extended_data",
    "[unit][fallout4][native_hit]") {
    check(!normalize(player_address, player_address, other_address, raw_flags(0x00002000U)),
        "kPredictDamage is ignored");
    check(!normalize(player_address, player_address, other_address, raw_flags(0x00004000U)),
        "kPredictBaseDamage is ignored");

    const auto generic = normalize(player_address, player_address, other_address);
    check(generic && generic->kind == EventKind::hit_received,
        "generic events without extended hit data are retained");
    check(generic && !generic->explosion && !generic->crippled_limb,
        "missing extended data leaves hit flags clear");
    check(generic && !generic->melee, "missing extended data does not infer a melee hit");
    check(generic && generic->magnitude == 0.5F,
        "missing extended data uses the conservative default magnitude");
    check(generic && generic->weapon_identity == weapon_identity,
        "current player weapon identity is preserved");
}

TEST_CASE("fallout4.native_hit.existing_extended_hit_flags_are_preserved",
    "[unit][fallout4][native_hit]") {
    constexpr auto flags = 0x00000018U | 0x00000003U | 0x00020000U | 0x00008000U | 0x00010000U;
    const auto event = normalize(player_address, player_address, other_address, raw_flags(flags));
    check(event && event->critical, "critical hit flags are copied");
    check(event && event->blocked, "block hit flags are copied");
    check(event && event->power_attack, "power attack flags are copied");
    check(event && event->bash, "bash flags are copied");
}

TEST_CASE("fallout4.native_hit.melee_hit_flag_is_explicit", "[unit][fallout4][native_hit]") {
    const auto melee =
        normalize(player_address, other_address, player_address, raw_flags(0x00040000U));
    check(melee && melee->melee, "raw kMeleeAttack mask identifies a melee hit");

    const auto no_melee = normalize(player_address, other_address, player_address, raw_flags(0));
    check(no_melee && !no_melee->melee,
        "an explicit flag set without kMeleeAttack remains non-melee");

    const auto missing_flags = normalize(player_address, other_address, player_address);
    check(missing_flags && !missing_flags->melee,
        "missing flags never infer melee from the current weapon");
}

TEST_CASE("fallout4.native_hit.health_damage_magnitude_normalization",
    "[unit][fallout4][native_hit]") {
    const auto no_damage =
        normalize(player_address, player_address, other_address, std::nullopt, 0.0F);
    const auto small_damage =
        normalize(player_address, player_address, other_address, std::nullopt, 1.0F);
    const auto reference_damage =
        normalize(player_address, player_address, other_address, std::nullopt, 50.0F);
    const auto large_damage =
        normalize(player_address, player_address, other_address, std::nullopt, 100.0F);
    const auto max_damage = normalize(player_address,
        player_address,
        other_address,
        std::nullopt,
        std::numeric_limits<float>::max());
    check(no_damage && no_damage->magnitude == 0.0F,
        "zero health damage is a valid zero magnitude");
    check(no_damage && no_damage->health_damage_known,
        "zero health damage is distinguished from an unknown amount");
    check(no_damage && !no_damage->confirmed_positive_health_damage,
        "zero health damage does not manufacture confirmed damage activity");
    check(no_damage && small_damage && reference_damage && large_damage && max_damage &&
              no_damage->magnitude < small_damage->magnitude &&
              small_damage->magnitude < reference_damage->magnitude &&
              reference_damage->magnitude < large_damage->magnitude &&
              large_damage->magnitude < max_damage->magnitude,
        "finite health damage maps monotonically through the soft knee");
    check(reference_damage && reference_damage->magnitude == 0.5F,
        "50 health-damage points is the soft-knee reference");
    check(small_damage && small_damage->confirmed_positive_health_damage && reference_damage &&
              reference_damage->confirmed_positive_health_damage,
        "finite positive incoming health damage is preserved as an explicit fact");
    check(max_damage && std::isfinite(max_damage->magnitude) && max_damage->magnitude <= 1.0F,
        "maximum finite float damage stays finite and bounded");

    const auto missing = normalize(player_address, player_address, other_address);
    const auto negative =
        normalize(player_address, player_address, other_address, std::nullopt, -1.0F);
    const auto nan = normalize(player_address,
        player_address,
        other_address,
        std::nullopt,
        std::numeric_limits<float>::quiet_NaN());
    const auto infinity = normalize(player_address,
        player_address,
        other_address,
        std::nullopt,
        std::numeric_limits<float>::infinity());
    check(missing && negative && nan && infinity && missing->magnitude == 0.5F &&
              negative->magnitude == 0.5F && nan->magnitude == 0.5F && infinity->magnitude == 0.5F,
        "missing, negative, and nonfinite health damage use the conservative default");
    check(missing && negative && nan && infinity && !missing->confirmed_positive_health_damage &&
              !negative->confirmed_positive_health_damage &&
              !nan->confirmed_positive_health_damage && !infinity->confirmed_positive_health_damage,
        "unknown or invalid damage magnitude cannot create confirmed activity");
    check(missing && negative && nan && infinity && !missing->health_damage_known &&
              !negative->health_damage_known && !nan->health_damage_known &&
              !infinity->health_damage_known,
        "missing or invalid health damage remains explicitly unknown");
}

TEST_CASE("fallout4.native_hit.player_attribution", "[unit][fallout4][native_hit]") {
    const auto incoming = normalize(player_address, player_address, other_address);
    check(incoming && incoming->kind == EventKind::hit_received,
        "player target is attributed as incoming damage");

    const auto outgoing = normalize(player_address, other_address, player_address);
    check(outgoing && outgoing->kind == EventKind::hit_dealt,
        "player cause is attributed as outgoing damage");

    const auto self_hit = normalize(player_address, player_address, player_address);
    check(self_hit && self_hit->kind == EventKind::hit_received,
        "self-hit attribution retains incoming-first behavior");

    check(!normalize(player_address, other_address, unrelated_address),
        "events involving only unrelated actors are rejected");
    check(!normalize(0, player_address, other_address), "an absent player address is rejected");
}
