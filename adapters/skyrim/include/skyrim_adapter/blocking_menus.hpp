#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace skyrim_adapter {

inline constexpr std::array known_blocking_menus{
    std::string_view{"Main Menu"},
    std::string_view{"Loading Menu"},
    std::string_view{"LoadWaitSpinner"},
    std::string_view{"TitleSequence Menu"},
    std::string_view{"InventoryMenu"},
    std::string_view{"StatsMenu"},
    std::string_view{"MagicMenu"},
    std::string_view{"ContainerMenu"},
    std::string_view{"FavoritesMenu"},
    std::string_view{"GiftMenu"},
    std::string_view{"BarterMenu"},
    std::string_view{"Dialogue Menu"},
    std::string_view{"Journal Menu"},
    std::string_view{"MapMenu"},
    std::string_view{"Lockpicking Menu"},
    std::string_view{"Book Menu"},
    std::string_view{"MessageBoxMenu"},
    std::string_view{"Sleep/Wait Menu"},
    std::string_view{"Training Menu"},
    std::string_view{"Crafting Menu"},
    std::string_view{"RaceSex Menu"},
    std::string_view{"Console"},
};

[[nodiscard]] inline bool is_known_blocking_menu(std::string_view name) noexcept {
    return std::ranges::find(known_blocking_menus, name) != known_blocking_menus.end();
}

} // namespace skyrim_adapter
