#pragma once

#include <fallout4_adapter/model.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace fallout4_adapter {
struct Config {
    PolicyConfig policy{};
    std::uint32_t slot{};
    bool diagnostics{};
};

enum class ConfigStatus : std::uint8_t { loaded, missing, invalid, io_error };

struct ConfigError {
    std::size_t line{};
    std::string_view message;
};

// Invalid input leaves the caller's configuration unchanged.
[[nodiscard]] ConfigStatus
parse_config(std::string_view text, Config& config, ConfigError* error = nullptr) noexcept;
[[nodiscard]] ConfigStatus read_config(const std::filesystem::path& path,
    Config& config,
    ConfigError* error = nullptr) noexcept;
} // namespace fallout4_adapter
