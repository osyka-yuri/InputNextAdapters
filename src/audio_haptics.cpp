#include <inputnext_adapters/audio_haptic_profile.hpp>
#include <inputnext_adapters/audio_haptics.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace inputnext::adapters {
namespace {
constexpr std::size_t header_size = 24;
constexpr std::size_t maximum_payload_size = 65'536;
constexpr std::size_t maximum_file_size = header_size + maximum_payload_size;
constexpr std::uint32_t bank_version = 1;
constexpr std::uint32_t maximum_record_count = 128;
static_assert(std::numeric_limits<float>::is_iec559);

std::uint8_t byte_value(std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
}

std::uint16_t read_u16(std::span<const std::byte> bytes, std::size_t& position) noexcept {
    const std::uint32_t low = byte_value(bytes[position]);
    const std::uint32_t high = byte_value(bytes[position + 1U]);
    const std::uint32_t value = low | (high << 8U);
    position += 2;
    return static_cast<std::uint16_t>(value);
}

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t& position) noexcept {
    std::uint32_t value = 0;
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
        value |= static_cast<std::uint32_t>(byte_value(bytes[position++])) << shift;
    }
    return value;
}

float read_f32(std::span<const std::byte> bytes, std::size_t& position) noexcept {
    return std::bit_cast<float>(read_u32(bytes, position));
}

bool is_name_byte(std::uint8_t value) noexcept {
    return (value >= static_cast<std::uint8_t>('A') && value <= static_cast<std::uint8_t>('Z')) ||
           (value >= static_cast<std::uint8_t>('a') && value <= static_cast<std::uint8_t>('z')) ||
           (value >= static_cast<std::uint8_t>('0') && value <= static_cast<std::uint8_t>('9')) ||
           value == static_cast<std::uint8_t>('_') || value == static_cast<std::uint8_t>('.');
}

std::uint32_t crc32(std::span<const std::byte> bytes) noexcept {
    std::uint32_t crc = 0xFFFF'FFFFU;
    for (const std::byte byte : bytes) {
        crc ^= static_cast<std::uint32_t>(byte_value(byte));
        for (unsigned int bit = 0; bit < 8U; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB8'8320U & mask);
        }
    }
    return crc ^ 0xFFFF'FFFFU;
}

AudioHapticBankResult invalid_bank() noexcept {
    return {.status = AudioHapticBankStatus::invalid, .bank = {}};
}

struct BankHeader {
    AudioHapticGame game;
    std::uint32_t record_count;
    std::span<const std::byte> payload;
    std::uint32_t expected_crc;
};

std::optional<BankHeader> read_bank_header(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() < header_size || byte_value(bytes[0]) != static_cast<std::uint8_t>('I') ||
        byte_value(bytes[1]) != static_cast<std::uint8_t>('N') ||
        byte_value(bytes[2]) != static_cast<std::uint8_t>('H') ||
        byte_value(bytes[3]) != static_cast<std::uint8_t>('B')) {
        return std::nullopt;
    }

    std::size_t position = 4;
    const std::uint32_t version = read_u32(bytes, position);
    const std::uint32_t game_tag = read_u32(bytes, position);
    const std::uint32_t record_count = read_u32(bytes, position);
    const std::uint32_t payload_size = read_u32(bytes, position);
    const std::uint32_t expected_crc = read_u32(bytes, position);
    if (version != bank_version || game_tag < 1U || game_tag > 3U || record_count == 0U ||
        record_count > maximum_record_count || payload_size > maximum_payload_size ||
        bytes.size() != header_size + static_cast<std::size_t>(payload_size)) {
        return std::nullopt;
    }

    return BankHeader{
        .game = static_cast<AudioHapticGame>(game_tag),
        .record_count = record_count,
        .payload = bytes.subspan(header_size, payload_size),
        .expected_crc = expected_crc,
    };
}

std::optional<AudioHapticEntry> read_entry(std::span<const std::byte> payload,
    std::size_t& position) {
    if (position + 2U > payload.size()) {
        return std::nullopt;
    }
    const std::uint16_t name_length = read_u16(payload, position);
    if (name_length == 0U || name_length > 96U ||
        position + static_cast<std::size_t>(name_length) > payload.size()) {
        return std::nullopt;
    }

    std::string section;
    section.reserve(name_length);
    for (std::size_t index = 0; index < name_length; ++index) {
        const std::uint8_t value = byte_value(payload[position++]);
        if (!is_name_byte(value)) {
            return std::nullopt;
        }
        section.push_back(static_cast<char>(value));
    }
    constexpr std::size_t record_value_size =
        (2U * sizeof(float)) + (2U * audio_haptic_knot_count * sizeof(float));
    if (payload.size() - position < record_value_size) {
        return std::nullopt;
    }

    AudioHapticProfile profile;
    profile.frequency = read_f32(payload, position);
    profile.second_frequency = read_f32(payload, position);
    profile.envelope.enabled = true;
    for (float& value : profile.envelope.primary) {
        value = read_f32(payload, position);
    }
    for (float& value : profile.envelope.secondary) {
        value = read_f32(payload, position);
    }
    if (!valid_audio_haptic_profile(profile)) {
        return std::nullopt;
    }
    return AudioHapticEntry{.section = std::move(section), .profile = profile};
}

bool read_entries(std::span<const std::byte> payload,
    std::uint32_t record_count,
    std::vector<AudioHapticEntry>& entries) {
    entries.reserve(record_count);
    std::size_t position = 0;
    for (std::uint32_t record = 0; record < record_count; ++record) {
        std::optional<AudioHapticEntry> entry = read_entry(payload, position);
        if (!entry || (!entries.empty() && entries.back().section >= entry->section)) {
            return false;
        }
        entries.push_back(std::move(*entry));
    }
    return position == payload.size();
}

} // namespace

AudioHapticBankResult parse_audio_haptic_bank(std::span<const std::byte> bytes) {
    const std::optional<BankHeader> header = read_bank_header(bytes);
    if (!header || crc32(header->payload) != header->expected_crc) {
        return invalid_bank();
    }

    try {
        AudioHapticBank bank;
        bank.game = header->game;
        if (!read_entries(header->payload, header->record_count, bank.entries)) {
            return invalid_bank();
        }
        return {.status = AudioHapticBankStatus::loaded, .bank = std::move(bank)};
    } catch (...) {
        return invalid_bank();
    }
}

AudioHapticBankResult load_audio_haptic_bank(const std::filesystem::path& path) noexcept {
    try {
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if (error) {
            return {.status = AudioHapticBankStatus::io_error, .bank = {}};
        }
        if (!exists) {
            return {.status = AudioHapticBankStatus::not_found, .bank = {}};
        }
        const std::uintmax_t file_size = std::filesystem::file_size(path, error);
        if (error) {
            return {.status = AudioHapticBankStatus::io_error, .bank = {}};
        }
        if (file_size > maximum_file_size) {
            return invalid_bank();
        }

        std::vector<std::byte> bytes(static_cast<std::size_t>(file_size));
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            return {.status = AudioHapticBankStatus::io_error, .bank = {}};
        }
        if (!bytes.empty()) {
            stream.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            if (!stream) {
                return {.status = AudioHapticBankStatus::io_error, .bank = {}};
            }
        }
        if (stream.peek() != std::char_traits<char>::eof()) {
            return invalid_bank();
        }
        if (stream.bad()) {
            return {.status = AudioHapticBankStatus::io_error, .bank = {}};
        }
        return parse_audio_haptic_bank(bytes);
    } catch (...) {
        return {.status = AudioHapticBankStatus::io_error, .bank = {}};
    }
}

} // namespace inputnext::adapters
