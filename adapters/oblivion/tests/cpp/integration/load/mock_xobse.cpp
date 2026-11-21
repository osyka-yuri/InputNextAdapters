#include "mock_xobse.hpp"
#include "native/xobse_sdk.hpp"

#include <algorithm>
#include <cstring>

namespace oblivion_adapter::test {

// Match xOBSE's EventCallback::Equals, which ignores native function pointers.
bool MockEventCallback::equals(const MockEventCallback& rhs) const noexcept {
    return object == rhs.object && source == rhs.source && callingObj == rhs.callingObj;
}

MockEnvironment::MockEnvironment() {
    reset();
}

MockEnvironment& MockEnvironment::instance() {
    static MockEnvironment s_env;
    return s_env;
}

void MockEnvironment::reset() noexcept {
    has_event_mgr = true;
    has_tasks2 = true;
    has_messaging = true;
    has_serialization = true;
    task_enqueue_result = true;
    listener_register_result = true;
    register_event_calls = 0;
    register_event_names.clear();
    unregister_event_calls = 0;
    enqueue_task_calls = 0;
    remove_task_calls = 0;
    register_listener_calls = 0;
    set_newgame_callback_calls = 0;
    handle = 1;
    registered_listener_sender.clear();
    registered_msg_callback = nullptr;
    enqueued_task_func = nullptr;
    active_task_ptr = nullptr;
    active_task.reset();
    registered_damage_func = nullptr;
    registered_newgame_callback = nullptr;
    xobse_callbacks.clear();

    event_mgr.RegisterEvent = &mock_register_event;
    event_mgr.UnregisterEvent = &mock_unregister_event;
    event_mgr.IsEventRegistered = &mock_is_event_registered;

    tasks2.version = OBSETasks2Interface::kVersion;
    tasks2.EnqueueTask = &mock_enqueue_task;
    tasks2.RemoveTask = &mock_remove_task;

    messaging.version = OBSEMessagingInterface::kVersion;
    messaging.RegisterListener = &mock_register_listener;

    serialization.version = OBSESerializationInterface::kVersion;
    serialization.SetNewGameCallback = &mock_set_newgame_callback;
}

void MockEnvironment::add_external_callback(const char* ev_name,
    EventManager::EventFunc func,
    TESObjectREFR* calling_obj,
    TESObjectREFR* source,
    TESForm* object) {
    xobse_callbacks[ev_name].push_back(MockEventCallback{
        .eventFunction = func,
        .source = source,
        .object = object,
        .callingObj = calling_obj,
    });
}

std::size_t MockEnvironment::count_callbacks(const char* ev_name) const {
    const auto callback_bucket = xobse_callbacks.find(ev_name);
    return callback_bucket != xobse_callbacks.end() ? callback_bucket->second.size() : 0;
}

bool MockEnvironment::has_function(const char* ev_name, EventManager::EventFunc func) const {
    const auto callback_bucket = xobse_callbacks.find(ev_name);
    if (callback_bucket == xobse_callbacks.end()) {
        return false;
    }
    return std::ranges::any_of(callback_bucket->second,
        [func](const auto& callback) { return callback.eventFunction == func; });
}

EventManager::EventFunc MockEnvironment::get_first_function(const char* ev_name) const {
    const auto callback_bucket = xobse_callbacks.find(ev_name);
    if (callback_bucket == xobse_callbacks.end() || callback_bucket->second.empty()) {
        return nullptr;
    }
    return callback_bucket->second.front().eventFunction;
}

bool MockEnvironment::mock_register_event(const char* name,
    EventManager::EventFunc func,
    void* arg0,
    void* arg1,
    TESObjectREFR* refr) {
    auto& env = instance();
    ++env.register_event_calls;
    if (!name || !func) {
        return false;
    }
    env.register_event_names.emplace_back(name);
    const MockEventCallback new_cb{
        .eventFunction = func,
        .source = static_cast<TESObjectREFR*>(arg0),
        .object = static_cast<TESForm*>(arg1),
        .callingObj = refr,
    };

    auto& list = env.xobse_callbacks[name];
    for (const auto& existing : list) {
        if (existing.equals(new_cb)) {
            return false;
        }
    }
    list.push_back(new_cb);
    if (std::strcmp(name, "onhealthdamage") == 0) {
        env.registered_damage_func = func;
    }
    return true;
}

namespace {

bool callback_matches(const std::string& ev_name,
    const MockEventCallback& to_remove,
    const MockEventCallback& candidate) noexcept {
    if (ev_name == "onhealthdamage") {
        if (to_remove.callingObj != nullptr &&
            to_remove.callingObj != reinterpret_cast<TESObjectREFR*>(candidate.object)) {
            return false;
        }
        return to_remove.source == nullptr || to_remove.source == candidate.source;
    }
    if (to_remove.object != nullptr && to_remove.object != candidate.object) {
        return false;
    }
    return to_remove.source == nullptr || to_remove.source == candidate.source;
}

} // namespace

// Match xOBSE's RemoveHandler rules for wildcard filters.
bool MockEnvironment::mock_unregister_event(const char* name,
    EventManager::EventFunc func,
    void* arg0,
    void* arg1,
    TESObjectREFR* refr) {
    auto& env = instance();
    ++env.unregister_event_calls;
    if (!name || !func) {
        return false;
    }

    const std::string ev_name = name;
    const auto callbacks_it = env.xobse_callbacks.find(ev_name);
    if (callbacks_it == env.xobse_callbacks.end()) {
        return false;
    }

    const MockEventCallback to_remove{
        .eventFunction = func,
        .source = static_cast<TESObjectREFR*>(arg0),
        .object = static_cast<TESForm*>(arg1),
        .callingObj = refr,
    };
    bool removed = false;

    for (auto iter = callbacks_it->second.begin(); iter != callbacks_it->second.end();) {
        if (callback_matches(ev_name, to_remove, *iter)) {
            iter = callbacks_it->second.erase(iter);
            removed = true;
        } else {
            ++iter;
        }
    }

    if (ev_name == "onhealthdamage") {
        env.registered_damage_func = env.get_first_function("onhealthdamage");
    }
    return removed;
}

bool MockEnvironment::mock_is_event_registered(const char* name,
    EventManager::EventFunc func,
    void* arg0,
    void* arg1,
    TESObjectREFR* refr) {
    auto& env = instance();
    if (!name) {
        return false;
    }
    const MockEventCallback probe{
        .eventFunction = func,
        .source = static_cast<TESObjectREFR*>(arg0),
        .object = static_cast<TESForm*>(arg1),
        .callingObj = refr,
    };
    const auto callback_bucket = env.xobse_callbacks.find(name);
    if (callback_bucket == env.xobse_callbacks.end()) {
        return false;
    }
    return std::ranges::any_of(callback_bucket->second,
        [&](const auto& existing) { return existing.equals(probe); });
}

Task<void>* MockEnvironment::mock_enqueue_task(TaskFunc<void> func) {
    auto& env = instance();
    ++env.enqueue_task_calls;
    if (!env.task_enqueue_result) {
        return nullptr;
    }
    env.active_task.emplace(func);
    env.active_task_ptr = &*env.active_task;
    env.enqueued_task_func = func;
    return env.active_task_ptr;
}

void MockEnvironment::mock_remove_task(Task<void>* task) {
    auto& env = instance();
    ++env.remove_task_calls;
    if (task != nullptr && task == env.active_task_ptr) {
        env.enqueued_task_func = nullptr;
        env.active_task_ptr = nullptr;
        env.active_task.reset();
    }
}

bool MockEnvironment::mock_register_listener(PluginHandle plugin_handle,
    const char* sender,
    OBSEMessagingInterface::EventCallback handler) {
    static_cast<void>(plugin_handle);
    auto& env = instance();
    ++env.register_listener_calls;
    env.registered_listener_sender = sender ? sender : "";
    if (!env.listener_register_result) {
        return false;
    }
    env.registered_msg_callback = handler;
    return true;
}

void MockEnvironment::mock_set_newgame_callback(PluginHandle plugin_handle,
    OBSESerializationInterface::EventCallback callback) {
    static_cast<void>(plugin_handle);
    auto& env = instance();
    ++env.set_newgame_callback_calls;
    env.registered_newgame_callback = callback;
}

void* __cdecl mock_query_interface(UInt32 id) {
    auto& env = MockEnvironment::instance();
    switch (id) {
    case kInterface_EventManager:
        return env.has_event_mgr ? &env.event_mgr : nullptr;
    case kInterface_Tasks2:
        return env.has_tasks2 ? &env.tasks2 : nullptr;
    case kInterface_Messaging:
        return env.has_messaging ? &env.messaging : nullptr;
    case kInterface_Serialization:
        return env.has_serialization ? &env.serialization : nullptr;
    default:
        return nullptr;
    }
}

PluginHandle __cdecl mock_get_plugin_handle() {
    return MockEnvironment::instance().handle;
}

OBSEInterface make_obse_interface() {
    OBSEInterface obse{};
    obse.obseVersion = 22;
    obse.oblivionVersion = OBLIVION_VERSION_1_2_416;
    obse.isEditor = 0;
    obse.QueryInterface = &mock_query_interface;
    obse.GetPluginHandle = &mock_get_plugin_handle;
    return obse;
}

} // namespace oblivion_adapter::test
