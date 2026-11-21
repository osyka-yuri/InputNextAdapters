#pragma once

#include "model.hpp"
#include "native/xobse_sdk.hpp"

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace oblivion_adapter::test {

using LoadFunction = bool(__cdecl*)(const OBSEInterface*);
using GetObservationFunction = bool(__cdecl*)(oblivion_adapter::Observation*);
using GetSessionEpochFunction = std::uint64_t(__cdecl*)();
using GetSessionStateFunction = std::uint8_t(__cdecl*)();

void require(bool condition, const char* message);

struct LoadedPlugin {
    HMODULE handle{nullptr};
    LoadFunction load_fn{nullptr};
    GetObservationFunction get_obs_fn{nullptr};
    GetSessionEpochFunction get_epoch_fn{nullptr};
    GetSessionStateFunction get_state_fn{nullptr};

    static LoadedPlugin load(const char* plugin_path);
    void unload() noexcept;
};

struct Scenario {
    const char* name;
    void (*func)(const char* plugin_path);
};

const std::vector<Scenario>& get_scenarios();
bool run_scenario(const std::string& name, const char* plugin_path);

} // namespace oblivion_adapter::test
