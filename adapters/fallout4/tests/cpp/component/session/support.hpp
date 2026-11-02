#pragma once

#include <catch2/catch_test_macros.hpp>
#include <fallout4_adapter/session.hpp>
#include <input_next_effects.h>

#include <chrono>
#include <concepts>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace fallout4_adapter::session_tests {

using namespace std::chrono_literals;
using namespace fallout4_adapter;

class FakeOutput;

inline bool same_context(const input_next_effects_context_v1& left,
    const input_next_effects_context_v1& right) {
    return left.struct_size == right.struct_size && left.reserved == right.reserved &&
           left.runtime_id[0] == right.runtime_id[0] && left.runtime_id[1] == right.runtime_id[1] &&
           left.id == right.id;
}

inline bool same_target(const input_next_device_target_v1& left,
    const input_next_device_target_v1& right) {
    return left.struct_size == right.struct_size && left.reserved == right.reserved &&
           left.runtime_id[0] == right.runtime_id[0] && left.runtime_id[1] == right.runtime_id[1] &&
           left.device_key == right.device_key && left.connection_id == right.connection_id;
}

inline bool same_receipt(const input_next_effects_close_receipt_v1& left,
    const input_next_effects_close_receipt_v1& right) {
    return left.struct_size == right.struct_size && left.disposition == right.disposition &&
           same_context(left.context, right.context) && same_target(left.target, right.target) &&
           left.close_request_id == right.close_request_id &&
           left.publication_revision == right.publication_revision;
}

inline void require(bool condition, const char* message) {
    INFO(message);
    REQUIRE(condition);
}

template <typename T>
const T& require_value(const std::optional<T>& opt, const char* message) {
    if (!opt.has_value()) {
        throw std::runtime_error{message};
    }
    return *opt;
}

template <typename T>
T& require_value(std::optional<T>& opt, const char* message) {
    if (!opt.has_value()) {
        throw std::runtime_error{message};
    }
    return *opt;
}

template <typename T>
const T& require_value(std::optional<T>&&, const char*) = delete;

template <typename T>
const T& require_value(const std::optional<T>&&, const char*) = delete;

template <typename Opt>
concept CanRequireValue = requires(Opt&& opt) { require_value(static_cast<Opt&&>(opt), "test"); };

static_assert(CanRequireValue<std::optional<int>&>);
static_assert(CanRequireValue<const std::optional<int>&>);
static_assert(!CanRequireValue<std::optional<int>>);
static_assert(!CanRequireValue<std::optional<int>&&>);
static_assert(!CanRequireValue<const std::optional<int>&&>);
static_assert(
    std::is_same_v<decltype(require_value(std::declval<std::optional<int>&>(), "test")), int&>);
static_assert(
    std::is_same_v<decltype(require_value(std::declval<const std::optional<int>&>(), "test")),
        const int&>);

Observation observation(MonoTime now, Mode mode = Mode::gameplay);
Ticket activate(Session& session, MonoTime now);
std::size_t shot_pulse_count(const FakeOutput& output);

} // namespace fallout4_adapter::session_tests
