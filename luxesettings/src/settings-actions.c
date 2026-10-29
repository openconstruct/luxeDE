/* Copyright (C) 2026 Jerry Howell
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "settings-actions.h"

#include <glib/gi18n.h>
#include <string.h>

static const char * const appearance_argv[] = { "luxeappearance", NULL };
static const char * const decorations_argv[] = { "luxeappearance-obconf", NULL };
static const char * const desktop_argv[] = { "frame", "--wallpaper-settings", NULL };
static const char * const panel_argv[] = { "luxepanelctl", "config", NULL };
static const char * const input_argv[] = { "luxeinput", NULL };
static const char * const display_argv[] = { "luxerandr", NULL };
static const char * const power_argv[] = { "luxepower", NULL };
static const char * const hotkey_argv[] = { "luxehotkey", "--gui=gtk", NULL };
static const char * const defaults_argv[] = { "luxesession-default-apps", NULL };
static const char * const startup_argv[] = { "luxesession-edit", NULL };
static const char * const software_argv[] = { "luxurystore", NULL };

const LuxeSettingsAction luxe_settings_actions[] = {
    { N_("Appearance"), N_("Appearance"), N_("Theme, colors, fonts, icons, and light or dark mode"), "luxeappearance", appearance_argv, FALSE },
    { N_("Appearance"), N_("Window Decorations"), N_("Openbox theme, window borders, behavior, and virtual desktops"), "org.luxe.ObConf", decorations_argv, FALSE },
    { N_("Desktop"), N_("Wallpaper"), N_("Choose a wallpaper folder; rotate daily or at next login"), "preferences-desktop-wallpaper", desktop_argv, FALSE },
    { N_("Desktop"), N_("Panel"), N_("Panel layout, applets, and taskbar behavior"), "luxepanel", panel_argv, TRUE },
    { N_("Devices"), N_("Keyboard & Mouse"), N_("Pointer behavior, key repeat, sound, and optional keyboard layout editing"), "luxeinput", input_argv, FALSE },
    { N_("Devices"), N_("Displays"), N_("Screen resolution, layout, and monitor settings"), "luxerandr", display_argv, FALSE },
    { N_("Devices"), N_("Power"), N_("Battery, sleep, lid, brightness, and locking"), "org.luxe.LuxePower", power_argv, FALSE },
    { N_("System"), N_("Keyboard Shortcuts"), N_("Manage global keyboard shortcuts"), "luxehotkey", hotkey_argv, FALSE },
    { N_("System"), N_("Default Applications"), N_("Choose the applications Luxe opens by default"), "luxesession-default-apps", defaults_argv, FALSE },
    { N_("System"), N_("Startup Applications"), N_("Manage programs that start with your session"), "luxesession-edit", startup_argv, FALSE },
    { N_("System"), N_("Software"), N_("Browse, install, and update software"), "luxurystore", software_argv, FALSE },
};

const guint luxe_settings_actions_count = G_N_ELEMENTS(luxe_settings_actions);

gboolean
luxe_settings_action_is_valid(const LuxeSettingsAction *action)
{
    const char * const *argument;

    if (action == NULL || action->section == NULL || action->title == NULL ||
        action->subtitle == NULL || action->icon_name == NULL || action->argv == NULL ||
        action->argv[0] == NULL || action->argv[0][0] == '\0')
        return FALSE;

    /* Direct argv entries make launching deterministic and keep shell syntax
     * out of the catalog entirely. */
    for (argument = action->argv; *argument != NULL; argument++) {
        if ((*argument)[0] == '\0' || strpbrk(*argument, "|&;<>`$") != NULL ||
            strchr(*argument, '\n') != NULL || strchr(*argument, '\r') != NULL)
            return FALSE;
    }

    return strcmp(action->argv[0], "sh") != 0 &&
           strcmp(action->argv[0], "bash") != 0 &&
           strcmp(action->argv[0], "/bin/sh") != 0 &&
           strcmp(action->argv[0], "/bin/bash") != 0;
}

