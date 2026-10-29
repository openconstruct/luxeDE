/* Copyright (C) 2026 Jerry Howell
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "menu-section-state.h"

struct _LuxeMenuSectionStates
{
    GHashTable *collapsed;
};

LuxeMenuSectionStates *
luxe_menu_section_states_new(void)
{
    LuxeMenuSectionStates *states = g_new0(LuxeMenuSectionStates, 1);

    states->collapsed = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    return states;
}

void
luxe_menu_section_states_free(LuxeMenuSectionStates *states)
{
    if (states == NULL)
        return;

    g_hash_table_unref(states->collapsed);
    g_free(states);
}

gboolean
luxe_menu_section_states_get(LuxeMenuSectionStates *states, const char *section_key)
{
    gpointer value;

    g_return_val_if_fail(states != NULL, TRUE);
    g_return_val_if_fail(section_key != NULL, TRUE);
    value = g_hash_table_lookup(states->collapsed, section_key);
    return value == NULL || GPOINTER_TO_INT(value) == 2;
}

void
luxe_menu_section_states_set(LuxeMenuSectionStates *states, const char *section_key,
                             gboolean collapsed)
{
    g_return_if_fail(states != NULL);
    g_return_if_fail(section_key != NULL);
    g_hash_table_replace(states->collapsed, g_strdup(section_key),
                         GINT_TO_POINTER(collapsed ? 2 : 1));
}
