#pragma once

#include <cstdint>
#include <optional>

namespace fallout4_adapter::native {
// Optional capability: unknown until a real CurrentRadiationSourceCount event arrives.
[[nodiscard]] bool install_radiation_source() noexcept;
void invalidate_radiation_lifecycle() noexcept;
[[nodiscard]] bool enable_radiation_lifecycle() noexcept;
[[nodiscard]] std::optional<std::uint32_t> radiation_source_count() noexcept;
} // namespace fallout4_adapter::native
