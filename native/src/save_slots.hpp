// SPDX-License-Identifier: MIT
// Reads the Grand Prix save list and the cards of the "Continue / Start new" screen.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace grand_prix_saves {

// The Grand Prix "Continue / Start new" screen.
constexpr uintptr_t kContinueVtable = 0x5739fa0;
constexpr int kFindModeSlot = 31; // the Grand Prix mode's entity
constexpr uintptr_t kCards = 0x328;
constexpr uintptr_t kCardSize = 0x540;
constexpr uintptr_t kCardSlotId = 0x8; // the save a card shows; the card's first byte is 0 when it has none

// The mode's save list.
constexpr uintptr_t kModeObject = 0xf08;
constexpr uintptr_t kModeChildren = 0x238;
constexpr uintptr_t kModeChildrenEnd = 0x240;
constexpr uintptr_t kSaveSlotsVtable = 0x57b6060;
constexpr uintptr_t kSlotsBegin = 0x268;
constexpr uintptr_t kSlotsEnd = 0x270;
constexpr uintptr_t kCurrentSlot = 0x280;

// One save in the list.
constexpr uintptr_t kSlotId = 0x0;
constexpr uintptr_t kSlotGuid = 0x8;
constexpr uintptr_t kSlotMode = 0xa8;
constexpr uintptr_t kSlotDate = 0xc8;
constexpr uintptr_t kSlotFlagsOne = 0xd0;
constexpr uintptr_t kSlotFlagsTwo = 0xd2;
constexpr uintptr_t kSlotTypeFlags = 0x171;

inline bool is_save_list(uintptr_t object)
{
    uintptr_t vtable = 0;
    return object && swofty::mem::read(object, vtable) && vtable == swofty::api->module_base() + kSaveSlotsVtable;
}

// The save list of a mode entity: the mode object itself, or one of its parts.
inline uintptr_t find_save_list(uintptr_t entity)
{
    uintptr_t mode = 0;
    if (!entity || !swofty::mem::read(entity + kModeObject, mode) || !mode) {
        return 0;
    }
    if (is_save_list(mode)) {
        return mode;
    }
    uintptr_t begin = 0;
    uintptr_t end = 0;
    if (!swofty::mem::read(mode + kModeChildren, begin) || !swofty::mem::read(mode + kModeChildrenEnd, end) ||
        end < begin || (end - begin) / 8 > 256) {
        return 0;
    }
    for (uintptr_t at = begin; at < end; at += 8) {
        uintptr_t child = 0;
        if (swofty::mem::read(at, child) && is_save_list(child)) {
            return child;
        }
    }
    return 0;
}

// A new Grand Prix is being set up: from "Create New" until the play menu is left (Back) or its Play cards show again.
inline bool g_setting_up_new = false;

// Every Grand Prix save list seen this game run (they live as long as the game).
inline std::vector<uintptr_t> g_known_lists;

inline void remember_list(uintptr_t list)
{
    if (!list) {
        return;
    }
    for (uintptr_t known : g_known_lists) {
        if (known == list) {
            return;
        }
    }
    g_known_lists.push_back(list);
}

// The save list of the Grand Prix screen, or 0.
inline uintptr_t save_list_of_screen(void *screen)
{
    using FindMode = uintptr_t (*)(void *);
    uintptr_t vtable = 0;
    uintptr_t find_mode = 0;
    if (!swofty::mem::read(reinterpret_cast<uintptr_t>(screen), vtable) ||
        !swofty::mem::read(vtable + 8 * uintptr_t(kFindModeSlot), find_mode) || !find_mode) {
        return 0;
    }
    uintptr_t list = find_save_list(reinterpret_cast<FindMode>(find_mode)(screen));
    remember_list(list);
    return list;
}

inline uintptr_t card_address(void *screen, int card)
{
    return reinterpret_cast<uintptr_t>(screen) + kCards + uintptr_t(card) * kCardSize;
}

constexpr uint8_t kGrandPrixMode = 13;

// The Grand Prix saves that hold a game, newest first (each save's place in memory).
inline std::vector<uintptr_t> grand_prix_saves(uintptr_t list)
{
    std::vector<std::pair<uint64_t, uintptr_t>> dated;
    uintptr_t begin = 0;
    uintptr_t end = 0;
    if (!is_save_list(list) || !swofty::mem::read(list + kSlotsBegin, begin) ||
        !swofty::mem::read(list + kSlotsEnd, end) || end < begin || (end - begin) / 8 > 64) {
        return {};
    }
    for (uintptr_t at = begin; at < end; at += 8) {
        uintptr_t slot = 0;
        uint8_t mode = 0;
        uint8_t has_game = 0;
        uint64_t date = 0;
        if (!swofty::mem::read(at, slot) || !slot || !swofty::mem::read(slot + kSlotMode, mode) ||
            !swofty::mem::read(slot + kSlotFlagsTwo, has_game) || mode != kGrandPrixMode || !has_game) {
            continue;
        }
        swofty::mem::read(slot + kSlotDate, date);
        dated.push_back({date, slot});
    }
    std::sort(dated.begin(), dated.end(), [](const auto &first, const auto &second) {
        return first.first > second.first;
    });
    std::vector<uintptr_t> saves;
    for (const auto &entry : dated) {
        saves.push_back(entry.second);
    }
    return saves;
}

} // namespace grand_prix_saves
