#pragma once

#include <string_view>

namespace skyrim_adapter::native::detail {
struct SpellKeywordFlags {
    bool ward{};
    bool fire{};
    bool frost{};
    bool shock{};
    bool restore_health{};
};

template <class EditorId>
[[nodiscard]] constexpr bool editor_id_matches(const EditorId& editor_id,
    std::string_view expected) noexcept {
    return editor_id.length() == expected.length() && editor_id == expected;
}

template <class EditorId>
constexpr void include_spell_keyword(SpellKeywordFlags& flags, const EditorId& editor_id) noexcept {
    if (editor_id_matches(editor_id, std::string_view{"MagicWard"})) {
        flags.ward = true;
    } else if (editor_id_matches(editor_id, std::string_view{"MagicDamageFire"})) {
        flags.fire = true;
    } else if (editor_id_matches(editor_id, std::string_view{"MagicDamageFrost"})) {
        flags.frost = true;
    } else if (editor_id_matches(editor_id, std::string_view{"MagicDamageShock"})) {
        flags.shock = true;
    } else if (editor_id_matches(editor_id, std::string_view{"MagicRestoreHealth"})) {
        flags.restore_health = true;
    }
}

template <class KeywordRange>
[[nodiscard]] constexpr SpellKeywordFlags scan_spell_keywords(
    const KeywordRange& keywords) noexcept {
    SpellKeywordFlags flags{};
    for (const auto* keyword : keywords) {
        if (keyword) {
            include_spell_keyword(flags, keyword->formEditorID);
        }
    }
    return flags;
}
} // namespace skyrim_adapter::native::detail
