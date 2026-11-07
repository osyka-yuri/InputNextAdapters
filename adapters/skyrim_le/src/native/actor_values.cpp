#include "actor_values.hpp"
#include <common/ITypes.h>
#include <skse/GameFormComponents.h>

namespace skyrim_adapter::native {
__declspec(noinline) ActorValueSample sample_actor_value(ActorValueOwner& owner,
    UInt32 actor_value) noexcept {
    return {.current = owner.GetCurrent(actor_value), .maximum = owner.GetMaximum(actor_value)};
}
} // namespace skyrim_adapter::native
