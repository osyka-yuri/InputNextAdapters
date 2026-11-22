#include "dry_fire_hook.hpp"

#include "diagnostics.hpp"
#include "native_state.hpp"

#include <Windows.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fallout_new_vegas {
namespace {
// FalloutNV 1.4.0.525 skips this Attack check for actors other than the player.
constexpr std::uintptr_t image_base = 0x00400000;
constexpr std::uintptr_t attack_sequence = 0x00893C63;
constexpr std::uintptr_t read_control = 0x00A24660;
constexpr std::array<std::uint8_t, 36> original_sequence{0x3B,
    0x05,
    0x3C,
    0xEA,
    0x1D,
    0x01,
    0x0F,
    0x94,
    0xC1,
    0x88,
    0x4D,
    0xC7,
    0x0F,
    0xB6,
    0x55,
    0xC7,
    0x85,
    0xD2,
    0x74,
    0x1C,
    0x6A,
    0x01,
    0x6A,
    0x04,
    0x8B,
    0x4D,
    0xC8,
    0xE8,
    0xDD,
    0x09,
    0x19,
    0x00,
    0x85,
    0xC0,
    0x75,
    0x0C};
constexpr std::size_t call_operand = 0x1C;
constexpr std::uintptr_t return_address = attack_sequence + call_operand + sizeof(std::uint32_t);
constexpr auto incompatible_call =
    "dry-fire feedback unavailable because the game call differs from FalloutNV 1.4.0.525";

struct InputState;
using ReadControl = std::int32_t(__thiscall*)(InputState*, std::int32_t, std::int32_t);

std::int32_t __fastcall read_empty_weapon_attack(InputState* input,
    [[maybe_unused]] void* edx,
    std::int32_t control,
    std::int32_t state) {
    const auto original = std::bit_cast<ReadControl>(read_control);
    const auto pressed = original(input, control, state);
    if (pressed != 0) {
        NativeState::instance().on_dry_fire();
    }
    return pressed;
}

std::uint8_t* verified_call_sequence() noexcept {
    const auto* module = GetModuleHandleW(nullptr);
    if (std::bit_cast<std::uintptr_t>(module) != image_base) {
        return nullptr;
    }
    auto* const code = std::bit_cast<std::uint8_t*>(attack_sequence);
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(code, &region, sizeof(region)) != sizeof(region) ||
        region.AllocationBase != module || region.State != MEM_COMMIT || region.Type != MEM_IMAGE ||
        (region.Protect != PAGE_EXECUTE_READ && region.Protect != PAGE_EXECUTE_READWRITE &&
            region.Protect != PAGE_EXECUTE_WRITECOPY) ||
        attack_sequence - std::bit_cast<std::uintptr_t>(region.BaseAddress) +
                original_sequence.size() >
            region.RegionSize ||
        std::memcmp(code, original_sequence.data(), original_sequence.size()) != 0) {
        return nullptr;
    }
    return code;
}
} // namespace

void install_dry_fire_hook() noexcept {
    auto* const code = verified_call_sequence();
    if (!code) {
        log(incompatible_call);
        return;
    }

    // A game call can reach the callback until process exit. Keep its code mapped for that lifetime.
    HMODULE pinned_module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&read_empty_weapon_attack),
            &pinned_module)) {
        log("dry-fire feedback unavailable because the plugin could not be pinned");
        return;
    }

    auto* const operand = code + call_operand;
    const auto offset = std::bit_cast<std::uintptr_t>(&read_empty_weapon_attack) - return_address;
    DWORD old_protection{};
    if (!VirtualProtect(operand, sizeof(offset), PAGE_EXECUTE_READWRITE, &old_protection)) {
        log("dry-fire feedback unavailable because the game call could not be updated");
        return;
    }

    const char* result = incompatible_call;
    if (std::memcmp(code, original_sequence.data(), original_sequence.size()) == 0) {
        // DeferredInit runs on the same game thread that executes this unaligned call.
        std::memcpy(operand, &offset, sizeof(offset));
        auto* const process = GetCurrentProcess();
        if (FlushInstructionCache(process, operand, sizeof(offset))) {
            result = "native dry-fire source ready";
        } else {
            std::memcpy(operand, original_sequence.data() + call_operand, sizeof(offset));
            static_cast<void>(FlushInstructionCache(process, operand, sizeof(offset)));
            result = "dry-fire feedback unavailable because flushing the updated game call failed";
        }
    }
    DWORD ignored{};
    if (!VirtualProtect(operand, sizeof(offset), old_protection, &ignored)) {
        log("could not restore the original protection of the dry-fire game call");
    }
    log(result);
}
} // namespace fallout_new_vegas
