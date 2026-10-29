/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXEPOLKIT_THEME_SCHEME_H
#define LUXEPOLKIT_THEME_SCHEME_H

#include <gio/gio.h>
#include <gtk/gtk.h>

static GSettings *luxepolkit_theme_settings_new(void)
{
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    GSettingsSchema *schema = source ? g_settings_schema_source_lookup(
        source, "org.gnome.desktop.interface", TRUE) : NULL;
    GSettings *settings = NULL;

    if (schema != NULL && g_settings_schema_has_key(schema, "color-scheme"))
        settings = g_settings_new_full(schema, NULL, NULL);
    if (schema != NULL)
        g_settings_schema_unref(schema);
    return settings;
}

static void luxepolkit_apply_color_scheme(GSettings *settings)
{
    if (settings == NULL || !gtk_is_initialized())
        return;
    g_autofree char *scheme = g_settings_get_string(settings, "color-scheme");
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme",
                 g_strcmp0(scheme, "prefer-dark") == 0, NULL);
}

#endif
