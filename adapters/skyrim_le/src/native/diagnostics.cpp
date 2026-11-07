#include "diagnostics.hpp"

#include <Windows.h>
#include <cstddef>
#include <filesystem>

namespace skyrim_adapter::native::diagnostics {
namespace {
SRWLOCK log_lock = SRWLOCK_INIT;
HANDLE log_file = INVALID_HANDLE_VALUE;

void append_line(const char* line, std::size_t length) noexcept {
    AcquireSRWLockExclusive(&log_lock);
    if (log_file != INVALID_HANDLE_VALUE) {
        DWORD written{};
        if (!WriteFile(log_file, line, static_cast<DWORD>(length), &written, nullptr) ||
            written != static_cast<DWORD>(length)) {
            CloseHandle(log_file);
            log_file = INVALID_HANDLE_VALUE;
        }
    }
    ReleaseSRWLockExclusive(&log_lock);
    OutputDebugStringA(line);
}
} // namespace

void detail::emit_line(LogLine& line) noexcept {
    const auto text = line.finish();
    append_line(text.data(), text.size());
}

void initialize(const std::filesystem::path& absolute_path) noexcept {
    try {
        if (!absolute_path.is_absolute()) {
            return;
        }
        AcquireSRWLockExclusive(&log_lock);
        if (log_file == INVALID_HANDLE_VALUE) {
            log_file = CreateFileW(absolute_path.c_str(),
                FILE_APPEND_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            if (log_file == INVALID_HANDLE_VALUE) {
                OutputDebugStringA("InputNext Skyrim LE: unable to create startup log.\n");
            }
        }
        ReleaseSRWLockExclusive(&log_lock);
    } catch (...) {
        OutputDebugStringA("InputNext Skyrim LE: diagnostics initialization failed; "
                           "plugin loading will continue.\n");
    }
}

} // namespace skyrim_adapter::native::diagnostics
