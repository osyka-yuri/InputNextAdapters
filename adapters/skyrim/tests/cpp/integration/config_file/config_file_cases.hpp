#pragma once
#include <filesystem>

namespace skyrim_adapter::tests::config_file {
void shipped_config_parsing_and_defaults(const std::filesystem::path& working_path,
    const std::filesystem::path& reference_path);
void working_config_catalogue(const std::filesystem::path& working_path);
void reference_config_catalogue(const std::filesystem::path& reference_path);
void invalid_file_diagnostic_ownership();
void missing_file_defaults_and_diagnostic_reset();
void directory_io_error_preserves_state();
void successful_file_read_clears_diagnostic(const std::filesystem::path& valid_path);
void boundary_file_at_64k_limit_is_accepted();
void oversized_file_rejected_transactionally();
} // namespace skyrim_adapter::tests::config_file
