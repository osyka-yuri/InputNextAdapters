#include "options_input.hpp"
#include "effect_model.hpp"
#include "input_next_core.h"

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>

namespace oblivion_adapter::native {
namespace {
constexpr auto resolve_interval = std::chrono::milliseconds{250};
// Menu bit in the normalized Core SDK gamepad state.
constexpr std::uint32_t menu_button = 1U << 4U;
} // namespace

bool foreground_is_game() noexcept {
    auto* const foreground = GetForegroundWindow();
    DWORD process_id{};
    return (foreground != nullptr) && GetWindowThreadProcessId(foreground, &process_id) != 0U &&
           process_id == GetCurrentProcessId();
}

std::uint64_t input_clock() noexcept {
    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now) || now.QuadPart < 0) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(now.QuadPart);
}

bool OptionsInput::bind(const std::filesystem::path& runtime_path, std::uint32_t slot) noexcept {
    auto* const module = GetModuleHandleW(runtime_path.c_str());
    if (!module) {
        return false;
    }
    const auto query = reinterpret_cast<pfn_input_next_runtime_query_core_api>(
        GetProcAddress(module, "input_next_runtime_query_core_api"));
    return bind(query, slot);
}

bool OptionsInput::bind(pfn_input_next_runtime_query_core_api query, std::uint32_t slot) noexcept {
    const auto* api = query ? query(INPUT_NEXT_CORE_ABI_VERSION) : nullptr;
    if (!api || api->struct_size != sizeof(*api) ||
        api->abi_version != INPUT_NEXT_CORE_ABI_VERSION || !api->resolve_target ||
        !api->read_latest) {
        return false;
    }
    slot_ = slot;
    api_ = api;
    return true;
}

void OptionsInput::resolve(MonoTime now) noexcept {
    if (now < next_resolve_) {
        return;
    }
    next_resolve_ = now + resolve_interval;
    input_next_device_selector_v1 selector{};
    selector.struct_size = sizeof(selector);
    selector.kind = INPUT_NEXT_CORE_SELECTOR_XINPUT_SLOT;
    selector.xinput_slot = slot_;
    selector.target.struct_size = sizeof(selector.target);
    input_next_device_target_v1 target{};
    target.struct_size = sizeof(target);
    if (api_->resolve_target(&selector, &target) != INPUT_NEXT_STATUS_OK) {
        return;
    }
    target_ = target;
    ++binding_;
}

void OptionsInput::poll(bool focused, MonoTime now) noexcept {
    if (!api_) {
        return;
    }
    if (!focused) {
        target_.reset();
        next_resolve_ = MonoTime::min();
        publish(std::nullopt);
        return;
    }
    if (!target_) {
        resolve(now);
        if (!target_) {
            return;
        }
    }

    input_next_input_snapshot_v1 snapshot{};
    snapshot.struct_size = sizeof(snapshot);
    const auto status = api_->read_latest(&*target_, &snapshot);
    if (status == INPUT_NEXT_STATUS_OK && snapshot.qpc != 0) {
        publish(OptionsSample{.polled_at = now,
            .binding = binding_,
            .qpc = snapshot.qpc,
            .down = (snapshot.state.buttons & menu_button) != 0});
    } else {
        publish(std::nullopt);
        if (status != INPUT_NEXT_STATUS_NO_DATA) {
            target_.reset();
        }
    }
}

void OptionsInput::publish(std::optional<OptionsSample> sample) noexcept {
    const std::scoped_lock lock(sample_mutex_);
    if (!sample && sample_) {
        // Require a fresh release even if the game thread never saw the missing report.
        ++binding_;
    }
    sample_ = sample;
}

std::optional<OptionsSample> OptionsInput::sample() const noexcept {
    const std::scoped_lock lock(sample_mutex_);
    return sample_;
}

} // namespace oblivion_adapter::native
