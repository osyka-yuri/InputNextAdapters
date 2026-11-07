#include "binding_test_support.hpp"
#include "native/actor_values.hpp"
#include "native/skse_sdk.hpp"

#include <array>
#include <cstddef>
#include <type_traits>

// ABI compile-time SDK contract assertions (32-bit x86).
static_assert(sizeof(void*) == 4, "Skyrim LE targets 32-bit x86 architecture");
static_assert(sizeof(ActorValueOwner) == sizeof(void*),
    "ActorValueOwner size must match single vtable pointer (4 bytes on x86)");
static_assert(alignof(ActorValueOwner) == 4, "ActorValueOwner alignment must be 4 bytes on x86");
static_assert(std::is_polymorphic_v<ActorValueOwner>, "ActorValueOwner must be polymorphic");
static_assert(sizeof(skyrim_adapter::native::ActorValueSample) == 8,
    "ActorValueSample is 8 bytes (two 32-bit floats)");
static_assert(sizeof(SKSEActionEvent) == 0x10, "SKSEActionEvent layout size assertion");
static_assert(sizeof(TESHitEvent) == 0x30, "TESHitEvent layout size assertion");
static_assert(sizeof(MenuOpenCloseEvent) == 0x08, "MenuOpenCloseEvent layout size assertion");
static_assert(sizeof(PluginInfo) == 0x0C, "PluginInfo size assertion (12 bytes)");
static_assert(sizeof(SKSEInterface) == 0x1C, "SKSEInterface size assertion (28 bytes)");

// Default test implementations of ActorValueOwner virtual methods to satisfy linker
// when building outside the Skyrim game engine executable.
ActorValueOwner::~ActorValueOwner() = default;

float ActorValueOwner::GetCurrent([[maybe_unused]] UInt32 arg) {
    return -1.0F;
}

float ActorValueOwner::GetMaximum([[maybe_unused]] UInt32 arg) {
    return -1.0F;
}

float ActorValueOwner::GetBase([[maybe_unused]] UInt32 arg) {
    return -1.0F;
}

void ActorValueOwner::SetBase([[maybe_unused]] UInt32 arg0, [[maybe_unused]] float arg1) {
}

void ActorValueOwner::ModBase([[maybe_unused]] UInt32 arg0, [[maybe_unused]] float arg1) {
}

void ActorValueOwner::Unk_06([[maybe_unused]] UInt32 arg0,
    [[maybe_unused]] UInt32 arg1,
    [[maybe_unused]] UInt32 arg2) {
}

void ActorValueOwner::SetCurrent([[maybe_unused]] UInt32 arg0, [[maybe_unused]] float arg1) {
}

bool ActorValueOwner::Unk_08() {
    return false;
}

namespace skyrim_le::binding_tests {
namespace {

// Actor-value virtual dispatch verification against mock fixture.
constexpr UInt32 kActorValue_Health = 24U;
constexpr UInt32 kActorValue_Magicka = 25U;
constexpr UInt32 kActorValue_Stamina = 26U;

// Verifies sample_actor_value() dispatches to virtual GetCurrent() and GetMaximum()
// with the supplied ActorValue ID, returning an ActorValueSample.
// Limitation: Executes against a harness mock fixture; does not verify runtime binary
// layout against the TESV.exe PlayerCharacter vtable.
class ActorValueOwnerFixture final : public ActorValueOwner {
public:
    float GetCurrent(UInt32 actor_value) override {
        if (current_calls < current_ids.size()) {
            current_ids[current_calls] = actor_value;
        }
        ++current_calls;
        switch (actor_value) {
        case kActorValue_Health:
            return 42.5F;
        case kActorValue_Magicka:
            return 100.0F;
        case kActorValue_Stamina:
            return 17.0F;
        default:
            return -100.0F;
        }
    }

    float GetMaximum(UInt32 actor_value) override {
        if (maximum_calls < maximum_ids.size()) {
            maximum_ids[maximum_calls] = actor_value;
        }
        ++maximum_calls;
        switch (actor_value) {
        case kActorValue_Health:
            return 85.0F;
        case kActorValue_Magicka:
            return 200.0F;
        case kActorValue_Stamina:
            return 40.0F;
        default:
            return -100.0F;
        }
    }

    std::array<UInt32, 3> current_ids{};
    std::array<UInt32, 3> maximum_ids{};
    std::size_t current_calls{};
    std::size_t maximum_calls{};
};

void test_actor_value_virtual_dispatch() {
    using skyrim_adapter::native::sample_actor_value;

    ActorValueOwnerFixture owner;
    const auto health = sample_actor_value(owner, kActorValue_Health);
    const auto magicka = sample_actor_value(owner, kActorValue_Magicka);
    const auto stamina = sample_actor_value(owner, kActorValue_Stamina);

    check(health.current == 42.5F && health.maximum == 85.0F,
        "actor value helper dispatches both health queries");
    check(magicka.current == 100.0F && magicka.maximum == 200.0F,
        "actor value helper dispatches both magicka queries");
    check(stamina.current == 17.0F && stamina.maximum == 40.0F,
        "actor value helper dispatches both stamina queries");

    check(owner.current_calls == 3U && owner.maximum_calls == 3U,
        "actor value helper calls both virtuals once per sample");
    check(owner.current_ids == std::array<UInt32, 3>{kActorValue_Health,
                                   kActorValue_Magicka,
                                   kActorValue_Stamina} &&
              owner.maximum_ids == std::array<UInt32, 3>{kActorValue_Health,
                                       kActorValue_Magicka,
                                       kActorValue_Stamina},
        "actor value helper forwards each value index to both virtuals");
}

} // namespace

void run_abi_tests() {
    test_actor_value_virtual_dispatch();
}
} // namespace skyrim_le::binding_tests
