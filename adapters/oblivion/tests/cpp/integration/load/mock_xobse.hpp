#pragma once

#include "native/xobse_sdk.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace oblivion_adapter::test {

struct MockEventCallback {
    EventManager::EventFunc eventFunction{nullptr};
    TESObjectREFR* source{nullptr};
    TESForm* object{nullptr};
    TESObjectREFR* callingObj{nullptr};

    [[nodiscard]] bool equals(const MockEventCallback& rhs) const noexcept;
};

class MockEnvironment {
public:
    static MockEnvironment& instance();

    MockEnvironment(const MockEnvironment&) = delete;
    MockEnvironment& operator=(const MockEnvironment&) = delete;
    MockEnvironment(MockEnvironment&&) = delete;
    MockEnvironment& operator=(MockEnvironment&&) = delete;

    void reset() noexcept;

    void add_external_callback(const char* ev_name,
        EventManager::EventFunc func,
        TESObjectREFR* calling_obj = nullptr,
        TESObjectREFR* source = nullptr,
        TESForm* object = nullptr);

    [[nodiscard]] std::size_t count_callbacks(const char* ev_name) const;
    [[nodiscard]] bool has_function(const char* ev_name, EventManager::EventFunc func) const;
    [[nodiscard]] EventManager::EventFunc get_first_function(const char* ev_name) const;

    static bool mock_register_event(const char* name,
        EventManager::EventFunc func,
        void* arg0,
        void* arg1,
        TESObjectREFR* refr);
    static bool mock_unregister_event(const char* name,
        EventManager::EventFunc func,
        void* arg0,
        void* arg1,
        TESObjectREFR* refr);
    static bool mock_is_event_registered(const char* name,
        EventManager::EventFunc func,
        void* arg0,
        void* arg1,
        TESObjectREFR* refr);

    static Task<void>* mock_enqueue_task(TaskFunc<void> func);
    static void mock_remove_task(Task<void>* task);

    static bool mock_register_listener(PluginHandle plugin_handle,
        const char* sender,
        OBSEMessagingInterface::EventCallback handler);
    static void mock_set_newgame_callback(PluginHandle plugin_handle,
        OBSESerializationInterface::EventCallback callback);

    bool has_event_mgr{true};
    bool has_tasks2{true};
    bool has_messaging{true};
    bool has_serialization{true};

    bool task_enqueue_result{true};
    bool listener_register_result{true};

    int register_event_calls{0};
    std::vector<std::string> register_event_names;
    int unregister_event_calls{0};
    int enqueue_task_calls{0};
    int remove_task_calls{0};
    int register_listener_calls{0};
    int set_newgame_callback_calls{0};

    PluginHandle handle{1};
    std::string registered_listener_sender;
    OBSEMessagingInterface::EventCallback registered_msg_callback{nullptr};
    TaskFunc<void> enqueued_task_func{nullptr};
    std::optional<Task<void>> active_task;
    Task<void>* active_task_ptr{nullptr};
    EventManager::EventFunc registered_damage_func{nullptr};
    OBSESerializationInterface::EventCallback registered_newgame_callback{nullptr};

    std::unordered_map<std::string, std::vector<MockEventCallback>> xobse_callbacks;

    OBSEEventManagerInterface event_mgr{};
    OBSETasks2Interface tasks2{};
    OBSEMessagingInterface messaging{};
    OBSESerializationInterface serialization{};

private:
    MockEnvironment();
    ~MockEnvironment() = default;
};

void* __cdecl mock_query_interface(UInt32 id);
PluginHandle __cdecl mock_get_plugin_handle();
OBSEInterface make_obse_interface();

} // namespace oblivion_adapter::test
