/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
/* Runs inside the existing session, not a new daemon. Preserve accessibility
 * overrides on old GTK, which cannot use prefers-contrast CSS media queries. */
#include "theme-watch.h"
#include "safe-file.h"
#include "../luxeappearance/src/theme-presets.h"
#include <gio/gio.h>

static GSettings *a11y, *interface;
static GFileMonitor *monitor, *root_monitor;
static guint pending;

static gboolean refresh(gpointer data)
{
    (void)data;
    pending = 0;
    g_autofree char *path = g_build_filename(g_get_user_config_dir(), "luxe", "appearance.ini", NULL);
    g_autofree char *contents = NULL;
    gsize len;
    g_autoptr(GKeyFile) key = g_key_file_new();
    if(!lxsession_read_bounded_file(path, 65536, &contents, &len, NULL) ||
       !g_key_file_load_from_data(key, contents, len, 0, NULL)) return G_SOURCE_REMOVE;
    g_autofree char *id = g_key_file_get_string(key, "Appearance", "theme", NULL);
    g_autofree char *name = g_strdup_printf("Luxe-%s", id ? id : "");
    const LuxeTheme *theme = luxe_theme_find(name);
    if(!theme && g_strcmp0(id, "custom")) return G_SOURCE_REMOVE;
    if(a11y && g_settings_get_boolean(a11y, "high-contrast")) theme = NULL;
    if(interface) {
        g_autofree char *selected = g_settings_get_string(interface, "gtk-theme");
        if(g_strcmp0(name, selected)) theme = NULL;
    }
    /* Refuses custom/symlinked CSS and writes only the marked bridge. Changes
     * reach newly launched apps; older libadwaita clients may need restart. */
    g_autoptr(GError) error = NULL;
    if(!luxe_theme_refresh_bridge(theme, &error))
        g_message("luxe theme bridge not updated: %s", error ? error->message : "invalid input");
    return G_SOURCE_REMOVE;
}

static void changed(void)
{
    if(!pending) pending = g_timeout_add(200, refresh, NULL);
}

static void watch_directory(void);

static void settings_changed(GSettings *settings, const char *key, gpointer data)
{
    (void)settings; (void)key; (void)data; changed();
}

static void file_changed(GFileMonitor *watch, GFile *file, GFile *other, GFileMonitorEvent event, gpointer data)
{
    (void)watch; (void)other; (void)event; (void)data;
    g_autofree char *base = g_file_get_basename(file);
    if(!g_strcmp0(base, "luxe")) watch_directory();
    if(!g_strcmp0(base, "appearance.ini") || !g_strcmp0(base, "luxe")) changed();
}

static GSettings *watch_settings(const char *schema_name, const char *key)
{
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    g_autoptr(GSettingsSchema) schema = source ? g_settings_schema_source_lookup(source, schema_name, TRUE) : NULL;
    if(!schema || !g_settings_schema_has_key(schema, key)) return NULL;
    GSettings *settings = g_settings_new_full(schema, NULL, NULL);
    g_signal_connect(settings, "changed", G_CALLBACK(settings_changed), NULL);
    return settings;
}

static void watch_directory(void)
{
    g_clear_object(&monitor);
    g_autofree char *path = g_build_filename(g_get_user_config_dir(), "luxe", NULL);
    if(g_file_test(path, G_FILE_TEST_IS_DIR)) {
        g_autoptr(GFile) directory = g_file_new_for_path(path);
        monitor = g_file_monitor_directory(directory, G_FILE_MONITOR_NONE, NULL, NULL);
        if(monitor) g_signal_connect(monitor, "changed", G_CALLBACK(file_changed), NULL);
    }
}

void luxe_theme_watch_start(void)
{
    if(a11y || interface || root_monitor) return;
    a11y = watch_settings("org.gnome.desktop.a11y.interface", "high-contrast");
    interface = watch_settings("org.gnome.desktop.interface", "gtk-theme");
    /* Parent watch sees first creation; child watch sees atomic file renames. */
    g_autoptr(GFile) root = g_file_new_for_path(g_get_user_config_dir());
    root_monitor = g_file_monitor_directory(root, G_FILE_MONITOR_NONE, NULL, NULL);
    if(root_monitor) g_signal_connect(root_monitor, "changed", G_CALLBACK(file_changed), NULL);
    watch_directory();
    refresh(NULL);
}

void luxe_theme_watch_stop(void)
{
    if(pending) g_source_remove(pending);
    pending = 0;
    g_clear_object(&monitor);
    g_clear_object(&root_monitor);
    g_clear_object(&a11y);
    g_clear_object(&interface);
}
