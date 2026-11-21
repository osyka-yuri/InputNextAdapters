#include "effect_model.hpp"
#include "native/bow_projectiles.hpp"
#include "native/event_handler.hpp"
#include "native/magic_effects.hpp"
#include "native/magic_forms.hpp"
#include "native/memory_guard.hpp"
#include "native/options_input.hpp"
#include "support/test_runner.hpp"

#include <GameExtraData.h>
#include <GameForms.h>
#include <GameObjects.h>
#include <input_next_core.h>
#include <inputnext_adapters/body_program.hpp>

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
#include <string_view>
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

struct FakeGamepad {
    std::uint32_t resolved_slot{};
    std::uint32_t buttons{};
    std::uint64_t qpc{1};
    std::uint64_t connection{1};
    input_next_status read_status{INPUT_NEXT_STATUS_OK};
};

FakeGamepad fake_gamepad;

input_next_status INPUT_NEXT_CORE_CALL resolve_gamepad(
    const input_next_device_selector_v1* selector,
    input_next_device_target_v1* target) noexcept {
    if (selector->struct_size != sizeof(*selector) ||
        selector->target.struct_size != sizeof(selector->target) ||
        selector->kind != INPUT_NEXT_CORE_SELECTOR_XINPUT_SLOT || selector->xinput_slot >= 4U ||
        target->struct_size != sizeof(*target)) {
        return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
    }
    fake_gamepad.resolved_slot = selector->xinput_slot;
    target->connection_id = fake_gamepad.connection;
    return INPUT_NEXT_STATUS_OK;
}

input_next_status INPUT_NEXT_CORE_CALL read_gamepad(const input_next_device_target_v1* target,
    input_next_input_snapshot_v1* snapshot) noexcept {
    if (target->struct_size != sizeof(*target) || snapshot->struct_size != sizeof(*snapshot)) {
        return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
    }
    if (target->connection_id != fake_gamepad.connection) {
        return INPUT_NEXT_STATUS_STALE_TARGET;
    }
    snapshot->state.buttons = fake_gamepad.buttons;
    snapshot->qpc = fake_gamepad.qpc;
    return fake_gamepad.read_status;
}

const input_next_core_api_v1* INPUT_NEXT_CORE_CALL query_gamepad(std::uint32_t abi) noexcept {
    static constexpr auto api = [] {
        input_next_core_api_v1 result{};
        result.struct_size = sizeof(result);
        result.abi_version = INPUT_NEXT_CORE_ABI_VERSION;
        result.resolve_target = resolve_gamepad;
        result.read_latest = read_gamepad;
        return result;
    }();
    return abi == INPUT_NEXT_CORE_ABI_VERSION ? &api : nullptr;
}

void test_options_press() {
    using namespace std::chrono_literals;
    using namespace oblivion_adapter::native;
    using oblivion_adapter::MonoTime;

    constexpr auto start = MonoTime{} + 1s;
    constexpr auto menu = 1U << 4U;
    constexpr auto right_shoulder = 1U << 9U;
    OptionsInput input;
    OptionsPress press;
    require(!input.bind(nullptr, 2), "Missing Core interface leaves the shortcut disabled");
    require(input.bind(query_gamepad, 2), "The shortcut binds the Core ABI");

    const auto read = [&](MonoTime sample_time, std::uint32_t buttons) {
        fake_gamepad.buttons = buttons;
        ++fake_gamepad.qpc;
        input.poll(true, sample_time);
        return press.take(input.sample(), sample_time);
    };
    require(!read(start, menu), "An already held Options is ignored on attach");
    require(fake_gamepad.resolved_slot == 2, "The shortcut uses the configured controller slot");
    require(!read(start + 20ms, right_shoulder), "The magic button never sends Escape");
    require(read(start + 40ms, menu), "A release followed by Options sends Escape");
    require(!read(start + 60ms, menu), "A held button cannot toggle pause repeatedly");

    input.poll(false, start + 80ms);
    require(!input.sample(), "Losing focus clears the polled button state");
    press.reset(fake_gamepad.qpc);
    require(!read(start + 100ms, menu), "A held button stays quiet when focus returns");
    require(!read(start + 120ms, 0), "Releasing the button rearms the shortcut");
    require(read(start + 140ms, menu), "A fresh press works after regaining focus");

    require(!read(start + 160ms, 0), "A release does not emit Escape");
    ++fake_gamepad.connection;
    require(!read(start + 180ms, menu) && !input.sample(),
        "A retired controller cannot send Escape through its old target");
    require(!read(start + 360ms, menu), "A held button is ignored after controller replacement");
    fake_gamepad.read_status = INPUT_NEXT_STATUS_NO_DATA;
    require(!read(start + 380ms, 0) && !input.sample(), "Missing data does not arm the shortcut");
    fake_gamepad.read_status = INPUT_NEXT_STATUS_OK;
    require(!read(start + 400ms, 0) && read(start + 420ms, menu),
        "A new controller can pause after a valid release");
}

