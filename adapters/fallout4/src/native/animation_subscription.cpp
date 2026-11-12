#include "animation_subscription.hpp"

#include "bindings.hpp"
#include "diagnostic_logging.hpp"

#include <RE/B/BSAnimationGraphManager.h>
#include <RE/B/BSSpinLock.h>
#include <RE/B/BSTSmartPointer.h>
#include <RE/I/IAnimationGraphManagerHolder.h>
#include <RE/P/PlayerCharacter.h>

#include <Windows.h>
#ifdef ERROR
#    undef ERROR
#endif

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace fallout4_adapter::native {
namespace {
constexpr std::size_t animation_source_offset = 0x68;
constexpr std::size_t graph_reference_count_offset = 0x08;

static_assert(offsetof(RE::BSAnimationGraphManager, graph) == 0x40);
static_assert(offsetof(RE::BSAnimationGraphManager, updateLock) == 0xC8);
static_assert(offsetof(RE::BSAnimationGraphManager, activeGraph) == 0xD8);

template <std::size_t Size>
[[nodiscard]] bool matches(const std::byte* image,
    std::uintptr_t rva,
    const std::array<std::uint8_t, Size>& bytes,
    std::size_t image_size) noexcept {
    return rva <= image_size && Size <= image_size - rva &&
           std::memcmp(image + rva, bytes.data(), bytes.size()) == 0;
}

[[nodiscard]] bool animation_layout_available() noexcept {
    static const bool available = [] {
        const auto base = checked_image_base();
        const auto image_size = checked_image_size();
        if (base == 0 || image_size == 0) {
            return false;
        }

        const auto* image = reinterpret_cast<const std::byte*>(base);

        constexpr std::array<std::uint8_t, 4> acquire{0xF0, 0xFF, 0x40, 0x08};
        constexpr std::array<std::uint8_t, 25> release{
            0xB8,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xF0,
            0x0F,
            0xC1,
            0x41,
            0x08,
            0x83,
            0xF8,
            0x01,
            0x75,
            0x0A,
            0x48,
            0x8B,
            0x01,
            0xBA,
            0x01,
            0x00,
            0x00,
            0x00,
            0xFF,
            0x10,
        };
        constexpr std::array<std::uint8_t, 9>
            source_offset{0x48, 0x83, 0xC1, 0x68, 0xE8, 0x3D, 0x15, 0x00, 0x00};
        const bool matched = matches(image, graph_reference_acquire_rva, acquire, image_size) &&
                             matches(image, graph_reference_release_rva, release, image_size) &&
                             matches(image, animation_source_rva, source_offset, image_size);
        if (!matched) {
            log_warn("Fallout 4 animation graph instruction signatures did not match; "
                     "compatibility not confirmed.");
            return false;
        }
        return true;
    }();
    return available;
}

} // namespace

AnimationGraphReference::~AnimationGraphReference() {
    reset();
}

AnimationGraphReference::AnimationGraphReference(AnimationGraphReference&& other) noexcept
    : graph_(std::exchange(other.graph_, nullptr)) {
}

AnimationGraphReference& AnimationGraphReference::operator=(
    AnimationGraphReference&& other) noexcept {
    if (this != &other) {
        reset();
        graph_ = std::exchange(other.graph_, nullptr);
    }
    return *this;
}

void AnimationGraphReference::reset() noexcept {
    auto* graph = std::exchange(graph_, nullptr);
    if (!graph) {
        return;
    }

    auto* count = reinterpret_cast<volatile LONG*>(
        reinterpret_cast<std::byte*>(graph) + graph_reference_count_offset);
    if (InterlockedDecrement(count) == 0) {
        void* const* const vtable = *reinterpret_cast<void* const**>(graph);
        using Destroy = void (*)(void*, std::uint32_t);
        reinterpret_cast<Destroy>(vtable[0])(graph, 1);
    }
}

bool AnimationSubscription::detach() noexcept {
    deactivate();
    if (!registered_source_) {
        graph_ = {};
        unregister_failed_ = false;
        return true;
    }

    try {
        registered_source_->UnregisterSink(sink_);
    } catch (...) {
        unregister_failed_ = true;
        return false;
    }
    registered_source_ = nullptr;
    graph_ = {};
    unregister_failed_ = false;
    return true;
}

bool AnimationSubscription::refresh(RE::PlayerCharacter* player, bool enabled) {
    if (!enabled || !player) {
        return detach();
    }
    if (!animation_layout_available()) {
        return false;
    }

    const auto* holder = static_cast<const RE::IAnimationGraphManagerHolder*>(player);
    RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
    if (!holder->GetAnimationGraphManagerImpl(manager) || !manager) {
        static_cast<void>(detach());
        return false;
    }

    AnimationGraphReference selected;
    {
        RE::BSAutoLock lock{manager->updateLock};
        if (manager->activeGraph < manager->graph.size()) {
            auto* graph = manager->graph[manager->activeGraph].get();
            if (graph) {
                auto* count = reinterpret_cast<volatile LONG*>(
                    reinterpret_cast<std::byte*>(graph) + graph_reference_count_offset);
                const auto references = InterlockedIncrement(count);
                if (references > 1) {
                    selected = AnimationGraphReference{graph};
                } else {
                    // A zero-count graph cannot be safely resurrected; undo only our increment.
                    static_cast<void>(InterlockedDecrement(count));
                }
            }
        }
    }
    if (!selected) {
        static_cast<void>(detach());
        return false;
    }

    auto* source = reinterpret_cast<Source*>(
        reinterpret_cast<std::byte*>(selected.get()) + animation_source_offset);
    if (!unregister_failed_ && registered_source_ == source && graph_.get() == selected.get()) {
        graph_ = std::move(selected);
        active_source_.store(source, std::memory_order_release);
        return true;
    }

    if (!detach()) {
        return false;
    }

    source->RegisterSink(sink_);
    graph_ = std::move(selected);
    registered_source_ = source;
    active_source_.store(source, std::memory_order_release);
    return true;
}
} // namespace fallout4_adapter::native
