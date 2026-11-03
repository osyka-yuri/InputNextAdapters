#include "cpp/component/native_dispatch/support.hpp"
#include "cpp/component/native_ui_capture/ui_capture_test.hpp"
#include "native/owner.hpp"
#include "support.hpp"

#include <array>
#include <atomic>
#include <filesystem>

#include <catch2/catch_test_macros.hpp>
#undef FAIL

namespace fallout4_adapter::native_owner_test {
using namespace fallout4_adapter;
using namespace fallout4_adapter::native;

TEST_CASE("fallout4.native_owner.owner_startup_publication_lifetime_inert_forwarding",
    "[component][fallout4][native_owner]") {
    Environment const environment;
    const auto runtime_path = std::filesystem::path{L"C:\\InputNext\\runtime.dll"};
    native_dispatch_test::reset();
    ui_capture_test::reset(700);
    {
        const auto owner_storage = make_test_owner();
        auto& owner = *owner_storage;
        NativeOwnerTestAccess::output(owner).connect_success = false;
        check(owner.start(runtime_path) == OwnerStart::failed_before_callbacks &&
                  !NativeOwnerTestAccess::callbacks_escaped(owner) &&
                  !native_dispatch_test::callback_active(),
            "runtime-output startup failure returns before any owner callback can escape");
    }
    native_dispatch_test::reset();
    ui_capture_test::reset(700);
    {
        const auto owner_storage = make_test_owner();
        auto& owner = *owner_storage;
        check(owner.start(std::filesystem::path{}) == OwnerStart::failed_before_callbacks &&
                  !NativeOwnerTestAccess::callbacks_escaped(owner) &&
                  !native_dispatch_test::callback_active(),
            "invalid runtime path returns before any owner callback can escape");
    }

    for (const auto failure : std::array{0, 1, 2}) {
        native_dispatch_test::reset();
        ui_capture_test::reset(701);
        OwnerStartupFixture fixture{};
        if (failure == 0) {
            fixture.interfaces = false;
        } else if (failure == 1) {
            fixture.pin = false;
        } else {
            fixture.worker = false;
        }
        const auto services = startup_services(fixture);
        StartupServicesBinding const services_binding{services};
        const auto owner_storage = make_test_owner();
        auto& owner = *owner_storage;
        check(owner.start(runtime_path) == OwnerStart::failed_before_callbacks &&
                  !NativeOwnerTestAccess::callbacks_escaped(owner) &&
                  !native_dispatch_test::callback_active(),
            "interface, pin and worker failures before escape leave no reachable Main owner "
            "callback");
        if (failure == 0) {
            check(fixture.worker_calls == 0 && fixture.listener_calls == 0,
                "unavailable interfaces stop startup before owner publication");
        } else if (failure == 1) {
            check(fixture.worker_calls == 0 && fixture.listener_calls == 0,
                "module-pin failure clears active owner before worker or callback publication");
        } else {
            check(fixture.worker_calls == 1 && fixture.listener_calls == 0 &&
                      !fixture.worker_saw_escaped,
                "worker-start failure clears active owner before callbacks escape");
        }
    }

    native_dispatch_test::reset();
    ui_capture_test::reset(702);
    startup_main_calls.store(0, std::memory_order_relaxed);
    native_dispatch_test::set_main_message_loop_original(startup_main_message_loop_original());
    OwnerStartupFixture activation_failure{};
    const auto activation_services = startup_services(activation_failure);
    {
        StartupServicesBinding const services_binding{activation_services};
        const auto owner_storage = make_test_owner();
        auto& owner = *owner_storage;
        check(owner.start(runtime_path) == OwnerStart::retained_inert &&
                  NativeOwnerTestAccess::callbacks_escaped(owner) &&
                  NativeOwnerTestAccess::inert(owner) && !native_dispatch_test::callback_active(),
            "dispatch activation failure after escape retains an inert pinned owner");
        native_dispatch_test::invoke_main(nullptr);
        check(startup_main_calls.load() == 1,
            "inactive retained Main wrapper forwards the message pump once after activation "
            "refusal");
    }

    native_dispatch_test::reset();
    ui_capture_test::reset(703);
    startup_main_calls.store(0, std::memory_order_relaxed);
    startup_provider_calls.store(0, std::memory_order_relaxed);
    const auto observations_before_startup =
        observation_provider_calls.load(std::memory_order_relaxed);
    set_observation_provider(&count_startup_observation);
    native_dispatch_test::set_main_message_loop_original(startup_main_message_loop_original());
    native_dispatch_test::set_install_ready();
    OwnerStartupFixture listener_failure{};
    listener_failure.listener = false;
    const auto listener_services = startup_services(listener_failure);
    {
        StartupServicesBinding const services_binding{listener_services};
        const auto owner_storage = make_test_owner();
        auto& owner = *owner_storage;
        check(owner.start(runtime_path) == OwnerStart::retained_inert &&
                  NativeOwnerTestAccess::callbacks_escaped(owner) &&
                  NativeOwnerTestAccess::inert(owner) && native_dispatch_test::callback_active(),
            "listener-registration failure after activation keeps the reachable owner inert");
        check(listener_failure.worker_calls == 1 && !listener_failure.worker_saw_escaped &&
                  listener_failure.listener_calls == 1 && listener_failure.listener_saw_escaped &&
                  listener_failure.listener_saw_activation,
            "actual startup activates only after worker success and callback lifetime escape");
        native_dispatch_test::invoke_main(nullptr);
        check(startup_main_calls.load() == 1 && startup_provider_calls.load() == 0 &&
                  observation_provider_calls.load() == observations_before_startup,
            "reachable inert owner callback does no observation work and forwards the Main message "
            "pump exactly once");
    }
}

TEST_CASE("fallout4.native_owner.owner_startup_start_output_exception_safety",
    "[component][fallout4][native_owner]") {
    Environment const environment;
    const auto runtime_path = std::filesystem::path{L"C:\\InputNext\\runtime.dll"};

    // Injected exception during start_output().
    {
        native_dispatch_test::reset();
        ui_capture_test::reset(710);
        OwnerStartupFixture failing_fixture{};
        failing_fixture.throw_on_start_output = true;
        const auto failing_services = startup_services(failing_fixture);
        StartupServicesBinding const services_binding{failing_services};

        {
            const auto owner_storage = make_test_owner();
            auto& owner = *owner_storage;
            bool exception_escaped = false;
            OwnerStart result{};
            try {
                result = owner.start(runtime_path);
            } catch (...) {
                exception_escaped = true;
            }

            check(!exception_escaped, "startup does not propagate the exception");
            check(result == OwnerStart::failed_before_callbacks,
                "startup returns failed_before_callbacks when start_output throws");
            check(!NativeOwnerTestAccess::callbacks_escaped(owner),
                "callbacks_escaped remains false when start_output throws");
            check(failing_fixture.start_output_calls == 1, "start_output was invoked exactly once");
            check(failing_fixture.worker_calls == 0, "no worker started after start_output threw");
            check(failing_fixture.listener_calls == 0,
                "no listener callback registered after start_output threw");
            check(!native_dispatch_test::callback_active(),
                "main observation callback is not active after start_output threw");
            check(failing_fixture.error_calls == 1 && failing_fixture.last_error != nullptr,
                "startup failure diagnostic was logged with a non-allocating message");
        }

        // Subsequent independent startup proceeds without stale active-owner publication.
        native_dispatch_test::reset();
        ui_capture_test::reset(711);
        native_dispatch_test::set_main_message_loop_original(startup_main_message_loop_original());
        native_dispatch_test::set_install_ready();
        OwnerStartupFixture healthy_fixture{};
        const auto healthy_services = startup_services(healthy_fixture);
        StartupServicesBinding const healthy_binding{healthy_services};

        const auto second_owner_storage = make_test_owner();
        auto& second_owner = *second_owner_storage;
        bool second_exception = false;
        OwnerStart second_result{};
        try {
            second_result = second_owner.start(runtime_path);
        } catch (...) {
            second_exception = true;
        }

        check(!second_exception, "subsequent startup does not propagate exception");
        check(second_result == OwnerStart::active,
            "subsequent independent startup successfully activates without stale active-owner "
            "publication");
        check(NativeOwnerTestAccess::callbacks_escaped(second_owner),
            "subsequent owner callbacks escaped");
        check(healthy_fixture.worker_calls == 1, "subsequent owner started worker");
        check(healthy_fixture.listener_calls == 1, "subsequent owner registered listener");
        check(native_dispatch_test::callback_active(),
            "subsequent owner main observation callback is active");
    }

    // Explicit teardown after failed startup.
    {
        native_dispatch_test::reset();
        ui_capture_test::reset(712);
        OwnerStartupFixture heap_fixture{};
        heap_fixture.throw_on_start_output = true;
        const auto heap_services = startup_services(heap_fixture);
        StartupServicesBinding const heap_binding{heap_services};

        auto heap_owner = make_test_owner();
        const auto heap_result = heap_owner->start(runtime_path);
        check(heap_result == OwnerStart::failed_before_callbacks &&
                  !NativeOwnerTestAccess::callbacks_escaped(*heap_owner) &&
                  !native_dispatch_test::callback_active(),
            "heap-allocated owner startup fails safely before callbacks escape");
        heap_owner.reset();
    }

    // Injected exception via output port start hook.
    {
        native_dispatch_test::reset();
        ui_capture_test::reset(713);
        const auto output_hook_owner_storage = make_test_owner();
        auto& output_hook_owner = *output_hook_owner_storage;
        NativeOwnerTestAccess::output(output_hook_owner).on_start = [] {
            throw std::bad_alloc{};
        };
        const auto output_result = output_hook_owner.start(runtime_path);
        check(output_result == OwnerStart::failed_before_callbacks &&
                  !NativeOwnerTestAccess::callbacks_escaped(output_hook_owner) &&
                  !native_dispatch_test::callback_active(),
            "injected exception via output port start hook returns failed_before_callbacks");
    }
}

} // namespace fallout4_adapter::native_owner_test
