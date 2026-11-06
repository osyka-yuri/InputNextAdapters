#include "content_observation.hpp"

#include <skyrim_adapter/content_observation.hpp>

#include <RE/A/Actor.h>
#include <RE/B/BSCoreTypes.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESGlobal.h>
#include <RE/T/TESRace.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string_view>
#include <type_traits>

namespace skyrim_adapter::native {
namespace {
using Need = detail::SurvivalNeed;
using Global = detail::SurvivalGlobal;

struct RaceBinding {
    RE::FormID local_form_id;
    std::string_view plugin;
    std::string_view editor_id;
};

constexpr RaceBinding werewolf_race_binding{
    .local_form_id = 0x000CDD84,
    .plugin = "Skyrim.esm",
    .editor_id = "WerewolfBeastRace",
};
constexpr RaceBinding vampire_lord_race_binding{
    .local_form_id = 0x0000283A,
    .plugin = "Dawnguard.esm",
    .editor_id = "DLC1VampireBeastRace",
};

struct CachedContentBindings {
    std::array<const RE::TESRace*, 2> races{};
    std::array<const RE::TESGlobal*, 4> globals{};
    bool resolved{};
};

static_assert(std::is_trivially_destructible_v<CachedContentBindings>);

[[nodiscard]] bool editor_id_matches(const RE::TESForm& form, std::string_view expected) noexcept {
    const char* const editor_id = form.GetFormEditorID();
    return editor_id != nullptr && std::string_view{editor_id} == expected;
}

[[nodiscard]] const CachedContentBindings& cached_content_bindings(
    RE::TESDataHandler& data) noexcept {
    // Content reads are admitted only on the game task after DataLoaded. These raw pointers refer to static game
    // forms, and this cache is deliberately game-task-only rather than synchronized.
    static CachedContentBindings cache{};
    if (!cache.resolved) {
        const auto* const werewolf =
            data.LookupForm<RE::TESRace>(werewolf_race_binding.local_form_id,
                werewolf_race_binding.plugin);
        if (werewolf != nullptr && editor_id_matches(*werewolf, werewolf_race_binding.editor_id)) {
            cache.races[0] = werewolf;
        }

        const auto* const vampire_lord =
            data.LookupForm<RE::TESRace>(vampire_lord_race_binding.local_form_id,
                vampire_lord_race_binding.plugin);
        if (vampire_lord != nullptr &&
            editor_id_matches(*vampire_lord, vampire_lord_race_binding.editor_id)) {
            cache.races[1] = vampire_lord;
        }

        constexpr std::array globals{Global::mode_enabled,
            Global::cold_need,
            Global::hunger_need,
            Global::exhaustion_need};
        for (std::size_t index = 0; index < globals.size(); ++index) {
            const auto binding = detail::global_binding(globals[index]);
            const auto* const global =
                data.LookupForm<RE::TESGlobal>(binding.local_form_id, binding.plugin);
            if (global == nullptr) {
                continue;
            }

            const char* const actual_editor_id = global->GetFormEditorID();
            const char actual_type_code = static_cast<char>(global->type.get());
            if (actual_editor_id != nullptr && detail::global_binding_metadata_matches(binding,
                                                   std::string_view{actual_editor_id},
                                                   actual_type_code)) {
                cache.globals[index] = global;
            }
        }
        cache.resolved = true;
    }
    return cache;
}

[[nodiscard]] std::optional<float> read_global(const CachedContentBindings& bindings,
    Global global) noexcept {
    const auto* const value = bindings.globals[static_cast<std::size_t>(global)];
    if (value == nullptr || !std::isfinite(value->value)) {
        return std::nullopt;
    }
    return value->value;
}

[[nodiscard]] std::optional<unsigned>
read_need(const CachedContentBindings& bindings, Global global, Need need) noexcept {
    const auto value = read_global(bindings, global);
    return value ? detail::need_stage(need, *value) : std::nullopt;
}

[[nodiscard]] std::optional<BeastForm> read_beast(const CachedContentBindings& bindings,
    const RE::Actor& actor) noexcept {
    const auto* const race = actor.GetRace();
    if (race == nullptr) {
        return std::nullopt;
    }
    if (bindings.races[0] == race) {
        return BeastForm::werewolf;
    }
    if (bindings.races[1] == race) {
        return BeastForm::vampire_lord;
    }
    return BeastForm::none;
}
} // namespace

ContentObservation read_content_observation(const RE::Actor& actor) noexcept {
    ContentObservation observation{};
    auto* const data = RE::TESDataHandler::GetSingleton();
    if (data == nullptr) {
        return observation;
    }

    const auto& bindings = cached_content_bindings(*data);
    observation.beast = read_beast(bindings, actor);

    const auto mode_enabled = read_global(bindings, Global::mode_enabled);
    if (!mode_enabled || *mode_enabled < 0.0F || *mode_enabled > 1.0F) {
        return observation;
    }
    observation.survival_enabled = *mode_enabled >= 0.5F;
    if (!observation.survival_enabled) {
        return observation;
    }

    observation.cold_stage = read_need(bindings, Global::cold_need, Need::cold);
    observation.hunger_stage = read_need(bindings, Global::hunger_need, Need::hunger);
    observation.exhaustion_stage = read_need(bindings, Global::exhaustion_need, Need::exhaustion);
    return observation;
}
} // namespace skyrim_adapter::native
