#pragma once

#include "effect_model.hpp"

#include <input_next_core.h>

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <utility>

namespace oblivion_adapter::native {

[[nodiscard]] bool foreground_is_game() noexcept;
[[nodiscard]] std::uint64_t input_clock() noexcept;

struct OptionsSample {
    MonoTime polled_at;
    std::uint64_t binding;
    std::uint64_t qpc;
    bool down;
};

// Resolve and sample the normalized InputNext Menu button off the game thread.
class OptionsInput final {
public:
    // The effects runtime must keep the module loaded for this object's lifetime.
    bool bind(const std::filesystem::path& runtime_path, std::uint32_t slot) noexcept;
    bool bind(pfn_input_next_runtime_query_core_api query, std::uint32_t slot) noexcept;

    [[nodiscard]] bool available() const noexcept { return api_ != nullptr; }

    void poll(bool focused, MonoTime now) noexcept;
    [[nodiscard]] std::optional<OptionsSample> sample() const noexcept;

private:
    void resolve(MonoTime now) noexcept;
    void publish(std::optional<OptionsSample> sample) noexcept;

    const input_next_core_api_v1* api_{};
    std::uint32_t slot_{};
    std::optional<input_next_device_target_v1> target_;
    std::uint64_t binding_{};
    MonoTime next_resolve_{MonoTime::min()};
    mutable std::mutex sample_mutex_;
    std::optional<OptionsSample> sample_;
};

// Require a released report before accepting a press for a new binding.
class OptionsPress final {
public:
    void reset(std::uint64_t after_qpc) noexcept {
        after_qpc_ = after_qpc;
        binding_ = 0;
        armed_ = false;
    }

    [[nodiscard]] bool take(const std::optional<OptionsSample>& sample, MonoTime now) noexcept {
        if (!sample || sample->polled_at > now || now - sample->polled_at > effect_max_age) {
            armed_ = false;
            return false;
        }
        if (sample->binding != binding_) {
            binding_ = sample->binding;
            armed_ = false;
        }
        const bool was_armed = std::exchange(armed_, !sample->down);
        return was_armed && sample->down && sample->qpc > after_qpc_;
    }

private:
    std::uint64_t after_qpc_{};
    std::uint64_t binding_{};
    bool armed_{};
};

enum class PauseContext : std::uint8_t { unavailable, gameplay, settings_menu };

[[nodiscard]] constexpr const char* escape_command(PauseContext context) noexcept {
    // DIK_ESCAPE is scan code 1. Menus use xOBSE's buffered input command.
    switch (context) {
    case PauseContext::gameplay:
        return "TapKey 1";
    case PauseContext::settings_menu:
        return "MenuTapKey 1";
    case PauseContext::unavailable:
        return nullptr;
    }
    return nullptr;
}

} // namespace oblivion_adapter::native
