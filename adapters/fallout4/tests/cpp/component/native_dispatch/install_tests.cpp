#include "cpp/component/native_ui_capture/ui_capture_test.hpp"
#include "native/dispatch.hpp"
#include "native/dispatch_operations.hpp"
#include "native/dispatch_pipeline.hpp"
#include "native/ui_capture.hpp"
#include "support.hpp"

#include <RE/B/BSFixedString.h>
#include <RE/C/ControlMap.h>
#include <RE/I/INPUT_DEVICE.h>

#include <Windows.h>

#include <catch2/catch_test_macros.hpp>
#undef FAIL

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

namespace fallout4_adapter::tests::dispatch {
using namespace fallout4_adapter::native;

namespace {
const RE::BSFixedString& empty_remap_event_argument() {
    // This empty fixture avoids the game-only string-pool release relocation at shutdown.
    alignas(RE::BSFixedString) static std::array<std::byte, sizeof(RE::BSFixedString)> storage{};
    static const auto* value =
        std::construct_at(reinterpret_cast<RE::BSFixedString*>(storage.data()));
    return *value;
}

RE::ControlMap* fixture_control_map_address() noexcept {
    static std::byte storage{};
    return reinterpret_cast<RE::ControlMap*>(std::addressof(storage));
}

RE::Main* fixture_main_address() noexcept {
    static std::byte storage{};
    return reinterpret_cast<RE::Main*>(std::addressof(storage));
}

struct InstallerProbe {
    std::array<char, 12> stages{};
    std::size_t stage_count{};
    std::array<NativeDispatchSite, 3> sites{};
    std::array<std::uint8_t, 3> masks{};
    std::array<NativeDispatchAttemptInspection, 3> inspections{};
    std::size_t attempt_count{};
    std::size_t fail_stage{};
    bool retained{};
    bool fatal{};

    [[nodiscard]] bool stage(char value) noexcept {
        stages[stage_count++] = value;
        return stage_count != fail_stage;
    }

    [[nodiscard]] bool preflight() noexcept { return stage('f'); }

    [[nodiscard]] bool prepare() noexcept { return stage('r'); }

    [[nodiscard]] bool pin_and_publish() noexcept { return stage('p'); }

    [[nodiscard]] bool precommit() noexcept { return stage('c'); }

    [[nodiscard]] NativeDispatchAttemptInspection attempt_once(NativeDispatchSite site,
        std::uint8_t expected_owned_mask) noexcept {
        const auto index = attempt_count++;
        sites[index] = site;
        masks[index] = expected_owned_mask;
        return inspections[index];
    }

    void retain_disabled() noexcept { retained = true; }

