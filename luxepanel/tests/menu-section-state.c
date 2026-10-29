/* Copyright (C) 2026 Jerry Howell
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <glib.h>

#include "menu-section-state.h"

static void
test_sections_default_closed_and_remember(void)
{
    LuxeMenuSectionStates *states = luxe_menu_section_states_new();

    g_assert_true(luxe_menu_section_states_get(states, "system:/Applications/Accessories"));
    luxe_menu_section_states_set(states, "system:/Applications/Accessories", FALSE);
    g_assert_false(luxe_menu_section_states_get(states, "system:/Applications/Accessories"));
    g_assert_true(luxe_menu_section_states_get(states, "system:/Applications/System"));
    luxe_menu_section_states_set(states, "system:/Applications/Accessories", TRUE);
    g_assert_true(luxe_menu_section_states_get(states, "system:/Applications/Accessories"));
    luxe_menu_section_states_free(states);
}

int
main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/menu/section-state/default-closed-and-remembers",
                    test_sections_default_closed_and_remember);
    return g_test_run();
}
