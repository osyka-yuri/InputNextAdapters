#include "native/observation.hpp"
#include "skyrim_adapter/model.hpp"
#include "support/test_runner.hpp"

#include <RE/A/Actor.h>
#include <RE/A/ActorState.h>
#include <RE/A/ActorValueOwner.h>
#include <RE/A/ActorValues.h>
#include <REL/Module.h>
#include <REL/Version.h>
#include <SKSE/Version.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string_view>

namespace {
using skyrim_adapter::native::detail::ActorSample;
using skyrim_adapter::native::detail::sample_actor;

constexpr std::size_t kOldActorValueOwnerOffset = 0xB0;
constexpr std::size_t kNewActorValueOwnerOffset = 0xB8;
constexpr std::size_t kOldActorStateOffset = 0xB8;
constexpr std::size_t kNewActorStateOffset = 0xC0;
constexpr std::size_t kCompilerActorValueOwnerOffset = 0x98;
constexpr std::size_t kCompilerActorStateOffset = 0xA0;
constexpr std::size_t kActorPrefixSize = 0xD0;
constexpr float kExpectedHealth = 73.0F;
constexpr float kExpectedStamina = 41.0F;
constexpr float kPoisonHealth = 12.0F;
constexpr float kPoisonStamina = 8.0F;

int failures{};

void check(bool passed, std::string_view label) {
    if (!passed) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

// The first nine virtual slots mirror RE::ActorValueOwner. These ABI fixtures are independent C++
// objects with real lifetimes; only the fixture boundary reinterprets them as CommonLib subobjects.
class FakeActorValueOwner {
public:
    FakeActorValueOwner() = default;
    FakeActorValueOwner(const FakeActorValueOwner&) = delete;
    FakeActorValueOwner& operator=(const FakeActorValueOwner&) = delete;
    FakeActorValueOwner(FakeActorValueOwner&&) = delete;
    FakeActorValueOwner& operator=(FakeActorValueOwner&&) = delete;
    virtual ~FakeActorValueOwner() = default;

    [[nodiscard]] virtual float GetActorValue(RE::ActorValue actor_value) const {
        switch (actor_value) {
        case RE::ActorValue::kHealth:
            return health();
        case RE::ActorValue::kStamina:
            return stamina();
        default:
            return -1.0F;
        }
    }

    [[nodiscard]] virtual float GetPermanentActorValue(
        [[maybe_unused]] RE::ActorValue actor_value) const {
        return 0.0F;
    }

    [[nodiscard]] virtual float GetBaseActorValue(
        [[maybe_unused]] RE::ActorValue actor_value) const {
        return 0.0F;
    }

    virtual void SetBaseActorValue([[maybe_unused]] RE::ActorValue actor_value,
        [[maybe_unused]] float value) {}

    virtual void ModBaseActorValue([[maybe_unused]] RE::ActorValue actor_value,
        [[maybe_unused]] float value) {}

    virtual void ModActorValue(
        [[maybe_unused]] RE::ACTOR_VALUE_MODIFIERS::ACTOR_VALUE_MODIFIER modifier,
        [[maybe_unused]] RE::ActorValue actor_value,
        [[maybe_unused]] float value) {}

    virtual void SetActorValue([[maybe_unused]] RE::ActorValue actor_value,
        [[maybe_unused]] float value) {}

    [[nodiscard]] virtual bool GetIsPlayerOwner() const { return false; }

protected:
    [[nodiscard]] virtual float health() const noexcept = 0;
    [[nodiscard]] virtual float stamina() const noexcept = 0;
};

class ExpectedActorValueOwner final : public FakeActorValueOwner {
protected:
    [[nodiscard]] float health() const noexcept override { return kExpectedHealth; }

    [[nodiscard]] float stamina() const noexcept override { return kExpectedStamina; }
};

class PoisonActorValueOwner final : public FakeActorValueOwner {
protected:
    [[nodiscard]] float health() const noexcept override { return kPoisonHealth; }

    [[nodiscard]] float stamina() const noexcept override { return kPoisonStamina; }
};

class SyntheticActorState {
public:
    SyntheticActorState() = default;
    SyntheticActorState(const SyntheticActorState&) = delete;
    SyntheticActorState& operator=(const SyntheticActorState&) = delete;
    SyntheticActorState(SyntheticActorState&&) = delete;
    SyntheticActorState& operator=(SyntheticActorState&&) = delete;
    virtual ~SyntheticActorState() = default;
    RE::ActorState::ActorState1 actorState1{};
    RE::ActorState::ActorState2 actorState2{};
};

static_assert(sizeof(ExpectedActorValueOwner) == sizeof(RE::ActorValueOwner));
static_assert(sizeof(PoisonActorValueOwner) == sizeof(RE::ActorValueOwner));
static_assert(sizeof(SyntheticActorState) == sizeof(RE::ActorState));

// This fixture represents only the engine-owned Actor ABI prefix. Its root is raw aligned storage,
// while zero-extra-storage owner/state ABI objects at selected offsets have C++ lifetimes. It calls
// only the production runtime accessors, the fake owner slot, and ActorState's inline field readers.
class ActorFixture {
public:
    explicit ActorFixture(bool new_layout) noexcept
        : storage_(make_poisoned_storage()),
          new_layout_(new_layout),
          poison_owner_(std::construct_at(reinterpret_cast<PoisonActorValueOwner*>(
              storage_.data() + kCompilerActorValueOwnerOffset))),
          owner_(std::construct_at(reinterpret_cast<ExpectedActorValueOwner*>(
              storage_.data() +
              (new_layout_ ? kNewActorValueOwnerOffset : kOldActorValueOwnerOffset)))),
          state_(std::construct_at(reinterpret_cast<SyntheticActorState*>(
              storage_.data() + (new_layout_ ? kNewActorStateOffset : kOldActorStateOffset)))) {
        std::uint32_t stale_state1{};
        std::uint32_t stale_state2{};
        std::memcpy(&stale_state1,
            storage_.data() + kCompilerActorStateOffset + sizeof(void*),
            sizeof(stale_state1));
        std::memcpy(&stale_state2,
            storage_.data() + kCompilerActorStateOffset + sizeof(void*) + sizeof(stale_state1),
            sizeof(stale_state2));
        const auto stale_life = (stale_state1 >> 21U) & 0xFU;
        const auto stale_attack = (stale_state1 >> 28U) & 0xFU;
        const auto stale_weapon = (stale_state2 >> 5U) & 0x7U;
        const auto life = stale_life == static_cast<std::uint32_t>(RE::ACTOR_LIFE_STATE::kAlive)
                              ? RE::ACTOR_LIFE_STATE::kDying
                              : RE::ACTOR_LIFE_STATE::kAlive;
        const auto weapon = stale_weapon == static_cast<std::uint32_t>(RE::WEAPON_STATE::kDrawn)
                                ? RE::WEAPON_STATE::kSheathed
                                : RE::WEAPON_STATE::kDrawn;
        const auto attack =
            stale_attack == static_cast<std::uint32_t>(RE::ATTACK_STATE_ENUM::kBowDraw)
                ? RE::ATTACK_STATE_ENUM::kNone
                : RE::ATTACK_STATE_ENUM::kBowDraw;
        state_->actorState1.lifeState = life;
        state_->actorState1.meleeAttackState = attack;
        state_->actorState1.movingForward = 1;
        state_->actorState1.sprinting = 1;
        state_->actorState1.swimming = 1;
        state_->actorState1.knockState = RE::KNOCK_STATE_ENUM::kDown;
        state_->actorState2.weaponState = weapon;
        state_->actorState2.staggered = 1;
        expected_ = {
            .health = kExpectedHealth,
            .stamina = kExpectedStamina,
            .life_state = life,
            .weapon_state = weapon,
            .attack_state = attack,
            .moving = true,
            .sprinting = true,
            .swimming = true,
            .staggered = true,
            .knocked_down = true,
            .gait = skyrim_adapter::MovementGait::sprinting,
        };
    }

    ActorFixture(const ActorFixture&) = delete;
    ActorFixture& operator=(const ActorFixture&) = delete;
    ActorFixture(ActorFixture&&) = delete;
    ActorFixture& operator=(ActorFixture&&) = delete;

    ~ActorFixture() {
        std::destroy_at(state_);
        std::destroy_at(owner_);
        if (poison_owner_) {
            std::destroy_at(poison_owner_);
        }
    }

    [[nodiscard]] RE::Actor& actor() noexcept {
        return *reinterpret_cast<RE::Actor*>(storage_.data());
    }

    [[nodiscard]] const ActorSample& expected() const noexcept { return expected_; }

private:
    static constexpr std::array<std::byte, kActorPrefixSize> make_poisoned_storage() noexcept {
        std::array<std::byte, kActorPrefixSize> storage{};
        storage.fill(std::byte{0xA5});
        return storage;
    }

    alignas(RE::Actor) std::array<std::byte, kActorPrefixSize> storage_{};
    bool new_layout_{};
    PoisonActorValueOwner* poison_owner_{};
    ExpectedActorValueOwner* owner_{};
    SyntheticActorState* state_{};
    ActorSample expected_{};
};

void verify_runtime(const REL::Version& version,
    REL::Module::Runtime runtime,
    std::string_view label) {
    if (!REL::Module::mock(version, runtime, L"SkyrimSE.exe", 0x1000)) {
        check(false, "CommonLib mock module initializes");
        REL::Module::reset();
        return;
    }
    const bool new_layout = version >= SKSE::RUNTIME_SSE_1_6_629;
    ActorFixture fixture(new_layout);
    auto& actor = fixture.actor();
    const auto* owner_base = static_cast<RE::ActorValueOwner*>(&actor);
    const auto* state_base = static_cast<RE::ActorState*>(&actor);
    const auto actor_address = reinterpret_cast<std::uintptr_t>(&actor);
    const bool owner_base_matches = reinterpret_cast<std::uintptr_t>(owner_base) - actor_address ==
                                    kCompilerActorValueOwnerOffset;
    const bool state_base_matches =
        reinterpret_cast<std::uintptr_t>(state_base) - actor_address == kCompilerActorStateOffset;
    check(owner_base_matches, "fixture poisons the compiler-selected ActorValueOwner base");
    check(state_base_matches, "fixture poisons the compiler-selected ActorState base");
    const auto sample = sample_actor(fixture.actor());
    check(sample.health == fixture.expected().health, label);
    check(sample.stamina == fixture.expected().stamina,
        "stamina uses the runtime-aware actor-value owner");
    check(sample.life_state == fixture.expected().life_state, label);
    check(sample.weapon_state == fixture.expected().weapon_state, label);
    check(sample.attack_state == fixture.expected().attack_state, label);
    check(sample.moving == fixture.expected().moving,
        "movement state uses the runtime-aware actor state");
    check(sample.sprinting == fixture.expected().sprinting,
        "sprinting state uses the runtime-aware actor state");
    check(sample.swimming == fixture.expected().swimming,
        "swimming state uses the runtime-aware actor state");
    check(sample.staggered == fixture.expected().staggered,
        "stagger state uses the runtime-aware actor state");
    check(sample.knocked_down == fixture.expected().knocked_down,
        "knockdown state uses the runtime-aware actor state");
    check(sample.gait == fixture.expected().gait,
        "movement gait uses the runtime-aware actor state");

    // These calls exercise the direct compiler-selected base behavior as a regression witness.
    if (owner_base_matches && state_base_matches) {
        check(actor.GetActorValue(RE::ActorValue::kHealth) == kPoisonHealth,
            "direct inherited actor-value access reads the poisoned compiler-selected offset");
        check(actor.GetActorValue(RE::ActorValue::kStamina) == kPoisonStamina,
            "direct inherited stamina access reads the poisoned compiler-selected offset");
        check(actor.GetLifeState() != fixture.expected().life_state,
            "direct inherited life-state access reads the poisoned compiler-selected offset");
        check(actor.GetWeaponState() != fixture.expected().weapon_state,
            "direct inherited weapon-state access reads the poisoned compiler-selected offset");
        check(actor.GetAttackState() != fixture.expected().attack_state,
            "direct inherited attack-state access reads the poisoned compiler-selected offset");
    }

    REL::Module::reset();
}

int run_tests() {
    verify_runtime(SKSE::RUNTIME_SSE_1_5_97,
        REL::Module::Runtime::SE,
        "1.5.97 values use the SE layout");
    verify_runtime(REL::Version(1, 6, 628, 0),
        REL::Module::Runtime::AE,
        "1.6.628 values use the old layout");
    verify_runtime(SKSE::RUNTIME_SSE_1_6_629,
        REL::Module::Runtime::AE,
        "1.6.629 values use the new layout");
    verify_runtime(SKSE::RUNTIME_SSE_1_6_1170,
        REL::Module::Runtime::AE,
        "1.6.1170 values use the new layout");
    verify_runtime(SKSE::RUNTIME_SSE_1_6_1179,
        REL::Module::Runtime::AE,
        "1.6.1179 values use the new layout");
    verify_runtime(SKSE::RUNTIME_SSE_1_7_104,
        REL::Module::Runtime::AE,
        "1.7.104 values use the new layout");

    if (failures != 0) {
        std::cerr << failures << " native actor observation check(s) failed\n";
        return 1;
    }
    std::cout << "native actor observation checks passed\n";
    return 0;
}
} // namespace

int main() {
    return inputnext::test::run_main(__FILE__, [] { return run_tests(); });
}
