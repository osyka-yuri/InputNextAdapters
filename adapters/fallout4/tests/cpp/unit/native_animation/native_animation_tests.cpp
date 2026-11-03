#include "fallout4_adapter/model.hpp"
#include "native/animation_cues.hpp"
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string_view>

namespace {
using fallout4_adapter::EventKind;
using fallout4_adapter::native::animation_cue;

[[nodiscard]] std::optional<EventKind> admitted_cue(std::string_view tag,
    bool in_power_armor) noexcept {
    const auto cue = animation_cue(tag);
    return cue && cue->allowed(in_power_armor) ? std::optional{cue->kind} : std::nullopt;
}

void check(bool condition, std::string_view message) {
    INFO(message);
    CHECK(condition);
}
} // namespace

TEST_CASE("fallout4.native_animation.animation_cues_preserve_power_armor_provenance",
    "[unit][fallout4][native_animation]") {
    const auto footstep = animation_cue("FootLeft");
    check(footstep && footstep->kind == EventKind::footstep && !footstep->power_armor_only,
        "recognized footstep tags can be filtered before player state is read");
    const auto landing = animation_cue("PowerArmorHardLanding");
    check(!landing, "physics observations own landings without animation duplication");
    check(!animation_cue("WeaponFire"),
        "unrelated animation tags are rejected without reading player state");

    check(admitted_cue("FootLeft", true) == EventKind::footstep,
        "left power armor footstep is recognized");
    check(admitted_cue("FootRight", true) == EventKind::footstep,
        "right power armor footstep is recognized");
    check(!admitted_cue("PowerArmorHardLanding", true),
        "power armor landing animation cannot duplicate the measured landing");
    check(admitted_cue("FootLeft", false) == EventKind::footstep,
        "ordinary left footsteps are recognized");
    check(admitted_cue("FootRight", false) == EventKind::footstep,
        "ordinary right footsteps are recognized");
    check(!admitted_cue("PowerArmorHardLanding", false), "ordinary landings are ignored");
    check(!admitted_cue("footleft", true), "animation tags remain case-sensitive");
    check(!admitted_cue("WeaponFire", true), "weapon fire does not duplicate shot events");
    check(!admitted_cue("PowerArmorLanding", true), "unverified landing tags are ignored");
    check(!admitted_cue(std::string_view{}, true), "empty animation tags are ignored");
}
