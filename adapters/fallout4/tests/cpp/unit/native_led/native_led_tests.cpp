#include "native/optional_id_map.hpp"
#include "native/radiation_cache.hpp"

#include <array>
#include <optional>
#include <span>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {
void check(bool condition, const char* message) {
    INFO(message);
    CHECK(condition);
}
} // namespace

TEST_CASE("fallout4.native_led.radiation_unknown_zero_positive_and_lifecycle_race",
    "[unit][fallout4][native_led]") {
    using fallout4_adapter::native::RadiationCountCache;
    RadiationCountCache cache;
    check(!cache.current_count(), "startup count remains unknown until a real event");
    check(cache.enable(), "admitted gameplay enables event publication");
    check(!cache.current_count(), "enabling does not fabricate zero exposure");

    const auto zero_token = cache.capture_event_token();
    const auto zero_token_value = zero_token.value_or(RadiationCountCache::EventToken{});
    check(zero_token.has_value() && cache.publish(zero_token_value, 0U),
        "a real zero-count event establishes known zero");
    check(cache.current_count() == 0U, "known zero remains distinct from unknown");

    const auto positive_token = cache.capture_event_token();
    const auto positive_token_value = positive_token.value_or(RadiationCountCache::EventToken{});
    check(positive_token.has_value() && cache.publish(positive_token_value, 3U),
        "a real positive source count is published");
    check(cache.current_count() == 3U, "positive source count is copied without scaling");

    const auto unavailable_token = cache.capture_event_token();
    const auto unavailable_token_value =
        unavailable_token.value_or(RadiationCountCache::EventToken{});
    check(unavailable_token.has_value() && cache.publish(unavailable_token_value, std::nullopt),
        "a source event without a value explicitly withdraws cached knowledge");
    check(!cache.current_count(), "unavailable source data must not retain positive exposure");

    const auto stale_token = cache.capture_event_token();
    const auto stale_token_value = stale_token.value_or(RadiationCountCache::EventToken{});
    check(stale_token.has_value(), "an admitted cache exposes an event token");
    check(cache.invalidate(), "load lifecycle advances generation");
    check(!cache.current_count(), "lifecycle invalidation clears knowledge immediately");
    check(cache.enable(), "fresh admitted gameplay opens the new generation");
    check(!cache.publish(stale_token_value, 8U),
        "a pre-reset event cannot publish across invalidation and re-enable");
    check(!cache.current_count(), "the stale publication cannot restore old radiation");
}

TEST_CASE("fallout4.native_led.radiation_generation_exhaustion_disables_without_wrap",
    "[unit][fallout4][native_led]") {
    using fallout4_adapter::native::RadiationCountCache;
    RadiationCountCache cache{RadiationCountCache::maximum_generation};
    check(cache.enable(), "the final valid lifecycle generation can be admitted");
    const auto token = cache.capture_event_token();
    const auto token_value = token.value_or(RadiationCountCache::EventToken{});
    check(token.has_value() && cache.publish(token_value, 2U),
        "final generation accepts its current event");
    check(!cache.invalidate(), "exhaustion disables publication instead of wrapping");
    const auto snapshot = cache.snapshot();
    check(snapshot.exhausted && !snapshot.accepting && !snapshot.known,
        "exhausted generation is disabled and unknown");
    check(!cache.enable() && !cache.capture_event_token(),
        "an exhausted cache cannot be re-enabled");
    check(!cache.publish(token_value, 7U), "the final old event cannot cross exhaustion");
}

TEST_CASE("fallout4.native_led.optional_ids_require_presence_and_matching_image_sections",
    "[unit][fallout4][native_led]") {
    using namespace fallout4_adapter::native;
    constexpr std::array mappings{
        OptionalIdMapping{.id = 101U, .offset = 0x110U},
        OptionalIdMapping{.id = 102U, .offset = 0x220U},
        OptionalIdMapping{.id = 103U, .offset = 0x230U},
    };
    constexpr std::array sections{
        ImageSectionRange{.rva = 0x100U,
            .size = 0x80U,
            .characteristics = image_section_read | image_section_execute},
        ImageSectionRange{.rva = 0x200U,
            .size = 0x80U,
            .characteristics = image_section_read | image_section_write},
    };
    constexpr std::array requests{
        OptionalAddressRequest{.id = 101U, .width = 1U, .kind = OptionalAddressKind::code},
        OptionalAddressRequest{.id = 102U, .width = 8U, .kind = OptionalAddressKind::data},
        OptionalAddressRequest{.id = 103U, .width = 8U, .kind = OptionalAddressKind::code},
        OptionalAddressRequest{.id = 104U, .width = 8U, .kind = OptionalAddressKind::data},
    };
    const auto result =
        resolve_optional_addresses(mappings, std::span{sections}, 0x1000U, requests);
    check(result.available[0] && result.rvas[0] == 0x110U,
        "code IDs resolve only into a readable executable section");
    check(result.available[1] && result.rvas[1] == 0x220U,
        "data IDs resolve only into readable non-executable data");
    check(!result.available[2], "a code ID mapped into a data section is rejected");
    check(!result.available[3] && result.rvas[3] == 0U,
        "missing IDs remain unavailable without a fatal lookup");
    check(!section_contains_rva(std::span{sections},
              0x1000U,
              0x27CU,
              8U,
              OptionalAddressKind::writable_data),
        "data pointers that cross a section boundary are rejected");
}
