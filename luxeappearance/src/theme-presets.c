/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "theme-presets.h"
#include "../../luxesession/safe-file.h"
#include <glib/gstdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#define BEGIN_MARK "/* BEGIN luxe managed colors */\n"
#define END_MARK "/* END luxe managed colors */\n"
#define BRIDGE_MARK "/* luxe managed color bridge */\n"

const LuxeTheme *luxe_theme_find(const char *name)
{
    if(!name || !g_str_has_prefix(name, "Luxe-")) return NULL;
    for(guint i = 0; i < G_N_ELEMENTS(luxe_themes); i++)
        if(g_str_equal(name + 5, luxe_themes[i].id)) return &luxe_themes[i];
    return NULL;
}

char *luxe_theme_colors(const LuxeTheme *theme, GError **error)
{
    if(!theme) return g_strdup("");
    g_autofree char *path = g_build_filename(LUXE_THEME_BRIDGE_DIR, theme->id, "colors.css", NULL);
    g_autofree char *legacy = NULL;
    gsize size;
    if(!lxsession_read_bounded_file(path, 65536, &legacy, &size, error)) return NULL;
    if(gtk_get_minor_version() < 16) return g_steal_pointer(&legacy);
    g_autofree char *modern_path = g_build_filename(LUXE_THEME_BRIDGE_DIR, theme->id, "modern.css", NULL);
    g_autofree char *modern = NULL;
    if(!lxsession_read_bounded_file(modern_path, 65536, &modern, &size, error)) return NULL;
    return g_strconcat(legacy, "\n", modern, NULL);
}

/* Insert imports before user rules, so explicit custom CSS continues to win.
 * Remove exactly our marked block, not other imports or matching user colors. */
char *luxe_theme_css_update(const char *existing, gboolean enabled, GError **error)
{
    const char *begin = strstr(existing, BEGIN_MARK);
    const char *end = strstr(existing, END_MARK);
    g_autofree char *preserved = NULL;
    if(begin || end) {
        if(!begin || !end || end < begin ||
           strstr(begin + strlen(BEGIN_MARK), BEGIN_MARK) ||
           strstr(end + strlen(END_MARK), END_MARK)) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "Malformed luxe CSS marker; custom CSS has not been replaced");
            return NULL;
        }
        g_autofree char *prefix = g_strndup(existing, begin - existing);
        preserved = g_strconcat(prefix, end + strlen(END_MARK), NULL);
    } else preserved = g_strdup(existing);
    return enabled ? g_strconcat(BEGIN_MARK, "@import url(\"luxe-colors.css\");\n", END_MARK, preserved, NULL)
                   : g_steal_pointer(&preserved);
}

static gboolean read_optional(const char *path, char **data, GError **error)
{
    struct stat st;
    gsize len;
    if(g_lstat(path, &st) != 0 && errno == ENOENT) {
        *data = g_strdup("");
        return TRUE;
    }
    return lxsession_read_bounded_file(path, 1024 * 1024, data, &len, error);
}

gboolean luxe_theme_write_bridge(const LuxeTheme *theme, GError **error)
{
    g_autofree char *css = g_build_filename(g_get_user_config_dir(), "gtk-4.0", "gtk.css", NULL);
    g_autofree char *bridge = g_build_filename(g_get_user_config_dir(), "gtk-4.0", "luxe-colors.css", NULL);
    g_autofree char *existing = NULL;
    g_autofree char *old_bridge = NULL;
    if(!read_optional(css, &existing, error) || !read_optional(bridge, &old_bridge, error)) return FALSE;
    if(*old_bridge && !g_str_has_prefix(old_bridge, BRIDGE_MARK)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                            "luxe-colors.css is not managed by luxe; it has not been replaced");
        return FALSE;
    }
    g_autofree char *updated = luxe_theme_css_update(existing, theme != NULL, error);
    if(!updated) return FALSE;
    g_autofree char *colors = luxe_theme_colors(theme, error);
    if(!colors) return FALSE;
    g_autofree char *payload = g_strconcat(BRIDGE_MARK, colors, NULL);
    if((theme || *old_bridge) &&
       !lxsession_atomic_write_file(bridge, payload, strlen(payload), error)) return FALSE;
    if((theme || strcmp(existing, updated)) &&
       !lxsession_atomic_write_file(css, updated, strlen(updated), error)) return FALSE;
    return TRUE;
}

gboolean luxe_theme_refresh_bridge(const LuxeTheme *theme, GError **error)
{
    g_autofree char *css = g_build_filename(g_get_user_config_dir(), "gtk-4.0", "gtk.css", NULL);
    g_autofree char *bridge = g_build_filename(g_get_user_config_dir(), "gtk-4.0", "luxe-colors.css", NULL);
    g_autofree char *existing = NULL;
    g_autofree char *old_bridge = NULL;
    if(!read_optional(css, &existing, error) || !read_optional(bridge, &old_bridge, error)) return FALSE;
    if(!strstr(existing, BEGIN_MARK) || !g_str_has_prefix(old_bridge, BRIDGE_MARK)) return TRUE;
    g_autofree char *colors = luxe_theme_colors(theme, error);
    if(!colors) return FALSE;
    g_autofree char *payload = g_strconcat(BRIDGE_MARK, colors, NULL);
    return !strcmp(payload, old_bridge) || lxsession_atomic_write_file(bridge, payload, strlen(payload), error);
}

char *luxe_theme_wallpaper(const LuxeTheme *theme)
{
    return theme ? g_strdup_printf("%s/%s.png", LUXE_THEME_WALLPAPER_DIR, theme->id) : NULL;
}
