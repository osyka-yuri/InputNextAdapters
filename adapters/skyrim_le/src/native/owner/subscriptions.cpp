#include "native/diagnostics.hpp"
#include "native/owner.hpp"
#include "native/skse_sdk.hpp"

namespace skyrim_adapter::native {
void NativeOwner::install_sinks() {
    if (!action_registered_) {
        if (!action_dispatcher_) {
            action_dispatcher_ = static_cast<EventDispatcher<SKSEActionEvent>*>(
                messaging_interface_->GetEventDispatcher(
                    SKSEMessagingInterface::kDispatcher_ActionEvent));
        }
        if (!action_dispatcher_) {
            diagnostics::write("action_sink=dispatcher_unavailable");
            enter_inert("action event dispatcher unavailable");
            return;
        }
        action_dispatcher_->AddEventSink(this);
        action_registered_ = true;
        diagnostics::write("action_sink=registered");
    }

    if (!hit_registered_ && g_hitEventDispatcher) {
        g_hitEventDispatcher->AddEventSink(this);
        hit_registered_ = true;
        diagnostics::write("hit_sink=registered");
    } else if (!hit_registered_ && !hit_sink_unavailable_logged_) {
        hit_sink_unavailable_logged_ = true;
        diagnostics::write("hit_sink=unavailable; retrying");
    }

    if (!menu_registered_) {
        if (auto* manager = MenuManager::GetSingleton()) {
            if (auto* dispatcher = manager->MenuOpenCloseEventDispatcher()) {
                dispatcher->AddEventSink(this);
                menu_registered_ = true;
                diagnostics::write("menu_sink=registered");
            }
        }
        if (!menu_registered_ && !menu_sink_unavailable_logged_) {
            menu_sink_unavailable_logged_ = true;
            diagnostics::write("menu_sink=unavailable; retrying");
        }
    }
}
} // namespace skyrim_adapter::native
