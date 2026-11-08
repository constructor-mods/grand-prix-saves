// SPDX-License-Identifier: MIT
// The game's own functions the addon uses, found by name through the launcher, or at their place in this game build.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <cstdint>

namespace grand_prix_saves {

// Game functions this build of the game has at these places.
constexpr uintptr_t kCardSetup = 0x1a3dcb0;      // makes a card's data under a parent and links the card to it
constexpr uintptr_t kMoveLink = 0x42bc170;       // moves a link: the old one is let go, the new one takes its place
constexpr uintptr_t kSetHasSave = 0x1a09040;     // marks a card as holding a save (or not)

using GetListChild = void **(*)(void *registry, void *parent, const uint32_t *hash);
using SetString = void *(*)(void *registry, void *parent, const char *name, void **value, int overwrite);
using SetBool = void *(*)(void *registry, void *parent, const char *name, uint8_t *value, int overwrite);
using SetNodeRef = void **(*)(void *registry, void *parent, const char *name, void *value, int overwrite);
using MakeText = uint32_t (*)(void **out, const char *text, uint32_t flags, uintptr_t length);
using CardSetup = void (*)(void *card, void *parent);
using MoveLink = void (*)(void *to, void *from);
using SetHasSave = void (*)(void *card, uint8_t has_save);
using FillCard = uint64_t (*)(void *screen, void *save, uint64_t card);

struct GameData {
    GetListChild get_list_child = nullptr;
    GetListChild get_child = nullptr;
    SetString set_string = nullptr;
    SetBool set_bool = nullptr;
    SetNodeRef set_node_ref = nullptr;
    MakeText make_text = nullptr;
    CardSetup card_setup = nullptr;
    MoveLink move_link = nullptr;
    SetHasSave set_has_save = nullptr;
    uintptr_t registry_slot = 0;
};

// Sends a named event to the screen, as the game does (for example "start_transition_out").
constexpr uintptr_t kSendEvent = 0x4320fe0;
struct ScreenEvent {
    uint32_t kind = 4;
    uint32_t unused_1 = 0;
    uint32_t value = 0;
    uint32_t unused_2 = 0;
    void *extra = nullptr;
};
using SendEvent = void (*)(void *screen, const char *name, void *event, void *more);

inline void send_screen_event(void *screen, const char *name)
{
    auto send_event = reinterpret_cast<SendEvent>(swofty::api->module_base() + kSendEvent);
    ScreenEvent event;
    send_event(screen, name, &event, nullptr);
}

inline uint32_t name_hash(const char *text)
{
    uint32_t hash = 5381;
    for (const char *at = text; *at; at++) {
        hash = hash * 33 + uint8_t(*at);
    }
    return hash;
}

// Finds a game function by name; when the launcher doesn't know the name, uses its place in this game build.
inline uintptr_t find_function(const char *name, uintptr_t place_in_build)
{
    uintptr_t address = name ? swofty::api->resolve(name) : 0;
    if (address) {
        return address;
    }
    return place_in_build ? swofty::api->module_base() + place_in_build : 0;
}

inline const GameData &game_data()
{
    static GameData data;
    static bool looked = false;
    if (!looked) {
        looked = true;
        data.get_list_child = reinterpret_cast<GetListChild>(find_function("Vm_GetOrCreate_ListChild", 0x42b30e0));
        data.get_child = reinterpret_cast<GetListChild>(find_function("Vm_GetOrCreate_ChildNode", 0x42b2bf0));
        data.set_string = reinterpret_cast<SetString>(find_function("Vm_Set_String", 0x846270));
        data.set_bool = reinterpret_cast<SetBool>(find_function("Vm_Set_Bool", 0x975db0));
        data.set_node_ref = reinterpret_cast<SetNodeRef>(find_function("Vm_Set_NodeRef", 0x83c270));
        data.make_text = reinterpret_cast<MakeText>(find_function("Ne_Make_StrFromCStr", 0x41974e0));
        data.card_setup = reinterpret_cast<CardSetup>(find_function(nullptr, kCardSetup));
        data.move_link = reinterpret_cast<MoveLink>(find_function("Vm_MoveAssign_NodeHandle", kMoveLink));
        data.set_has_save = reinterpret_cast<SetHasSave>(find_function(nullptr, kSetHasSave));
        data.registry_slot = find_function("g_Vm_Registry", 0x6ca54f8);
    }
    return data;
}

inline bool game_data_found(const GameData &data)
{
    return data.get_list_child && data.get_child && data.set_string && data.set_bool && data.set_node_ref &&
           data.make_text &&
           data.card_setup && data.move_link && data.set_has_save && data.registry_slot;
}

} // namespace grand_prix_saves
