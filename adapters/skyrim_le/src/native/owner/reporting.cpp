#include "native/diagnostics.hpp"
#include "native/observation.hpp"
#include "native/owner.hpp"
#include "native/translators.hpp"

#include <atomic>
#include <cstdint>
#include <string_view>

namespace skyrim_adapter::native {
namespace {
void log_first_value(std::atomic<std::uint32_t>& seen,
    std::uint32_t value,
    const char* name) noexcept {
    if (value >= 32) {
        return;
    }
    const auto bit = std::uint32_t{1} << value;
    if ((seen.fetch_or(bit, std::memory_order_relaxed) & bit) == 0) {
        diagnostics::write(name, "=", static_cast<unsigned>(value));
    }
}

[[nodiscard]] const char* menu_reason_name(detail::MenuBlockReason reason) noexcept {
    switch (reason) {
    case detail::MenuBlockReason::missing_instance:
        return "missing_instance";
    case detail::MenuBlockReason::pause:
        return "pause";
    case detail::MenuBlockReason::cursor:
        return "cursor";
    case detail::MenuBlockReason::pause_and_cursor:
        return "pause_and_cursor";
    case detail::MenuBlockReason::snapshot_unreliable:
        return "snapshot_unreliable";
    case detail::MenuBlockReason::manager_unavailable:
        return "manager_unavailable";
    case detail::MenuBlockReason::clear:
    default:
        return "clear";
    }
}

[[nodiscard]] const char* vibration_preference_name(
    detail::VibrationPreference preference) noexcept {
    switch (preference) {
    case detail::VibrationPreference::invalid:
        return "invalid";
    case detail::VibrationPreference::disabled:
        return "disabled";
    case detail::VibrationPreference::enabled:
        return "enabled";
    case detail::VibrationPreference::missing:
    default:
        return "missing";
    }
}

} // namespace

void NativeOwner::record_output_diagnostics() noexcept {
    const auto current = coordinator_.diagnostics();
    log_first_value(output_status_seen_,
        static_cast<std::uint32_t>(current.last_status),
        "output_status");
    log_first_value(output_route_seen_, current.accepted_body_route, "body_route");
    log_first_value(output_fallback_seen_, current.body_fallback_reason, "body_fallback");
    log_first_value(output_owner_seen_, current.body_owner_present ? 1U : 0U, "body_owner_present");
}

void NativeOwner::record_observation_diagnostics(
    const detail::ObservationDiagnostics& current) noexcept {
    if (!menu_diagnostics_seen_ || current.menu != last_menu_diagnostics_) {
        if (menu_diagnostics_logged_ < 16) {
            if (current.menu.clear) {
                diagnostics::write("menu_state=clear reason=clear flags=0x",
                    diagnostics::detail::Hex32{current.menu.flags});
            } else if (current.menu.name_length != 0) {
                diagnostics::write("menu_state=blocking name=",
                    std::string_view{current.menu.name.data(), current.menu.name_length},
                    " reason=",
                    menu_reason_name(current.menu.reason),
                    " flags=0x",
                    diagnostics::detail::Hex32{current.menu.flags});
            } else {
                diagnostics::write("menu_state=blocked reason=",
                    menu_reason_name(current.menu.reason),
                    " flags=0x",
                    diagnostics::detail::Hex32{current.menu.flags});
            }
            ++menu_diagnostics_logged_;
        }
        last_menu_diagnostics_ = current.menu;
        menu_diagnostics_seen_ = true;
    }

    const auto vibration = static_cast<std::uint8_t>(current.vibration);
    const auto bit = static_cast<std::uint8_t>(std::uint8_t{1} << vibration);
    if ((vibration_states_seen_ & bit) == 0) {
        diagnostics::write("body_vibration_preference=",
            vibration_preference_name(current.vibration));
        vibration_states_seen_ = static_cast<std::uint8_t>(vibration_states_seen_ | bit);
    }
}
} // namespace skyrim_adapter::native
