#pragma once

#include <fallout_gamebryo/model.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fallout_new_vegas {
enum class NativeSignalKind : std::uint8_t {
    fire_weapon,
    projectile_create,
    dry_fire,
    melee_swing,
    thrown,
    hit_received,
    misc_stat_change,
    item_equipped,
    lockpick_broken,
    terminal_hacked,
    terminal_hack_failed,
    vats_select,
    vats_accept,
};

struct NativeSignal {
    fallout_gamebryo::MonoTime at{fallout_gamebryo::MonoTime::duration::zero()};
    std::uint64_t epoch{};
    std::uint64_t frame{};
    fallout_gamebryo::Mode mode{fallout_gamebryo::Mode::unavailable};
    fallout_gamebryo::Menu menu{fallout_gamebryo::Menu::none};
    NativeSignalKind kind{NativeSignalKind::fire_weapon};
    std::uint32_t source{};
    std::uint32_t target{};
    std::uint32_t form{};
    std::uint32_t projectile{};
    std::uintptr_t hit_reference{};
    fallout_gamebryo::WeaponClass weapon_class{fallout_gamebryo::WeaponClass::none};
    fallout_gamebryo::HitOutcome hit_outcome{fallout_gamebryo::HitOutcome::ordinary};
    std::uint32_t detail{};
    std::int32_t delta{};
    std::uint8_t projectiles_per_shot{1};
    bool automatic{};
    bool confirmed_damage{};
    bool explosion_damage{};
};

class SignalTranslator final {
public:
    void reset(std::uint64_t epoch) noexcept;
    void advance(fallout_gamebryo::MonoTime now) noexcept;
    void cancel_pending_shots() noexcept;
    [[nodiscard]] std::optional<fallout_gamebryo::Event> translate(const NativeSignal& signal,
        std::uint32_t player) noexcept;

private:
    struct PendingFire {
        NativeSignal signal;
        unsigned int remaining{};
        bool emitted{};
    };

    struct ProjectileIdentity {
        fallout_gamebryo::MonoTime at;
        std::uint32_t reference{};
    };

    struct HitIdentity {
        std::uintptr_t reference{};
        std::uint32_t source{};
        std::uint32_t target{};
        std::uint64_t frame{};
        bool explosion{};
    };

    struct BlastIdentity {
        fallout_gamebryo::MonoTime at;
        std::uintptr_t reference{};
    };

    [[nodiscard]] fallout_gamebryo::Event make_event(const NativeSignal& signal,
        fallout_gamebryo::EventKind kind) noexcept;
    [[nodiscard]] std::optional<fallout_gamebryo::Event> make_menu_event(const NativeSignal& signal,
        fallout_gamebryo::EventKind kind,
        std::uint32_t reference) noexcept;
    [[nodiscard]] std::optional<fallout_gamebryo::Event> translate_player_action(
        const NativeSignal& signal) noexcept;
    [[nodiscard]] std::optional<fallout_gamebryo::Event> translate_discharge(
        const NativeSignal& signal) noexcept;
    [[nodiscard]] std::optional<fallout_gamebryo::Event> translate_hit(const NativeSignal& signal,
        std::uint32_t player) noexcept;
    [[nodiscard]] std::optional<fallout_gamebryo::Event> match_fire(
        const NativeSignal& fire) noexcept;
    [[nodiscard]] std::optional<fallout_gamebryo::Event> match_projectile(
        const NativeSignal& projectile) noexcept;
    [[nodiscard]] bool repeated_projectile(const NativeSignal& signal) noexcept;
    [[nodiscard]] bool repeated_hit(const NativeSignal& signal) noexcept;
    [[nodiscard]] bool repeated_player_blast(const NativeSignal& signal) noexcept;
    void expire(fallout_gamebryo::MonoTime now) noexcept;

    std::uint64_t epoch_{};
    std::uint64_t next_action_{1};
    std::array<std::optional<PendingFire>, fallout_gamebryo::event_capacity> pending_fires_{};
    std::array<std::optional<NativeSignal>, fallout_gamebryo::event_capacity>
        pending_projectiles_{};
    std::array<ProjectileIdentity, fallout_gamebryo::event_capacity> recent_projectiles_{};
    std::array<HitIdentity, 32> recent_hits_{};
    std::array<BlastIdentity, 32> recent_player_blasts_{};
    std::size_t next_projectile_identity_{};
    std::size_t next_hit_identity_{};
    std::size_t next_blast_identity_{};
};
} // namespace fallout_new_vegas
