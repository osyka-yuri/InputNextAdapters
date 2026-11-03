#include "fallout4_adapter/model.hpp"
#include "native/observation.hpp"
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace {
using namespace fallout4_adapter;
using namespace fallout4_adapter::native;

struct WeaponFixture {
    WeaponKind kind{WeaponKind::unsupported};
    bool shotgun_keyword{};
    bool heavy_keyword{};
    bool energy_keyword{};
    bool pistol_keyword{};
    bool rifle_keyword{};
    bool rotary_keyword{};
    bool flame_keyword{};
    bool cryo_keyword{};
    bool launcher_keyword{};
    std::int8_t projectile_count{};
};

[[nodiscard]] constexpr WeaponFamily classify_fixture(WeaponFixture facts) noexcept {
    return classify_weapon_family_lazy(
        facts.kind,
        [&facts](WeaponKeywordGroup group) noexcept {
            switch (group) {
            case WeaponKeywordGroup::rotary:
                return facts.rotary_keyword;
            case WeaponKeywordGroup::flame:
                return facts.flame_keyword;
            case WeaponKeywordGroup::cryo:
                return facts.cryo_keyword;
            case WeaponKeywordGroup::launcher:
                return facts.launcher_keyword;
            case WeaponKeywordGroup::shotgun:
                return facts.shotgun_keyword;
            case WeaponKeywordGroup::heavy:
                return facts.heavy_keyword;
            case WeaponKeywordGroup::energy:
                return facts.energy_keyword;
            case WeaponKeywordGroup::pistol:
                return facts.pistol_keyword;
            case WeaponKeywordGroup::rifle:
                return facts.rifle_keyword;
            }
            return false;
        },
        facts.projectile_count);
}

void check(bool condition, std::string_view message) {
    INFO(message);
    CHECK(condition);
}

[[nodiscard]] ContextFacts ready_facts() noexcept {
    ContextFacts facts{};
    facts.available = true;
    facts.alive = true;
    facts.focused = true;
    return facts;
}

struct FakeKeywordForm {
    std::array<std::string_view, 4> keywords{};

    [[nodiscard]] bool HasKeywordString(std::string_view keyword) const noexcept {
        return std::ranges::any_of(keywords,
            [keyword](const auto value) noexcept { return value == keyword; });
    }
};

[[nodiscard]] WeaponFamily classify_fake_weapon_keywords(const FakeKeywordForm& base_form,
    const FakeKeywordForm* instance_form) noexcept {
    const auto* effective_form = select_weapon_keyword_form(&base_form, instance_form);
    WeaponFixture facts{};
    facts.kind = WeaponKind::firearm;
    facts.pistol_keyword = effective_form->HasKeywordString("WeaponTypePistol");
    facts.rifle_keyword = effective_form->HasKeywordString("WeaponTypeRifle");
    facts.rotary_keyword = effective_form->HasKeywordString("WeaponTypeMinigun") ||
                           effective_form->HasKeywordString("WeaponTypeGatlingLaser");
    facts.flame_keyword = effective_form->HasKeywordString("WeaponTypeFlamer");
    facts.cryo_keyword = effective_form->HasKeywordString("WeaponTypeCryolater");
    facts.launcher_keyword = effective_form->HasKeywordString("WeaponTypeMissileLauncher") ||
                             effective_form->HasKeywordString("WeaponTypeFatman") ||
                             effective_form->HasKeywordString("WeaponTypeBroadsider");
    return classify_fixture(facts);
}
} // namespace

TEST_CASE("fallout4.native_observation.signed_gun_state_field",
    "[component][fallout4][native_observation]") {
    struct SignedGunStateField {
        std::int32_t state : 4;
    };

    const SignedGunStateField sighted_fire{.state = -8};
    check(sighted_fire.state == -8 && gun_state_bits(sighted_fire.state) == 8U,
        "the SDK's sign-extended sighted-fire state decodes to its stored value 8");
    check(gun_state_bits(4) == 4U && gun_state_bits(5) == 5U && gun_state_bits(6) == 6U &&
              gun_state_bits(7) == 7U,
        "reload, throw, sighted and ordinary firing states retain their values");
    check(gun_state_bits(-7) == 9U && gun_state_bits(-1) == 15U,
        "unrecognized signed nibble values remain distinct from sighted fire");
}

