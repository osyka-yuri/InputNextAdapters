#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fallout_gamebryo/settings.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>

namespace {
using fallout_gamebryo::ConfigStatus;
using fallout_gamebryo::Settings;

[[nodiscard]] bool declares_section(const std::string& text, std::string_view name) {
    return text.contains("\n[" + std::string{name} + "]\n");
}

TEST_CASE("The New Vegas reference INI lists every profile at its default settings",
    "[new-vegas][configuration]") {
    const std::filesystem::path directory{INPUTNEXT_FNV_INI_DIRECTORY};
    const auto reference_path = directory / "InputNextFalloutNewVegas.reference.ini";
    const auto reference = std::make_unique<Settings>();
    REQUIRE(fallout_gamebryo::read_config(reference_path, *reference) == ConfigStatus::loaded);
    std::ifstream file{reference_path};
    REQUIRE(file.is_open());
    const std::string text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};

    const auto defaults = std::make_unique<Settings>();
    for (const auto& profile : fallout_gamebryo::body_profiles) {
        CAPTURE(profile.section);
        const auto index = static_cast<std::size_t>(profile.effect);
        CHECK(declares_section(text, profile.section));
        CHECK(reference->body[index].enabled == defaults->body[index].enabled);
        CHECK(reference->body[index].strength_scale == defaults->body[index].strength_scale);
        CHECK(reference->body[index].duration_scale == defaults->body[index].duration_scale);
    }
    for (const auto& profile : fallout_gamebryo::trigger_profiles) {
        CAPTURE(profile.section);
        const auto index = static_cast<std::size_t>(profile.profile);
        CHECK(declares_section(text, profile.section));
        CHECK(reference->trigger[index].enabled == defaults->trigger[index].enabled);
        CHECK(reference->trigger[index].strength_scale == defaults->trigger[index].strength_scale);
        CHECK(reference->trigger[index].resistance_start ==
              defaults->trigger[index].resistance_start);
    }

    Settings active{};
    REQUIRE(fallout_gamebryo::read_config(directory / "InputNextFalloutNewVegas.ini", active) ==
            ConfigStatus::loaded);
    CHECK(active.gait_cues == reference->gait_cues);
    CHECK(active.led.stealth_enabled == reference->led.stealth_enabled);
    REQUIRE(fallout_gamebryo::parse_config("[LED]\nStealthEnabled=false\n", active) ==
            ConfigStatus::loaded);
    CHECK_FALSE(active.led.stealth_enabled);
}
} // namespace
