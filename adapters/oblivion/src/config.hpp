#pragma once

#include "effect_settings.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace oblivion_adapter {

enum class ConfigStatus : std::uint8_t { loaded, missing, invalid, io_error };

struct ConfigError {
    std::size_t line{};
    std::string_view message; // Points to a static literal.
};

// Empty input selects the defaults. Invalid input leaves settings untouched.
[[nodiscard]] bool
parse_config(std::string_view contents, EffectSettings& settings, ConfigError* error = nullptr);

// A missing file selects the defaults. Parse and I/O errors leave settings untouched.
[[nodiscard]] ConfigStatus read_config_file(const std::filesystem::path& path,
    EffectSettings& settings,
    ConfigError* error = nullptr);

} // namespace oblivion_adapter
