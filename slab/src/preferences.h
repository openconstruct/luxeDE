/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <gtk/gtk.h>
static inline GSettings *slab_preferences(void)
{
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    GSettingsSchema *schema = source ? g_settings_schema_source_lookup(source, "org.luxe.slab", TRUE) : NULL;
    if (!schema) return NULL;
    GSettings *settings = g_settings_new_full(schema, NULL, NULL);
    g_settings_schema_unref(schema); return settings;
}
static inline gboolean slab_setting_integer_range(const char *key, int *minimum, int *maximum)
{
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    GSettingsSchema *schema = source ? g_settings_schema_source_lookup(source, "org.luxe.slab", TRUE) : NULL;
    if (!schema || !g_settings_schema_has_key(schema, key)) {
        if (schema) g_settings_schema_unref(schema);
        return FALSE;
    }
    GSettingsSchemaKey *schema_key = g_settings_schema_get_key(schema, key);
    g_autoptr(GVariant) range = g_settings_schema_key_get_range(schema_key);
    const char *kind = NULL; g_autoptr(GVariant) bounds = NULL;
    gboolean valid = FALSE;
    g_variant_get(range, "(&sv)", &kind, &bounds);
    if (g_str_equal(kind, "range") && g_variant_is_of_type(bounds, G_VARIANT_TYPE("(ii)"))) {
        g_variant_get(bounds, "(ii)", minimum, maximum);
        valid = TRUE;
    }
    g_settings_schema_key_unref(schema_key); g_settings_schema_unref(schema);
    return valid;
}
static inline void slab_theme_changed(GSettings *settings, const char *key, gpointer data)
{
    (void)key; (void)data;
    g_autofree char *theme = g_settings_get_string(settings, "gtk-theme");
    g_autofree char *icons = g_settings_get_string(settings, "icon-theme");
    g_autofree char *scheme = g_settings_get_string(settings, "color-scheme");
    g_autoptr(GSettings) prefs = slab_preferences();
    g_autofree char *override = prefs ? g_settings_get_string(prefs, "theme-name") : NULL;
    if (override && *override) { g_free(theme); theme = g_strdup(override); }
    g_object_set(gtk_settings_get_default(), "gtk-theme-name", theme,
        "gtk-icon-theme-name", icons, "gtk-application-prefer-dark-theme", g_str_equal(scheme, "prefer-dark"), NULL);
}
static inline void slab_override_changed(GSettings *settings, const char *key, gpointer data)
{
    (void)settings; (void)key; slab_theme_changed(G_SETTINGS(data), NULL, NULL);
}
static inline void slab_follow_theme(void)
{
    GtkSettings *gtk = gtk_settings_get_default();
    if (g_object_get_data(G_OBJECT(gtk), "slab-theme")) return;
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    GSettingsSchema *schema = source ? g_settings_schema_source_lookup(source, "org.gnome.desktop.interface", TRUE) : NULL;
    if (!schema) return; /* GTK/XSettings fallback outside luxe. */
    if (g_settings_schema_has_key(schema, "gtk-theme") && g_settings_schema_has_key(schema, "icon-theme") &&
        g_settings_schema_has_key(schema, "color-scheme")) {
        GSettings *settings = g_settings_new_full(schema, NULL, NULL);
        slab_theme_changed(settings, NULL, NULL);
        g_signal_connect(settings, "changed::gtk-theme", G_CALLBACK(slab_theme_changed), NULL);
        g_signal_connect(settings, "changed::icon-theme", G_CALLBACK(slab_theme_changed), NULL);
        g_signal_connect(settings, "changed::color-scheme", G_CALLBACK(slab_theme_changed), NULL);
        g_object_set_data_full(G_OBJECT(gtk), "slab-theme", settings, g_object_unref);
        GSettings *prefs = slab_preferences();
        if (prefs) {
            g_signal_connect(prefs, "changed::theme-name", G_CALLBACK(slab_override_changed), settings);
            g_object_set_data_full(G_OBJECT(gtk), "slab-theme-preferences", prefs, g_object_unref);
        }
    }
    g_settings_schema_unref(schema);
}
