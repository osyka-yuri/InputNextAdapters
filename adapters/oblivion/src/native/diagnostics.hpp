#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace oblivion_adapter::native::diagnostics {

void initialize(const std::filesystem::path& log_path) noexcept;
void write(std::string_view message) noexcept;
void shutdown() noexcept;

[[nodiscard]] std::string format_form_id(std::uint32_t form_id);

} // namespace oblivion_adapter::native::diagnostics
