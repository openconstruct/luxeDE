/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "../theme-watch.h"
#include "../../luxeappearance/src/theme-presets.h"
#include <glib/gstdio.h>
#include <unistd.h>
#include <string.h>

static void settle(void)
{
    gint64 until = g_get_monotonic_time() + 350000;
    while(g_get_monotonic_time() < until) {
        while(g_main_context_iteration(NULL, FALSE));
        g_usleep(1000);
    }
}

static void check_bridge(gboolean colored)
{
    g_autofree char *path = g_build_filename(g_get_user_config_dir(), "gtk-4.0", "luxe-colors.css", NULL);
    g_autofree char *data = NULL;
    g_assert_true(g_file_get_contents(path, &data, NULL, NULL));
    g_assert_true(g_str_has_prefix(data, "/* luxe managed color bridge */\n"));
    g_assert_cmpint(strstr(data, "#2e3440") != NULL, ==, colored);
}

int main(void)
{
    g_autofree char *tmp = g_dir_make_tmp("luxe-theme-watch-XXXXXX", NULL);
    g_assert_nonnull(tmp);
    g_setenv("XDG_CONFIG_HOME", tmp, TRUE);
    g_setenv("GSETTINGS_BACKEND", "memory", TRUE);
    g_autoptr(GSettings) interface = g_settings_new("org.gnome.desktop.interface");
    g_autoptr(GSettings) a11y = g_settings_new("org.gnome.desktop.a11y.interface");
    g_settings_set_string(interface, "gtk-theme", "Luxe-nord");
    luxe_theme_watch_start();
    /* First preset directory appears after session startup. */
    g_autofree char *dir = g_build_filename(tmp, "luxe", NULL);
    g_assert_cmpint(g_mkdir(dir, 0700), ==, 0);
    g_autofree char *state = g_build_filename(dir, "appearance.ini", NULL);
    g_assert_true(g_file_set_contents(state, "[Appearance]\ntheme=nord\n", -1, NULL));
    g_autoptr(GError) error = NULL;
    g_assert_true(luxe_theme_write_bridge(luxe_theme_find("Luxe-nord"), &error));
    settle(); check_bridge(TRUE);
    g_settings_set_boolean(a11y, "high-contrast", TRUE);
    settle(); check_bridge(FALSE);
    g_settings_set_boolean(a11y, "high-contrast", FALSE);
    settle(); check_bridge(TRUE);
    g_settings_set_string(interface, "gtk-theme", "HighContrast");
    settle(); check_bridge(FALSE);
    g_settings_set_string(interface, "gtk-theme", "Luxe-nord");
    settle(); check_bridge(TRUE);
    luxe_theme_watch_stop();
    luxe_theme_watch_start(); check_bridge(TRUE); luxe_theme_watch_stop();
    /* Do not follow a replaced custom bridge/symlink. */
    g_autofree char *bridge = g_build_filename(tmp, "gtk-4.0", "luxe-colors.css", NULL);
    g_autofree char *target = g_build_filename(tmp, "keep.css", NULL);
    g_assert_true(g_file_set_contents(target, "keep me", -1, NULL));
    g_assert_cmpint(g_unlink(bridge), ==, 0);
    g_assert_cmpint(symlink(target, bridge), ==, 0);
    g_assert_false(luxe_theme_refresh_bridge(NULL, &error));
    g_clear_error(&error);
    g_autofree char *kept = NULL;
    g_assert_true(g_file_get_contents(target, &kept, NULL, NULL));
    g_assert_cmpstr(kept, ==, "keep me");
    g_unlink(bridge); g_unlink(target); g_unlink(state);
    g_autofree char *css = g_build_filename(tmp, "gtk-4.0", "gtk.css", NULL);
    g_unlink(css);
    g_autofree char *gtkdir = g_build_filename(tmp, "gtk-4.0", NULL);
    g_rmdir(gtkdir); g_rmdir(dir); g_rmdir(tmp);
    g_print("PASS: theme watch, first creation, high contrast, external themes, teardown and symlink preservation\n");
    return 0;
}
