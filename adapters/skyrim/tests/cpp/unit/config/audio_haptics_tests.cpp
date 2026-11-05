#include <catch2/catch_test_macros.hpp>
#include <skyrim_adapter/native/config.hpp>

#include <memory>

TEST_CASE("skyrim.config.audio_haptics_can_be_disabled_without_mutating_on_invalid_input",
    "[unit][skyrim][config][audio_haptics]") {
    using namespace skyrim_adapter::native;
    const auto config = std::make_unique<NativeConfig>();
    CHECK(config->effects.audio_haptics_enabled);
    REQUIRE(parse_config("[InputNextSkyrim]\nAudioHapticsEnabled=false\n", *config));
    CHECK_FALSE(config->effects.audio_haptics_enabled);
    CHECK_FALSE(parse_config("[InputNextSkyrim]\nAudioHapticsEnabled=invalid\n", *config));
    CHECK_FALSE(config->effects.audio_haptics_enabled);
    CHECK(config->effects.body_enabled);
    CHECK(config->effects.triggers_enabled);
}
