// SPDX-License-Identifier: MIT
// Picking Grand Prix from the menu: the game opens the saves screen only when the save in use holds a Grand Prix.
// After "Create New" the save in use is the new, still empty one; backing out of the new Grand Prix leaves it in use,
// so the game would go straight to a new Grand Prix and the other saves look gone. Here, just before the game decides,
// and when the menu is left (Back), an empty save in use is swapped for the newest save that holds a Grand Prix.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include "stock_cards.hpp"
#include "save_slots.hpp"

#include <cstdint>
#include <vector>

namespace grand_prix_saves {

constexpr uintptr_t kMenuVtable = 0x572ac70;     // the play menu (Grand Prix, Time Trial, ...)
constexpr int kMenuCommandSlot = 10;
constexpr uintptr_t kMenuCommandFunction = 0x1a77d60;
constexpr uint32_t kMenuBack = 0x493fe92b;        // "back_pressed": leaving the menu
constexpr uint32_t kPlayCardsShown = 0x1b4084ea;  // the Play cards (Grand Prix, Time Trial) are shown
constexpr uint32_t kYearPicked = 0xa2f65c38;      // a year picked: the game then opens the saves or a new Grand Prix
constexpr uintptr_t kMenuModes = 0x2f0;          // in the menu: the game's modes
constexpr uintptr_t kFindMode = 0x7d0de0;        // finds a mode by name (modes, name)
constexpr const char *kGrandPrixModes[] = {"F1_Championship_Season", "F1_2026_Championship_Season"};

using MenuCommand = uint64_t (*)(void *menu, void *command, void *data, void *more);
using FindMode = uintptr_t (*)(void *modes, const char *name);
using SelectSave = int (*)(void *list, uint64_t save_id, bool force, uint32_t unused);

inline MenuCommand g_original_menu_command = nullptr;

// In one save list: when the save in use holds no Grand Prix, the newest one that does is put in use.
inline void use_a_real_save_in(uintptr_t list, const char *where)
{
    auto select_save = reinterpret_cast<SelectSave>(swofty::api->resolve("Game_SaveSlotManager_SelectSlot"));
    uintptr_t current = 0;
    std::vector<uintptr_t> saves = grand_prix_saves(list);
    bool listed = is_save_list(list) && swofty::mem::read(list + kCurrentSlot, current);
    if (!select_save || !listed || holds_grand_prix(current) || saves.empty()) {
        return;
    }
    uint64_t id = 0;
    swofty::mem::read(saves.front() + kSlotId, id);
    int result = select_save(reinterpret_cast<void *>(list), id, false, 0);
    swofty::log("[grand-prix-saves] %s: the save in use was empty, save %llu put back in use (result %d)", where,
                static_cast<unsigned long long>(id), result);
}

// The Grand Prix save lists, found through the game's modes.
inline void use_a_real_save_from(uintptr_t modes, const char *where)
{
    if (!modes) {
        return;
    }
    auto find_mode = reinterpret_cast<FindMode>(swofty::api->module_base() + kFindMode);
    for (const char *name : kGrandPrixModes) {
        uintptr_t list = find_save_list(find_mode(reinterpret_cast<void *>(modes), name));
        remember_list(list);
        use_a_real_save_in(list, where);
    }
}

// Puts the newest Grand Prix save in use when the one in use is empty (from the play menu).
inline void use_a_real_save(void *menu)
{
    uintptr_t modes = 0;
    if (!swofty::mem::read(reinterpret_cast<uintptr_t>(menu) + kMenuModes, modes) || !modes) {
        return;
    }
    use_a_real_save_from(modes, "play menu");
}

inline uint64_t menu_command(void *menu, void *command_data, void *data, void *more)
{
    uint32_t name = 0;
    uintptr_t name_at = 0;
    if (command_data) {
        swofty::mem::read(reinterpret_cast<uintptr_t>(command_data), name_at);
    }
    if (name_at) {
        swofty::mem::read(name_at, name);
    }
    // Back out of the menu, or back on the Play cards: a new Grand Prix being set up is left.
    if ((name == kMenuBack || name == kPlayCardsShown) && g_setting_up_new) {
        g_setting_up_new = false;
        swofty::log("[grand-prix-saves] play menu: the new Grand Prix setup was left");
    }
    // Only right before the game decides between the saves and a new Grand Prix (a year picked), or when the menu is
    // left, and never while a new Grand Prix is being set up (its save is the empty one in use on purpose).
    if (!g_setting_up_new && (name == kYearPicked || name == kMenuBack)) {
        use_a_real_save(menu);
        for (uintptr_t list : g_known_lists) {
            use_a_real_save_in(list, "play menu");
        }
    }
    return g_original_menu_command(menu, command_data, data, more);
}

inline void catch_menu()
{
    uintptr_t base = swofty::api->module_base();
    auto *entry = reinterpret_cast<void **>(base + kMenuVtable + 8 * uintptr_t(kMenuCommandSlot));
    bool caught = false;
    if (*entry == reinterpret_cast<void *>(base + kMenuCommandFunction)) {
        g_original_menu_command = reinterpret_cast<MenuCommand>(*entry);
        DWORD old_protect = 0;
        if (VirtualProtect(entry, sizeof(void *), PAGE_READWRITE, &old_protect)) {
            *entry = reinterpret_cast<void *>(&menu_command);
            VirtualProtect(entry, sizeof(void *), old_protect, &old_protect);
            caught = true;
        }
    }
    swofty::log("[grand-prix-saves] play menu: %s", caught ? "on" : "off (this game build is not supported)");
}

} // namespace grand_prix_saves
