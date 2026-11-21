#include "effect_model.hpp"
#include "native/bow_projectiles.hpp"
#include "native/event_handler.hpp"
#include "native/magic_effects.hpp"
#include "native/magic_forms.hpp"
#include "native/memory_guard.hpp"
#include "support/test_runner.hpp"

#include <GameExtraData.h>
#include <GameForms.h>
#include <GameObjects.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <excpt.h>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace {

using oblivion_adapter::native::memory_exception_filter;

static_assert(memory_exception_filter(EXCEPTION_ACCESS_VIOLATION) == EXCEPTION_EXECUTE_HANDLER);
static_assert(memory_exception_filter(EXCEPTION_IN_PAGE_ERROR) == EXCEPTION_EXECUTE_HANDLER);
static_assert(memory_exception_filter(EXCEPTION_INT_DIVIDE_BY_ZERO) == EXCEPTION_CONTINUE_SEARCH);
static_assert(memory_exception_filter(EXCEPTION_STACK_OVERFLOW) == EXCEPTION_CONTINUE_SEARCH);

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

void test_damage_payload_decoding() {
    using namespace oblivion_adapter::native::detail;

    float expected_damage = 25.5F;
    const void* raw_arg = nullptr;
    std::memcpy(static_cast<void*>(&raw_arg), &expected_damage, sizeof(float));
    require(decode_damage_payload(raw_arg) == 25.5F,
        "decode_damage_payload unpacks positive float correctly");

    expected_damage = 0.0F;
    std::memcpy(static_cast<void*>(&raw_arg), &expected_damage, sizeof(float));
    require(decode_damage_payload(raw_arg) == 0.0F, "decode_damage_payload handles zero");

    expected_damage = -12.0F;
    std::memcpy(static_cast<void*>(&raw_arg), &expected_damage, sizeof(float));
    require(decode_damage_payload(raw_arg) == -12.0F, "decode_damage_payload handles negative");

    expected_damage = std::numeric_limits<float>::infinity();
    std::memcpy(static_cast<void*>(&raw_arg), &expected_damage, sizeof(float));
    require(std::isinf(decode_damage_payload(raw_arg)) && decode_damage_payload(raw_arg) > 0.0F,
        "decode_damage_payload handles positive infinity");

    expected_damage = -std::numeric_limits<float>::infinity();
    std::memcpy(static_cast<void*>(&raw_arg), &expected_damage, sizeof(float));
    require(std::isinf(decode_damage_payload(raw_arg)) && decode_damage_payload(raw_arg) < 0.0F,
        "decode_damage_payload handles negative infinity");

    expected_damage = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(static_cast<void*>(&raw_arg), &expected_damage, sizeof(float));
    require(std::isnan(decode_damage_payload(raw_arg)), "decode_damage_payload handles quiet NaN");
}

void test_magic_effect_codes() {
    using oblivion_adapter::MagicKind;
    using oblivion_adapter::native::CastEffects;
    using oblivion_adapter::native::classify_magic_effect;
    using oblivion_adapter::native::effect_code;
    static_assert(effect_code('F', 'I', 'D', 'G') == 0x47444946U);
    require(classify_magic_effect(effect_code('F', 'I', 'D', 'G')) == MagicKind::fire,
        "Fire Damage has the fire profile");
    require(classify_magic_effect(effect_code('R', 'E', 'H', 'E')) == MagicKind::healing,
        "Restore Health has the healing profile");

    CastEffects shields;
    shields.add(effect_code('F', 'I', 'S', 'H'));
    shields.add(effect_code('F', 'R', 'S', 'H'));
    shields.add(effect_code('L', 'I', 'S', 'H'));
    require(shields.kind(true) == MagicKind::shield, "All three elemental shields are protective");
    require(shields.kind(false) == MagicKind::generic, "Truncated effect lists remain neutral");
    shields.add(effect_code('S', 'H', 'D', 'G'));
    require(shields.kind(true) == MagicKind::generic, "Shield mixed with damage remains neutral");
}

// Model the x86 object layout without calling engine constructors or virtual methods.
template <class T>
struct SdkMemory {
    alignas(T) std::array<std::byte, sizeof(T)> bytes{};

    void write(std::size_t offset, const auto& value) noexcept {
        constexpr auto value_size = sizeof(std::remove_cvref_t<decltype(value)>);
        std::memcpy(bytes.data() + offset, static_cast<const void*>(&value), value_size);
    }

    [[nodiscard]] T* address() noexcept { return reinterpret_cast<T*>(bytes.data()); }
};

