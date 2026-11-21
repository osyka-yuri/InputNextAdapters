#pragma once

#include "effect_model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace oblivion_adapter::native {

inline constexpr std::size_t projectile_scan_limit = 256;

struct ProjectileEvidence {
    std::uint32_t id{};
    float elapsed_seconds{};
    bool player_owned{};
};

struct ProjectileScan {
    std::array<ProjectileEvidence, projectile_scan_limit> arrows{};
    std::size_t count{};
};

// Scan the observed cell synchronously and retain only IDs and timestamps.
class BowProjectileTracker {
public:
    void reset() noexcept { *this = {}; }

    [[nodiscard]] std::uint32_t note_attachment(const EffectObservation& observation,
        const auto& scan_cell) noexcept {
        const auto sampled_at = observation.sampled_at;
        if (!update_context(observation, sampled_at)) {
            return 0;
        }
        ProjectileScan scan{};
        if (!read_scan(scan_cell, scan)) {
            finish(true);
            return 0;
        }
        // One scan checks whether the previous draw fired and records the new baseline.
        const auto previous_shot =
            has_baseline_ && within_release_window(sampled_at) ? find_arrow(scan) : 0U;
        finish(false);
        capture_baseline(scan);
        return previous_shot;
    }

    [[nodiscard]] std::uint32_t
    sample(const EffectObservation& observation, MonoTime now, const auto& scan_cell) noexcept {
        if (!update_context(observation, now)) {
            return 0;
        }
        const bool attached = observation.bow == BowPhase::attached;
        if (wait_for_detach_) {
            wait_for_detach_ = attached;
            return 0;
        }
        if (attached) {
            return !has_baseline_ || release_at_ ? note_attachment(observation, scan_cell) : 0U;
        }
        if (!has_baseline_) {
            return 0;
        }
        if (!release_at_) {
            release_at_ = observation.sampled_at;
        }
        if (!within_release_window(now)) {
            finish(false);
            return 0;
        }

        ProjectileScan scan{};
        if (!read_scan(scan_cell, scan)) {
            finish(false);
            return 0;
        }
        const auto detected = find_arrow(scan);
        if (detected != 0 || observation.bow == BowPhase::drawing ||
            now - *release_at_ >= effect_max_age) {
            finish(false);
        }
        return detected;
    }

private:
    [[nodiscard]] bool within_release_window(MonoTime now) const noexcept {
        return !release_at_ || (now >= *release_at_ && now - *release_at_ <= effect_max_age);
    }

    [[nodiscard]] std::uint32_t find_arrow(const ProjectileScan& scan) const noexcept {
        constexpr float max_elapsed = std::chrono::duration<float>(effect_max_age).count();
        float youngest = max_elapsed;
        std::uint32_t detected{};
        for (const auto& arrow : std::span{scan.arrows.data(), scan.count}) {
            if (arrow.id != 0 && arrow.player_owned && std::isfinite(arrow.elapsed_seconds) &&
                std::ranges::find(baseline_, arrow.id) == baseline_.end() &&
                arrow.elapsed_seconds >= 0.0F && arrow.elapsed_seconds <= youngest) {
                youngest = arrow.elapsed_seconds;
                detected = arrow.id;
            }
        }
        return detected;
    }

    bool update_context(const EffectObservation& observation, MonoTime now) noexcept {
        const auto time = observation.sampled_at;
        if (observation.mode != EffectMode::gameplay || !observation.alive ||
            observation.weapon != Weapon::bow || observation.weapon_id == 0 ||
            observation.cell_id == 0 || observation.player_id == 0 || time > now ||
            now - time > effect_max_age) {
            reset();
            return false;
        }
        if (!last_sample_ || time < *last_sample_ || time - *last_sample_ > effect_max_age ||
            player_id_ != observation.player_id || epoch_ != observation.epoch ||
            cell_id_ != observation.cell_id || weapon_id_ != observation.weapon_id) {
            reset();
            player_id_ = observation.player_id;
            epoch_ = observation.epoch;
            cell_id_ = observation.cell_id;
            weapon_id_ = observation.weapon_id;
        }
        last_sample_ = time;
        return true;
    }

    [[nodiscard]] bool read_scan(const auto& scan_cell, ProjectileScan& scan) const noexcept {
        return scan_cell(cell_id_, scan) && scan.count <= scan.arrows.size();
    }

    void capture_baseline(const ProjectileScan& scan) noexcept {
        has_baseline_ = true;
        baseline_.fill(0);
        for (std::size_t index = 0; index < scan.count; ++index) {
            baseline_[index] = scan.arrows[index].id;
        }
    }

    void finish(bool attached) noexcept {
        has_baseline_ = false;
        release_at_.reset();
        wait_for_detach_ = attached;
    }

    std::array<std::uint32_t, projectile_scan_limit> baseline_{};
    std::optional<MonoTime> last_sample_;
    std::optional<MonoTime> release_at_;
    std::uint64_t epoch_{};
    std::uint32_t player_id_{};
    std::uint32_t cell_id_{};
    std::uint32_t weapon_id_{};
    bool has_baseline_{};
    bool wait_for_detach_{};
};

} // namespace oblivion_adapter::native
