#include "../owner.hpp"

#include <RE/B/BGSFootstepEvent.h>
#include <RE/B/BGSFootstepManager.h>
#include <RE/B/BSTEvent.h>
#include <RE/M/MenuOpenCloseEvent.h>
#include <RE/S/ScriptEventSourceHolder.h>
#include <RE/T/TESEquipEvent.h>
#include <RE/T/TESHitEvent.h>
#include <RE/T/TESPlayerBowShotEvent.h>
#include <RE/U/UI.h>

#include <SKSE/API.h>
#include <SKSE/Logger.h>

namespace skyrim_adapter::native {
void NativeOwner::install_sinks() {
    if (!action_source_attempted_) {
        action_source_attempted_ = true;
        if (auto* actions = SKSE::GetActionEventSource()) {
            actions->AddEventSink(this);
            action_registered_ = true;
        } else {
            try {
                SKSE::log::error(
                    "SKSE ActionEvent source is unavailable; retaining pinned owner inert.");
            } catch (...) {
                fallback_diagnostic(
                    "InputNext Skyrim: ActionEvent source unavailable; owner retained inert.\n");
            }
            enter_inert();
            return;
        }
    }

    auto* scripts = RE::ScriptEventSourceHolder::GetSingleton();
    auto* user_interface = RE::UI::GetSingleton();
    if (scripts && !hit_registered_) {
        scripts->AddEventSink(static_cast<RE::BSTEventSink<RE::TESHitEvent>*>(this));
        hit_registered_ = true;
    }
    if (scripts && !equip_registered_) {
        scripts->AddEventSink(static_cast<RE::BSTEventSink<RE::TESEquipEvent>*>(this));
        equip_registered_ = true;
    }
    if (scripts && !bow_shot_registered_) {
        scripts->AddEventSink(static_cast<RE::BSTEventSink<RE::TESPlayerBowShotEvent>*>(this));
        bow_shot_registered_ = true;
    }
    if (user_interface && !menu_registered_) {
        user_interface->AddEventSink(static_cast<RE::BSTEventSink<RE::MenuOpenCloseEvent>*>(this));
        menu_registered_ = true;
    }

    if (!action_registered_ || !scripts || !user_interface || !hit_registered_ ||
        !equip_registered_ || !bow_shot_registered_ || !menu_registered_) {
        try {
            SKSE::log::error(
                "A mandatory native event source is unavailable; retaining pinned owner inert.");
        } catch (...) {
            fallback_diagnostic(
                "InputNext Skyrim: mandatory event source unavailable; owner retained inert.\n");
        }
        enter_inert();
    }
}

void NativeOwner::install_footstep_sink() {
    if (footstep_registered_) {
        return;
    }
    if (auto* footsteps = RE::BGSFootstepManager::GetSingleton()) {
        footsteps->AddEventSink(static_cast<RE::BSTEventSink<RE::BGSFootstepEvent>*>(this));
        footstep_registered_ = true;
    }
}
} // namespace skyrim_adapter::native
