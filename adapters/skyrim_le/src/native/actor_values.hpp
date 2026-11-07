#pragma once

#include "native/skse_sdk.hpp"

namespace skyrim_adapter::native {
struct ActorValueSample final {
    float current{};
    float maximum{};
};

[[nodiscard]] ActorValueSample sample_actor_value(ActorValueOwner& owner,
    UInt32 actor_value) noexcept;
} // namespace skyrim_adapter::native
