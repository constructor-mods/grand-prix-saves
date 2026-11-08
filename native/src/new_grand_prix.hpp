// SPDX-License-Identifier: MIT
// "Create New": the game deletes the save on its first card, then starts the new Grand Prix in the save in use.
// Here it is pointed at a save that does not exist, so nothing is deleted, and an empty save is put in use first, so
// the new Grand Prix gets a save of its own. At the most saves allowed, the oldest save is the one that goes.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include "save_slots.hpp"

#include <windows.h>

#include <cstdint>

namespace grand_prix_saves {

constexpr int kStartNewSlot = 22; // the action that runs when "Start new Grand Prix" is accepted
constexpr uintptr_t kStartNewFunction = 0x1bcc790;
constexpr uint64_t kNoSave = 0xffffffffffffffff;
constexpr int kSavesToKeep = 10;

// The game's own "make a new save": it keeps the one in use and makes the new one the one in use.
constexpr uintptr_t kCreateSaveFunction = 0x29d1890;
constexpr uintptr_t kSlotName = 0x20;
constexpr uint8_t kNoMode = 255;              // a save that never held a game

using StartNew = void (*)(void *screen);
using CreateSave = int (*)(void *list, const char *name, uint64_t *new_id);
using SelectSave = int (*)(void *list, uint64_t save_id, bool force, uint32_t unused);
inline StartNew g_original_start_new = nullptr;

// A save with nothing in it: a new save made for a new Grand Prix that was then not set up.
inline bool is_empty_save(uintptr_t save)
{
    uint8_t mode = 0;
    uint8_t has_data = 1;
    return save && swofty::mem::read(save + kSlotMode, mode) && swofty::mem::read(save + kSlotFlagsTwo, has_data) &&
           mode == kNoMode && !has_data;
}

// Puts an empty save in use for the new Grand Prix: the one in use if it is empty, else an empty one left from
// before (so they never pile up), else a new one named like the one in use.
inline void make_new_save(uintptr_t list)
{
    uintptr_t current = 0;
    if (!swofty::mem::read(list + kCurrentSlot, current) || !current) {
        return;
    }
    if (is_empty_save(current)) {
        return;
    }
    uintptr_t begin = 0;
    uintptr_t end = 0;
    if (swofty::mem::read(list + kSlotsBegin, begin) && swofty::mem::read(list + kSlotsEnd, end) && end >= begin &&
        (end - begin) / 8 <= 64) {
        for (uintptr_t at = begin; at < end; at += 8) {
            uintptr_t save = 0;
            if (!swofty::mem::read(at, save) || !is_empty_save(save)) {
                continue;
            }
            auto select_save = reinterpret_cast<SelectSave>(swofty::api->resolve("Game_SaveSlotManager_SelectSlot"));
            uint64_t id = 0;
            swofty::mem::read(save + kSlotId, id);
            if (select_save && select_save(reinterpret_cast<void *>(list), id, false, 0) == 0) {
                swofty::log("[grand-prix-saves] new Grand Prix: empty save %llu used again",
                            static_cast<unsigned long long>(id));
                return;
            }
        }
    }
    uintptr_t name = 0;
    if (!swofty::mem::read(current + kSlotName, name) || !name) {
        return;
    }
    auto create_save = reinterpret_cast<CreateSave>(swofty::api->module_base() + kCreateSaveFunction);
    uint64_t new_id = 0;
    create_save(reinterpret_cast<void *>(list), reinterpret_cast<const char *>(name), &new_id);
    swofty::log("[grand-prix-saves] new Grand Prix: new save %llu", static_cast<unsigned long long>(new_id));
}

inline void start_new(void *screen)
{
    uintptr_t list = save_list_of_screen(screen);
    std::vector<uintptr_t> saves = grand_prix_saves(list);

    // The game deletes the save of the first card, so the id there is the one to change.
    uint64_t *delete_id = reinterpret_cast<uint64_t *>(card_address(screen, 0) + kCardSlotId);
    uint64_t first_id = *delete_id;
    g_setting_up_new = true;
    if (int(saves.size()) < kSavesToKeep) {
        *delete_id = kNoSave;
        make_new_save(list);
    } else {
        uint64_t oldest = 0;
        swofty::mem::read(saves.back() + kSlotId, oldest);
        *delete_id = oldest;
        swofty::log("[grand-prix-saves] %d saves, the most allowed: the oldest one goes", int(saves.size()));
    }
    g_original_start_new(screen);
    *delete_id = first_id;
}

inline void catch_start_new()
{
    uintptr_t base = swofty::api->module_base();
    auto *entry = reinterpret_cast<void **>(base + kContinueVtable + 8 * uintptr_t(kStartNewSlot));
    if (*entry != reinterpret_cast<void *>(base + kStartNewFunction)) {
        swofty::log("[grand-prix-saves] Grand Prix screen: this game build is not supported");
        return;
    }
    g_original_start_new = reinterpret_cast<StartNew>(*entry);
    DWORD old_protect = 0;
    if (!VirtualProtect(entry, sizeof(void *), PAGE_READWRITE, &old_protect)) {
        return;
    }
    *entry = reinterpret_cast<void *>(&start_new);
    VirtualProtect(entry, sizeof(void *), old_protect, &old_protect);
}

} // namespace grand_prix_saves
