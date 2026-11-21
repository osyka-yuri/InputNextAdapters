#include "magic_forms.hpp"
#include "memory_guard.hpp"

#include "effect_model.hpp"
#include "magic_effects.hpp"
#include "xobse_sdk.hpp"

#include <GameForms.h>

#include <cstdint>

namespace oblivion_adapter::native {
namespace {

const EffectItemList* effect_list(const TESForm* form) noexcept {
    switch (form->typeID) {
    case kFormType_Spell:
        return &static_cast<const SpellItem*>(form)->magicItem.list;
    case kFormType_Enchantment:
        return &static_cast<const EnchantmentItem*>(form)->magicItem.list;
    case kFormType_AlchemyItem:
        return &static_cast<const AlchemyItem*>(form)->magicItem.list;
    case kFormType_Ingredient:
        return &static_cast<const IngredientItem*>(form)->magicItem.list;
    default:
        return nullptr;
    }
}

bool read_cast_origin(const TESForm* form, CastOrigin& origin) noexcept {
    if (form->typeID == kFormType_Spell) {
        switch (static_cast<const SpellItem*>(form)->spellType) {
        case SpellItem::kType_Spell:
            origin = CastOrigin::spell;
            return true;
        case SpellItem::kType_Power:
        case SpellItem::kType_LesserPower:
            origin = CastOrigin::power;
            return true;
        default:
            return false;
        }
    }
    if (form->typeID == kFormType_Enchantment) {
        switch (static_cast<const EnchantmentItem*>(form)->enchantType) {
        case EnchantmentItem::kEnchant_Scroll:
            origin = CastOrigin::scroll;
            return true;
        case EnchantmentItem::kEnchant_Staff:
            origin = CastOrigin::staff;
            return true;
        default:
            return false;
        }
    }
    return false;
}

MagicKind classify_effects(const EffectItemList* list) noexcept {
    if (!list) {
        return MagicKind::generic;
    }
    constexpr std::uint32_t max_effect_items = 64;
    // GameForms.h embeds the first BSSimpleList entry in EffectItemList.
    const auto* entry = &list->effectList;
    CastEffects effects;
    for (std::uint32_t visited = 0; entry && visited < max_effect_items; ++visited) {
        if (const auto* item = entry->effectItem) {
            effects.add(item->setting ? item->setting->effectCode : item->effectCode);
        }
        entry = entry->next;
    }
    return effects.kind(entry == nullptr);
}

} // namespace

bool read_magic_form(const TESForm* form, EffectEvent& event) noexcept {
    const bool casting = event.kind == EffectEventKind::cast;
    if (!casting && event.kind != EffectEventKind::magic_applied) {
        return false;
    }
    return guarded_game_read([&] {
        if (!form || form->refID == 0) {
            return false;
        }
        CastOrigin origin = CastOrigin::spell;
        if (casting && !read_cast_origin(form, origin)) {
            return false;
        }
        event.magic = classify_effects(effect_list(form));
        event.cast_origin = origin;
        event.source_id = form->refID;
        return true;
    });
}

} // namespace oblivion_adapter::native
