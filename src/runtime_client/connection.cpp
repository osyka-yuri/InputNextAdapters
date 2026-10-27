#include "inputnext_adapters/runtime_client.hpp"

#include <input_next_effects.h>

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <type_traits>
#include <utility>

namespace inputnext::adapters {
namespace {

struct module_deleter final {
    void operator()(HMODULE module) const noexcept { FreeLibrary(module); }
};

constexpr std::uint32_t kEffectsAbiVersion = INPUT_NEXT_EFFECTS_ABI_VERSION;

bool valid_api(const input_next_effects_api_v1& api) noexcept {
    return api.struct_size == sizeof(input_next_effects_api_v1) &&
           api.abi_version == kEffectsAbiVersion && api.resolve_target != nullptr &&
           api.open_context != nullptr && api.query_context != nullptr && api.submit != nullptr &&
           api.clear != nullptr && api.submit_body != nullptr && api.clear_body != nullptr &&
           api.close_context != nullptr && api.ack_close != nullptr &&
           api.query_progress != nullptr;
}

} // namespace

RuntimeClient::~RuntimeClient() noexcept {
    disconnect();
}

RuntimeClient::RuntimeClient(RuntimeClient&& other) noexcept
    : module_(std::exchange(other.module_, nullptr)),
      api_(std::exchange(other.api_, input_next_effects_api_v1{})) {
}

RuntimeClient& RuntimeClient::operator=(RuntimeClient&& other) noexcept {
    if (this != &other) {
        disconnect();
        module_ = std::exchange(other.module_, nullptr);
        api_ = std::exchange(other.api_, input_next_effects_api_v1{});
    }
    return *this;
}

ConnectResult RuntimeClient::connect(const std::filesystem::path& path) {
    if (connected()) {
        return {.error = ConnectError::already_connected, .native_error = 0};
    }

    auto loader_path = path;
    loader_path.make_preferred();
    const auto& native_path = loader_path.native();
    if (native_path.empty() || native_path.contains(L'\0') || !loader_path.is_absolute()) {
        return {.error = ConnectError::invalid_path, .native_error = ERROR_INVALID_PARAMETER};
    }

    std::unique_ptr<std::remove_pointer_t<HMODULE>, module_deleter> module{
        LoadLibraryExW(native_path.c_str(),
            nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
    if (module == nullptr) {
        return {.error = ConnectError::load_failed, .native_error = GetLastError()};
    }

    static_assert(sizeof(pfn_input_next_runtime_query_effects_api) == sizeof(FARPROC));
    const auto query = reinterpret_cast<pfn_input_next_runtime_query_effects_api>(
        GetProcAddress(module.get(), "input_next_runtime_query_effects_api"));
    if (query == nullptr) {
        const auto error = GetLastError();
        return {.error = ConnectError::query_missing, .native_error = error};
    }

    const auto* const queried_api = query(kEffectsAbiVersion);
    if (queried_api == nullptr) {
        return {.error = ConnectError::query_unavailable, .native_error = 0};
    }

    if (!valid_api(*queried_api)) {
        return {.error = ConnectError::incompatible_api, .native_error = 0};
    }

    module_ = module.release();
    api_ = *queried_api;
    return {.error = ConnectError::none, .native_error = 0};
}

void RuntimeClient::disconnect() noexcept {
    auto* const module = static_cast<HMODULE>(module_);
    module_ = nullptr;
    api_ = {};

    if (module != nullptr) {
        FreeLibrary(module);
    }
}

bool RuntimeClient::connected() const noexcept {
    return module_ != nullptr;
}

} // namespace inputnext::adapters
