#include "support/test_runner.hpp"

#include <array>
#include <chrono>
#include <input_next_core.h>
#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/led_policy.hpp>
#include <inputnext_adapters/output.hpp>
#include <inputnext_adapters/output_frame.hpp>
#include <inputnext_adapters/output_session.hpp>
#include <memory>

namespace {
class ConsumerObserver final : public inputnext::adapters::DeliveryObserver {
public:
    [[nodiscard]] bool admitted(
        [[maybe_unused]] inputnext::adapters::MonoTime now) const noexcept override {
        return false;
    }

    void binding_lost([[maybe_unused]] input_next_status status) noexcept override {}

    void binding_changed(
        [[maybe_unused]] inputnext::adapters::MonoTime started_at) noexcept override {}

    void body_rebound(
        [[maybe_unused]] inputnext::adapters::MonoTime baseline_at) noexcept override {}

    void admission_expired() noexcept override {}
};

int run_tests() {
    using namespace inputnext::adapters;

    BodyProgram const empty{};
    std::array<float, 2> samples{1.0F, 1.0F};
    const auto window = build_body_pcm(empty, MonoTime{}, samples);
    if (body_program_active(empty, MonoTime{}) || window.frame_count != 1 || samples[0] != 0.0F ||
        samples[1] != 0.0F) {
        return 1;
    }

    LedPolicy led_policy;
    LedObservation observation{};
    observation.sampled_at = MonoTime{} + std::chrono::seconds{1};
    observation.subject_id = 1U;
    observation.health = LedHealthSample{.current = 100.0F, .maximum = 100.0F};
    if (led_policy.update(observation, observation.sampled_at)) {
        return 3;
    }
    observation.sampled_at += std::chrono::milliseconds{1};
    observation.health = LedHealthSample{.current = 90.0F, .maximum = 100.0F};
    OutputFrame frame{};
    frame.leds = led_policy.update(observation, observation.sampled_at);
    if (!frame.leds || frame.leds->source_at != observation.sampled_at ||
        led_policy.mode() != LedMode::health) {
        return 3;
    }

    RuntimeOutput output;
    ConsumerObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0U);
    const auto diagnostics = session->diagnostics();
    return session->bound() || session->has_body_owner() || diagnostics.body_owner_present ? 2 : 0;
}
} // namespace

int main() {
    return inputnext::test::run_main(__FILE__, [] { return run_tests(); });
}
