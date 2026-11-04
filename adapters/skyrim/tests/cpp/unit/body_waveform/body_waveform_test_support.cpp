#include "body_waveform_test_support.hpp"
#include "skyrim_adapter/model.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace skyrim_adapter::body_waveform_tests {
namespace {
int failures{};
}

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

int failure_count() noexcept {
    return failures;
}

MonoTime at(std::int64_t milliseconds) {
    return MonoTime{} + std::chrono::milliseconds{milliseconds};
}

BodyCue cue(BodyCueKind kind, std::uint64_t id, std::chrono::milliseconds duration, float scale) {
    BodyCue result{};
    result.kind = kind;
    result.started_at = at(0);
    result.duration_ms = static_cast<std::uint32_t>(duration.count());
    result.stable_id = id;
    result.scale = scale;
    return result;
}

} // namespace skyrim_adapter::body_waveform_tests