TEST_CASE("fallout4.native_observation.context_stack_resolution",
    "[component][fallout4][native_observation]") {
    constexpr std::array gameplay{0, 0x11};
    auto facts = resolve_context_stack(gameplay, false);
    facts.available = true;
    facts.alive = true;
    facts.focused = true;
    check(facts.context_stack_valid && classify_mode(facts) == Mode::gameplay,
        "normal context stack is gameplay");

    constexpr std::array selection{0, 0x0D};
    facts = resolve_context_stack(selection, false);
    facts.available = true;
    facts.alive = true;
    facts.focused = true;
    check(facts.selection_context && facts.selection_priority == 2,
        "VATS selection retains its stack priority");
    check(classify_mode(facts) == Mode::vats_selection,
        "selection context with idle native VATS is selection");

    constexpr std::array playback{0, 0x0D, 0x0E};
    facts = resolve_context_stack(playback, true);
    facts.available = true;
    facts.alive = true;
    facts.focused = true;
    check(facts.selection_context && facts.playback_context &&
              facts.playback_priority > facts.selection_priority,
        "the later VATS playback context has higher priority");
    check(classify_mode(facts) == Mode::vats_playback,
        "native playback and top playback context agree");

    constexpr std::array reverse_playback{0, 0x0E, 0x0D};
    facts = resolve_context_stack(reverse_playback, false);
    facts.available = true;
    facts.alive = true;
    facts.focused = true;
    check(classify_mode(facts) == Mode::vats_selection,
        "top selection context wins when both VATS contexts remain");

    facts.native_playback = true;
    check(classify_mode(facts) == Mode::transition,
        "conflicting VATS context priority and native mode are neutral");

    facts = resolve_context_stack(selection, true);
    facts.available = true;
    facts.alive = true;
    facts.focused = true;
    check(classify_mode(facts) == Mode::transition,
        "native playback without playback context is neutral");

    constexpr std::span<const std::int32_t> empty_contexts{};
    facts = resolve_context_stack(empty_contexts, false);
    facts.available = true;
    facts.alive = true;
    facts.focused = true;
    check(!facts.context_stack_valid && classify_mode(facts) == Mode::transition,
        "an empty context stack is not gameplay");

    constexpr std::array invalid_context{0, 0x22};
    facts = resolve_context_stack(invalid_context, false);
    facts.available = true;
    facts.alive = true;
    facts.focused = true;
    check(!facts.context_stack_valid && classify_mode(facts) == Mode::transition,
        "unknown context IDs fail closed");

    constexpr std::array oversized_contexts = [] {
        std::array<std::int32_t, max_context_entries + 1> values{};
        values.fill(0);
        return values;
    }();
    facts = resolve_context_stack(oversized_contexts, false);
    facts.available = true;
    facts.alive = true;
    facts.focused = true;
    check(!facts.context_stack_valid && classify_mode(facts) == Mode::transition,
        "oversized context stacks fail closed");

    facts = ready_facts();
    facts.context_stack_valid = true;
    facts.blocking_menu = true;
    check(classify_mode(facts) == Mode::menu, "blocking menus allow only neutral delivery");
    facts.blocking_menu = false;
    facts.focused = false;
    check(classify_mode(facts) == Mode::suspended, "focus loss suspends before context resolution");
}

