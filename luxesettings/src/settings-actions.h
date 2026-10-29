/* Copyright (C) 2026 Jerry Howell
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <glib.h>

typedef struct {
    const char *section;
    const char *title;
    const char *subtitle;
    const char *icon_name;
    const char * const *argv;
    /* Wait asynchronously and surface exit errors for short-lived controls. */
    gboolean wait_for_exit;
} LuxeSettingsAction;

extern const LuxeSettingsAction luxe_settings_actions[];
extern const guint luxe_settings_actions_count;

/* Validate data before it reaches g_spawn_async().  Settings actions are
 * direct executable argument vectors; Luxe Settings never invokes a shell. */
gboolean luxe_settings_action_is_valid(const LuxeSettingsAction *action);

