#include "ui_capture_test.hpp"
#include "native/ui_capture.hpp"
#include "native/ui_capture_pipeline.hpp"

#include <RE/B/BSFixedString.h>
#include <RE/C/ControlMap.h>
#include <RE/I/INPUT_DEVICE.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace fallout4_adapter::native::ui_capture_test {

namespace {
std::atomic<ExitClobber> g_test_exit_clobber{nullptr};
std::atomic<SeededOriginal> g_test_seeded_original{nullptr};
std::atomic<RemapOriginal> g_test_remap_original{nullptr};

std::uint64_t test_helper_trampoline(std::uint8_t original_cl, std::uint64_t entry_rax) {
    if (const auto target = g_test_seeded_original.load(std::memory_order_acquire)) {
        return target(original_cl, entry_rax);
    }
    return 0;
}

bool test_remap_trampoline(RE::ControlMap* control_map,
    const RE::BSFixedString& event_name,
    RE::INPUT_DEVICE device,
    std::int32_t button) {
    if (const auto target = g_test_remap_original.load(std::memory_order_acquire)) {
        return target(control_map, event_name, device, button);
    }
    return false;
}

std::mutex g_test_pipeline_mutex;
std::shared_ptr<NativeUiCapturePipeline> g_test_pipeline{};

// Thread-local gate lease stack preserving Win32 SRWLOCK exclusive thread affinity
// and ensuring release_gate() always releases the exact pipeline instance acquired by try_gate().
thread_local std::vector<std::shared_ptr<NativeUiCapturePipeline>> t_held_gate_leases;

std::shared_ptr<NativeUiCapturePipeline> active_pipeline_shared() noexcept {
    std::scoped_lock lock(g_test_pipeline_mutex);
    if (!g_test_pipeline) {
        g_test_pipeline =
            std::make_shared<NativeUiCapturePipeline>(NativeUiCapturePipeline::InitialState{
                .generation = 1,
                .in_flight = 0,
                .closed = false,
                .original = &test_helper_trampoline,
                .remap_original = &test_remap_trampoline,
            });
    }
    return g_test_pipeline;
}
} // namespace

void reset(std::uint64_t generation) noexcept {
    g_test_exit_clobber.store(nullptr, std::memory_order_release);
    auto next = std::make_shared<NativeUiCapturePipeline>(NativeUiCapturePipeline::InitialState{
        .generation = generation,
        .in_flight = 0,
        .closed = (generation == 0),
        .original = &test_helper_trampoline,
        .remap_original = &test_remap_trampoline,
    });
    std::shared_ptr<NativeUiCapturePipeline> prev;
    {
        std::scoped_lock lock(g_test_pipeline_mutex);
        prev = std::move(g_test_pipeline);
        g_test_pipeline = std::move(next);
    }
    // prev destructor runs here outside g_test_pipeline_mutex
}

void clear_originals() noexcept {
    g_test_seeded_original.store(nullptr, std::memory_order_release);
    g_test_remap_original.store(nullptr, std::memory_order_release);
}

void set_original(SeededOriginal original) noexcept {
    g_test_seeded_original.store(original, std::memory_order_release);
}

void set_remap_original(RemapOriginal original) noexcept {
    g_test_remap_original.store(original, std::memory_order_release);
}

void set_generation(std::uint64_t generation) noexcept {
    std::shared_ptr<NativeUiCapturePipeline> prev;
    std::shared_ptr<NativeUiCapturePipeline> next;
    {
        std::scoped_lock lock(g_test_pipeline_mutex);
        const auto in_flight_count = g_test_pipeline ? g_test_pipeline->in_flight() : 0;
        next = std::make_shared<NativeUiCapturePipeline>(NativeUiCapturePipeline::InitialState{
            .generation = generation,
            .in_flight = in_flight_count,
            .closed = (generation == 0),
            .original = &test_helper_trampoline,
            .remap_original = &test_remap_trampoline,
        });
        prev = std::move(g_test_pipeline);
        g_test_pipeline = std::move(next);
    }
    // prev destructor runs here outside g_test_pipeline_mutex
}