    void fatal_control_flow() noexcept { fatal = true; }
};

enum class FixtureMutation : std::uint8_t {
    none,
    helper_entry,
    helper_displacement,
    helper_tail,
    remap_entry,
    remap_tail,
    main_displacement,
    helper_cell,
    remap_cell,
    main_relay,
    resource_tail,
};

struct ProductionInstallFixture {
    std::byte* image{};
    std::byte* pool{};
    bool bindings{true};
    bool interfaces{true};
    bool pin{true};
    bool precommit{true};
    bool allocation_available{true};
    bool change_helper_guard_on_pin{};
    std::uint32_t bindings_calls{};
    std::uint32_t interface_calls{};
    std::uint32_t writes{};
    std::uint32_t flushes{};
    std::uint32_t fail_write_attempt{};
    std::uint32_t fail_flush_attempt{};
    std::uint32_t mutation_attempt{1};
    bool write_failure_after_copy{};
    FixtureMutation mutation{FixtureMutation::none};
    bool mutation_applied{};
    std::uint32_t helper_calls{};
    std::uint32_t remap_calls{};
    std::uint32_t main_calls{};
    std::array<std::uintptr_t, 3> write_addresses{};
    std::uint64_t helper_return{0xDEADBEEF12345678ULL};
    bool remap_return{true};
    std::unique_ptr<NativeDispatchPipeline> pipeline{std::make_unique<NativeDispatchPipeline>()};
};

ProductionInstallFixture* active_fixture{};
constexpr std::size_t fixture_image_size = 0x3400000;
constexpr std::size_t fixture_pool_offset = 0x3200000;

std::uint64_t fixture_helper_original(std::uint8_t /*flag*/, std::uint64_t /*entry_rax*/) {
    ++active_fixture->helper_calls;
    return active_fixture->helper_return;
}

static_assert(std::is_same_v<decltype(&fixture_helper_original), SeededOriginal>);

bool fixture_remap_original(RE::ControlMap* /*control_map*/,
    const RE::BSFixedString& /*event_name*/,
    RE::INPUT_DEVICE /*device*/,
    std::int32_t /*button*/) {
    ++active_fixture->remap_calls;
    return active_fixture->remap_return;
}

std::byte* fixture_allocate(void* context, std::size_t size) noexcept {
    const auto& fixture = *static_cast<ProductionInstallFixture*>(context);
    return size == native_dispatch_pool_size && fixture.allocation_available ? fixture.pool
                                                                             : nullptr;
}

bool fixture_bindings(void* context) noexcept {
    auto& fixture = *static_cast<ProductionInstallFixture*>(context);
    ++fixture.bindings_calls;
    return fixture.bindings;
}

bool fixture_interfaces(void* context) noexcept {
    auto& fixture = *static_cast<ProductionInstallFixture*>(context);
    ++fixture.interface_calls;
    return fixture.interfaces;
}

bool fixture_pin(void* context) noexcept {
    const auto& fixture = *static_cast<ProductionInstallFixture*>(context);
    if (fixture.change_helper_guard_on_pin) {
        fixture.image[native_dispatch_helper_rva + 1U] ^= std::byte{0x01};
    }
    return fixture.pin;
}

bool fixture_precommit(void* context) noexcept {
    return static_cast<ProductionInstallFixture*>(context)->precommit;
}

void apply_fixture_mutation(ProductionInstallFixture& fixture, std::uint32_t attempt) noexcept {
    if (fixture.mutation_applied || fixture.mutation_attempt != attempt) {
        return;
    }
    fixture.mutation_applied = true;
    switch (fixture.mutation) {
    case FixtureMutation::none:
        break;
    case FixtureMutation::helper_entry:
        fixture.image[native_dispatch_helper_rva] ^= std::byte{0x01};
        break;
    case FixtureMutation::helper_displacement:
        fixture.image[native_dispatch_helper_rva + 2U] ^= std::byte{0x10};
        break;
    case FixtureMutation::helper_tail:
        fixture.image[native_dispatch_helper_rva + 7U] ^= std::byte{0x01};
        break;
    case FixtureMutation::remap_entry:
        fixture.image[native_dispatch_remap_button_rva] ^= std::byte{0x01};
        break;
    case FixtureMutation::remap_tail:
        fixture.image[native_dispatch_remap_button_rva + 12U] ^= std::byte{0x01};
        break;
    case FixtureMutation::main_displacement:
        fixture.image[native_dispatch_main_call_rva + 2U] ^= std::byte{0x10};
        break;
    case FixtureMutation::helper_cell:
        fixture.pool[0] ^= std::byte{0x01};
        break;
    case FixtureMutation::remap_cell:
        fixture.pool[64] ^= std::byte{0x01};
        break;
    case FixtureMutation::main_relay:
        fixture.pool[48] ^= std::byte{0x01};
        break;
    case FixtureMutation::resource_tail:
        fixture.pool[127] ^= std::byte{0x01};
        break;
    }
}

bool fixture_write(void* context,
    std::uintptr_t address,
    std::span<const std::byte> bytes) noexcept {
    auto& fixture = *static_cast<ProductionInstallFixture*>(context);
    const auto attempt = ++fixture.writes;
    if (attempt <= fixture.write_addresses.size()) {
        fixture.write_addresses[attempt - 1U] = address;
    }
    const bool fail = fixture.fail_write_attempt == attempt;
    if (!fail || fixture.write_failure_after_copy) {
        std::memcpy(reinterpret_cast<void*>(address), bytes.data(), bytes.size());
    }
    apply_fixture_mutation(fixture, attempt);
    return !fail;
}

bool fixture_flush(void* context,
    std::uintptr_t /*target_address*/,
    std::size_t /*byte_count*/) noexcept {
    auto& fixture = *static_cast<ProductionInstallFixture*>(context);
    const auto attempt = ++fixture.flushes;
    return fixture.fail_flush_attempt != attempt;
}

static_assert(std::is_same_v<decltype(&fixture_flush),
    bool (*)(void*, std::uintptr_t, std::size_t) noexcept>);

void fixture_main_original(RE::Main* /*main*/) {
    ++active_fixture->main_calls;
}

static_assert(std::is_same_v<decltype(&fixture_main_original), MainMessageLoopOriginal>);

struct TestDispatchPlatform {
    ProductionInstallFixture& fixture;
    NativeDispatchPipeline& pipeline;

