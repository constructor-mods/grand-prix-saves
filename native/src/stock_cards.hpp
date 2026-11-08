// SPDX-License-Identifier: MIT
// The Grand Prix screen's own cards. The game fills them from the save list; they stay hidden and are used to show
// and continue the saves of the list. They show only the newest save, so the second one is always the game's own
// "start new" card. Each time the game fills its first card, the list of saves is built (again).
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include "save_list.hpp"
#include "save_slots.hpp"

#include <windows.h>

#include <cstdint>
#include <vector>

namespace grand_prix_saves {

constexpr int kSkipSlot = 34;  // asks whether a save is left out of the cards
constexpr uintptr_t kSkipFunction = 0x1a077b0;
constexpr int kFillSlot = 28;  // fills one card from a save
constexpr uintptr_t kFillFunction = 0x1bccd80;

using SkipSave = uint64_t (*)(void *screen, void *save);
inline SkipSave g_original_skip = nullptr;
inline FillCard g_original_fill = nullptr;

// Every Grand Prix save but the newest is left out of the game's own cards.
inline uint64_t skip_save(void *screen, void *save)
{
    std::vector<uintptr_t> saves = grand_prix_saves(save_list_of_screen(screen));
    auto at = reinterpret_cast<uintptr_t>(save);
    for (size_t index = 1; index < saves.size(); index++) {
        if (saves[index] == at) {
            return 1;
        }
    }
    return g_original_skip(screen, save);
}

inline uint64_t fill_card(void *screen, void *save, uint64_t card)
{
    uint64_t result = g_original_fill(screen, save, card);
    if ((card & 0xff) == 0) {
        build_save_list(screen, grand_prix_saves(save_list_of_screen(screen)), reinterpret_cast<uintptr_t>(save),
                        g_original_fill);
    }
    return result;
}

// Puts a function of ours in place of the screen's own one (after checking it is the one this game build has).
inline bool swap_entry(int slot, uintptr_t expected, void *replacement, void **original)
{
    uintptr_t base = swofty::api->module_base();
    auto *entry = reinterpret_cast<void **>(base + kContinueVtable + 8 * uintptr_t(slot));
    if (*entry != reinterpret_cast<void *>(base + expected)) {
        return false;
    }
    *original = *entry;
    DWORD old_protect = 0;
    if (!VirtualProtect(entry, sizeof(void *), PAGE_READWRITE, &old_protect)) {
        return false;
    }
    *entry = replacement;
    VirtualProtect(entry, sizeof(void *), old_protect, &old_protect);
    return true;
}

inline void catch_cards()
{
    bool skip_ok = swap_entry(kSkipSlot, kSkipFunction, reinterpret_cast<void *>(&skip_save),
                              reinterpret_cast<void **>(&g_original_skip));
    bool fill_ok = swap_entry(kFillSlot, kFillFunction, reinterpret_cast<void *>(&fill_card),
                              reinterpret_cast<void **>(&g_original_fill));
    if (!skip_ok || !fill_ok) {
        swofty::log("[grand-prix-saves] Grand Prix screen: this game build is not supported");
    }
}

} // namespace grand_prix_saves
