#pragma once

#include <array>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace skyrim_adapter::native::diagnostics {
namespace detail {

struct Hex32 final {
    std::uint32_t value{};
};

class LogLine final {
public:
    static constexpr std::size_t capacity = 512;

    void append(std::string_view text) noexcept {
        for (const char character : text) {
            if (character == '\0' || size_ == capacity - 1) {
                return;
            }
            bytes_[size_++] = character;
        }
    }

    void append(Hex32 hex) noexcept {
        constexpr std::string_view digits{"0123456789ABCDEF"};
        for (std::size_t index = 0; index < 8; ++index) {
            const auto shift = static_cast<unsigned>((7 - index) * 4);
            append_character(digits[(hex.value >> shift) & 0x0FU]);
        }
    }

    template <std::integral Integer>
    requires(!std::same_as<std::remove_cv_t<Integer>, bool>)
    void append(Integer value) noexcept {
        std::array<char, static_cast<std::size_t>(std::numeric_limits<Integer>::digits10) + 4>
            digits{};
        const auto result = std::to_chars(digits.data(), digits.data() + digits.size(), value);
        if (result.ec == std::errc{}) {
            append(std::string_view{digits.data(),
                static_cast<std::size_t>(result.ptr - digits.data())});
        }
    }

    [[nodiscard]] std::string_view finish() noexcept {
        if (size_ == 0 || bytes_[size_ - 1] != '\n') {
            if (size_ == capacity - 1) {
                bytes_[size_ - 1] = '\n';
            } else {
                bytes_[size_++] = '\n';
            }
        }
        bytes_[size_] = '\0';
        return {bytes_.data(), size_};
    }

private:
    void append_character(char character) noexcept {
        if (size_ < capacity - 1) {
            bytes_[size_++] = character;
        }
    }

    std::array<char, capacity> bytes_{};
    std::size_t size_{};
};

void emit_line(LogLine& line) noexcept;

} // namespace detail

void initialize(const std::filesystem::path& absolute_path) noexcept;

template <typename... Parts>
void write(const Parts&... parts) noexcept {
    detail::LogLine line;
    (line.append(parts), ...);
    detail::emit_line(line);
}

} // namespace skyrim_adapter::native::diagnostics