TEST_CASE("fallout4.native_observation.owned_ui_scalar_projection",
    "[component][fallout4][native_observation]") {
    UiScalarSnapshot snapshot{};
    snapshot.valid = true;
    snapshot.generation = 41;
    snapshot.context_count = 2;
    snapshot.contexts[0] = 0;
    snapshot.contexts[1] = 0x11;
    snapshot.native_vats_mode = 0;
    snapshot.attack_key = 0x0AU;
    snapshot.aim_key = 0x09U;

    auto projection = project_ui_snapshot(snapshot);
    check(projection.valid && projection.top_context == 0x11,
        "owned scalar context stack projects its selected top context");
    check(ui_generation_matches(41, snapshot.generation),
        "matching UI generations admit their owned snapshot");
    check(!ui_generation_matches(0, 0) && !ui_generation_matches(41, 42),
        "closed and retired UI generations are rejected");
    const auto remapped = trigger_assignments(snapshot.attack_key, snapshot.aim_key);
    check(remapped.attack == TriggerSide::right && remapped.aim == TriggerSide::left,
        "copied remapped action keys retain their independent trigger assignments");

    snapshot.menu_count = 1;
    snapshot.menus[0] =
        UiMenuScalar{.flags = (1U << 6U) | (1U << 0U), .input_context = 0, .vats_menu_name = false};
    projection = project_ui_snapshot(snapshot);
    auto facts = projection.context;
    facts.available = true;
    facts.alive = true;
    facts.focused = true;
    check(projection.valid && facts.blocking_menu, "copied on-stack pause menu remains a blocker");
    check(classify_mode(facts) == Mode::menu,
        "blocking menu projection restricts delivery to neutral");

    snapshot.menus[0] = UiMenuScalar{.flags = 1U << 6U, .input_context = 0, .vats_menu_name = true};
    projection = project_ui_snapshot(snapshot);
    check(!projection.valid, "VATS menu without a matching VATS context fails closed");

    snapshot.context_count = 2;
    snapshot.contexts[1] = 0x0D;
    snapshot.menus[0] =
        UiMenuScalar{.flags = 1U << 6U, .input_context = 0x0D, .vats_menu_name = false};
    projection = project_ui_snapshot(snapshot);
    check(projection.valid && projection.context.selection_context,
        "copied VATS menu and context remain coherent");

    snapshot.native_vats_mode = 2;
    check(!project_ui_snapshot(snapshot).valid, "unknown native VATS modes fail closed");
    snapshot.native_vats_mode = 0;
    snapshot.context_count = static_cast<std::uint8_t>(max_context_entries + 1U);
    check(!project_ui_snapshot(snapshot).valid, "oversized context snapshots fail closed");
}

TEST_CASE("fallout4.native_observation.ui_provenance_admission",
    "[component][fallout4][native_observation]") {
    constexpr std::uint64_t epoch = 23;
    constexpr std::uint64_t generation = 71;
    check(ui_provenance_matches(epoch, epoch, generation, generation, generation),
        "one lifecycle epoch and UI generation admits the production task/sample/cache/event "
        "predicate");
    check(!ui_provenance_matches(epoch, epoch, generation, generation + 1U, generation),
        "a sample or cached event from a retired generation is rejected");
    check(!ui_provenance_matches(epoch, epoch, generation, generation, generation + 1U),
        "a transition after capture invalidates the existing generation provenance");
    check(!ui_provenance_matches(epoch, epoch + 1U, generation, generation, generation),
        "a retired lifecycle epoch cannot be upgraded by a matching UI generation");
    check(!ui_provenance_matches(epoch, epoch, 0, 0, 0),
        "closed UI generation cannot admit a task, sample, cache, or event");
}

