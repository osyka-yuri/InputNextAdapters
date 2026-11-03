#include "radiation_source.hpp"

#include "led_bindings.hpp"
#include "radiation_cache.hpp"

#include <RE/B/BSTEvent.h>
#include <RE/C/CurrentRadiationSourceCount.h>

#include <atomic>
#include <cstdint>
#include <new>
#include <optional>

namespace fallout4_adapter::native {
namespace {
class RadiationCountSink final : public RE::BSTEventSink<RE::CurrentRadiationSourceCount> {
public:
    explicit RadiationCountSink(RadiationCountCache& cache) noexcept : cache_(cache) {}

    RE::BSEventNotifyControl ProcessEvent(const RE::CurrentRadiationSourceCount& event,
        [[maybe_unused]] RE::BSTEventSource<RE::CurrentRadiationSourceCount>* source) override {
        // Notify invokes sinks while holding the source spin lock. This callback only copies one
        // scalar and performs a single lock-free CAS; it never touches engine state or output.
        const auto token = cache_.capture_event_token();
        if (!token) {
            return RE::BSEventNotifyControl::kContinue;
        }
        std::optional<std::uint32_t> value;
        if (event.optionalValue.has_value()) {
            value = event.optionalValue.value();
        }
        static_cast<void>(cache_.publish(*token, value));
        return RE::BSEventNotifyControl::kContinue;
    }

private:
    RadiationCountCache& cache_;
};

RadiationCountCache* radiation_cache_instance() noexcept {
    // Intentionally process/module lifetime. The global event source deletes itself at shutdown;
    // keeping both sink and scalar cache alive avoids dangling sink callbacks.
    static auto* cache = new (std::nothrow) RadiationCountCache{};
    return cache;
}

RadiationCountSink* radiation_sink_instance(RadiationCountCache& cache) noexcept {
    static auto* sink = new (std::nothrow) RadiationCountSink{cache};
    return sink;
}

std::atomic<bool>& sink_registered() noexcept {
    static auto* registered = new (std::nothrow) std::atomic<bool>{false};
    static std::atomic<bool> fallback{false};
    return registered ? *registered : fallback;
}
} // namespace

bool install_radiation_source() noexcept {
    auto& registered = sink_registered();
    if (registered.load(std::memory_order_acquire)) {
        return true;
    }
    auto* cache = radiation_cache_instance();
    if (!cache) {
        return false;
    }
    auto* sink = radiation_sink_instance(*cache);
    if (!sink || !register_radiation_count_sink(sink)) {
        return false;
    }
    registered.store(true, std::memory_order_release);
    return true;
}

void invalidate_radiation_lifecycle() noexcept {
    if (auto* cache = radiation_cache_instance()) {
        static_cast<void>(cache->invalidate());
    }
}

bool enable_radiation_lifecycle() noexcept {
    if (auto* cache = radiation_cache_instance()) {
        return cache->enable();
    }
    return false;
}

std::optional<std::uint32_t> radiation_source_count() noexcept {
    if (const auto* cache = radiation_cache_instance()) {
        return cache->current_count();
    }
    return std::nullopt;
}
} // namespace fallout4_adapter::native
