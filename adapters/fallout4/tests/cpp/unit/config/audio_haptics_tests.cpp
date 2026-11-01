#include <catch2/catch_test_macros.hpp>
#include <fallout4_adapter/config.hpp>

#include <memory>

TEST_CASE("fallout4.config.audio_haptics_opt_out_is_transactional",
    "[unit][fallout4][config][audio_haptics]") {
    using namespace fallout4_adapter;
    const auto config = std::make_unique<Config>();
    CHECK(config->policy.effects.audio_haptics_enabled);
    REQUIRE(
        parse_config("[Effects]\nAudioHapticsEnabled=false\n", *config) == ConfigStatus::loaded);
    CHECK_FALSE(config->policy.effects.audio_haptics_enabled);
    CHECK(
        parse_config("[Effects]\nAudioHapticsEnabled=invalid\n", *config) == ConfigStatus::invalid);
    CHECK_FALSE(config->policy.effects.audio_haptics_enabled);
    CHECK(config->policy.effects.body_enabled);
    CHECK(config->policy.effects.triggers_enabled);
}
