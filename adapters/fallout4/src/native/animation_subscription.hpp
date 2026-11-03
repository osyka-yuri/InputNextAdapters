#pragma once

#include <RE/B/BSAnimationGraphEvent.h>
#include <RE/B/BSTEvent.h>
#include <RE/P/PlayerCharacter.h>

#include <atomic>

namespace fallout4_adapter::native {
class AnimationGraphReference final {
public:
    AnimationGraphReference() noexcept = default;
    ~AnimationGraphReference();

    AnimationGraphReference(const AnimationGraphReference&) = delete;
    AnimationGraphReference& operator=(const AnimationGraphReference&) = delete;
    AnimationGraphReference(AnimationGraphReference&& other) noexcept;
    AnimationGraphReference& operator=(AnimationGraphReference&& other) noexcept;

    [[nodiscard]] RE::BShkbAnimationGraph* get() const noexcept { return graph_; }

    [[nodiscard]] explicit operator bool() const noexcept { return graph_ != nullptr; }

private:
    friend class AnimationSubscription;

    explicit AnimationGraphReference(RE::BShkbAnimationGraph* graph) noexcept : graph_(graph) {}

    void reset() noexcept;

    RE::BShkbAnimationGraph* graph_{};
};

class AnimationSubscription final {
public:
    using Event = RE::BSAnimationGraphEvent;
    using Source = RE::BSTEventSource<Event>;

    explicit AnimationSubscription(RE::BSTEventSink<Event>* sink) noexcept : sink_(sink) {}

    ~AnimationSubscription() = default;

    AnimationSubscription(const AnimationSubscription&) = delete;
    AnimationSubscription& operator=(const AnimationSubscription&) = delete;
    AnimationSubscription(AnimationSubscription&&) = delete;
    AnimationSubscription& operator=(AnimationSubscription&&) = delete;

    // Game-thread only. `player` is borrowed for this call and is never retained.
    [[nodiscard]] bool refresh(RE::PlayerCharacter* player, bool enabled);

    // Safe from any thread: callbacks stop being admitted immediately.
    void deactivate() noexcept { active_source_.store(nullptr, std::memory_order_release); }

    // Game-thread only. Failure retains the graph reference for a later retry.
    [[nodiscard]] bool detach() noexcept;

    [[nodiscard]] bool accepts(const Source* source) const noexcept {
        return (source != nullptr) && active_source_.load(std::memory_order_acquire) == source;
    }

    [[nodiscard]] bool active() const noexcept {
        return active_source_.load(std::memory_order_acquire) != nullptr;
    }

private:
    RE::BSTEventSink<Event>* sink_{};
    Source* registered_source_{};
    AnimationGraphReference graph_;
    std::atomic<Source*> active_source_;
    bool unregister_failed_{};
};
} // namespace fallout4_adapter::native
