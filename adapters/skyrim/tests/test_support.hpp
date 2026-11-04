#pragma once

#include "skyrim_adapter/model.hpp"

#include <concepts>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>
#include <type_traits>

namespace skyrim_adapter::test {

[[noreturn]] inline void test_fail(std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

template <typename T>
const T& require_value(const std::optional<T>& opt,
    std::string_view message = "expected engaged optional") {
    if (!opt.has_value()) {
        test_fail(message);
    }
    return *opt;
}

template <typename T>
T& require_value(std::optional<T>& opt, std::string_view message = "expected engaged optional") {
    if (!opt.has_value()) {
        test_fail(message);
    }
    return *opt;
}

template <typename T>
const T& require_value(std::optional<T>&&, std::string_view = "expected engaged optional") = delete;

template <typename T>
const T& require_value(const std::optional<T>&&,
    std::string_view = "expected engaged optional") = delete;

inline WorldObservation& require_world(Observation& obs) {
    return require_value(obs.world, "observation world is missing");
}

inline const WorldObservation& require_world(const Observation& obs) {
    return require_value(obs.world, "observation world is missing");
}

void require_world(Observation&&) = delete;
void require_world(const Observation&&) = delete;

inline WorldLocation& require_location(WorldObservation& world) {
    return require_value(world.location, "world location is missing");
}

inline const WorldLocation& require_location(const WorldObservation& world) {
    return require_value(world.location, "world location is missing");
}

void require_location(WorldObservation&&) = delete;
void require_location(const WorldObservation&&) = delete;

template <typename Opt>
concept CanRequireValue = requires(Opt&& opt) { require_value(static_cast<Opt&&>(opt)); };

static_assert(CanRequireValue<std::optional<int>&>);
static_assert(CanRequireValue<const std::optional<int>&>);
static_assert(!CanRequireValue<std::optional<int>>);
static_assert(!CanRequireValue<std::optional<int>&&>);
static_assert(!CanRequireValue<const std::optional<int>&&>);
static_assert(std::is_same_v<decltype(require_value(std::declval<std::optional<int>&>())), int&>);
static_assert(
    std::is_same_v<decltype(require_value(std::declval<const std::optional<int>&>())), const int&>);

} // namespace skyrim_adapter::test
