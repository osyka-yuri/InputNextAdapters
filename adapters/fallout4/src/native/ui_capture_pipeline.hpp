#pragma once

#include "native/ui_capture.hpp"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace fallout4_adapter::native {

[[noreturn]] void fatal_unexpected_entry() noexcept;

class NativeUiCapturePipeline {
public:
    struct InitialState {
        std::uint64_t generation{1};
        std::uint64_t in_flight{0};
        bool closed{false};
        SeededOriginal original{nullptr};
        RemapOriginal remap_original{nullptr};
    };

    NativeUiCapturePipeline() noexcept : NativeUiCapturePipeline(InitialState{}) {}

    explicit NativeUiCapturePipeline(InitialState initial) noexcept
        : transition_gate_(SRWLOCK_INIT),
          in_flight_transitions_(initial.in_flight),
          transition_generation_(initial.generation),
          capture_closed_(initial.closed),
          original_gateway_(initial.original),
          remap_original_gateway_(initial.remap_original) {}

    NativeUiCapturePipeline(const NativeUiCapturePipeline&) = delete;
    NativeUiCapturePipeline& operator=(const NativeUiCapturePipeline&) = delete;
    NativeUiCapturePipeline(NativeUiCapturePipeline&&) = delete;
    NativeUiCapturePipeline& operator=(NativeUiCapturePipeline&&) = delete;
    ~NativeUiCapturePipeline() = default;

    [[nodiscard]] bool transition_enter() noexcept {
        AcquireSRWLockExclusive(&transition_gate_);
        const auto generation = transition_generation_.load(std::memory_order_relaxed);
        const auto in_flight = in_flight_transitions_.load(std::memory_order_relaxed);
        if (capture_closed_.load(std::memory_order_relaxed) || generation == 0 ||
            generation == std::numeric_limits<std::uint64_t>::max() ||
            in_flight == std::numeric_limits<std::uint64_t>::max()) {
            capture_closed_.store(true, std::memory_order_release);
            transition_generation_.store(0, std::memory_order_release);
            ReleaseSRWLockExclusive(&transition_gate_);
            return false;
        }
        in_flight_transitions_.store(in_flight + 1, std::memory_order_release);
        transition_generation_.store(generation + 1, std::memory_order_release);
        ReleaseSRWLockExclusive(&transition_gate_);
        return true;
    }

    void transition_leave() noexcept {
        AcquireSRWLockExclusive(&transition_gate_);
        const auto in_flight = in_flight_transitions_.load(std::memory_order_relaxed);
        if (in_flight == 0) {
            capture_closed_.store(true, std::memory_order_release);
            transition_generation_.store(0, std::memory_order_release);
        } else {
            in_flight_transitions_.store(in_flight - 1, std::memory_order_release);
        }
        ReleaseSRWLockExclusive(&transition_gate_);
    }

    void close_capture_without_gate() noexcept {
        capture_closed_.store(true, std::memory_order_release);
        transition_generation_.store(0, std::memory_order_release);
    }

    void disable_ui_capture() noexcept {
        AcquireSRWLockExclusive(&transition_gate_);
        capture_closed_.store(true, std::memory_order_release);
        transition_generation_.store(0, std::memory_order_release);
        ReleaseSRWLockExclusive(&transition_gate_);
    }

    [[nodiscard]] std::uint64_t generation() const noexcept {
        if (capture_closed_.load(std::memory_order_acquire)) {
            return 0;
        }
        return transition_generation_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool publish_originals(SeededOriginal helper, RemapOriginal remap) noexcept {
        if (!helper || !remap) {
            return false;
        }
        SeededOriginal expected_helper{};
        if (!original_gateway_.compare_exchange_strong(expected_helper,
                helper,
                std::memory_order_acq_rel)) {
            return false;
        }
        RemapOriginal expected_remap{};
        return remap_original_gateway_.compare_exchange_strong(expected_remap,
            remap,
            std::memory_order_acq_rel);
    }

    [[nodiscard]] SeededOriginal original() const noexcept {
        return original_gateway_.load(std::memory_order_acquire);
    }

    [[nodiscard]] RemapOriginal remap_original() const noexcept {
        return remap_original_gateway_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool try_gate() noexcept {
        return TryAcquireSRWLockExclusive(&transition_gate_) != FALSE;
    }

    void release_gate() noexcept { ReleaseSRWLockExclusive(&transition_gate_); }

    [[nodiscard]] std::uint64_t in_flight() const noexcept {
        return in_flight_transitions_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool closed() const noexcept {
        return capture_closed_.load(std::memory_order_acquire);
    }

private:
    mutable SRWLOCK transition_gate_;
    std::atomic<std::uint64_t> in_flight_transitions_;
    std::atomic<std::uint64_t> transition_generation_;
    std::atomic<bool> capture_closed_;
    std::atomic<SeededOriginal> original_gateway_;
    std::atomic<RemapOriginal> remap_original_gateway_;
};

NativeUiCapturePipeline& ui_capture_pipeline() noexcept;

template <class Pipeline>
inline std::uint64_t
invoke_ui_transition(Pipeline& pipeline, std::uint8_t original_cl, std::uint64_t entry_rax) {
    const auto original = pipeline.original();
    if (!original) {
        fatal_unexpected_entry();
    }

    const bool tracked = pipeline.transition_enter();
    std::uint64_t returned_rax{};
    try {
        returned_rax = original(original_cl, entry_rax);
    } catch (...) {
        pipeline.close_capture_without_gate();
        throw;
    }
    if (tracked) {
        pipeline.transition_leave();
    }
    return returned_rax;
}

template <class Pipeline>
inline bool invoke_remap_button(Pipeline& pipeline,
    RE::ControlMap* control_map,
    const RE::BSFixedString& event_name,
    RE::INPUT_DEVICE device,
    std::int32_t button) {
    const auto original = pipeline.remap_original();
    if (!original) {
        fatal_unexpected_entry();
    }

    const bool tracked = pipeline.transition_enter();
    bool result{};
    try {
        result = original(control_map, event_name, device, button);
    } catch (...) {
        pipeline.close_capture_without_gate();
        throw;
    }
    if (tracked) {
        pipeline.transition_leave();
    }
    return result;
}

} // namespace fallout4_adapter::native