// The byte fixtures exercise member offsets in the pinned MSVC object layout.
#ifdef __clang__
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Winvalid-offsetof"
#endif
void test_soul_trap_payload() {
    using oblivion_adapter::EffectEvent;
    using oblivion_adapter::EffectEventKind;
    using oblivion_adapter::native::detail::read_player_soul_trap;

    SdkMemory<TESObjectREFR> captured;
    captured.write(offsetof(TESForm, typeID), std::uint8_t{kFormType_ACRE});
    captured.write(offsetof(TESForm, refID), std::uint32_t{0x123});
    SdkMemory<TESObjectREFR> gem_ref;
    gem_ref.write(offsetof(TESForm, typeID), std::uint8_t{kFormType_REFR});
    SdkMemory<TESForm> gem;
    gem.write(offsetof(TESForm, typeID), std::uint8_t{kFormType_SoulGem});
    gem.write(offsetof(TESForm, refID), std::uint32_t{0x456});
    gem_ref.write(offsetof(TESObjectREFR, baseForm), gem.address());

    EffectEvent event{.kind = EffectEventKind::soul_trap, .at = {}, .actor_id = 0x14};
    SdkMemory<TESObjectREFR> npc_caster;
    require(!read_player_soul_trap(captured.address(),
                gem_ref.address(),
                npc_caster.address(),
                event) &&
                event.target_id == 0 && event.source_id == 0,
        "An NPC calling object is not the payload of the player-only xOBSE hook");
    require(!read_player_soul_trap(captured.address(), nullptr, nullptr, event) &&
                !read_player_soul_trap(captured.address(), gem.address(), nullptr, event),
        "The identity of a captured actor alone does not confirm the gem was filled");

    const auto free_page = [](void* page) noexcept {
        VirtualFree(page, 0, MEM_RELEASE);
    };
    const std::unique_ptr<void, decltype(free_page)> inaccessible{
        VirtualAlloc(nullptr, sizeof(TESObjectREFR), MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS),
        free_page};
    if (!inaccessible) {
        throw std::runtime_error{"Allocate an unreadable page for the soul gem fixture"};
    }
    require(!read_player_soul_trap(captured.address(), inaccessible.get(), nullptr, event) &&
                event.target_id == 0 && event.source_id == 0,
        "An unreadable game reference cannot publish a soul trap event");

    require(read_player_soul_trap(captured.address(), gem_ref.address(), nullptr, event) &&
                event.actor_id == 0x14 && event.target_id == 0x123 && event.source_id == 0x456,
        "A native player capture records the actor and base gem IDs during the callback");
}

template <class Form>
SdkMemory<Form> magic_form(std::uint8_t type, EffectItem* item) noexcept {
    SdkMemory<Form> form;
    form.write(offsetof(TESForm, typeID), type);
    form.write(offsetof(TESForm, refID), std::uint32_t{0x1234});
    if constexpr (!std::is_same_v<Form, TESForm>) {
        form.write(offsetof(Form, magicItem) + offsetof(MagicItem, list) +
                       offsetof(EffectItemList, effectList),
            EffectItemList::Entry{.effectItem = item, .next = nullptr});
    }
    return form;
}

void test_applied_magic_forms() {
    using namespace oblivion_adapter;
    using oblivion_adapter::native::effect_code;
    using oblivion_adapter::native::read_magic_form;

    SdkMemory<EffectItem> item;
    item.write(offsetof(EffectItem, effectCode), effect_code('F', 'I', 'D', 'G'));
    auto weapon = magic_form<EnchantmentItem>(kFormType_Enchantment, item.address());
    weapon.write(offsetof(EnchantmentItem, enchantType),
        static_cast<std::uint32_t>(EnchantmentItem::kEnchant_Weapon));
    auto potion = magic_form<AlchemyItem>(kFormType_AlchemyItem, item.address());
    auto ingredient = magic_form<IngredientItem>(kFormType_Ingredient, item.address());
    auto unknown = magic_form<TESForm>(kFormType_Global, nullptr);
    EffectEvent event{.kind = EffectEventKind::magic_applied, .at = {}};
    for (const TESForm* form :
        std::array<const TESForm*, 3>{weapon.address(), potion.address(), ingredient.address()}) {
        require(read_magic_form(form, event) && event.magic == MagicKind::fire &&
                    event.source_id == 0x1234,
            "Incoming enchantment, potion and ingredient effects retain their typed "
            "classification");
    }
    require(read_magic_form(unknown.address(), event) && event.magic == MagicKind::generic,
        "An unrecognized applied form keeps its neutral profile");

    event.kind = EffectEventKind::cast;
    require(!read_magic_form(weapon.address(), event) &&
                !read_magic_form(potion.address(), event) &&
                !read_magic_form(ingredient.address(), event),
        "Applications from weapon enchantments and consumables cannot impersonate player casts");
    weapon.write(offsetof(EnchantmentItem, enchantType),
        static_cast<std::uint32_t>(EnchantmentItem::kEnchant_Staff));
    require(read_magic_form(weapon.address(), event) && event.cast_origin == CastOrigin::staff,
        "A staff enchantment remains a supported cast origin");
}

#ifdef __clang__
#    pragma clang diagnostic pop
#endif

