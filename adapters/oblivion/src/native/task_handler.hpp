#pragma once

#include "effect_model.hpp"
#include "xobse_sdk.hpp"

namespace oblivion_adapter::native {

class TaskHandler {
public:
    static bool register_task(OBSETasks2Interface* tasks) noexcept;
    static void unregister_task(OBSETasks2Interface* tasks) noexcept;

    static void on_game_tick() noexcept;
    static void record(EffectEvent event) noexcept;
    static void note_bow_attachment(PlayerCharacter* player, const EffectEvent& event) noexcept;
    [[nodiscard]] static EffectMode last_effect_mode() noexcept;

    static void reset_diagnostics() noexcept;
    static void reset_state() noexcept;

private:
    static inline Task<void>* s_task_ptr{nullptr};
};

} // namespace oblivion_adapter::native
