// SPDX-License-Identifier: MIT
// The list of saves the Grand Prix screen shows: one card per save, newest first, then a card to start a new one.
// The screen's list builds one card for each entry of this list.
//
// Each entry gets the same data the game's own cards have: the game's card setup makes it under the entry,
// and the game's own card fill writes it, by lending the first stock card the entry's links for one fill.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include "game_functions.hpp"
#include "save_slots.hpp"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace grand_prix_saves {

constexpr uintptr_t kScreenData = 0x10;          // the screen's data root
constexpr const char *kListName = "gp_saves";
constexpr const char *kCardTemplate = "gp_save_card";
constexpr const char *kSelectionName = "gp_selected";
constexpr uintptr_t kSelectionLink = 0x58;       // in the selection node: the link to the focused card's entry
constexpr int kMostSaves = 10;

// A card's links to its data: 24 of them, 0x20 apart, from 0x18.
constexpr uintptr_t kFirstLink = 0x18;
constexpr uintptr_t kLinkStep = 0x20;
constexpr int kLinkCount = 24;

// A link: the node, then the next and previous links to the same node (a table pointer sits before it).
struct Link {
    void *node = nullptr;
    void *next = nullptr;
    void *previous = nullptr;
};

// One card of the list: its data node and the save it shows (0 for "Create New").
struct Row {
    uint8_t *entry = nullptr;
    uintptr_t save = 0;
};

inline uint64_t g_rebuilt_at = 0;                // when the list was last built again (after a delete)
constexpr uint64_t kPressQuietMs = 500;
inline std::vector<Row> g_rows;
inline uint8_t *g_selection = nullptr;   // the list writes its focused card here

inline void set_text(const GameData &data, void *registry, uint8_t *node, const char *name, const char *value)
{
    void *text = nullptr;
    data.make_text(&text, value, 0, 0);
    data.set_string(registry, node + 8, name, &text, 1);
}

// The day a save was last played, as the game shows it: "08 / 11 / 2026".
inline std::string last_played(uintptr_t save)
{
    uint64_t date = 0;
    swofty::mem::read(save + kSlotDate, date);
    std::time_t when = static_cast<std::time_t>(date);
    std::tm day = {};
    if (!date || localtime_s(&day, &when) != 0) {
        return "";
    }
    char text[32];
    std::snprintf(text, sizeof text, "%02d / %02d / %04d", day.tm_mday, day.tm_mon + 1, day.tm_year + 1900);
    return text;
}

inline Link *link_at(uint8_t *card, int index)
{
    return reinterpret_cast<Link *>(card + kFirstLink + uintptr_t(index) * kLinkStep);
}

// Fills a card's data with one save, using the game's own fill on the first stock card:
// the stock card's links are moved aside, the card's links lent to it, and both put back after.
inline void fill_with_game(const GameData &data, void *screen, uint8_t *card, uintptr_t save, FillCard fill)
{
    uint8_t *stock_card = static_cast<uint8_t *>(screen) + kCards;
    std::vector<Link> kept(kLinkCount);
    for (int index = 0; index < kLinkCount; index++) {
        data.move_link(&kept[index], link_at(stock_card, index));
        data.move_link(link_at(stock_card, index), link_at(card, index));
    }
    fill(screen, reinterpret_cast<void *>(save), 0);
    for (int index = 0; index < kLinkCount; index++) {
        data.move_link(link_at(card, index), link_at(stock_card, index));
        data.move_link(link_at(stock_card, index), &kept[index]);
    }
}

// How many cards the list had last time (to hide the ones left over after a delete).
inline int g_last_card_count = 0;
// Set when F2 is pressed on the screen: the next build is the list built again after a delete.
inline bool g_delete_pressed = false;

// The list entry at a place: the one there now, or a new one. The game names list entries entry_0, entry_1, ...;
// asking by name each time means no entry is ever kept after the game lets go of it (closing the screen frees them).
inline uint8_t *entry_at(const GameData &data, void *registry, void *list, int index)
{
    char name[32];
    std::snprintf(name, sizeof name, "entry_%d", index);
    uint32_t hash = name_hash(name);
    void **entry_handle = data.get_child(registry, static_cast<uint8_t *>(list) + 8, &hash);
    return entry_handle && *entry_handle ? static_cast<uint8_t *>(*entry_handle) : nullptr;
}

