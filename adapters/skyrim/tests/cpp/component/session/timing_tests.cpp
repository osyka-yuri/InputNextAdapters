#include "session/support/fake_output.hpp"
#include "session/support/session_test_support.hpp"
#include <algorithm>
#include <barrier>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <input_next_core.h>
#include <input_next_effects.h>
#include <latch>
#include <memory>
#include <mutex>
#include <optional>
#include <skyrim_adapter/coordinator.hpp>
#include <skyrim_adapter/model.hpp>
#include <span>
#include <thread>
#include <vector>

namespace skyrim_adapter::tests::session {
namespace {

using namespace std::chrono_literals;

struct ThreadJoiner {
    std::thread& t;

    explicit ThreadJoiner(std::thread& target) noexcept : t(target) {}

    ThreadJoiner(const ThreadJoiner&) = delete;
    ThreadJoiner& operator=(const ThreadJoiner&) = delete;
    ThreadJoiner(ThreadJoiner&&) = delete;
    ThreadJoiner& operator=(ThreadJoiner&&) = delete;

    ~ThreadJoiner() {
        if (t.joinable()) {
            t.join();
        }
    }
};

} // namespace

TEST_CASE("skyrim.session.concurrent_ticks_consume_once", "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    advertise_hd(fixture.output, 913);
    fixture.settle(idle());
    const auto before = fixture.output.body_submissions.size();
    fixture.time += 10ms;
    Action fire{};
    fire.event.kind = EventKind::voice_fire;
    fire.event.actor_id = 0x14;
    fire.event.observed_at = fixture.time;
    fire.event.sequence = 1;
    fixture.observe(idle(), std::span{&fire, 1});
    std::barrier ready{3};
    const auto tick = [&] {
        ready.arrive_and_wait();
        fixture.coordinator.tick(fixture.time);
    };
    std::thread worker{tick};
    ThreadJoiner const first_join{worker};
    std::thread native{tick};
    ThreadJoiner const second_join{native};
    ready.arrive_and_wait();
    worker.join();
    native.join();
    INFO("concurrent ticks duplicated or lost the body shout pulse");
    REQUIRE((fixture.output.body_submissions.size() == before + 1 &&
             fixture.output.body_submissions.back().request.pcm_command ==
                 INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE));
}

TEST_CASE("skyrim.session.production_time_is_sampled_after_contention",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.time = std::chrono::steady_clock::now();
    fixture.observe(casting());
    fixture.coordinator.tick(fixture.time);
    fixture.time = std::chrono::steady_clock::now();
    // The first read must start strictly after target selection.
    fixture.observe(casting());
    fixture.coordinator.tick(fixture.time);
    INFO("production-time fixture failed to establish a hold");
    REQUIRE(!fixture.output.submissions.empty());

    std::mutex pause_gate;
    std::condition_variable paused;
    bool release{};
    std::latch entered{1};
    std::latch waiting{1};
    fixture.output.on_query = [&] {
        std::unique_lock lock(pause_gate);
        entered.count_down();
        paused.wait(lock, [&] { return release; });
    };
    std::thread first{[&] { fixture.coordinator.tick(); }};
    ThreadJoiner const first_join{first};
    entered.wait();
    std::thread second{[&] {
        waiting.count_down();
        fixture.coordinator.tick();
    }};
    ThreadJoiner const second_join{second};
    waiting.wait();
    {
        std::unique_lock lock(pause_gate);
        paused.wait_until(lock, fixture.time + 180ms, [] { return false; });
        release = true;
    }
    paused.notify_all();
    first.join();
    second.join();
    INFO("tick used a pre-lock clock and kept an expired observation");
    REQUIRE(!fixture.output.clears.empty());
}

TEST_CASE("skyrim.session.production_query_delay_drops_expired_frame_and_withdraws_owners",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.time = std::chrono::steady_clock::now();
    fixture.observe(casting());
    fixture.coordinator.tick(fixture.time); // Select the target before admitting this sample.
    fixture.time = std::chrono::steady_clock::now();
    fixture.observe(casting());
    fixture.coordinator.tick(fixture.time);
    INFO("production query-delay fixture did not establish a trigger hold");
    REQUIRE(!fixture.output.submissions.empty());
    INFO("production query-delay fixture did not establish a body lease");
    REQUIRE((fixture.coordinator.diagnostics().body_owner_present &&
             fixture.output.active_body_lease != 0));

    const auto old_lease = fixture.output.active_body_lease;
    const auto old_lease_deadline = fixture.time + 200ms;
    const auto body_before = fixture.output.body_submissions.size();
    const auto trigger_before = fixture.output.submissions.size();
    fixture.time = std::chrono::steady_clock::now();
    const auto fire = spell_release(Hand::left, fixture.time, 7001, SpellKind::fire);
    fixture.observe(casting(), std::span{&fire, 1});
    fixture.output.on_query = [] {
        std::this_thread::sleep_for(180ms);
    };
    fixture.coordinator.tick();

    INFO("a successful but delayed target query replayed the expired body event");
    REQUIRE(fixture.output.body_submissions.size() == body_before);
    INFO("a successful but delayed target query emitted a trigger from an expired observation");
    REQUIRE(fixture.output.submissions.size() == trigger_before);
    INFO("the exact accepted trigger owner was withdrawn after the delayed query");
    REQUIRE((!fixture.output.clears.empty() && fixture.output.clears.back().mask != 0 &&
             same_device_target(fixture.output.clears.back().target, fixture.output.target)));
    const bool old_lease_was_cleared = std::ranges::any_of(fixture.output.body_clears,
        [&](const input_next_effects_body_clear_v1& clear) { return clear.lease_id == old_lease; });
    INFO("the accepted body lease was neither cleared nor allowed to reach its recorded expiry");
    REQUIRE((old_lease_was_cleared || std::chrono::steady_clock::now() >= old_lease_deadline));
    INFO("the coordinator retained its expired body owner after the delayed query");
    REQUIRE(!fixture.coordinator.diagnostics().body_owner_present);
}

TEST_CASE("skyrim.session.production_output_delay_expires_followup_intents",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    advertise_hd(fixture.output, 926);
    fixture.time = std::chrono::steady_clock::now();
    fixture.observe(idle());
    fixture.coordinator.tick(fixture.time); // Bind first.
    fixture.time = std::chrono::steady_clock::now();
    fixture.observe(idle());
    fixture.coordinator.tick(fixture.time);

    fixture.time = std::chrono::steady_clock::now();
    Action release{};
    release.event = raw_event(EventKind::bow_release, 0x14, fixture.time);
    release.event.hand = Hand::right;
    release.event.sequence = 801;
    release.source_kind = EquipmentKind::bow;
    release.weapon = WeaponClass::bow;
    release.source_known = true;
    release.outgoing = true;
    fixture.observe(idle(), std::span{&release, 1});
    const auto body_before = fixture.output.body_submissions.size();
    const auto triggers_before = fixture.output.submissions.size();
    const auto old_lease = fixture.output.active_body_lease;
    const auto old_sequence = fixture.output.body_submissions.back().request.sequence;
    const auto body_clears_before = fixture.output.body_clears.size();
    const auto trigger_clears_before = fixture.output.clears.size();
    bool delayed{};
    MonoTime body_submit_entry_at{};
    std::size_t trigger_submissions_at_body_entry{};
    std::optional<TriggerSubmission> bow_release_trigger_at_body_entry;
    fixture.output.on_body_submit = [&] {
        if (!delayed) {
            delayed = true;
            body_submit_entry_at = std::chrono::steady_clock::now();
            trigger_submissions_at_body_entry = fixture.output.submissions.size();
            const auto bow_release_trigger = std::find_if(
                fixture.output.submissions.begin() + static_cast<std::ptrdiff_t>(triggers_before),
                fixture.output.submissions.end(),
                [](const input_next_effects_request_v1& request) {
                    return request.channel_mask == INPUT_NEXT_EFFECTS_RIGHT_TRIGGER &&
                           request.right_trigger.mode == INPUT_NEXT_EFFECTS_TRIGGER_OFF;
                });
            if (bow_release_trigger != fixture.output.submissions.end()) {
                bow_release_trigger_at_body_entry = *bow_release_trigger;
            }
            std::this_thread::sleep_for(100ms);
        }
    };
    fixture.coordinator.tick();

    INFO("body output sequencing hook was not reached");
    REQUIRE(delayed);
    INFO("the accepted bow-release trigger was not submitted before the body call");
    if (!bow_release_trigger_at_body_entry) {
        FAIL("the bow-release trigger was not captured at body entry");
        return;
    }
    const auto bow_release_trigger = bow_release_trigger_at_body_entry.value();
    INFO("the output port did not accept the bow-release trigger before the body call");
    REQUIRE(fixture.output.submit_status == INPUT_NEXT_STATUS_OK);
    INFO("the bow-release trigger used the wrong channel or payload");
    REQUIRE((bow_release_trigger.channel_mask == INPUT_NEXT_EFFECTS_RIGHT_TRIGGER &&
             bow_release_trigger.right_trigger.mode == INPUT_NEXT_EFFECTS_TRIGGER_OFF));
    INFO("the bow-release trigger did not retain a valid portion of its original 60ms TTL");
    REQUIRE((bow_release_trigger.ttl_ms > 0 && bow_release_trigger.ttl_ms <= 60));
    INFO("the bow-release trigger sample timestamp was in the future at body entry");
    REQUIRE(release.event.observed_at <= body_submit_entry_at);
    INFO("a trigger was submitted after the body call began");
    REQUIRE(fixture.output.submissions.size() == trigger_submissions_at_body_entry);
    const auto exact_expired_trigger_clears = std::count_if(
        fixture.output.clears.begin() + static_cast<std::ptrdiff_t>(trigger_clears_before),
        fixture.output.clears.end(),
        [&](const Clear& clear) {
            return clear.mask == INPUT_NEXT_EFFECTS_RIGHT_TRIGGER &&
                   same_effects_context(clear.context, bow_release_trigger.context) &&
                   same_device_target(clear.target, bow_release_trigger.target);
        });
    INFO("the expired bow-release trigger was not cleared or retired through its exact context");
    REQUIRE((
        exact_expired_trigger_clears == 1 ||
        has_acked_close(fixture.output, bow_release_trigger.context, bow_release_trigger.target)));
    const auto matching_stop = std::find_if(fixture.output.body_submissions.begin() +
                                                static_cast<std::ptrdiff_t>(body_before),
        fixture.output.body_submissions.end(),
        [&](const BodySubmission& submission) {
            return submission.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_STOP &&
                   submission.request.lease_id == old_lease &&
                   submission.request.sequence > old_sequence;
        });
    if (matching_stop != fixture.output.body_submissions.end()) {
        INFO("the accepted STOP retained an expired rumble fallback");
        REQUIRE((matching_stop->request.fallback_rumble.low_frequency == 0.0F &&
                 matching_stop->request.fallback_rumble.high_frequency == 0.0F));
    } else {
        const bool exact_owner_clear = std::any_of(
            fixture.output.body_clears.begin() + static_cast<std::ptrdiff_t>(body_clears_before),
            fixture.output.body_clears.end(),
            [&](const input_next_effects_body_clear_v1& clear) {
                return clear.lease_id == fixture.output.last_accepted_body_lease;
            });
        const auto lease_deadline = fixture.output.last_body_accepted_at +
                                    std::chrono::milliseconds{fixture.output.last_body_ttl_ms};
        const bool accepted_lease_expired = std::chrono::steady_clock::now() >= lease_deadline;
        INFO(
            "stale sample neither withdrew its exact accepted body owner nor reached lease expiry");
        REQUIRE((exact_owner_clear || accepted_lease_expired));
        INFO("stale sample retained an expired body owner after delayed output");
        REQUIRE(!fixture.coordinator.diagnostics().body_owner_present);
    }
}

TEST_CASE("skyrim.session.production_delayed_submit_clears_lease_from_return_time",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    advertise_hd(fixture.output, 914);
    fixture.time = std::chrono::steady_clock::now();
    fixture.observe(idle());
    fixture.coordinator.tick(fixture.time); // Bind without acquiring a body lease.
    const auto body_before = fixture.output.body_submissions.size();
    const auto triggers_before = fixture.output.submissions.size();
    const auto trigger_clears_before = fixture.output.clears.size();
    fixture.time = std::chrono::steady_clock::now();
    const auto fire = spell_release(Hand::left, fixture.time, 7003, SpellKind::fire);
    fixture.observe(casting(), std::span{&fire, 1});
    constexpr std::uint32_t trigger_mask =
        INPUT_NEXT_EFFECTS_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_RIGHT_TRIGGER;
    const auto has_positive_trigger = [](const input_next_effects_request_v1& request) {
        return ((request.channel_mask & INPUT_NEXT_EFFECTS_LEFT_TRIGGER) != 0 &&
                   request.left_trigger.mode != INPUT_NEXT_EFFECTS_TRIGGER_OFF) ||
               ((request.channel_mask & INPUT_NEXT_EFFECTS_RIGHT_TRIGGER) != 0 &&
                   request.right_trigger.mode != INPUT_NEXT_EFFECTS_TRIGGER_OFF);
    };
    std::size_t trigger_submissions_at_body_entry{};
    std::size_t positive_trigger_submissions_at_body_entry{};
    std::vector<input_next_effects_request_v1> positive_triggers_at_body_entry;
    std::uint32_t accepted_trigger_channels{};
    std::optional<input_next_device_target_v1> accepted_trigger_target;
    std::optional<input_next_effects_context_v1> accepted_trigger_context;
    MonoTime body_submit_entry_at{};
    fixture.output.on_body_submit = [&] {
        if (body_submit_entry_at == MonoTime{}) {
            body_submit_entry_at = std::chrono::steady_clock::now();
            trigger_submissions_at_body_entry = fixture.output.submissions.size();
            for (auto submission = fixture.output.submissions.begin() +
                                   static_cast<std::ptrdiff_t>(triggers_before);
                submission != fixture.output.submissions.end();
                ++submission) {
                const auto channels = submission->channel_mask & trigger_mask;
                accepted_trigger_channels |= channels;
                if (channels != 0 && !accepted_trigger_target) {
                    accepted_trigger_target = submission->target;
                    accepted_trigger_context = submission->context;
                }
                if (has_positive_trigger(*submission)) {
                    ++positive_trigger_submissions_at_body_entry;
                    positive_triggers_at_body_entry.push_back(*submission);
                }
            }
            std::this_thread::sleep_for(250ms);
        }
    };
    fixture.coordinator.tick();

    INFO("delayed output did not make exactly one fresh body lease acquisition");
    REQUIRE((fixture.output.body_submissions.size() == body_before + 1 &&
             fixture.output.body_submissions.back().request.pcm_command ==
                 INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
             fixture.output.body_submissions.back().request.lease_id == 0));
    INFO("the fire trigger was not submitted before the delayed body call");
    REQUIRE((positive_trigger_submissions_at_body_entry != 0 &&
             positive_trigger_submissions_at_body_entry == positive_triggers_at_body_entry.size() &&
             accepted_trigger_channels != 0));
    if (!accepted_trigger_context || !accepted_trigger_target) {
        FAIL("the accepted fire trigger did not retain its context and target");
        return;
    }
    const auto accepted_context = accepted_trigger_context.value();
    const auto accepted_target = accepted_trigger_target.value();
    INFO("the output port did not accept the fire trigger before the body call");
    REQUIRE(fixture.output.submit_status == INPUT_NEXT_STATUS_OK);
    INFO("the fire trigger sample timestamp was in the future at body entry");
    REQUIRE(fire.event.observed_at <= body_submit_entry_at);
    INFO("the accepted fire trigger had no remaining TTL at body entry");
    REQUIRE(std::ranges::all_of(positive_triggers_at_body_entry,
        [](const input_next_effects_request_v1& request) { return request.ttl_ms > 0; }));
    INFO("delayed body completion positively resubmitted an expired fire trigger");
    REQUIRE(std::none_of(fixture.output.submissions.begin() +
                             static_cast<std::ptrdiff_t>(trigger_submissions_at_body_entry),
        fixture.output.submissions.end(),
        has_positive_trigger));
    const auto exact_fire_trigger_clears = std::count_if(
        fixture.output.clears.begin() + static_cast<std::ptrdiff_t>(trigger_clears_before),
        fixture.output.clears.end(),
        [&](const Clear& clear) {
            return clear.mask == accepted_trigger_channels &&
                   same_effects_context(clear.context, accepted_context) &&
                   same_device_target(clear.target, accepted_target);
        });
    INFO("the expired fire trigger was not cleared or retired through its exact context");
    REQUIRE((exact_fire_trigger_clears == 1 ||
             has_acked_close(fixture.output, accepted_context, accepted_target)));
    const auto accepted_lease = fixture.output.last_accepted_body_lease;
    INFO("post-return cleanup neither cleared the exact lease nor retired its context");
    REQUIRE((accepted_lease != 0 &&
             (std::ranges::any_of(fixture.output.body_clears,
                  [&](const BodyClear& clear) { return clear.lease_id == accepted_lease; }) ||
                 has_acked_close(fixture.output, accepted_context, accepted_target))));
    INFO("delayed body acceptance retained its expired owner");
    REQUIRE(!fixture.coordinator.diagnostics().body_owner_present);

    const auto replacement_count = std::count_if(fixture.output.body_submissions.begin(),
        fixture.output.body_submissions.end(),
        [](const BodySubmission& submission) {
            return submission.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE;
        });
    fixture.time = std::chrono::steady_clock::now();
    fixture.observe(idle());
    fixture.coordinator.tick();
    const auto later_replacement_count = std::count_if(fixture.output.body_submissions.begin(),
        fixture.output.body_submissions.end(),
        [](const BodySubmission& submission) {
            return submission.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE;
        });
    INFO("the delayed one-shot event replayed after fresh observation resumed output");
    REQUIRE(later_replacement_count == replacement_count);
}

TEST_CASE("skyrim.session.explicit_time_remains_fixed_across_delayed_query",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    advertise_hd(fixture.output, 915);
    fixture.settle(idle());
    fixture.time += 10ms;
    const auto fire = spell_release(Hand::left, fixture.time, 7002, SpellKind::fire);
    fixture.observe(casting(), std::span{&fire, 1});
    const auto body_before = fixture.output.body_submissions.size();
    fixture.output.on_query = [] {
        std::this_thread::sleep_for(180ms);
    };
    fixture.coordinator.tick(fixture.time);

    INFO("explicit tick time was replaced with wall time during a delayed query");
    REQUIRE((fixture.output.body_submissions.size() > body_before &&
             fixture.output.body_submissions.back().request.pcm_command ==
                 INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE));
}

} // namespace skyrim_adapter::tests::session
