/* Copyright (C) 2026 Jerry Howell
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <glib.h>

typedef struct _LuxeMenuSectionStates LuxeMenuSectionStates;

LuxeMenuSectionStates *luxe_menu_section_states_new(void);
void luxe_menu_section_states_free(LuxeMenuSectionStates *states);

/* Sections begin folded.  The caller keeps these states only for the
 * lifetime it chooses (the panel menu keeps them for its running session). */
gboolean luxe_menu_section_states_get(LuxeMenuSectionStates *states,
                                      const char *section_key);
void luxe_menu_section_states_set(LuxeMenuSectionStates *states,
                                  const char *section_key,
                                  gboolean collapsed);
