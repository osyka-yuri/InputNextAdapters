#pragma once

#include <skyrim_adapter/effect_settings.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace skyrim_adapter::native {
struct NativeConfig {
    unsigned slot{};
    float trigger_scale{1.0F};
    float rumble_scale{1.0F};
    skyrim_adapter::EffectSettings effects{};
};

enum class ConfigStatus : std::uint8_t { loaded, missing, invalid, io_error };

struct ConfigError {
    std::size_t line{};
    std::string section;
    std::string key;
    std::string_view message; // Always points to a static literal.
};

[[nodiscard]] bool
parse_config(std::string_view contents, NativeConfig& config, ConfigError* error = nullptr);
[[nodiscard]] ConfigStatus read_config_file(const std::filesystem::path& path,
    NativeConfig& config,
    ConfigError* error = nullptr);
} // namespace skyrim_adapter::native