// Adds one card to the list. With a save, the game fills it; without, it is the card to start a new one.
inline void add_card(const GameData &data, void *registry, void *list, int index, void *screen, uintptr_t save,
                     FillCard fill)
{
    uint8_t *entry = entry_at(data, registry, list, index);
    if (!entry) {
        swofty::log("[grand-prix-saves] save list: card %d could not be made", index);
        return;
    }
    g_rows.push_back({entry, save});
    // The card finds its data under the entry's own name.
    char path[32];
    std::snprintf(path, sizeof path, "entry_%d", index);
    set_text(data, registry, entry, "type", kCardTemplate);
    set_text(data, registry, entry, "context_path", path);
    uint8_t yes = 1;
    data.set_bool(registry, entry + 8, "spawnable", &yes, 1);

    // A card of our own, only to make the data and lend its links; let go of after.
    auto *card = static_cast<uint8_t *>(std::calloc(1, kCardSize));
    if (!card) {
        return;
    }
    data.card_setup(card, entry + 8);
    if (save) {
        // As the game's card loop does before its fill: the card has a save, and when it was last played.
        data.set_has_save(card, 1);
        fill_with_game(data, screen, card, save, fill);
        set_text(data, registry, entry, "label", last_played(save).c_str());
    }
    Link empty;
    for (int link = 0; link < kLinkCount; link++) {
        data.move_link(link_at(card, link), &empty);
    }
    std::free(card);
}

// Builds the list each time the game fills its own cards (the screen opening, and after a delete): the Grand Prix
// saves, newest first, then "Create New". first_save is the save the game put on its first card; it gets it back.
inline void build_save_list(void *screen, const std::vector<uintptr_t> &saves, uintptr_t first_save, FillCard fill)
{
    const GameData &data = game_data();
    if (!game_data_found(data)) {
        swofty::log("[grand-prix-saves] save list: a game function was not found");
        return;
    }
    uintptr_t root = 0;
    void *registry = nullptr;
    if (!swofty::mem::read(reinterpret_cast<uintptr_t>(screen) + kScreenData, root) || !root ||
        !swofty::mem::read(data.registry_slot, registry) || !registry) {
        swofty::log("[grand-prix-saves] save list: the screen has no data root");
        return;
    }
    // After a delete the screen is told, once the list is filled again, so it builds its cards again.
    bool again = g_delete_pressed;
    g_delete_pressed = false;

    uint32_t list_hash = name_hash(kListName);
    void **list_handle = data.get_list_child(registry, reinterpret_cast<void *>(root + 8), &list_hash);
    if (!list_handle || !*list_handle) {
        swofty::log("[grand-prix-saves] save list: the list could not be made");
        return;
    }
    // Where the list writes its focused card.
    Link no_card;
    void **selection_handle = data.set_node_ref(registry, reinterpret_cast<void *>(root + 8), kSelectionName, &no_card, 1);
    g_selection = selection_handle && *selection_handle ? static_cast<uint8_t *>(*selection_handle) : nullptr;

    g_rows.clear();
    int index = 0;
    for (uintptr_t save : saves) {
        if (index >= kMostSaves) {
            break;
        }
        add_card(data, registry, *list_handle, index, screen, save, fill);
        index++;
    }
    // One more card to start a new Grand Prix, while there is room for another save.
    int cards = index;
    if (index < kMostSaves) {
        add_card(data, registry, *list_handle, index, screen, 0, fill);
        cards++;
    }
    // Entries left over from a longer list (a save was deleted) are hidden.
    uint8_t no = 0;
    for (int left_over = cards; left_over < g_last_card_count; left_over++) {
        uint8_t *entry = entry_at(data, registry, *list_handle, left_over);
        if (entry) {
            data.set_bool(registry, entry + 8, "spawnable", &no, 1);
        }
    }
    g_last_card_count = cards;
    // The first stock card shows its own save again (none when the list is built again by the addon).
    if (first_save) {
        fill(screen, reinterpret_cast<void *>(first_save), 0);
    }
    swofty::log("[grand-prix-saves] save list: %d save card(s)%s%s", index, index < kMostSaves ? " and a new card" : "",
                again ? " (built again)" : "");
    if (again) {
        g_rebuilt_at = GetTickCount64();
        send_screen_event(screen, "gp_saves_rebuilt");
    }
}

// The card the list has focused, or -1. The list's selection holds a link to one of the card entries.
inline int focused_row()
{
    if (!g_selection) {
        return -1;
    }
    // The selection holds a link to the focused entry at 0x58; the link's node is 8 bytes into it.
    uintptr_t link = 0;
    uintptr_t entry = 0;
    if (!swofty::mem::read(reinterpret_cast<uintptr_t>(g_selection) + kSelectionLink, link) || !link ||
        !swofty::mem::read(link + 8, entry)) {
        return -1;
    }
    for (size_t row = 0; row < g_rows.size(); row++) {
        if (entry == reinterpret_cast<uintptr_t>(g_rows[row].entry)) {
            return int(row);
        }
    }
    return -1;
}

} // namespace grand_prix_saves