void set_in_flight(std::uint64_t count) noexcept {
    std::shared_ptr<NativeUiCapturePipeline> prev;
    std::shared_ptr<NativeUiCapturePipeline> next;
    {
        std::scoped_lock lock(g_test_pipeline_mutex);
        const auto gen = g_test_pipeline ? g_test_pipeline->generation() : 1;
        const bool is_closed = g_test_pipeline ? g_test_pipeline->closed() : false;
        next = std::make_shared<NativeUiCapturePipeline>(NativeUiCapturePipeline::InitialState{
            .generation = (gen == 0 && !is_closed) ? 1 : gen,
            .in_flight = count,
            .closed = is_closed,
            .original = &test_helper_trampoline,
            .remap_original = &test_remap_trampoline,
        });
        prev = std::move(g_test_pipeline);
        g_test_pipeline = std::move(next);
    }
    // prev destructor runs here outside g_test_pipeline_mutex
}

void set_exit_clobber(ExitClobber clobber) noexcept {
    g_test_exit_clobber.store(clobber, std::memory_order_release);
}

std::uint64_t generation() noexcept {
    const auto pipeline = active_pipeline_shared();
    return pipeline->generation();
}

std::uint64_t in_flight() noexcept {
    const auto pipeline = active_pipeline_shared();
    return pipeline->in_flight();
}

bool closed() noexcept {
    const auto pipeline = active_pipeline_shared();
    return pipeline->closed();
}

bool try_gate() noexcept {
    const auto pipeline = active_pipeline_shared();
    // Acquire bookkeeping storage before taking the non-recursive SRWLOCK.
    try {
        t_held_gate_leases.push_back(pipeline);
    } catch (...) {
        return false;
    }
    if (pipeline->try_gate()) {
        return true;
    }
    t_held_gate_leases.pop_back();
    return false;
}

void release_gate() noexcept {
    if (!t_held_gate_leases.empty()) {
        const auto pipeline = std::move(t_held_gate_leases.back());
        t_held_gate_leases.pop_back();
        pipeline->release_gate();
    }
}

bool try_process_lock(volatile long* words, std::uint32_t thread_id) noexcept {
    return native::try_process_lock_words(words, thread_id);
}

void release_process_lock(volatile long* words) noexcept {
    native::release_process_lock_words(words);
}

bool try_read_count(volatile long* count) noexcept {
    return native::try_read_count(count);
}

void release_read_count(volatile long* count) noexcept {
    native::release_read_count(count);
}

bool project_static_fields(std::uintptr_t image,
    UiStaticOffsets offsets,
    UiStaticFields& fields) noexcept {
    return native::project_ui_static_fields(image, offsets, fields);
}

bool static_fields_eligible(std::uintptr_t image, const UiStaticFields& fields) noexcept {
    return native::ui_static_fields_eligible(image, fields);
}

bool eligible(std::uintptr_t image,
    UiStaticOffsets offsets,
    std::uint64_t expected_generation) noexcept {
    UiStaticFields fields{};
    const auto pipeline = active_pipeline_shared();
    return project_ui_static_fields(image, offsets, fields) && capture_eligible(image,
                                                                   fields,
                                                                   expected_generation,
                                                                   pipeline->generation(),
                                                                   pipeline->in_flight(),
                                                                   pipeline->closed());
}

namespace {
std::uint64_t test_ui_transition_interceptor(std::uint8_t original_cl, std::uint64_t entry_rax) {
    const auto pipeline = active_pipeline_shared();
    const auto result = invoke_ui_transition(*pipeline, original_cl, entry_rax);
    if (const auto clobber = g_test_exit_clobber.load(std::memory_order_acquire)) {
        clobber();
    }
    return result;
}
} // namespace

std::uintptr_t interceptor_address() noexcept {
    return reinterpret_cast<std::uintptr_t>(&test_ui_transition_interceptor);
}

std::uint64_t invoke(std::uint8_t original_cl, std::uint64_t entry_rax) {
    return test_ui_transition_interceptor(original_cl, entry_rax);
}

bool invoke_remap(RE::ControlMap* control_map,
    const RE::BSFixedString& event_name,
    RE::INPUT_DEVICE device,
    std::int32_t button) {
    const auto pipeline = active_pipeline_shared();
    return invoke_remap_button(*pipeline, control_map, event_name, device, button);
}

} // namespace fallout4_adapter::native::ui_capture_test