void test_projectile_confirmation() {
    using namespace std::chrono_literals;
    using namespace oblivion_adapter;
    using oblivion_adapter::native::BowProjectileTracker;
    using oblivion_adapter::native::ProjectileScan;

    EffectObservation observation{
        .sampled_at = MonoTime{} + 100ms,
        .epoch = 1,
        .mode = EffectMode::gameplay,
        .alive = true,
        .player_id = 0x14,
        .cell_id = 50,
        .weapon = Weapon::bow,
        .weapon_id = 67,
        .bow = BowPhase::attached,
    };
    BowProjectileTracker tracker;
    ProjectileScan evidence{};
    evidence.arrows[0] = {.id = 1001, .elapsed_seconds = 0.02F, .player_owned = true};
    evidence.count = 1;
    const auto scan = [&](std::uint32_t cell, ProjectileScan& result) noexcept {
        result = evidence;
        return cell == observation.cell_id;
    };
    const auto advance = [&](std::chrono::milliseconds elapsed, BowPhase phase) {
        observation.sampled_at += elapsed;
        observation.bow = phase;
        return tracker.sample(observation, observation.sampled_at, scan);
    };

    require(advance(0ms, BowPhase::attached) == 0U, "Attachment establishes a projectile baseline");
    evidence.arrows[1] = {.id = 1002, .elapsed_seconds = 0.01F, .player_owned = false};
    evidence.count = 2;
    require(advance(20ms, BowPhase::none) == 0U,
        "Cancellation and NPC projectiles do not confirm a shot");
    require(advance(90ms, BowPhase::none) == 0U, "An old player arrow cannot be replayed");
    require(advance(61ms, BowPhase::none) == 0U,
        "The search stops after the 150 ms release window");

    require(advance(12ms, BowPhase::attached) == 0U, "The next shot gets a fresh baseline");
    evidence.arrows[2] = {.id = 1003, .elapsed_seconds = 0.015F, .player_owned = true};
    evidence.count = 3;
    require(advance(12ms, BowPhase::none) == 1003U,
        "Only a fresh newly spawned player projectile confirms release");
    require(advance(12ms, BowPhase::none) == 0U, "An accepted arrow cannot be replayed");

    require(advance(12ms, BowPhase::attached) == 0U, "Can attach with the same bow again");
    observation.cell_id = 51;
    evidence.arrows[3] = {.id = 1004, .elapsed_seconds = 0.02F, .player_owned = true};
    evidence.count = 4;
    require(advance(12ms, BowPhase::none) == 0U,
        "A change of cell invalidates the attachment before an arrow is considered");

    require(tracker.note_attachment(observation, scan) == 0U,
        "The xOBSE attachment callback takes the pre-launch baseline");
    evidence.arrows[4] = {.id = 1005, .elapsed_seconds = 0.002F, .player_owned = true};
    evidence.count = 5;
    require(advance(8ms, BowPhase::drawing) == 1005U,
        "A prior shot is confirmed even if the next sampled phase already shows drawing");

    require(tracker.note_attachment(observation, scan) == 0U, "A fresh draw saves its baseline");
    evidence.arrows[5] = {.id = 1006, .elapsed_seconds = 0.002F, .player_owned = true};
    evidence.count = 6;
    observation.sampled_at += 8ms;
    require(tracker.note_attachment(observation, scan) == 1006U,
        "A second attachment confirms the prior shot if both arrived between samples");

    evidence.arrows[6] = {.id = 1007, .elapsed_seconds = 0.002F, .player_owned = true};
    evidence.count = 7;
    require(advance(8ms, BowPhase::none) == 1007U,
        "The second attachment takes its own baseline before the next shot");

    require(tracker.note_attachment(observation,
                [](std::uint32_t, ProjectileScan&) { return false; }) == 0U,
        "A failed scan does not produce an attachment baseline");
    require(advance(8ms, BowPhase::none) == 0U,
        "A release without a trustworthy pre-launch scan cannot confirm an arrow");

    require(tracker.note_attachment(observation, scan) == 0U, "A fresh callback can recover");
    require(advance(1ms, BowPhase::none) == 0U, "The first observed detachment starts the window");
    require(advance(130ms, BowPhase::none) == 0U, "A release alone does not confirm a shot");
    evidence.arrows[7] = {.id = 1008, .elapsed_seconds = 0.001F, .player_owned = true};
    evidence.count = 8;
    require(advance(21ms, BowPhase::none) == 0U,
        "Repeated detachment samples cannot restart the 150 ms window");
}

} // namespace

int main() {
    return inputnext::test::run_main("Oblivion native ABI tests", [] {
        test_damage_payload_decoding();
        test_soul_trap_payload();
        test_magic_effect_codes();
        test_applied_magic_forms();
        test_projectile_confirmation();
        std::cout << "All Oblivion native ABI tests passed.\n";
        return 0;
    });
}
