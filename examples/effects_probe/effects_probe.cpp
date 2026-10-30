#include <inputnext_adapters/runtime_client.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

int run_probe(int argc, const wchar_t* const* argv) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--help") {
        std::wcout << L"Usage: effects_probe <absolute-runtime-dll-path>\n"
                   << L"Loads the selected runtime and inspects slots 0..3. It never submits or "
                      L"clears an effect.\n";
        return 0;
    }

    if (argc != 2) {
        std::wcerr << L"Pass one absolute path to a trusted InputNext runtime DLL. Use --help for "
                      L"details.\n";
        return 2;
    }

    inputnext::adapters::RuntimeClient client;
    const auto result = client.connect(std::filesystem::path(argv[1]));
    if (!result) {
        std::wcerr << L"Runtime connection failed (error "
                   << static_cast<std::uint32_t>(result.error) << L", native "
                   << result.native_error << L").\n";
        return 1;
    }

    std::wcout << L"Connected. Inspecting target availability; no effect request will be sent.\n";
    for (std::uint32_t slot = 0; slot != 4; ++slot) {
        input_next_device_selector_v1 selector{};
        selector.struct_size = sizeof(selector);
        selector.target.struct_size = sizeof(selector.target);
        selector.kind = INPUT_NEXT_CORE_SELECTOR_XINPUT_SLOT;
        selector.xinput_slot = slot;
        input_next_device_target_v1 target{};
        input_next_effects_capabilities_v1 capabilities{};
        const auto status = client.resolve_target(selector, target, capabilities);
        if (status != INPUT_NEXT_STATUS_OK) {
            std::wcout << L"slot " << slot << L": status " << status << L"\n";
            continue;
        }

        const bool body_lease_available =
            (capabilities.feature_flags & INPUT_NEXT_EFFECTS_CAP_BODY_LEASE) != 0;
        const bool hd_ready_flag =
            (capabilities.feature_flags & INPUT_NEXT_EFFECTS_CAP_HD_READY) != 0;
        const bool hd_pcm_ready = hd_ready_flag && capabilities.audio_instance_id != 0;
        std::wcout << L"slot " << slot << L": device key " << target.device_key << L", connection "
                   << target.connection_id << L", channels 0x" << std::hex
                   << capabilities.supported_channels << L", feature flags 0x"
                   << capabilities.feature_flags << std::dec << L", body lease "
                   << (body_lease_available ? L"available" : L"unavailable") << L", HD PCM "
                   << (hd_pcm_ready ? L"ready" : L"not ready") << L" (instance "
                   << capabilities.audio_instance_id << L")\n";
    }

    client.disconnect();
    return 0;
}

} // namespace

extern "C" int wmain(int argc, wchar_t* const* argv) noexcept {
    try {
        return run_probe(argc, argv);
    } catch (const std::exception& error) {
        static_cast<void>(std::fputs("effects_probe fatal error: ", stderr));
        static_cast<void>(std::fputs(error.what(), stderr));
        static_cast<void>(std::fputc('\n', stderr));
        return 1;
    } catch (...) {
        static_cast<void>(
            std::fputs("effects_probe fatal error: unexpected unknown exception\n", stderr));
        return 1;
    }
}
