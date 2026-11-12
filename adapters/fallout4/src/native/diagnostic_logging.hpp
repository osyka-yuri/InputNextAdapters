#pragma once

#include <REX/LOG.h>

#include <Windows.h>
#ifdef ERROR
#    undef ERROR
#endif

#include <format>
#include <utility>

namespace fallout4_adapter::native {

namespace detail {
inline void fallback_diagnostic_log() noexcept {
    OutputDebugStringA("InputNextFallout4: diagnostic logging failed.\n");
}
} // namespace detail

template <class... Args>
void log_warn(std::format_string<Args...> fmt, Args&&... args) noexcept {
    try {
        REX::WARN{fmt, std::forward<Args>(args)...};
    } catch (...) {
        detail::fallback_diagnostic_log();
    }
}

template <class... Args>
void log_error(std::format_string<Args...> fmt, Args&&... args) noexcept {
    try {
        REX::ERROR{fmt, std::forward<Args>(args)...};
    } catch (...) {
        detail::fallback_diagnostic_log();
    }
}

} // namespace fallout4_adapter::native
