// SPDX-License-Identifier: MIT
// Grand Prix Saves: keeps more than one Grand Prix save.
// Include mixin.hpp first: registry.hpp redefines SWOFTY_ADDON so the DLL exports SwoftyAddonRegister.
#include <swofty/mixin.hpp>
#include <swofty/mem.hpp>

#include <swofty/registry.hpp>

#include "stock_cards.hpp"
#include "list_actions.hpp"
#include "new_grand_prix.hpp"
#include "resume_save.hpp"

SWOFTY_ON_LOAD
{
    grand_prix_saves::catch_cards();
    grand_prix_saves::catch_commands();
    grand_prix_saves::catch_start_new();
    grand_prix_saves::catch_menu();
}

SWOFTY_REGISTER
{
}

SWOFTY_ADDON()