    [[nodiscard]] std::uintptr_t image_base() const noexcept {
        return reinterpret_cast<std::uintptr_t>(fixture.image);
    }

    [[nodiscard]] static const F4SE::TrampolineInterface* trampoline() noexcept {
        return reinterpret_cast<const F4SE::TrampolineInterface*>(std::uintptr_t{1});
    }

    [[nodiscard]] bool bindings_available() const noexcept { return fixture_bindings(&fixture); }

    [[nodiscard]] bool interfaces_supported(
        const F4SE::TrampolineInterface* /*trampoline*/) const noexcept {
        return fixture_interfaces(&fixture);
    }

    [[nodiscard]] std::byte* allocate_branch_pool(const F4SE::TrampolineInterface* /*trampoline*/,
        std::size_t size) const noexcept {
        return fixture_allocate(&fixture, size);
    }

    [[nodiscard]] bool pin_module() const noexcept { return fixture_pin(&fixture); }

    [[nodiscard]] bool publish_originals([[maybe_unused]] SeededOriginal helper,
        [[maybe_unused]] RemapOriginal remap,
        [[maybe_unused]] MainMessageLoopOriginal main_loop) const noexcept {
        ui_capture_test::set_original(&fixture_helper_original);
        ui_capture_test::set_remap_original(&fixture_remap_original);
        return pipeline.publish_main_original(&fixture_main_original);
    }

    [[nodiscard]] bool precommit() const noexcept { return fixture_precommit(&fixture); }

    [[nodiscard]] bool write_safe(std::uintptr_t address,
        std::span<const std::byte> bytes) const noexcept {
        return fixture_write(&fixture, address, bytes);
    }

    [[nodiscard]] bool flush_instruction_cache(std::uintptr_t address,
        std::size_t size) const noexcept {
        return fixture_flush(&fixture, address, size);
    }

    static void retain_disabled() noexcept { disable_ui_capture(); }

    static void fatal_control_flow() noexcept { native_dispatch_test::record_fatal_control_flow(); }

    [[nodiscard]] static std::uintptr_t helper_interceptor() noexcept {
        return reinterpret_cast<std::uintptr_t>(&ui_transition_interceptor);
    }

    [[nodiscard]] static std::uintptr_t remap_interceptor() noexcept {
        return reinterpret_cast<std::uintptr_t>(&remap_button_interceptor);
    }

    [[nodiscard]] static std::uintptr_t main_wrapper() noexcept {
        return reinterpret_cast<std::uintptr_t>(&main_message_loop_wrapper);
    }
};

using TestDispatchOperations = BasicNativeDispatchOperations<TestDispatchPlatform>;

void initialize_install_fixture(ProductionInstallFixture& fixture) {
    std::memset(fixture.image + native_dispatch_helper_rva, 0, native_dispatch_helper_guard.size());
    std::memset(fixture.image + native_dispatch_remap_button_rva,
        0,
        native_dispatch_remap_guard.size());
    std::memset(fixture.image + native_dispatch_main_call_rva -
                    native_dispatch_main_arguments.size(),
        0,
        native_dispatch_main_arguments.size());
    std::memcpy(fixture.image + native_dispatch_helper_rva,
        native_dispatch_helper_guard.data(),
        native_dispatch_helper_guard.size());
    std::memcpy(fixture.image + native_dispatch_remap_button_rva,
        native_dispatch_remap_guard.data(),
        native_dispatch_remap_guard.size());
    std::memcpy(fixture.image + native_dispatch_main_call_rva -
                    native_dispatch_main_arguments.size(),
        native_dispatch_main_arguments.data(),
        native_dispatch_main_arguments.size());
    std::memcpy(fixture.image + native_dispatch_main_call_rva,
        native_dispatch_main_original_call.data(),
        native_dispatch_main_original_call.size());
    std::memset(fixture.image + native_dispatch_manager_slot_rva, 0, sizeof(std::uintptr_t));
    std::memset(fixture.pool, 0, native_dispatch_pool_size);
    fixture.bindings = true;
    fixture.interfaces = true;
    fixture.pin = true;
    fixture.precommit = true;
    fixture.allocation_available = true;
    fixture.change_helper_guard_on_pin = false;
    fixture.bindings_calls = 0;
    fixture.interface_calls = 0;
    fixture.writes = 0;
    fixture.flushes = 0;
    fixture.fail_write_attempt = 0;
    fixture.fail_flush_attempt = 0;
    fixture.mutation_attempt = 1;
    fixture.write_failure_after_copy = false;
    fixture.mutation = FixtureMutation::none;
    fixture.mutation_applied = false;
    fixture.helper_calls = 0;
    fixture.remap_calls = 0;
    fixture.main_calls = 0;
    fixture.write_addresses.fill(0);
    fixture.remap_return = true;
    native_dispatch_test::reset();
    fixture.pipeline = std::make_unique<NativeDispatchPipeline>();
    native_dispatch_test::set_active_pipeline(fixture.pipeline.get());
    ui_capture_test::reset(17);
    active_fixture = &fixture;
}

NativeDispatchInstall run_production_fixture_install(ProductionInstallFixture& fixture) {
    TestDispatchPlatform platform{
        .fixture = fixture,
        .pipeline = *fixture.pipeline,
    };
    TestDispatchOperations operations{platform};
    return fixture.pipeline->install(operations);
}

class ScopedActiveFixtureReset final {
public:
    ~ScopedActiveFixtureReset() noexcept {
        native_dispatch_test::set_active_pipeline(nullptr);
        native_dispatch_test::reset();
        ui_capture_test::reset(1);
        active_fixture = nullptr;
    }

