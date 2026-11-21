#include "diagnostics.hpp"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

namespace oblivion_adapter::native::diagnostics {
namespace {

std::mutex s_log_mutex;
HANDLE s_log_file = INVALID_HANDLE_VALUE;

bool write_exact(HANDLE file, const void* data, DWORD bytes) noexcept {
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    if (bytes == 0) {
        return true;
    }
    DWORD written = 0;
    return WriteFile(file, data, bytes, &written, nullptr) != FALSE && written == bytes;
}

void append_log(std::string_view message) noexcept {
    std::array<char, 512> stack_buf{};
    const bool fits_stack = (message.size() + 2 <= stack_buf.size());

    if (fits_stack) {
        std::memcpy(stack_buf.data(), message.data(), message.size());
        stack_buf[message.size()] = '\n';
        stack_buf[message.size() + 1] = '\0';
    }

    {
        try {
            std::scoped_lock lock(s_log_mutex);
            if (s_log_file != INVALID_HANDLE_VALUE) {
                bool write_succeeded = false;
                if (fits_stack) {
                    write_succeeded = write_exact(s_log_file,
                        stack_buf.data(),
                        static_cast<DWORD>(message.size()) + 1);
                } else {
                    write_succeeded = write_exact(s_log_file,
                                          message.data(),
                                          static_cast<DWORD>(message.size())) &&
                                      write_exact(s_log_file, "\n", 1);
                }
                if (!write_succeeded) {
                    CloseHandle(s_log_file);
                    s_log_file = INVALID_HANDLE_VALUE;
                }
            }
        } catch (...) {
            OutputDebugStringA("InputNextOblivion: failed to acquire log mutex.\n");
        }
    }

    if (fits_stack) {
        OutputDebugStringA(stack_buf.data());
    } else {
        try {
            const std::string line = std::string(message) + "\n";
            OutputDebugStringA(line.c_str());
        } catch (...) {
            OutputDebugStringA("InputNextOblivion: failed to prepare debug message.\n");
        }
    }
}

} // namespace

void initialize(const std::filesystem::path& log_path) noexcept {
    try {
        std::scoped_lock lock(s_log_mutex);
        if (s_log_file == INVALID_HANDLE_VALUE) {
            s_log_file = CreateFileW(log_path.c_str(),
                FILE_APPEND_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            if (s_log_file == INVALID_HANDLE_VALUE) {
                OutputDebugStringA("InputNextOblivion: failed to open log file.\n");
            }
        }
    } catch (...) {
        OutputDebugStringA("InputNextOblivion: diagnostics initialization failed.\n");
    }
}

void write(std::string_view message) noexcept {
    append_log(message);
}

void shutdown() noexcept {
    try {
        std::scoped_lock lock(s_log_mutex);
        if (s_log_file != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(s_log_file);
            CloseHandle(s_log_file);
            s_log_file = INVALID_HANDLE_VALUE;
        }
    } catch (...) {
        OutputDebugStringA("InputNextOblivion: failed to acquire log mutex on shutdown.\n");
    }
}

std::string format_form_id(std::uint32_t form_id) {
    std::array<char, 16> buf{};
    static_cast<void>(std::snprintf(buf.data(), buf.size(), "0x%08X", form_id));
    return {buf.data()};
}

} // namespace oblivion_adapter::native::diagnostics
