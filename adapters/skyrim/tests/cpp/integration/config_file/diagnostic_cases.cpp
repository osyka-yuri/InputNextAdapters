#include "config/config_test_support.hpp"
#include "config_file_cases.hpp"
#include "config_file_support.hpp"
#include "support/native_test_checks.hpp"
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <skyrim_adapter/native/config.hpp>
#include <string>
#include <system_error>

namespace skyrim_adapter::tests::config_file {
void invalid_file_diagnostic_ownership() {
    using namespace skyrim_adapter::native;
    TemporaryConfigDirectory temporary;
    check(temporary.create(), "temporary directory is available for config diagnostic checks");
    if (!temporary) {
        return;
    }
    const auto invalid_path = temporary.file("invalid.ini");
    {
        std::ofstream invalid_file(invalid_path, std::ios::binary);
        check(static_cast<bool>(invalid_file), "temporary invalid config can be written");
        if (!invalid_file) {
            return;
        }
        invalid_file << "[OwnedDiagnosticSection]\n";
    }
    ConfigError error{.line = 99,
        .section = "stale section",
        .key = "stale key",
        .message = "stale message"};
    const auto config = std::make_unique<NativeConfig>();
    config->slot = 2;
    config->trigger_scale = 0.5F;
    config->rumble_scale = 0.75F;
    const auto before_invalid = std::make_unique<NativeConfig>(*config);
    const auto invalid_status = read_config_file(invalid_path, *config, &error);
    check(invalid_status == ConfigStatus::invalid && error.line == 1 &&
              error.section == "OwnedDiagnosticSection" && error.key.empty() &&
              error.message == "Unknown section name.",
        "file-read diagnostic owns section text after its input buffer is destroyed");
    check(skyrim_adapter::tests::config::same_config(*config, *before_invalid),
        "invalid file read leaves the caller config unchanged");
}

void missing_file_defaults_and_diagnostic_reset() {
    using namespace skyrim_adapter::native;
    TemporaryConfigDirectory temporary;
    check(temporary.create(), "temporary directory is available for config missing-file check");
    if (!temporary) {
        return;
    }
    const auto missing_path = temporary.file("missing.ini");
    ConfigError error{.line = 99,
        .section = "stale section",
        .key = "stale key",
        .message = "stale message"};
    const auto config = std::make_unique<NativeConfig>();
    config->slot = 2;
    config->trigger_scale = 0.5F;
    config->rumble_scale = 0.75F;
    const auto missing_status = read_config_file(missing_path, *config, &error);
    const auto expected_default = std::make_unique<NativeConfig>();
    check(missing_status == ConfigStatus::missing && error.line == 0 && error.section.empty() &&
              error.key.empty() && error.message.empty() &&
              skyrim_adapter::tests::config::same_config(*config, *expected_default),
        "missing file resets diagnostics and restores neutral defaults");
}

void directory_io_error_preserves_state() {
    using namespace skyrim_adapter::native;
    std::error_code filesystem_error;
    TemporaryConfigDirectory temporary;
    check(temporary.create(), "temporary directory is available for config I/O error check");
    if (!temporary) {
        return;
    }
    const auto directory_path = temporary.file("io-error.directory");
    const bool directory_created =
        std::filesystem::create_directory(directory_path, filesystem_error);
    check(directory_created && !filesystem_error,
        "temporary directory can be created for I/O error check");
    if (!directory_created || filesystem_error) {
        return;
    }
    ConfigError error{.line = 99,
        .section = "stale section",
        .key = "stale key",
        .message = "stale message"};
    const auto config = std::make_unique<NativeConfig>();
    config->slot = 2;
    config->trigger_scale = 0.5F;
    config->rumble_scale = 0.75F;
    const auto before_io = std::make_unique<NativeConfig>(*config);
    const auto io_status = read_config_file(directory_path, *config, &error);
    check(io_status == ConfigStatus::io_error && error.line == 0 && error.section.empty() &&
              error.key.empty() && error.message.empty(),
        "I/O failure clears stale diagnostics");
    check(skyrim_adapter::tests::config::same_config(*config, *before_io),
        "I/O failure leaves the caller config unchanged");
}

void successful_file_read_clears_diagnostic(const std::filesystem::path& valid_path) {
    using namespace skyrim_adapter::native;
    ConfigError error{.line = 99,
        .section = "stale section",
        .key = "stale key",
        .message = "stale message"};
    const auto config = std::make_unique<NativeConfig>();
    const auto success_status = read_config_file(valid_path, *config, &error);
    check(success_status == ConfigStatus::loaded && error.line == 0 && error.section.empty() &&
              error.key.empty() && error.message.empty(),
        "successful file read clears stale diagnostics after earlier failures");
}

void boundary_file_at_64k_limit_is_accepted() {
    using namespace skyrim_adapter::native;
    constexpr std::size_t limit_bytes = std::size_t{64} * 1024;
    TemporaryConfigDirectory temporary;
    check(temporary.create(), "temporary directory is available for boundary config check");
    if (!temporary) {
        return;
    }
    const auto valid_boundary_path = temporary.file("boundary_64k.ini");
    {
        std::ofstream file(valid_boundary_path, std::ios::binary);
        check(static_cast<bool>(file), "temporary 64 KiB config can be written");
        if (!file) {
            return;
        }
        std::string content = "[InputNextSkyrim]\nControllerSlot=1\n; ";
        content.append(limit_bytes - content.size() - 1, 'x');
        content.push_back('\n');
        check(content.size() == limit_bytes, "boundary file content is exactly 64 KiB");
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
    ConfigError error{.line = 99,
        .section = "stale section",
        .key = "stale key",
        .message = "stale message"};
    const auto config = std::make_unique<NativeConfig>();
    const auto status = read_config_file(valid_boundary_path, *config, &error);
    check(status == ConfigStatus::loaded && config->slot == 1 && error.line == 0 &&
              error.section.empty() && error.key.empty() && error.message.empty(),
        "valid file at exactly 64 KiB boundary loads successfully and clears stale diagnostics");
}

void oversized_file_rejected_transactionally() {
    using namespace skyrim_adapter::native;
    constexpr std::size_t oversized_bytes = (std::size_t{64} * 1024) + 1;
    TemporaryConfigDirectory temporary;
    check(temporary.create(), "temporary directory is available for oversized config check");
    if (!temporary) {
        return;
    }
    const auto oversized_path = temporary.file("oversized_64k_plus_1.ini");
    {
        std::ofstream file(oversized_path, std::ios::binary);
        check(static_cast<bool>(file), "temporary oversized config can be written");
        if (!file) {
            return;
        }
        std::string content = "[InputNextSkyrim]\nControllerSlot=1\n; ";
        content.append(oversized_bytes - content.size() - 1, 'x');
        content.push_back('\n');
        check(content.size() == oversized_bytes,
            "oversized file content is exactly 64 KiB + 1 byte");
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
    ConfigError error{.line = 99,
        .section = "stale section",
        .key = "stale key",
        .message = "stale message"};
    const auto config = std::make_unique<NativeConfig>();
    config->slot = 2;
    config->trigger_scale = 0.5F;
    config->rumble_scale = 0.75F;
    const auto before = std::make_unique<NativeConfig>(*config);
    const auto status = read_config_file(oversized_path, *config, &error);
    check(status == ConfigStatus::invalid && error.line == 0 && error.section.empty() &&
              error.key.empty() && error.message == "configuration exceeds 64 KiB",
        "oversized file at 64 KiB + 1 byte is rejected with expected diagnostic");
    check(skyrim_adapter::tests::config::same_config(*config, *before),
        "oversized file read preserves the original configuration");
}
} // namespace skyrim_adapter::tests::config_file