    ScopedActiveFixtureReset() = default;
    ScopedActiveFixtureReset(const ScopedActiveFixtureReset&) = delete;
    ScopedActiveFixtureReset& operator=(const ScopedActiveFixtureReset&) = delete;
    ScopedActiveFixtureReset(ScopedActiveFixtureReset&&) = delete;
    ScopedActiveFixtureReset& operator=(ScopedActiveFixtureReset&&) = delete;
};

struct TestFixtureScope {
    ProductionInstallFixture fixture{};
    ScopedVirtualAllocation image_region{fixture_image_size, PAGE_READWRITE};
    ScopedActiveFixtureReset active_fixture_reset{};

    [[nodiscard]] bool valid() const noexcept { return image_region.get() != nullptr; }

    void setup() {
        check(valid(), "installer fixture allocates an isolated image-sized byte region");
        if (valid()) {
            fixture.image = image_region.get();
            fixture.pool = image_region.get() + fixture_pool_offset;
        }
    }
};
} // namespace

TEST_CASE("fallout4.native_dispatch.fixed_installer_sequence_and_stages",
    "[component][fallout4][native_dispatch]") {
    ScopedDispatchState const dispatch_state;
    InstallerProbe ready{};
    ready.inspections.fill({
        .all_sites_safe = true,
        .expected_prefix = true,
        .write_succeeded = true,
        .instruction_cache_flushed = true,
    });
    check(perform_native_dispatch_install(ready) == NativeDispatchInstall::ready,
        "fixed installer accepts the verified helper-to-remap-to-Main sequence");
    constexpr std::array expected_stages{'f', 'r', 'p', 'c'};
    constexpr std::array expected_sites{
        NativeDispatchSite::helper,
        NativeDispatchSite::remap_button,
        NativeDispatchSite::main_message_loop,
    };
    constexpr std::array<std::uint8_t, 3> expected_masks{1, 3, 7};
    check(ready.stage_count == expected_stages.size() &&
              ready.stages == std::array<char, 12>{'f', 'r', 'p', 'c'},
        "fixed installer preflights, prepares, pins and publishes, then rechecks before any write");
    check(ready.attempt_count == expected_sites.size(),
        "fixed installer attempts exactly three native sites");
    for (std::size_t index = 0; index < expected_sites.size(); ++index) {
        check(ready.sites[index] == expected_sites[index],
            "fixed installer preserves the required write order");
        check(ready.masks[index] == expected_masks[index],
            "each attempt requires only the installed prefix");
    }

    InstallerProbe preflight_failure{};
    preflight_failure.fail_stage = 1;
    check(perform_native_dispatch_install(preflight_failure) ==
                  NativeDispatchInstall::unavailable_before_attempt &&
              preflight_failure.attempt_count == 0 && !preflight_failure.retained,
        "preflight failure performs no site writes and remains unavailable before attempt");
}

TEST_CASE("fallout4.native_dispatch.installer_probe_failure_disposition",
    "[component][fallout4][native_dispatch]") {
    ScopedDispatchState const dispatch_state;
    InstallerProbe write_failure{};
    write_failure.inspections[0] = {
        .all_sites_safe = true,
        .expected_prefix = true,
        .write_succeeded = false,
        .instruction_cache_flushed = true,
    };
    check(perform_native_dispatch_install(write_failure) ==
                  NativeDispatchInstall::retained_disabled &&
              write_failure.attempt_count == 1 && write_failure.retained && !write_failure.fatal,
        "a failed first write is inspected and retained disabled without attempting later sites");

    InstallerProbe unexpected_control{};
    unexpected_control.inspections[0] = {
        .all_sites_safe = false,
        .expected_prefix = false,
        .write_succeeded = true,
        .instruction_cache_flushed = true,
    };
    check(perform_native_dispatch_install(unexpected_control) ==
                  NativeDispatchInstall::retained_disabled &&
              unexpected_control.attempt_count == 1 && unexpected_control.retained &&
              unexpected_control.fatal,
        "unexpected control flow after a write fails fast and cannot continue the sequence");
    check(decide_native_dispatch_attempt(NativeDispatchAttemptInspection{
              .all_sites_safe = true,
              .expected_prefix = true,
              .write_succeeded = true,
              .instruction_cache_flushed = false,
          }) == NativeDispatchAttemptResult::retained_disabled &&
              decide_native_dispatch_attempt(NativeDispatchAttemptInspection{
                  .all_sites_safe = false,
                  .expected_prefix = true,
                  .write_succeeded = true,
                  .instruction_cache_flushed = true,
              }) == NativeDispatchAttemptResult::fatal_control_flow,
        "attempt disposition distinguishes cache/write uncertainty from unsafe site bytes");
}

TEST_CASE("fallout4.native_dispatch.production_installer_preflight_rejections",
    "[component][fallout4][native_dispatch]") {
    ScopedDispatchState const dispatch_state;
    TestFixtureScope scope;
    scope.setup();
    if (!scope.valid()) {
        return;
    }
    auto& fixture = scope.fixture;

    const auto no_write_preflight = [&](const char* message, auto mutate_fixture) {
        initialize_install_fixture(fixture);
        mutate_fixture(fixture);
        const auto result = run_production_fixture_install(fixture);
        check(result == NativeDispatchInstall::unavailable_before_attempt && fixture.writes == 0 &&
                  native_dispatch_test::fatal_control_flow_count() == 0,
            message);
    };
    no_write_preflight("actual installer rejects changed helper guard without any site write",
        [](auto& install_fixture) {
            install_fixture.image[native_dispatch_helper_rva + 4U] ^= std::byte{1};
        });
    no_write_preflight("actual installer rejects changed RemapButton guard without any site write",
        [](auto& install_fixture) {
            install_fixture.image[native_dispatch_remap_button_rva + 9U] ^= std::byte{1};
        });
    no_write_preflight(
        "actual installer rejects changed Main argument block without any site write",
        [](auto& install_fixture) {
            install_fixture.image[native_dispatch_main_call_rva -
                                  native_dispatch_main_arguments.size() + 5U] ^= std::byte{1};
        });
    no_write_preflight(
        "actual installer rejects changed original CALL target without any site write",
        [](auto& install_fixture) {
            install_fixture.image[native_dispatch_main_call_rva + 2U] ^= std::byte{1};
        });
    no_write_preflight("actual installer rejects a non-null manager slot without any site write",
        [](auto& install_fixture) {
            const std::uintptr_t occupied = 0x12345678U;
            std::memcpy(install_fixture.image + native_dispatch_manager_slot_rva,
                &occupied,
                sizeof(occupied));
        });
    no_write_preflight(
        "actual installer rejects unavailable checked bindings without any site write",
        [](auto& install_fixture) { install_fixture.bindings = false; });
    no_write_preflight("actual installer rejects unsupported interfaces without any site write",
        [](auto& install_fixture) { install_fixture.interfaces = false; });
}

TEST_CASE("fallout4.native_dispatch.production_installer_preparation_and_precommit_guards",
    "[component][fallout4][native_dispatch]") {
    ScopedDispatchState const dispatch_state;
    TestFixtureScope scope;
    scope.setup();
    if (!scope.valid()) {
        return;
    }
    auto& fixture = scope.fixture;

    initialize_install_fixture(fixture);
    fixture.allocation_available = false;
    check(run_production_fixture_install(fixture) ==
                  NativeDispatchInstall::unavailable_before_attempt &&
              fixture.writes == 0,
        "actual installer allocation refusal causes zero site writes");
    initialize_install_fixture(fixture);
    fixture.fail_flush_attempt = 1;
    check(run_production_fixture_install(fixture) ==
                  NativeDispatchInstall::unavailable_before_attempt &&
              fixture.writes == 0,
        "actual installer prepared-pool flush refusal causes zero site writes");
    initialize_install_fixture(fixture);
    fixture.pin = false;
    check(run_production_fixture_install(fixture) ==
                  NativeDispatchInstall::unavailable_before_attempt &&
              fixture.writes == 0,
        "actual installer module-pin refusal causes zero site writes");
    initialize_install_fixture(fixture);
    fixture.precommit = false;
    check(run_production_fixture_install(fixture) ==
                  NativeDispatchInstall::unavailable_before_attempt &&
              fixture.writes == 0,
        "actual installer precommit refusal causes zero site writes");
    initialize_install_fixture(fixture);
    fixture.change_helper_guard_on_pin = true;
    check(run_production_fixture_install(fixture) ==
                  NativeDispatchInstall::unavailable_before_attempt &&
              fixture.writes == 0,
        "actual installer detects a changed site guard during precommit and performs zero writes");
}

TEST_CASE("fallout4.native_dispatch.production_installer_successful_install_order",
    "[component][fallout4][native_dispatch]") {
    ScopedDispatchState const dispatch_state;
    TestFixtureScope scope;
    scope.setup();
    if (!scope.valid()) {
        return;
    }
    auto& fixture = scope.fixture;
    const auto* image = scope.image_region.get();

    initialize_install_fixture(fixture);
    check(run_production_fixture_install(fixture) == NativeDispatchInstall::ready,
        "actual installer commits all three sites");
    check(fixture.writes == 3 &&
              fixture.write_addresses[0] ==
                  reinterpret_cast<std::uintptr_t>(image) + native_dispatch_helper_rva &&
              fixture.write_addresses[1] ==
                  reinterpret_cast<std::uintptr_t>(image) + native_dispatch_remap_button_rva &&
              fixture.write_addresses[2] ==
                  reinterpret_cast<std::uintptr_t>(image) + native_dispatch_main_call_rva,
        "actual installer writes helper, RemapButton and Main in the fixed order");
}

TEST_CASE("fallout4.native_dispatch.production_installer_site_write_and_flush_failures",
    "[component][fallout4][native_dispatch]") {
    ScopedDispatchState const dispatch_state;
    TestFixtureScope scope;
    scope.setup();
    if (!scope.valid()) {
        return;
    }
    auto& fixture = scope.fixture;

    constexpr std::array<const char*, 3> intact_failure_messages{
        "intact helper after write refusal is retained disabled with no later write",
        "intact RemapButton after write refusal is retained disabled with no later write",
        "intact Main after write refusal is retained disabled with no later write",
    };
    constexpr std::array<const char*, 3> owned_failure_messages{
        "owned helper after write refusal is retained disabled with no later write",
        "owned RemapButton after write refusal is retained disabled with no later write",
        "owned Main after write refusal is retained disabled with no later write",
    };
    constexpr std::array<const char*, 3> flush_failure_messages{
        "helper cache-flush refusal is retained disabled with no later write",
        "RemapButton cache-flush refusal is retained disabled with no later write",
        "Main cache-flush refusal is retained disabled with no later write",
    };
    for (std::uint32_t site = 0; site < 3; ++site) {
        initialize_install_fixture(fixture);
        fixture.fail_write_attempt = site + 1U;
        check(run_production_fixture_install(fixture) == NativeDispatchInstall::retained_disabled &&
                  fixture.writes == site + 1U &&
                  native_dispatch_test::fatal_control_flow_count() == 0,
            intact_failure_messages[site]);

        initialize_install_fixture(fixture);
        fixture.fail_write_attempt = site + 1U;
        fixture.write_failure_after_copy = true;
        check(run_production_fixture_install(fixture) == NativeDispatchInstall::retained_disabled &&
                  fixture.writes == site + 1U &&
                  native_dispatch_test::fatal_control_flow_count() == 0,
            owned_failure_messages[site]);

        initialize_install_fixture(fixture);
        fixture.fail_flush_attempt = site + 2U;
        check(run_production_fixture_install(fixture) == NativeDispatchInstall::retained_disabled &&
                  fixture.writes == site + 1U &&
                  native_dispatch_test::fatal_control_flow_count() == 0,
            flush_failure_messages[site]);
    }
}

TEST_CASE("fallout4.native_dispatch.production_installer_corrupt_mutation_fatal_fail_fast",
    "[component][fallout4][native_dispatch]") {
    ScopedDispatchState const dispatch_state;
    TestFixtureScope scope;
    scope.setup();
    if (!scope.valid()) {
        return;
    }
    auto& fixture = scope.fixture;

    constexpr std::array mutation_cases{
        std::pair{FixtureMutation::helper_entry, 1U},
        std::pair{FixtureMutation::helper_displacement, 1U},
        std::pair{FixtureMutation::helper_tail, 1U},
        std::pair{FixtureMutation::remap_entry, 2U},
        std::pair{FixtureMutation::remap_tail, 2U},
        std::pair{FixtureMutation::main_displacement, 3U},
        std::pair{FixtureMutation::helper_cell, 1U},
        std::pair{FixtureMutation::remap_cell, 2U},
        std::pair{FixtureMutation::main_relay, 3U},
        std::pair{FixtureMutation::resource_tail, 1U},
    };
    constexpr std::array<const char*, mutation_cases.size()> mutation_messages{
        "corrupt helper entry bytes choose the nonterminating fail-fast disposition",
        "corrupt helper displacement chooses the nonterminating fail-fast disposition",
        "corrupt helper untouched tail chooses the nonterminating fail-fast disposition",
        "corrupt remap entry bytes choose the nonterminating fail-fast disposition",
        "corrupt remap untouched tail chooses the nonterminating fail-fast disposition",
        "corrupt Main CALL displacement chooses the nonterminating fail-fast disposition",
        "corrupt helper bridge cell chooses the nonterminating fail-fast disposition",
        "corrupt remap pointer cell chooses the nonterminating fail-fast disposition",
        "corrupt Main relay chooses the nonterminating fail-fast disposition",
        "corrupt prepared resource tail chooses the nonterminating fail-fast disposition",
    };
    for (std::size_t index = 0; index < mutation_cases.size(); ++index) {
        initialize_install_fixture(fixture);
        fixture.mutation = mutation_cases[index].first;
        fixture.mutation_attempt = mutation_cases[index].second;
        check(run_production_fixture_install(fixture) == NativeDispatchInstall::retained_disabled &&
                  fixture.writes == mutation_cases[index].second &&
                  native_dispatch_test::fatal_control_flow_count() == 1,
            mutation_messages[index]);
    }
}

TEST_CASE("fallout4.native_dispatch.production_installer_retained_disabled_forwarding",
    "[component][fallout4][native_dispatch]") {
    ScopedDispatchState const dispatch_state;
    TestFixtureScope scope;
    scope.setup();
    if (!scope.valid()) {
        return;
    }
    auto& fixture = scope.fixture;

    initialize_install_fixture(fixture);
    fixture.fail_write_attempt = 3;
    fixture.write_failure_after_copy = true;
    check(run_production_fixture_install(fixture) == NativeDispatchInstall::retained_disabled,
        "owned three-site prefix remains disabled after the final Main write refusal");
    const auto helper_result = ui_capture_test::invoke(0xFEU, 0xABCDEF1234567890ULL);
    const auto& event_name = empty_remap_event_argument();
    const auto remap_result = ui_capture_test::invoke_remap(fixture_control_map_address(),
        event_name,
        RE::INPUT_DEVICE::kGamepad,
        23);
    native_dispatch_test::invoke_main(fixture_main_address());
    check(helper_result == fixture.helper_return && fixture.helper_calls == 1,
        "retained helper wrapper still forwards exactly once to its published original");
    check(remap_result == fixture.remap_return && fixture.remap_calls == 1,
        "retained RemapButton wrapper still forwards exactly once to its published original");
    check(fixture.main_calls == 1 && !native_dispatch_test::callback_active(),
        "retained Main wrapper stays inactive and forwards the Main pointer exactly once");
}

} // namespace fallout4_adapter::tests::dispatch
