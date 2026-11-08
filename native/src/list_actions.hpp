// SPDX-License-Identifier: MIT
// What the save list's cards do. The list raises two commands of its own; for each, the focused card's save is put on
// the screen's first stock card (as the game's card loop does), then the game's own focus or press runs for that card.
// The stock screen shows only the newest save, so its second card is always the game's own "start new" card.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include "stock_cards.hpp"
#include "save_list.hpp"
#include "save_slots.hpp"

#include <windows.h>

#include <cstdint>

namespace grand_prix_saves {

constexpr int kCommandSlot = 10;                 // the screen's command handler
constexpr uintptr_t kCommandFunction = 0x1a0b280;
constexpr uintptr_t kFocusCard = 0xc8;           // the screen's own "a card was focused" (screen, card)
constexpr uintptr_t kPressCard = 0xc0;           // the screen's own "a card was pressed" (screen, card)
constexpr uintptr_t kScreenState = 0x1368;
constexpr uint8_t kStateStartNew = 3;            // on its way out to start a new Grand Prix
constexpr int kNewCard = 1;                      // the stock card that is always "start new"

using Command = uint64_t (*)(void *screen, void *command, void *data, void *more);
using CardAction = uint64_t (*)(void *screen, uint64_t card);

inline Command g_original_command = nullptr;

inline uint64_t run_screen_function(void *screen, uintptr_t offset, uint64_t card)
{
    uintptr_t table = 0;
    uintptr_t function = 0;
    if (!swofty::mem::read(reinterpret_cast<uintptr_t>(screen), table) ||
        !swofty::mem::read(table + offset, function) || !function) {
        return 0;
    }
    return reinterpret_cast<CardAction>(function)(screen, card);
}

// Saves always go on stock card 0 (the game's own first card; stock card 2 is never set up by the game and shows no
// track). Stock card 1 is always the game's "start new" card.
constexpr int kSaveCard = 0;

// The game's card loop sets the card's track (title, picture, flag) after its fill, from the save's track number.
constexpr uintptr_t kSetCardTrack = 0x1a3a960;   // (screen, card, track)
constexpr uintptr_t kSlotTrack = 0xb5;           // in a save: its track number
using SetCardTrack = void (*)(void *screen, uint64_t card, uint64_t track);

// Puts a save on stock card 0, as the game's card loop does: its id, "has a save", the game's fill, then its track.
inline void put_on_save_card(void *screen, uintptr_t save)
{
    const GameData &data = game_data();
    uint8_t *card = static_cast<uint8_t *>(screen) + kCards + uintptr_t(kSaveCard) * kCardSize;
    uint64_t id = 0;
    swofty::mem::read(save + kSlotId, id);
    *reinterpret_cast<uint64_t *>(card + kCardSlotId) = id;
    data.set_has_save(card, 1);
    g_original_fill(screen, reinterpret_cast<void *>(save), uint64_t(kSaveCard));
    uint8_t track = 0;
    swofty::mem::read(save + kSlotTrack, track);
    auto set_card_track = reinterpret_cast<SetCardTrack>(swofty::api->module_base() + kSetCardTrack);
    set_card_track(screen, kSaveCard, track);
}

// Makes stock card 1 the game's "start new" card again: no save, and the right panel's "New Grand Prix" look
// (its use_new_variation switch, link 0x38) on. A save shown on it earlier would otherwise stay in the panel.
constexpr uintptr_t kNewLookLink = 0x38;
constexpr uintptr_t kSetBoolNode = 0x973e20;     // sets a yes/no data node (node, value, 0, 0)
using SetBoolNode = void (*)(void *node, uint8_t *value, void *unused_1, void *unused_2);

inline void make_new_card(void *screen)
{
    const GameData &data = game_data();
    uint8_t *card = static_cast<uint8_t *>(screen) + kCards + uintptr_t(kNewCard) * kCardSize;
    data.set_has_save(card, 0);
    uintptr_t node = 0;
    if (swofty::mem::read(reinterpret_cast<uintptr_t>(card) + kNewLookLink, node) && node) {
        auto set_bool_node = reinterpret_cast<SetBoolNode>(swofty::api->module_base() + kSetBoolNode);
        uint8_t yes = 1;
        set_bool_node(reinterpret_cast<void *>(node), &yes, nullptr, nullptr);
    }
}

// Starts a new Grand Prix straight away: the screen leaves as it does after "Accept" on the game's warning.
inline void start_new_now(void *screen)
{
    *(static_cast<uint8_t *>(screen) + kScreenState) = kStateStartNew;
    send_screen_event(screen, "start_transition_out");
}

inline uint64_t command(void *screen, void *command_data, void *data, void *more)
{
    uint32_t name = 0;
    uintptr_t name_at = 0;
    if (command_data && swofty::mem::read(reinterpret_cast<uintptr_t>(command_data), name_at) && name_at) {
        swofty::mem::read(name_at, name);
    }
    bool focused = name == name_hash("gp_card_focused");
    bool pressed = name == name_hash("gp_card_pressed");
    if (name == name_hash("delete_slot_pressed")) {
        // F2: if the game deletes the save, the list it fills next is the list built again after a delete.
        g_delete_pressed = true;
    }
    if (name == name_hash("transition_out_finished")) {
        g_delete_pressed = false;
    }
    if (!focused && !pressed) {
        return g_original_command(screen, command_data, data, more);
    }
    int row = focused_row();
    if (row < 0 || row >= int(g_rows.size())) {
        swofty::log("[grand-prix-saves] card %s: no card focused", focused ? "focus" : "press");
        return 0;
    }
    uintptr_t save = g_rows[row].save;
    if (focused) {
        if (save) {
            // The right panel only shows all of a card again (the track picture too) when another card is selected:
            // so the "start new" card is selected first, then the save's card.
            make_new_card(screen);
            run_screen_function(screen, kFocusCard, kNewCard);
            put_on_save_card(screen, save);
            return run_screen_function(screen, kFocusCard, kSaveCard);
        }
        make_new_card(screen);
        return run_screen_function(screen, kFocusCard, kNewCard);
    }
    // The Enter that accepts the game's delete message must not also press the card the list lands on.
    if (GetTickCount64() - g_rebuilt_at < kPressQuietMs) {
        swofty::log("[grand-prix-saves] card press right after the list was built again: ignored");
        return 0;
    }
    if (save) {
        // Put on the save card again (the screen may have filled its cards since the focus), then pressed.
        put_on_save_card(screen, save);
        swofty::log("[grand-prix-saves] continue the save on card %d", row + 1);
        return run_screen_function(screen, kPressCard, kSaveCard);
    }
    swofty::log("[grand-prix-saves] start a new Grand Prix (no warning)");
    start_new_now(screen);
    return 0;
}

inline void catch_commands()
{
    bool caught = swap_entry(kCommandSlot, kCommandFunction, reinterpret_cast<void *>(&command),
                             reinterpret_cast<void **>(&g_original_command));
    swofty::log("[grand-prix-saves] card actions: %s", caught ? "on" : "off (this game build is not supported)");
}

} // namespace grand_prix_saves