TEST_CASE("fallout4.native_observation.actor_value_normalization",
    "[component][fallout4][native_observation]") {
    const auto ordinary = normalized_actor_value(70.0F, -30.0F);
    check(ordinary && std::abs(*ordinary - 0.7F) < 0.0001F,
        "current minus damage yields normalized resource");
    const auto absolute = absolute_health_sample(70.0F, -30.0F);
    check(absolute && absolute->current == 70.0F && absolute->maximum == 100.0F,
        "LED copies absolute current health and the same gameplay maximum");

    const auto depleted_below_zero = normalized_actor_value(-20.0F, -120.0F);
    check(depleted_below_zero && *depleted_below_zero == 0.0F,
        "negative current values normalize to zero");

    const auto over_maximum = normalized_actor_value(120.0F, 20.0F);
    check(over_maximum && *over_maximum == 1.0F, "ratios above maximum clamp to one");

    check(!normalized_actor_value(std::numeric_limits<float>::quiet_NaN(), 0.0F),
        "NaN current is unavailable");
    check(!normalized_actor_value(50.0F, std::numeric_limits<float>::infinity()),
        "infinite damage is unavailable");
    check(!normalized_actor_value(20.0F, 20.0F), "zero maximum is unavailable");
    check(!normalized_actor_value(20.0F, 21.0F), "negative maximum is unavailable");
    check(!normalized_actor_value(std::numeric_limits<float>::max(),
              -std::numeric_limits<float>::max()),
        "overflowed maximum is unavailable");
    check(!absolute_health_sample(std::numeric_limits<float>::quiet_NaN(), 0.0F) &&
              !absolute_health_sample(20.0F, 20.0F) && !absolute_health_sample(20.0F, 21.0F) &&
              !absolute_health_sample(std::numeric_limits<float>::max(),
                  -std::numeric_limits<float>::max()),
        "invalid, zero, negative, and overflowed LED maxima remain unavailable");
}

TEST_CASE("fallout4.native_observation.trigger_mapping",
    "[component][fallout4][native_observation]") {
    check(trigger_side_from_key(0x09U) == TriggerSide::left,
        "gamepad left trigger code is recognized");
    check(trigger_side_from_key(0x10009U) == TriggerSide::left,
        "combined left trigger code is recognized");
    check(trigger_side_from_key(0x0AU) == TriggerSide::right,
        "gamepad right trigger code is recognized");
    check(trigger_side_from_key(0x1000AU) == TriggerSide::right,
        "combined right trigger code is recognized");
    check(trigger_side_from_key(0xFFFFFFFFU) == TriggerSide::none,
        "unknown and invalid mappings are neutral");

    const auto mapped = trigger_assignments(0x0AU, 0x09U);
    check(mapped.attack == TriggerSide::right && mapped.aim == TriggerSide::left,
        "distinct action mappings remain independent");
    const auto collision = trigger_assignments(0x09U, 0x10009U);
    check(collision.attack == TriggerSide::none && collision.aim == TriggerSide::none,
        "ambiguous same-trigger bindings disable both profiles");
}