void test_pause_command_and_session_fence() {
    using namespace std::chrono_literals;
    using namespace oblivion_adapter::native;
    using oblivion_adapter::MonoTime;

    require(std::string_view{escape_command(PauseContext::gameplay)} == "TapKey 1",
        "Gameplay uses the one-frame Escape input");
    require(std::string_view{escape_command(PauseContext::settings_menu)} == "MenuTapKey 1",
        "Pause and settings use the buffered Escape input");
    require(escape_command(PauseContext::unavailable) == nullptr,
        "Unrelated menus and the console receive no Escape");

    constexpr auto now = MonoTime{} + 1s;
    OptionsPress press;
    press.reset(100);
    OptionsSample sample{.polled_at = now, .binding = 1, .qpc = 98, .down = false};
    require(!press.take(sample, now), "A released snapshot can establish a baseline");
    sample.down = true;
    sample.qpc = 100;
    require(!press.take(sample, now), "A press from before a save load cannot replay");
    sample.down = false;
    sample.qpc = 101;
    require(!press.take(sample, now), "A current release arms the shortcut");
    sample.down = true;
    sample.qpc = 102;
    require(!press.take(sample, now + 151ms), "An expired worker sample cannot open pause");
    require(!press.take(sample, now + 160ms), "Expired input does not rearm a held button");
}

void test_options_prolonged_no_data_and_reconnect() {
    using namespace std::chrono_literals;
    using namespace oblivion_adapter::native;
    using oblivion_adapter::MonoTime;

    fake_gamepad = {};
    constexpr auto start = MonoTime{} + 1s;
    constexpr auto menu = 1U << 4U;
    OptionsInput input;
    OptionsPress press;
    require(input.bind(query_gamepad, 0), "The Options reader binds the test gamepad");
    const auto poll = [&](MonoTime now, std::uint32_t buttons) {
        fake_gamepad.buttons = buttons;
        ++fake_gamepad.qpc;
        input.poll(true, now);
    };
    poll(start, 0);
    require(!press.take(input.sample(), start), "An up report only arms Options");

    fake_gamepad.read_status = INPUT_NEXT_STATUS_NO_DATA;
    for (auto elapsed = 20ms; elapsed <= 2s; elapsed += 20ms) {
        poll(start + elapsed, menu);
        require(!input.sample(), "Repeated NO_DATA invalidates the prior button report");
    }
    // The game thread missed the NO_DATA reports while stalled.
    fake_gamepad.read_status = INPUT_NEXT_STATUS_OK;
    poll(start + 2020ms, menu);
    require(!press.take(input.sample(), start + 2020ms),
        "A held button after a gap cannot use the pre-gap release baseline");

    fake_gamepad.read_status = INPUT_NEXT_STATUS_NO_DATA;
    poll(start + 2040ms, 0);
    ++fake_gamepad.connection;
    poll(start + 2060ms, menu);
    poll(start + 2080ms, menu);
    require(!press.take(input.sample(), start + 2080ms) && !input.sample(),
        "A replacement connection without reports does not arm Options");
    fake_gamepad.read_status = INPUT_NEXT_STATUS_OK;
    poll(start + 2100ms, menu);
    require(!press.take(input.sample(), start + 2100ms), "A new held press cannot toggle pause");
    poll(start + 2120ms, 0);
    require(!press.take(input.sample(), start + 2120ms), "Releasing the new pad rearms Options");
    poll(start + 2140ms, menu);
    require(press.take(input.sample(), start + 2140ms),
        "Options works after reconnect and release");
}

} // namespace

int main() {
    return inputnext::test::run_main("Oblivion native ABI tests", [] {
        test_damage_payload_decoding();
        test_soul_trap_payload();
        test_magic_effect_codes();
        test_applied_magic_forms();
        test_projectile_confirmation();
        test_options_press();
        test_pause_command_and_session_fence();
        test_options_prolonged_no_data_and_reconnect();
        std::cout << "All Oblivion native ABI tests passed.\n";
        return 0;
    });
}
