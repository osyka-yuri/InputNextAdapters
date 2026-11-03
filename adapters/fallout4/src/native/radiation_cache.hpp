#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

namespace fallout4_adapter::native {
class RadiationCountCache final {
public:
    static constexpr std::uint32_t maximum_generation = (std::uint32_t{1} << 29U) - 1U;

    struct EventToken {
        std::uint32_t generation{};
    };

    struct Snapshot {
        std::uint32_t generation{};
        std::uint32_t count{};
        bool accepting{};
        bool known{};
        bool exhausted{};
    };

    explicit RadiationCountCache(std::uint32_t initial_generation = 1U) noexcept
        : state_(pack(initial_generation <= maximum_generation ? initial_generation : 1U,
              false,
              false,
              false,
              0U)) {}

    RadiationCountCache(const RadiationCountCache&) = delete;
    RadiationCountCache& operator=(const RadiationCountCache&) = delete;
    RadiationCountCache(RadiationCountCache&&) = delete;
    RadiationCountCache& operator=(RadiationCountCache&&) = delete;
    ~RadiationCountCache() = default;

    // Advances lifecycle identity and clears cached knowledge. It never wraps.
    [[nodiscard]] bool invalidate() noexcept {
        auto observed = state_.load(std::memory_order_acquire);
        for (;;) {
            const auto generation = generation_of(observed);
            if (generation == maximum_generation) {
                const auto disabled = pack(generation, false, false, true, 0U);
                if (state_.compare_exchange_weak(observed,
                        disabled,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire)) {
                    return false;
                }
                continue;
            }
            const auto next = pack(generation + 1U, false, false, exhausted_of(observed), 0U);
            if (state_.compare_exchange_weak(observed,
                    next,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                return true;
            }
        }
    }

    // Called only after the native pipeline admits a fresh gameplay observation.
    [[nodiscard]] bool enable() noexcept {
        auto observed = state_.load(std::memory_order_acquire);
        for (;;) {
            const auto generation = generation_of(observed);
            if (exhausted_of(observed)) {
                return false;
            }
            if (accepting_of(observed)) {
                return true;
            }
            const auto enabled = pack(generation, true, false, false, 0U);
            if (state_.compare_exchange_weak(observed,
                    enabled,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                return true;
            }
        }
    }

    // The callback captures this token before copying the engine scalar.
    [[nodiscard]] std::optional<EventToken> capture_event_token() const noexcept {
        const auto state = state_.load(std::memory_order_acquire);
        if (!accepting_of(state)) {
            return std::nullopt;
        }
        return EventToken{generation_of(state)};
    }

    // Single compare-exchange: stale-generation or racing events are dropped without retry.
    [[nodiscard]] bool publish(EventToken token, std::optional<std::uint32_t> count) noexcept {
        auto expected = state_.load(std::memory_order_acquire);
        if (!accepting_of(expected) || generation_of(expected) != token.generation) {
            return false;
        }
        const auto desired =
            pack(token.generation, true, count.has_value(), false, count.value_or(0U));
        return state_.compare_exchange_strong(expected,
            desired,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

    [[nodiscard]] std::optional<std::uint32_t> current_count() const noexcept {
        const auto state = state_.load(std::memory_order_acquire);
        if (!accepting_of(state) || !known_of(state)) {
            return std::nullopt;
        }
        return count_of(state);
    }

    [[nodiscard]] Snapshot snapshot() const noexcept {
        const auto state = state_.load(std::memory_order_acquire);
        return Snapshot{.generation = generation_of(state),
            .count = count_of(state),
            .accepting = accepting_of(state),
            .known = known_of(state),
            .exhausted = exhausted_of(state)};
    }

private:
    static constexpr std::uint64_t known_bit = std::uint64_t{1} << 62U;
    static constexpr std::uint64_t accepting_bit = std::uint64_t{1} << 63U;
    static constexpr std::uint64_t exhausted_bit = std::uint64_t{1} << 61U;
    static constexpr std::uint64_t generation_mask = static_cast<std::uint64_t>(maximum_generation)
                                                     << 32U;

    [[nodiscard]] static constexpr std::uint64_t pack(std::uint32_t generation,
        bool accepting,
        bool known,
        bool exhausted,
        std::uint32_t count) noexcept {
        return (static_cast<std::uint64_t>(generation) << 32U) | (accepting ? accepting_bit : 0U) |
               (known ? known_bit : 0U) | (exhausted ? exhausted_bit : 0U) | count;
    }

    [[nodiscard]] static constexpr std::uint32_t generation_of(std::uint64_t state) noexcept {
        return static_cast<std::uint32_t>((state & generation_mask) >> 32U);
    }

    [[nodiscard]] static constexpr std::uint32_t count_of(std::uint64_t state) noexcept {
        return static_cast<std::uint32_t>(state);
    }

    [[nodiscard]] static constexpr bool accepting_of(std::uint64_t state) noexcept {
        return (state & accepting_bit) != 0;
    }

    [[nodiscard]] static constexpr bool known_of(std::uint64_t state) noexcept {
        return (state & known_bit) != 0;
    }

    [[nodiscard]] static constexpr bool exhausted_of(std::uint64_t state) noexcept {
        return (state & exhausted_bit) != 0;
    }

    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
        "radiation event publication requires lock-free 64-bit atomics");
    std::atomic<std::uint64_t> state_;
};
} // namespace fallout4_adapter::native