TEST_CASE("fallout4.native_observation.weapon_family_resolution_and_identity",
    "[component][fallout4][native_observation]") {
    check(classify_fixture({.kind = WeaponKind::unarmed}) == WeaponFamily::unarmed,
        "hand-to-hand weapons retain their family");
    check(classify_fixture({.kind = WeaponKind::melee}) == WeaponFamily::melee,
        "melee weapons retain their family");
    check(classify_fixture({.kind = WeaponKind::two_handed_melee}) ==
              WeaponFamily::two_handed_melee,
        "two-handed melee weapons retain their separate family");
    check(classify_fixture({.kind = WeaponKind::firearm, .projectile_count = 8}) ==
              WeaponFamily::shotgun,
        "multiple projectile weapons use the shotgun profile");
    check(classify_fixture(
              {.kind = WeaponKind::firearm, .rifle_keyword = true, .projectile_count = 8}) ==
              WeaponFamily::rifle,
        "an identified multi-projectile rifle keeps its weapon-family feel");
    check(classify_fixture({.kind = WeaponKind::firearm, .shotgun_keyword = true}) ==
              WeaponFamily::shotgun,
        "the explicit shotgun keyword selects the shotgun profile");
    check(classify_fixture({.kind = WeaponKind::firearm, .heavy_keyword = true}) ==
              WeaponFamily::heavy,
        "the heavy weapon keyword selects the heavy profile");
    check(classify_fixture(
              {.kind = WeaponKind::firearm, .energy_keyword = true, .pistol_keyword = true}) ==
              WeaponFamily::energy,
        "energy classification takes precedence over the pistol keyword");
    check(classify_fixture({.kind = WeaponKind::firearm, .pistol_keyword = true}) ==
              WeaponFamily::pistol,
        "the pistol keyword selects the pistol profile");
    check(classify_fixture({.kind = WeaponKind::firearm, .rifle_keyword = true}) ==
              WeaponFamily::rifle,
        "the rifle keyword selects the rifle profile");
    check(classify_fixture({.kind = WeaponKind::firearm,
              .heavy_keyword = true,
              .rotary_keyword = true,
              .projectile_count = 8}) == WeaponFamily::rotary,
        "rotary keywords take precedence over heavy and multi-projectile fallbacks");
    check(classify_fixture(
              {.kind = WeaponKind::firearm, .heavy_keyword = true, .flame_keyword = true}) ==
              WeaponFamily::flame,
        "the effective flamer keyword selects its distinct family");
    check(classify_fixture(
              {.kind = WeaponKind::firearm, .heavy_keyword = true, .cryo_keyword = true}) ==
              WeaponFamily::cryo,
        "the canonical Cryolater keyword takes precedence over the heavy fallback");
    check(classify_fixture({.kind = WeaponKind::firearm,
              .heavy_keyword = true,
              .launcher_keyword = true,
              .projectile_count = 8}) == WeaponFamily::launcher,
        "launcher keywords take precedence over heavy and multi-projectile fallbacks");
    check(classify_fixture({.kind = WeaponKind::firearm}) == WeaponFamily::generic_firearm,
        "unclassified firearms use the moderate generic profile");
    check(classify_fixture({.kind = WeaponKind::explosive}) == WeaponFamily::none,
        "throwable types do not masquerade as firearms");
    check(classify_fixture({.kind = WeaponKind::unsupported}) == WeaponFamily::none,
        "unsupported weapon types remain unidentified");

    constexpr auto identity = weapon_identity(0x1234U, 0x5678U);
    static_assert(identity != 0, "identified weapon instances have a nonzero opaque identity");
    static_assert(identity == weapon_identity(0x1234U, 0x5678U),
        "weapon identity is stable for matching observations and events");
    static_assert(identity != weapon_identity(0x1235U, 0x5678U),
        "different weapon forms do not share their identity");
    static_assert(identity != weapon_identity(0x1234U, 0x5679U),
        "different instance addresses do not share their identity");
    static_assert(weapon_identity(0, 0x5678U) == 0, "an absent weapon form has no identity");
}

TEST_CASE("fallout4.native_observation.weapon_readiness",
    "[component][fallout4][native_observation]") {
    auto facts = WeaponReadinessFacts{
        .kind = WeaponKind::firearm,
        .identified = true,
        .fully_drawn = true,
    };
    check(weapon_ready(facts), "an identified fully drawn firearm is ready");

    facts.blocked = true;
    check(!weapon_ready(facts), "a blocked firearm is not ready");
    facts.kind = WeaponKind::melee;
    check(weapon_ready(facts), "a confirmed melee block does not suppress melee readiness");
    facts.kind = WeaponKind::two_handed_melee;
    check(weapon_ready(facts),
        "a blocked two-handed melee weapon remains ready for its secondary trigger");
    facts.kind = WeaponKind::unarmed;
    check(weapon_ready(facts), "a blocked unarmed weapon remains ready for its secondary trigger");

    facts.reloading = true;
    check(!weapon_ready(facts), "reloading suppresses melee readiness");
    facts.reloading = false;
    facts.throwing = true;
    check(!weapon_ready(facts), "throwing suppresses melee readiness");
    facts.throwing = false;
    facts.fully_drawn = false;
    check(!weapon_ready(facts), "a sheathed or transitioning weapon is not ready");
    facts.fully_drawn = true;
    facts.identified = false;
    check(!weapon_ready(facts), "an unidentified weapon is not ready");
    facts.identified = true;
    facts.kind = WeaponKind::explosive;
    check(!weapon_ready(facts), "throwables are never ready for held feedback");
    facts.kind = WeaponKind::unsupported;
    check(!weapon_ready(facts), "unsupported weapon kinds are never ready");
}

