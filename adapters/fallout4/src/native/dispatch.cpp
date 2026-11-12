#include "dispatch.hpp"
#include "dispatch_operations.hpp"
#include "dispatch_pipeline.hpp"

#include "bindings.hpp"
#include "ui_capture.hpp"
#include "ui_capture_abi.hpp"

#include <F4SE/API.h>
#include <F4SE/Interfaces.h>
#include <REL/Utility.h>

#include <Windows.h>
#ifdef ERROR
#    undef ERROR
#endif

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace fallout4_adapter::native {

[[noreturn]] void fatal_unexpected_dispatch() noexcept {
    OutputDebugStringA(
        "FATAL: Fallout 4 native dispatch hook encountered unexpected control-flow state.\n");
    RaiseFailFastException(nullptr, nullptr, 0);
    static_cast<void>(TerminateProcess(GetCurrentProcess(), ERROR_UNHANDLED_EXCEPTION));
    __fastfail(FAST_FAIL_FATAL_APP_EXIT);
}

namespace {
std::byte module_anchor{};
NativeDispatchPipeline g_dispatch_pipeline;

[[nodiscard]] bool pin_dispatch_module() noexcept {
    HMODULE module{};
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                  GET_MODULE_HANDLE_EX_FLAG_PIN,
               reinterpret_cast<LPCWSTR>(&module_anchor),
               &module) != FALSE;
}

[[nodiscard]] bool supported_interfaces(const F4SE::TrampolineInterface* trampoline) noexcept {
    const auto* messaging = F4SE::GetMessagingInterface();
    return (trampoline != nullptr) && (messaging != nullptr) &&
           supported_ui_capture_host(REL::Version{1, 11, 240, 0},
               F4SE::GetF4SEVersion(),
               messaging->Version(),
               trampoline->Version());
}

struct ProductionDispatchPlatform {
    NativeDispatchPipeline& pipeline;

    [[nodiscard]] static std::uintptr_t image_base() noexcept { return checked_image_base(); }

    [[nodiscard]] static const F4SE::TrampolineInterface* trampoline() noexcept {
        return F4SE::GetTrampolineInterface();
    }

    [[nodiscard]] static bool bindings_available() noexcept { return native::bindings_available(); }

    [[nodiscard]] static bool interfaces_supported(
        const F4SE::TrampolineInterface* trampoline) noexcept {
        return supported_interfaces(trampoline);
    }

    [[nodiscard]] static std::byte*
    allocate_branch_pool(const F4SE::TrampolineInterface* trampoline, std::size_t size) noexcept {
        return trampoline ? static_cast<std::byte*>(trampoline->AllocateFromBranchPool(size))
                          : nullptr;
    }

    [[nodiscard]] static bool pin_module() noexcept { return pin_dispatch_module(); }

    [[nodiscard]] bool publish_originals(SeededOriginal helper,
        RemapOriginal remap,
        MainMessageLoopOriginal main_loop) const noexcept {
        if (!publish_ui_capture_originals(helper, remap)) {
            return false;
        }
        return pipeline.publish_main_original(main_loop);
    }

    [[nodiscard]] static bool precommit() noexcept { return true; }

    [[nodiscard]] static bool write_safe(std::uintptr_t address,
        std::span<const std::byte> bytes) noexcept {
        return REL::WriteSafe(address, bytes.data(), bytes.size());
    }

    [[nodiscard]] static bool flush_instruction_cache(std::uintptr_t address,
        std::size_t size) noexcept {
        return FlushInstructionCache(GetCurrentProcess(),
                   reinterpret_cast<const void*>(address),
                   size) != FALSE;
    }

    static void retain_disabled() noexcept { disable_ui_capture(); }

    [[noreturn]] static void fatal_control_flow() noexcept { fatal_unexpected_dispatch(); }

    [[nodiscard]] static std::uintptr_t helper_interceptor() noexcept {
        return reinterpret_cast<std::uintptr_t>(&ui_transition_interceptor);
    }

    [[nodiscard]] static std::uintptr_t remap_interceptor() noexcept {
        return reinterpret_cast<std::uintptr_t>(&remap_button_interceptor);
    }

    [[nodiscard]] static std::uintptr_t main_wrapper() noexcept {
        return reinterpret_cast<std::uintptr_t>(&main_message_loop_wrapper);
    }
};

using ProductionDispatchOperations = BasicNativeDispatchOperations<ProductionDispatchPlatform>;

} // namespace

NativeDispatchPipeline& dispatch_pipeline() noexcept {
    return g_dispatch_pipeline;
}

NativeDispatchInstall install_native_dispatch() noexcept {
    ProductionDispatchPlatform platform{g_dispatch_pipeline};
    ProductionDispatchOperations operations{platform};
    return g_dispatch_pipeline.install(operations);
}

bool activate_main_observation(MainObservationCallback callback) noexcept {
    return g_dispatch_pipeline.activate_main_observation(callback);
}

void main_message_loop_wrapper(RE::Main* main) {
    g_dispatch_pipeline.invoke_main(main);
}

bool is_main_observation_active() noexcept {
    return g_dispatch_pipeline.is_main_observation_active();
}

NativeDispatchInstall native_dispatch_status() noexcept {
    return g_dispatch_pipeline.status();
}

} // namespace fallout4_adapter::native