TEST_CASE("fallout4.native_observation.weapon_family_keyword_queries_are_lazy",
    "[component][fallout4][native_observation]") {
    constexpr std::array all_groups{
        WeaponKeywordGroup::rotary,
        WeaponKeywordGroup::flame,
        WeaponKeywordGroup::cryo,
        WeaponKeywordGroup::launcher,
        WeaponKeywordGroup::shotgun,
        WeaponKeywordGroup::heavy,
        WeaponKeywordGroup::energy,
        WeaponKeywordGroup::pistol,
        WeaponKeywordGroup::rifle,
    };

    std::array<WeaponKeywordGroup, all_groups.size()> queried{};
    std::size_t query_count{};
    const auto query = [&](WeaponKeywordGroup group) noexcept {
        queried[query_count++] = group;
        return group == WeaponKeywordGroup::energy;
    };

    for (const auto kind : {
             WeaponKind::unarmed,
             WeaponKind::melee,
             WeaponKind::two_handed_melee,
             WeaponKind::explosive,
             WeaponKind::unsupported,
         }) {
        query_count = 0;
        static_cast<void>(classify_weapon_family_lazy(kind, query, 0));
        check(query_count == 0, "non-firearm weapon kinds never query keyword forms");
    }

    query_count = 0;
    check(classify_weapon_family_lazy(WeaponKind::firearm, query, 0) == WeaponFamily::energy,
        "lazy firearm classification preserves the energy family result");
    bool explicit_priority_matches = query_count == 7;
    for (std::size_t index = 0; index < query_count; ++index) {
        explicit_priority_matches =
            explicit_priority_matches && queried[index] == all_groups[index];
    }
    check(explicit_priority_matches,
        "firearm keyword groups are queried only in family priority through the first match");

    query_count = 0;
    const auto no_match = [&](WeaponKeywordGroup group) noexcept {
        queried[query_count++] = group;
        return false;
    };
    check(classify_weapon_family_lazy(WeaponKind::firearm, no_match, 3) == WeaponFamily::shotgun,
        "projectile-count fallback remains after every explicit firearm family");
    check(query_count == all_groups.size(),
        "projectile fallback runs only after all keyword groups are absent");
}

TEST_CASE("fallout4.native_observation.weapon_instance_keywords_replace_base_keywords",
    "[component][fallout4][native_observation]") {
    constexpr FakeKeywordForm base{{"WeaponTypePistol", {}, {}, {}}};
    constexpr FakeKeywordForm rifle_instance{{"WeaponTypeRifle", {}, {}, {}}};
    constexpr FakeKeywordForm empty_instance{};
    constexpr FakeKeywordForm minigun_instance{{"WeaponTypeMinigun", "WeaponTypeHeavyGun", {}, {}}};

    check(classify_fake_weapon_keywords(base, &rifle_instance) == WeaponFamily::rifle,
        "a rifle instance replaces the base pistol keyword");
    check(classify_fake_weapon_keywords(base, &empty_instance) == WeaponFamily::generic_firearm,
        "a present empty instance form does not inherit removed base keywords");
    check(classify_fake_weapon_keywords(base, nullptr) == WeaponFamily::pistol,
        "the base keyword form is the fallback when no instance form exists");
    check(classify_fake_weapon_keywords(base, &minigun_instance) == WeaponFamily::rotary,
        "the effective instance keyword selects a special weapon family over inherited base data");
}
