/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gtk/gtk.h>
#include "../luxe-dialogs.h"
#include <glib/gstdio.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "main-dlg.h"
#include "main-dlg-ui.h"
#include "demo.h"
#include "glade-support.h"
#include "theme-presets.h"
#include "../../shared/luxe-rcxml-lock.h"
#include "../../luxesession/safe-file.h"
#include <sys/stat.h>
#include "luxe-gtk-ui.h"

enum {
    COL_DISP_NAME,
    COL_NAME,
    N_COLS
};

#define GET_WIDGET_WITH_TYPE(name, type) name = type( lookup_widget( dlg, #name ))

#define INIT_LIST_VIEW(name, prop) \
    GET_WIDGET_WITH_TYPE( name##_view, GTK_TREE_VIEW ); \
    name##_list = init_tree_view( name##_view, G_CALLBACK(on_list_sel_changed), prop);

/* Theme directory I/O is performed by a cancellable worker. */
static void load_all_themes_now(void);
static void schedule_theme_load(void);

extern gboolean under_luxesession;	/* wether we are under luxesession */

static GtkWidget* apply_button = NULL;
static gboolean wallpaper_apply_pending = FALSE;
static GSubprocess *active_install_process = NULL;

#define enable_apply()      gtk_widget_set_sensitive(apply_button, !wallpaper_apply_pending)
#define disable_apply()     gtk_widget_set_sensitive(apply_button, FALSE)

static GtkWidget* main_dlg_widget = NULL;
static GtkTreeView* gtk_theme_view = NULL;
static GtkListStore* gtk_theme_list = NULL;

static GtkTreeView* icon_theme_view = NULL;
static GtkListStore* icon_theme_list = NULL;
static GtkToggleButton* color_mode_light = NULL;
static GtkToggleButton* color_mode_dark = NULL;
static gboolean color_mode_syncing = FALSE;
static GtkCheckButton *theme_wallpaper = NULL;
static char *last_dark_theme = NULL;
static char *old_last_dark_theme = NULL;
static GtkCssProvider *preset_preview = NULL;
static char *preset_preview_name = NULL;
static gboolean preset_preview_high_contrast = FALSE;
static gboolean preset_preview_initialized = FALSE;
static gboolean old_wallpaper_follow = FALSE;
static gboolean appearance_is_dark(void);
static void check_pending_apply(void);
static char *read_frame_wallpaper_source(GError **error);

static gboolean frame_wallpaper_source_matches(const char *last_source)
{
    if(!last_source || !*last_source) return TRUE;
    g_autoptr(GError) error = NULL;
    g_autofree char *current = read_frame_wallpaper_source(&error);
    return !current || g_str_equal(current, last_source);
}

#if CURSOR_THEME
static GtkTreeView* cursor_theme_view = NULL;
static GtkListStore* cursor_theme_list = NULL;
#endif

static char* gtk_theme_name = NULL;
static char* icon_theme_name = NULL;
static char* font_name = NULL;
static char* cursor_theme_name = NULL;
static gint cursor_theme_size = 0;
/* Toolbar style as plain int (GtkToolbarStyle is gone in GTK4):
 * 0 = icons only, 1 = text only, 2 = text below icons (both),
 * 3 = text beside icons. */
static gint tb_style = 2;

static char* rc_file = NULL;

/* Live-preview state: every change is applied to GtkSettings
 * immediately (this restyled the whole app, demo included) and marks
 * the session dirty. Apply writes the files; closing a dirty session
 * without applying restores the startup values. */
static gboolean settings_dirty = FALSE;
static char* old_gtk_theme_name = NULL;
static char* old_icon_theme_name = NULL;
static char* old_font_name = NULL;
#if CURSOR_THEME
static char* old_cursor_theme_name = NULL;
static gint old_cursor_theme_size = 0;
#endif
static gint old_tb_style = 2;

static void save_startup_settings(void)
{
    g_free(old_last_dark_theme);
    old_last_dark_theme = g_strdup(last_dark_theme);
    old_wallpaper_follow = theme_wallpaper && gtk_check_button_get_active(theme_wallpaper);
    g_free(old_gtk_theme_name);
    g_free(old_icon_theme_name);
    g_free(old_font_name);
#if CURSOR_THEME
    g_free(old_cursor_theme_name);
#endif
    old_gtk_theme_name = g_strdup(gtk_theme_name);
    old_icon_theme_name = g_strdup(icon_theme_name);
    old_font_name = g_strdup(font_name);
#if CURSOR_THEME
    old_cursor_theme_name = g_strdup(cursor_theme_name);
    old_cursor_theme_size = cursor_theme_size;
#endif
    old_tb_style = tb_style;
}

static void apply_to_settings(void)
{
    GtkSettings* settings = gtk_settings_get_default();
    if(gtk_theme_name)
        g_object_set(settings, "gtk-theme-name", gtk_theme_name, NULL);
    if(icon_theme_name)
        g_object_set(settings, "gtk-icon-theme-name", icon_theme_name, NULL);
    if(font_name)
        g_object_set(settings, "gtk-font-name", font_name, NULL);
#if CURSOR_THEME
    /* NOTE: gtk-cursor-theme-name/gtk-cursor-theme-size still exist as
     * GtkSettings properties in GTK4. */
    if(cursor_theme_name)
        g_object_set(settings, "gtk-cursor-theme-name", cursor_theme_name, NULL);
    g_object_set(settings, "gtk-cursor-theme-size", cursor_theme_size, NULL);
#endif
    /* NOTE: gtk-toolbar-style no longer exists in GTK4; the demo
     * toolbar is refreshed directly instead. */
    demo_refresh_toolbar();
    const LuxeTheme *theme = luxe_theme_find(gtk_theme_name);
    gboolean high_contrast = luxe_style_get_high_contrast(luxe_style_settings());
    if(!preset_preview_initialized || g_strcmp0(preset_preview_name, gtk_theme_name) != 0 ||
       preset_preview_high_contrast != high_contrast) {
        if(preset_preview) {
            gtk_style_context_remove_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(preset_preview));
            g_clear_object(&preset_preview);
        }
        g_clear_pointer(&preset_preview_name, g_free);
        if(theme && !high_contrast) {
            g_autofree char *css = luxe_theme_colors(theme, NULL);
            if(css) {
                preset_preview = gtk_css_provider_new();
                gtk_css_provider_load_from_string(preset_preview, css);
                gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(preset_preview), GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
                preset_preview_name = g_strdup(gtk_theme_name);
            }
        }
        preset_preview_high_contrast = high_contrast;
        preset_preview_initialized = TRUE;
    }
    luxe_style_set_color_scheme(luxe_style_settings(),
        appearance_is_dark() ? LUXE_COLOR_SCHEME_FORCE_DARK : LUXE_COLOR_SCHEME_FORCE_LIGHT);
}

static void restore_startup_settings(void)
{
    GtkSettings* settings = gtk_settings_get_default();
    g_free(gtk_theme_name);
    g_free(icon_theme_name);
    g_free(font_name);
    gtk_theme_name = g_strdup(old_gtk_theme_name);
    icon_theme_name = g_strdup(old_icon_theme_name);
    font_name = g_strdup(old_font_name);
#if CURSOR_THEME
    g_free(cursor_theme_name);
    cursor_theme_name = g_strdup(old_cursor_theme_name);
    cursor_theme_size = old_cursor_theme_size;
#endif
    tb_style = old_tb_style;
    g_free(last_dark_theme);
    last_dark_theme = g_strdup(old_last_dark_theme);
    if(theme_wallpaper) gtk_check_button_set_active(theme_wallpaper, old_wallpaper_follow);
    if(gtk_theme_name)
        g_object_set(settings, "gtk-theme-name", gtk_theme_name, NULL);
    if(icon_theme_name)
        g_object_set(settings, "gtk-icon-theme-name", icon_theme_name, NULL);
    if(font_name)
        g_object_set(settings, "gtk-font-name", font_name, NULL);
#if CURSOR_THEME
    if(cursor_theme_name)
        g_object_set(settings, "gtk-cursor-theme-name", cursor_theme_name, NULL);
    g_object_set(settings, "gtk-cursor-theme-size", cursor_theme_size, NULL);
#endif
    demo_refresh_toolbar();
    apply_to_settings();
}

static void mark_dirty(void)
{
    apply_to_settings();
    settings_dirty = TRUE;
    enable_apply();
}

/* The quick color-mode selector deliberately changes both members of a
 * matched pair.  The full Window and Icon pages remain available for custom
 * combinations, but Light/Dark is a one-click coherent default. */
static void sync_color_mode_selector(void)
{
    gboolean is_light;
    gboolean is_dark;

    if(!color_mode_light || !color_mode_dark)
        return;
    const LuxeTheme *theme = luxe_theme_find(gtk_theme_name);
    is_light = ((theme && !theme->dark) || g_strcmp0(gtk_theme_name, "Adwaita") == 0) &&
               g_strcmp0(icon_theme_name, "Luxe-Light") == 0;
    is_dark = ((theme && theme->dark) || g_strcmp0(gtk_theme_name, "Adwaita-dark") == 0) &&
              g_strcmp0(icon_theme_name, "Luxe-Dark") == 0;
    color_mode_syncing = TRUE;
    if(is_light)
        gtk_toggle_button_set_active(color_mode_light, TRUE);
    else if(is_dark)
        gtk_toggle_button_set_active(color_mode_dark, TRUE);
    else
    {
        gtk_toggle_button_set_active(color_mode_light, FALSE);
        gtk_toggle_button_set_active(color_mode_dark, FALSE);
    }
    color_mode_syncing = FALSE;
}

static void select_list_theme(GtkTreeView* view, GtkListStore* list, const char* name)
{
    GtkTreeIter it;
    gboolean valid;
    if(!view || !list || !name)
        return;
    valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(list), &it);
    while(valid)
    {
        char* candidate = NULL;
        gtk_tree_model_get(GTK_TREE_MODEL(list), &it, COL_NAME, &candidate, -1);
        if(g_strcmp0(candidate, name) == 0)
        {
            gtk_tree_selection_select_iter(gtk_tree_view_get_selection(view), &it);
            g_free(candidate);
            return;
        }
        g_free(candidate);
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(list), &it);
    }
}

static void on_color_mode_toggled(GtkToggleButton* button, gpointer user_data)
{
    const gboolean dark = GPOINTER_TO_INT(user_data);
    gboolean changed = FALSE;
    (void)button;
    if(color_mode_syncing || !gtk_toggle_button_get_active(button))
        return;

    g_autofree char *asset = g_build_filename(LUXE_THEME_BRIDGE_DIR, "light", "colors.css", NULL);
    const char *chosen = g_file_test(asset, G_FILE_TEST_IS_REGULAR)
        ? (dark ? (last_dark_theme ? last_dark_theme : "Luxe-dark") : "Luxe-light")
        : (dark ? "Adwaita-dark" : "Adwaita");
    if(g_strcmp0(gtk_theme_name, chosen) != 0)
    {
        g_free(gtk_theme_name);
        gtk_theme_name = g_strdup(chosen);
        changed = TRUE;
    }
    if(g_strcmp0(icon_theme_name, dark ? "Luxe-Dark" : "Luxe-Light") != 0)
    {
        g_free(icon_theme_name);
        icon_theme_name = g_strdup(dark ? "Luxe-Dark" : "Luxe-Light");
        changed = TRUE;
    }
    if(!changed)
        return;
    sync_color_mode_selector();
    select_list_theme(gtk_theme_view, gtk_theme_list, gtk_theme_name);
    select_list_theme(icon_theme_view, icon_theme_list, icon_theme_name);
    mark_dirty();
}

static char *appearance_state_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "luxe", "appearance.ini", NULL);
}

static void draw_preset(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data)
{
    (void)area;
    const LuxeTheme *theme = data;
    GdkRGBA c;
    gdk_rgba_parse(&c, theme->bg);
    cairo_set_source_rgb(cr, c.red, c.green, c.blue); cairo_paint(cr);
    gdk_rgba_parse(&c, theme->accent);
    cairo_set_source_rgb(cr, c.red, c.green, c.blue);
    cairo_rectangle(cr, w * .65, 0, w * .35, h); cairo_fill(cr);
    gdk_rgba_parse(&c, theme->fg);
    cairo_set_source_rgb(cr, c.red, c.green, c.blue);
    cairo_rectangle(cr, 8, h / 2 - 2, w * .35, 4); cairo_fill(cr);
}

static void on_preset_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    const LuxeTheme *theme = data;
    g_free(gtk_theme_name); gtk_theme_name = g_strdup_printf("Luxe-%s", theme->id);
    g_free(icon_theme_name); icon_theme_name = g_strdup(theme->dark ? "Luxe-Dark" : "Luxe-Light");
    if(theme->dark) {
        g_free(last_dark_theme); last_dark_theme = g_strdup(gtk_theme_name);
    }
    select_list_theme(gtk_theme_view, gtk_theme_list, gtk_theme_name);
    select_list_theme(icon_theme_view, icon_theme_list, icon_theme_name);
    sync_color_mode_selector();
    mark_dirty();
}

static void on_wallpaper_follow_toggled(GtkCheckButton *button, gpointer data)
{
    (void)button; (void)data;
    settings_dirty = TRUE;
    if(apply_button) enable_apply();
}

static void init_presets(GtkWidget *dlg)
{
    g_autofree char *state_path = appearance_state_path();
    g_autofree char *contents = NULL;
    gsize len;
    g_autoptr(GKeyFile) state = g_key_file_new();
    if(lxsession_read_bounded_file(state_path, 65536, &contents, &len, NULL))
        g_key_file_load_from_data(state, contents, len, G_KEY_FILE_KEEP_COMMENTS, NULL);
    last_dark_theme = g_key_file_get_string(state, "Appearance", "last-dark-theme", NULL);
    const LuxeTheme *remembered = luxe_theme_find(last_dark_theme);
    if(!remembered || !remembered->dark) {
        g_free(last_dark_theme); last_dark_theme = g_strdup("Luxe-dark");
    }
    const LuxeTheme *active = luxe_theme_find(gtk_theme_name);
    if(active && active->dark) {
        g_free(last_dark_theme); last_dark_theme = g_strdup(gtk_theme_name);
    }
    theme_wallpaper = GTK_CHECK_BUTTON(lookup_widget(dlg, "theme_wallpaper"));
    gboolean follow = g_key_file_get_boolean(state, "Appearance", "wallpaper-follow", NULL);
    /* Frame owns this state. Appearance compares the last successfully
     * applied source with Frame's current source; PCManager is migration-only. */
    g_autofree char *last_wallpaper = g_key_file_get_string(state, "Appearance", "last-wallpaper", NULL);
    if(follow && !frame_wallpaper_source_matches(last_wallpaper)) follow = FALSE;
    gtk_check_button_set_active(theme_wallpaper, follow);
    g_signal_connect(theme_wallpaper, "toggled", G_CALLBACK(on_wallpaper_follow_toggled), NULL);
    GtkFlowBox *cards = GTK_FLOW_BOX(lookup_widget(dlg, "theme_cards"));
    for(guint i = 0; i < G_N_ELEMENTS(luxe_themes); i++) {
        const LuxeTheme *theme = &luxe_themes[i];
        GtkWidget *button = gtk_button_new();
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        GtkWidget *swatch = gtk_drawing_area_new();
        gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(swatch), 130);
        gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(swatch), 24);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(swatch), draw_preset, (gpointer)theme, NULL);
        gtk_box_append(GTK_BOX(box), swatch);
        gtk_box_append(GTK_BOX(box), gtk_label_new(theme->name));
        gtk_button_set_child(GTK_BUTTON(button), box);
        g_autofree char *asset = g_build_filename(LUXE_THEME_BRIDGE_DIR, theme->id, "colors.css", NULL);
        gboolean available = g_file_test(asset, G_FILE_TEST_IS_REGULAR);
        gtk_widget_set_sensitive(button, available);
        gtk_widget_set_tooltip_text(button, available ? (theme->dark ? _("Dark global theme") : _("Light global theme")) : _("Install the matching LuxeSession package to use this theme"));
        gtk_accessible_update_property(GTK_ACCESSIBLE(button), GTK_ACCESSIBLE_PROPERTY_LABEL, theme->name, -1);
        g_signal_connect(button, "clicked", G_CALLBACK(on_preset_clicked), (gpointer)theme);
        gtk_flow_box_insert(cards, button, -1);
    }
}

static gboolean write_appearance_state(GError **error)
{
    g_autofree char *path = appearance_state_path();
    g_autofree char *contents = NULL;
    gsize len;
    g_autoptr(GKeyFile) state = g_key_file_new();
    if(g_file_test(path, G_FILE_TEST_EXISTS)) {
        if(!lxsession_read_bounded_file(path, 65536, &contents, &len, error) ||
           !g_key_file_load_from_data(state, contents, len, G_KEY_FILE_KEEP_COMMENTS, error)) return FALSE;
    }
    const LuxeTheme *theme = luxe_theme_find(gtk_theme_name);
    gboolean follow = theme_wallpaper && gtk_check_button_get_active(theme_wallpaper);
    g_key_file_set_string(state, "Appearance", "theme", theme ? theme->id : "custom");
    g_key_file_set_string(state, "Appearance", "last-dark-theme", last_dark_theme ? last_dark_theme : "Luxe-dark");
    g_key_file_set_boolean(state, "Appearance", "wallpaper-follow", follow);
    g_autofree char *serialized = g_key_file_to_data(state, &len, error);
    return serialized && lxsession_atomic_write_file(path, serialized, len, error);
}

typedef struct {
    char *source;
} WallpaperApply;

static gboolean save_applied_wallpaper_source(const char *source, GError **error)
{
    g_autofree char *path = appearance_state_path();
    g_autofree char *contents = NULL;
    gsize length = 0;
    g_autoptr(GKeyFile) state = g_key_file_new();
    if(g_file_test(path, G_FILE_TEST_EXISTS) &&
       (!lxsession_read_bounded_file(path, 65536, &contents, &length, error) ||
        !g_key_file_load_from_data(state, contents, length, G_KEY_FILE_KEEP_COMMENTS, error)))
        return FALSE;
    g_key_file_set_string(state, "Appearance", "last-wallpaper", source);
    g_autofree char *updated = g_key_file_to_data(state, &length, error);
    return updated && lxsession_atomic_write_file(path, updated, length, error);
}

static char *read_frame_wallpaper_source(GError **error)
{
    g_autofree char *path = g_build_filename(g_get_user_config_dir(), "luxe", "wallpaper.ini", NULL);
    g_autofree char *contents = NULL;
    gsize length = 0;
    g_autoptr(GKeyFile) state = g_key_file_new();
    if(!lxsession_read_bounded_file(path, 1024 * 1024, &contents, &length, error) ||
       !g_key_file_load_from_data(state, contents, length, G_KEY_FILE_NONE, error))
        return NULL;
    return g_key_file_get_string(state, "Wallpaper", "source", error);
}

static void wallpaper_apply_finished(GObject *object, GAsyncResult *result, gpointer user_data)
{
    WallpaperApply *job = user_data;
    g_autofree char *stdout_text = NULL;
    g_autofree char *stderr_text = NULL;
    g_autoptr(GError) error = NULL;
    gboolean ok = g_subprocess_communicate_utf8_finish(G_SUBPROCESS(object), result,
                                                       &stdout_text, &stderr_text, &error) &&
                  g_subprocess_get_successful(G_SUBPROCESS(object));
    if(ok) {
        g_autofree char *reported_source = read_frame_wallpaper_source(&error);
        g_autofree char *expected = g_canonicalize_filename(job->source, NULL);
        g_autofree char *actual = reported_source ? g_canonicalize_filename(reported_source, NULL) : NULL;
        if(!actual || !g_str_equal(expected, actual)) {
            if(!error)
                g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                    "Frame did not persist the requested wallpaper source.");
            ok = FALSE;
        }
        if(ok)
            ok = save_applied_wallpaper_source(actual, &error);
    }
    if(!ok && !error)
        g_set_error(&error, G_IO_ERROR, G_IO_ERROR_FAILED, "Frame could not apply the wallpaper%s%s",
                    stderr_text && *stderr_text ? ": " : "", stderr_text ? stderr_text : "");
    wallpaper_apply_pending = FALSE;
    if(main_dlg_widget) {
        if(ok) {
            if(settings_dirty) enable_apply();
        } else {
            const char *detail = error ? error->message : _("Unknown error");
            luxe_show_error(GTK_WINDOW(main_dlg_widget),
                            ok ? _("Wallpaper changed") : _("Theme saved, but wallpaper could not be changed"),
                            detail);
            if(settings_dirty) enable_apply();
        }
    }
    g_clear_error(&error);
    g_free(job->source);
    g_free(job);
}

static void apply_preset_wallpaper(void)
{
    const LuxeTheme *theme = luxe_theme_find(gtk_theme_name);
    if(!theme || !theme_wallpaper || !gtk_check_button_get_active(theme_wallpaper)) return;
    g_autofree char *wallpaper = luxe_theme_wallpaper(theme);
    if(!g_file_test(wallpaper, G_FILE_TEST_IS_REGULAR)) {
        luxe_show_error(GTK_WINDOW(main_dlg_widget), _("Theme saved, but wallpaper could not be changed"),
                        _("The bundled wallpaper for this theme is missing."));
        return;
    }
    g_autoptr(GError) error = NULL;
    GSubprocess *process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDERR_PIPE, &error,
                                            "frame", "--set-wallpaper", wallpaper, NULL);
    if(!process) {
        luxe_show_error(GTK_WINDOW(main_dlg_widget), _("Theme saved, but wallpaper could not be changed"),
                        error ? error->message : _("Could not start Frame."));
        return;
    }
    WallpaperApply *job = g_new0(WallpaperApply, 1);
    job->source = g_strdup(wallpaper);
    wallpaper_apply_pending = TRUE;
    disable_apply();
    g_subprocess_communicate_utf8_async(process, NULL, NULL, wallpaper_apply_finished, job);
    g_object_unref(process);
}

gint main_dlg_get_tb_style( void )
{
    return tb_style;
}

static GtkWidget* demo_box = NULL;

/* Owned; DESKTOP_SESSION may contain anything, so it is validated and
 * duplicated rather than borrowed from the environment. */
static char* session_name = NULL;

/* DESKTOP_SESSION is used as a directory name below ~/.config/luxesession.
 * Anything that is not a single plain component must never become a path. */
static gboolean valid_session_name(const char* name)
{
    return name && *name && !g_str_equal(name, ".") && !g_str_equal(name, "..") &&
           !strchr(name, '/') && !strchr(name, '\\');
}

static char* default_session_name(void)
{
    const char* env = g_getenv("DESKTOP_SESSION");
    return valid_session_name(env) ? g_strdup(env) : g_strdup("LXDE");
}

static void ensure_session_name(void)
{
    if(!session_name)
        session_name = default_session_name();
}

/* Preserve unrelated settings and propagate failures to Apply. */
static gboolean save_key_file(GKeyFile* key, const char* path, GError** error)
{
    char* dir = g_path_get_dirname(path);
    gboolean ok;
    if(g_mkdir_with_parents(dir, 0755) != 0)
    {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Cannot create %s: %s", dir, g_strerror(errno));
        g_free(dir);
        return FALSE;
    }
    g_free(dir);
    ok = g_key_file_save_to_file(key, path, error);
    return ok;
}

/* Quote a gtkrc string value without mangling UTF-8: only backslash,
 * double-quote and C0 controls are escaped. g_strescape() octal-escapes
 * bytes >= 0x80, corrupting non-ASCII theme names. */
static char* rc_escape(const char* s)
{
    GString* out;
    const char* p;
    if(!s)
        return g_strdup("");
    out = g_string_sized_new(strlen(s) + 16);
    for(p = s; *p; ++p)
    {
        /* Theme names come from directory entries, which may contain any
         * byte except NUL and '/'. Escape every C0 control as \xNN so the
         * gtkrc stays well-formed, and keep UTF-8 sequences intact. */
        if(*p == '\\')
            g_string_append(out, "\\\\");
        else if(*p == '"')
            g_string_append(out, "\\\"");
        else if((guchar)*p < 0x20)
            g_string_append_printf(out, "\\x%02x", (guchar)*p);
        else
            g_string_append_c(out, *p);
    }
    return g_string_free(out, FALSE);
}

/* Set one top-level gtkrc key, preserving the value's exact quoting style
 * when the key already exists. `numeric` selects an unquoted assignment. */
static void rc_set_setting(GString* buffer, const char* key, const char* escaped_value, gboolean numeric)
{
    /* [^\n\r]* stops before the line break, so a value at the end of the
     * buffer cannot swallow the following line. */
    g_autofree char* pattern = g_strdup_printf("(?m)^(%s[ \t]*=[ \t]*)[^\n\r]*", key);
    GRegex* regex = g_regex_new(pattern, 0, 0, NULL);
    /* The match is spliced manually, so the replacement is the bare value:
     * a "\\1" prefix here would be inserted literally. */
    g_autofree char* replacement = numeric
        ? g_strdup(escaped_value)
        : g_strdup_printf("\"%s\"", escaped_value);
    g_autofree char* updated = NULL;
    if(regex)
    {
        /* Replace only the first assignment: g_regex_replace() takes a start
         * position, not a match count. */
        GMatchInfo* match = NULL;
        if(g_regex_match_full(regex, buffer->str, -1, 0, 0, &match, NULL))
        {
            int start = 0, end = 0;
            int key_start = 0, key_end = 0;
            /* Group 1 is "key<spaces>="; keeping it preserves the user's
             * spacing while only the value is replaced. */
            if(g_match_info_fetch_pos(match, 0, &start, &end) &&
               g_match_info_fetch_pos(match, 1, &key_start, &key_end))
            {
                g_autofree char* prefix = g_strndup(buffer->str, (gsize)start);
                g_autofree char* key_part = g_strndup(buffer->str + start, (gsize)(key_end - start));
                g_autofree char* suffix = g_strdup(buffer->str + end);
                updated = g_strconcat(prefix, key_part, replacement, suffix, NULL);
            }
        }
        if(match)
            g_match_info_free(match);
        g_regex_unref(regex);
    }
    if(updated)
    {
        /* Preserve the original trailing newline. */
        gboolean trailing = buffer->len && buffer->str[buffer->len - 1] == '\n';
        g_string_assign(buffer, updated);
        if(trailing && (buffer->len == 0 || buffer->str[buffer->len - 1] != '\n'))
            g_string_append_c(buffer, '\n');
    }
    else
    {
        g_string_append_c(buffer, '\n');
        g_string_append_printf(buffer, "%s=", key);
        if(numeric)
            g_string_append(buffer, escaped_value);
        else
            g_string_append_printf(buffer, "\"%s\"", escaped_value);
        g_string_append_c(buffer, '\n');
    }
}

static gboolean write_rc_file(const char* path, GError** error)
{
    g_autofree char* theme = rc_escape(gtk_theme_name);
    g_autofree char* icons = rc_escape(icon_theme_name);
    g_autofree char* font = rc_escape(font_name);
    g_autofree char* cursor = rc_escape(cursor_theme_name ? cursor_theme_name : "");
    g_autofree char* home = rc_escape(g_get_home_dir());
    g_autofree char* cursor_size = g_strdup_printf("%d", cursor_theme_size);
    g_autofree char* toolbar = g_strdup_printf("%d", tb_style);
    /* GTK2 applications read ~/.gtkrc-2.0. Replacing that file wholesale
     * destroyed unrelated user settings, bindings and comments, so an
     * existing file is rewritten in place and only the six Luxe keys are
     * touched (appended when absent). */
    g_autofree char* existing = NULL;
    g_autofree char* data = NULL;
    gsize length = 0;
    gboolean ok;

    if(g_file_test(path, G_FILE_TEST_IS_SYMLINK))
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                    "Refusing to modify the symlinked gtkrc file: %s", path);
        return FALSE;
    }
    if(g_file_test(path, G_FILE_TEST_EXISTS))
    {
        if(!g_file_get_contents(path, &existing, &length, error))
            return FALSE;
        if(length != strlen(existing))
        {
            g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                                "The existing gtkrc file contains NUL bytes; refusing to rewrite it");
            return FALSE;
        }
    }

    if(existing)
    {
        GString* buffer = g_string_new(existing);
        rc_set_setting(buffer, "gtk-theme-name", theme, FALSE);
        rc_set_setting(buffer, "gtk-icon-theme-name", icons, FALSE);
        rc_set_setting(buffer, "gtk-font-name", font, FALSE);
        rc_set_setting(buffer, "gtk-cursor-theme-name", cursor, FALSE);
        rc_set_setting(buffer, "gtk-cursor-theme-size", cursor_size, TRUE);
        rc_set_setting(buffer, "gtk-toolbar-style", toolbar, TRUE);
        data = g_string_free(buffer, FALSE);
    }
    else
    {
        data = g_strdup_printf("# Written by LuxeAppearance.\n"
            "gtk-theme-name=\"%s\"\ngtk-icon-theme-name=\"%s\"\ngtk-font-name=\"%s\"\n"
            "gtk-cursor-theme-name=\"%s\"\ngtk-cursor-theme-size=%s\ngtk-toolbar-style=%s\n"
            "include \"%s/.gtkrc-2.0.mine\"\n",
            theme, icons, font, cursor, cursor_size, toolbar, home);
    }
    ok = g_file_set_contents(path, data, -1, error);
    return ok;
}

static gboolean write_settings_file(const char* version, GError** error)
{
    char* path = g_build_filename(g_get_user_config_dir(), version, "settings.ini", NULL);
    GKeyFile* key = g_key_file_new();
    gboolean ok;
    if(g_file_test(path, G_FILE_TEST_EXISTS) &&
       !g_key_file_load_from_file(key, path, G_KEY_FILE_KEEP_COMMENTS, error))
    { g_free(path); g_key_file_unref(key); return FALSE; }
    g_key_file_set_string(key, "Settings", "gtk-theme-name", gtk_theme_name);
    g_key_file_set_string(key, "Settings", "gtk-icon-theme-name", icon_theme_name);
    g_key_file_set_string(key, "Settings", "gtk-font-name", font_name);
    g_key_file_set_string(key, "Settings", "gtk-cursor-theme-name", cursor_theme_name ? cursor_theme_name : "default");
    g_key_file_set_integer(key, "Settings", "gtk-cursor-theme-size", cursor_theme_size);
    /* Complete presets have a fixed GTK stylesheet; libadwaita gets its mode
     * from the global color-scheme, not its unsupported GtkSettings flag. */
    g_key_file_set_boolean(key, "Settings", "gtk-application-prefer-dark-theme",
        appearance_is_dark() && !(g_str_equal(version, "gtk-4.0") && luxe_theme_find(gtk_theme_name)));
    /* gtk-toolbar-style exists only in GTK3. Writing it into the GTK4 file
     * made GTK4 warn about an unknown key, so it is stored for GTK3 only. */
    if(g_strcmp0(version, "gtk-3.0") == 0)
        g_key_file_set_integer(key, "Settings", "gtk-toolbar-style", tb_style);
    ok = save_key_file(key, path, error);
    g_key_file_unref(key); g_free(path); return ok;
}

static gboolean write_luxesession_config(GError** error)
{
    char* path;
    ensure_session_name();
    path = g_build_filename(g_get_user_config_dir(), "luxesession", session_name, "desktop.conf", NULL);
    GKeyFile* key = g_key_file_new();
    gboolean ok;
    if(g_file_test(path, G_FILE_TEST_EXISTS))
    {
        if(!g_key_file_load_from_file(key, path, G_KEY_FILE_KEEP_COMMENTS, error))
        { g_free(path); g_key_file_unref(key); return FALSE; }
    }
    else
    {
        const char* const* dir;
        gboolean seeded = FALSE;
        for(dir = g_get_system_config_dirs(); *dir; ++dir)
        {
            char* system = g_build_filename(*dir, "luxesession", session_name, "desktop.conf", NULL);
            ok = g_key_file_load_from_file(key, system, G_KEY_FILE_KEEP_COMMENTS, NULL);
            g_free(system); if(ok) { seeded = TRUE; break; }
        }
        if(!seeded)
        {
            /* desktop.conf launches the session's core components; creating
             * it from scratch with only GTK keys would break the next
             * session. Never synthesize it from nothing. */
            g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_NOT_FOUND,
                        "No luxesession desktop.conf template is installed; log in to the Luxe session once before applying settings.");
            g_key_file_unref(key);
            g_free(path);
            return FALSE;
        }
    }
    g_key_file_set_string(key, "GTK", "sNet/ThemeName", gtk_theme_name);
    g_key_file_set_string(key, "GTK", "sNet/IconThemeName", icon_theme_name);
    g_key_file_set_string(key, "GTK", "sGtk/FontName", font_name);
    g_key_file_set_string(key, "GTK", "sGtk/CursorThemeName", cursor_theme_name ? cursor_theme_name : "default");
    g_key_file_set_integer(key, "GTK", "iGtk/CursorThemeSize", cursor_theme_size);
    g_key_file_set_integer(key, "GTK", "iGtk/ToolbarStyle", tb_style);
    ok = save_key_file(key, path, error);
    g_key_file_unref(key); g_free(path); return ok;
}

/* Update only the Openbox theme name, preserving every other user setting,
 * comment and binding in rc.xml. The session default is used only when no
 * user configuration exists; never synthesize a bare Openbox configuration. */
static char* openbox_config_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "openbox", "rc.xml", NULL);
}

/* The Openbox variant is decided once so the rc.xml write and the
 * org.gnome.desktop.interface color-scheme can never disagree. */
static gboolean appearance_is_dark(void)
{
    const LuxeTheme *theme = luxe_theme_find(gtk_theme_name);
    if(theme) return theme->dark;
    if(gtk_theme_name && g_str_has_prefix(gtk_theme_name, "HighContrast"))
        return g_str_has_suffix(gtk_theme_name, "Inverse") || g_str_has_suffix(gtk_theme_name, "-dark");
    return g_str_has_suffix(gtk_theme_name, "-dark") ||
           g_strcmp0(icon_theme_name, "Luxe-Dark") == 0;
}

/* Read the opened file with a hard byte limit. Query its version on the same
 * stream both before and after reading, so an in-place edit during the read
 * cannot be paired with a later version identifier. */
static gboolean read_openbox_config(GFile *file, char **contents, gsize *length,
                                     char **etag, GError **error)
{
    enum { LIMIT = 1024 * 1024 };
    g_autofree char *path = g_file_get_path(file);
    struct stat path_before, opened_before, opened_after, path_after;
    int fd = -1;
    if(!path || g_lstat(path, &path_before) != 0 || !S_ISREG(path_before.st_mode)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE,
                            "Openbox configuration must be a regular, non-symlink file.");
        return FALSE;
    }
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if(fd < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Cannot safely open Openbox configuration: %s", g_strerror(errno));
        return FALSE;
    }
    if(fstat(fd, &opened_before) != 0 || !S_ISREG(opened_before.st_mode) ||
       opened_before.st_dev != path_before.st_dev || opened_before.st_ino != path_before.st_ino) {
        close(fd);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE,
                            "Openbox configuration changed while it was opened.");
        return FALSE;
    }
    g_autoptr(GFileInfo) before = g_file_query_info(file,
        G_FILE_ATTRIBUTE_ETAG_VALUE "," G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, error);
    if(!before) { close(fd); return FALSE; }
    const char *version = g_file_info_get_etag(before);
    if(g_file_info_get_is_symlink(before) || !version) {
        close(fd);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            "Openbox configuration is not a versioned regular file.");
        return FALSE;
    }
    g_autoptr(GByteArray) bytes = g_byte_array_new();
    guint8 buffer[8192];
    for(;;) {
        ssize_t n = read(fd, buffer, sizeof buffer);
        if(n < 0 && errno == EINTR) continue;
        if(n < 0) {
            int saved_errno = errno; close(fd);
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
                        "Cannot read Openbox configuration: %s", g_strerror(saved_errno));
            return FALSE;
        }
        if(n == 0) break;
        if((gsize)n > LIMIT - bytes->len) {
            close(fd);
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                                "Openbox configuration exceeds the 1 MiB limit.");
            return FALSE;
        }
        g_byte_array_append(bytes, buffer, (guint)n);
    }
    gboolean stable_fd = fstat(fd, &opened_after) == 0 &&
        opened_before.st_dev == opened_after.st_dev && opened_before.st_ino == opened_after.st_ino &&
        opened_before.st_size == opened_after.st_size && opened_before.st_mtime == opened_after.st_mtime &&
        opened_before.st_ctime == opened_after.st_ctime;
    close(fd);
    if(!stable_fd || g_lstat(path, &path_after) != 0 ||
       path_after.st_dev != opened_before.st_dev || path_after.st_ino != opened_before.st_ino) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_WRONG_ETAG,
                            "Openbox configuration changed while it was read.");
        return FALSE;
    }
    g_autoptr(GFileInfo) after = g_file_query_info(file,
        G_FILE_ATTRIBUTE_ETAG_VALUE "," G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, error);
    if(!after) return FALSE;
    if(g_file_info_get_is_symlink(after) || g_strcmp0(version, g_file_info_get_etag(after))) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_WRONG_ETAG,
                            "Openbox configuration changed while it was read.");
        return FALSE;
    }
    *length = bytes->len;
    g_byte_array_append(bytes, (const guint8 *)"", 1);
    *contents = (char *)g_byte_array_free(g_steal_pointer(&bytes), FALSE);
    *etag = g_strdup(version);
    return TRUE;
}

static gboolean write_openbox_theme_config(const char *expected, gsize expected_len,
                                           gboolean expected_exists, gboolean check_snapshot,
                                           gboolean *may_have_written, GError** error)
{
    const char* theme = luxe_theme_find(gtk_theme_name) ? gtk_theme_name :
        (appearance_is_dark() ? "Luxe-tokyo-night" : "Luxe-light");
    g_autofree char* path = openbox_config_path();
    g_autofree char* contents = NULL;
    g_autofree char* updated = NULL;
    g_autoptr(GFile) file = NULL;
    g_autofree char *etag = NULL;
    GError* local_error_out = NULL;
    gsize length = 0;
    gboolean from_template = FALSE;
    gboolean ok = FALSE;
    int rcxml_lock = -1;

    if(may_have_written) *may_have_written = FALSE;

    if(g_file_test(path, G_FILE_TEST_EXISTS))
    {
        /* Refuse a symlinked rc.xml: g_file_get_contents() would follow it and
         * the rewrite would then land on the link target. */
        if(g_file_test(path, G_FILE_TEST_IS_SYMLINK))
        {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_ISDIR,
                        "Refusing to modify the symlinked Openbox configuration: %s", path);
            return FALSE;
        }
        file = g_file_new_for_path(path);
        ok = read_openbox_config(file, &contents, &length, &etag, error);
    }
    else
    {
        from_template = TRUE;
        ok = lxsession_read_bounded_file(OPENBOX_DEFAULT_CONFIG, 1024 * 1024, &contents, &length, error);
    }
    if(!ok)
    {
        if(!error || !*error)
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOENT,
                        "No Openbox rc.xml template is installed");
        return FALSE;
    }
    if(check_snapshot && (expected_exists == from_template ||
       (expected_exists && (length != expected_len || memcmp(contents, expected, length) != 0)))) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_WRONG_ETAG,
                            "Openbox configuration changed during Apply; the external edit was preserved.");
        return FALSE;
    }
    if(!from_template && !etag) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            "Cannot safely update Openbox configuration without a version identifier.");
        return FALSE;
    }
    /* A successful read is not a successful rewrite. All later early exits
     * must remain failures until the desired configuration is established. */
    ok = FALSE;
    if(length != strlen(contents))
    {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                            "The Openbox configuration contains NUL bytes; refusing to rewrite it");
        return FALSE;
    }
    {
        xmlDocPtr document = xmlReadMemory(contents, (int)length, "rc.xml", NULL,
            XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_COMPACT);
        xmlNodePtr root = document ? xmlDocGetRootElement(document) : NULL;
        xmlNodePtr theme_node = NULL;
        xmlNodePtr name_node = NULL;
        if(root && xmlStrEqual(root->name, BAD_CAST "openbox_config")) {
            for(xmlNodePtr child = root->children; child; child = child->next) {
                if(child->type == XML_ELEMENT_NODE && xmlStrEqual(child->name, BAD_CAST "theme")) {
                    theme_node = child;
                    break;
                }
            }
        }
        if(theme_node) {
            for(xmlNodePtr child = theme_node->children; child; child = child->next) {
                if(child->type == XML_ELEMENT_NODE && xmlStrEqual(child->name, BAD_CAST "name")) {
                    name_node = child;
                    break;
                }
            }
        }
        gboolean valid_name = name_node != NULL;
        if(valid_name) {
            for(xmlNodePtr child = name_node->children; child; child = child->next) {
                if(child->type == XML_ELEMENT_NODE) {
                    valid_name = FALSE;
                    break;
                }
            }
        }
        if(!valid_name) {
            if(document) xmlFreeDoc(document);
            g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                        "Openbox rc.xml has no valid direct theme/name setting");
            goto out;
        }
        xmlChar *current_name = xmlNodeGetContent(name_node);
        if(!from_template && current_name && g_str_equal((const char *)current_name, theme)) {
            xmlFree(current_name);
            xmlFreeDoc(document);
            ok = TRUE;
            goto out;
        }
        if(current_name) xmlFree(current_name);
        xmlNodeSetContent(name_node, BAD_CAST theme);
        xmlChar *serialized = NULL;
        int serialized_len = 0;
        xmlDocDumpMemoryEnc(document, &serialized, &serialized_len, "UTF-8");
        xmlFreeDoc(document);
        if(!serialized || serialized_len <= 0) {
            if(serialized) xmlFree(serialized);
            g_set_error_literal(error, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                                "Could not serialize Openbox rc.xml");
            goto out;
        }
        updated = g_strndup((const char *)serialized, (gsize)serialized_len);
        xmlFree(serialized);
    }
    if(!from_template && g_strcmp0(contents, updated) == 0)
    {
        ok = TRUE;
        goto out;
    }
    {
        g_autofree char* dir = g_path_get_dirname(path);
        if(g_mkdir_with_parents(dir, 0700) != 0)
        {
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                        "Cannot create %s: %s", dir, g_strerror(errno));
            goto out;
        }
    }
    rcxml_lock = luxe_rcxml_lock_acquire(path, error);
    if(rcxml_lock < 0) goto out;
    if(!file)
        file = g_file_new_for_path(path);
    if(from_template) {
        /* Creating a new configuration must not replace a file that appeared
         * after the snapshot. GFile's create operation is exclusive. */
        g_autoptr(GFileOutputStream) stream = g_file_create(file, G_FILE_CREATE_PRIVATE, NULL, error);
        if(!stream) goto out;
        if(may_have_written) *may_have_written = TRUE;
        ok = g_output_stream_write_all(G_OUTPUT_STREAM(stream), updated, strlen(updated),
                                       NULL, NULL, error) &&
             g_output_stream_close(G_OUTPUT_STREAM(stream), NULL, error);
        goto out;
    }
    /* Other failures may occur after replacement, so retain rollback evidence.
     * WRONG_ETAG specifically means the replacement did not occur. */
    if(may_have_written) *may_have_written = TRUE;
    ok = g_file_replace_contents(file, updated, strlen(updated), etag, FALSE,
                                 G_FILE_CREATE_PRIVATE, NULL, NULL, &local_error_out);
    if(!ok)
    {
        if(may_have_written && g_error_matches(local_error_out, G_IO_ERROR, G_IO_ERROR_WRONG_ETAG))
            *may_have_written = FALSE;
        if(local_error_out && !from_template)
            g_prefix_error(&local_error_out, "The Openbox configuration could not be saved: ");
        g_propagate_error(error, g_steal_pointer(&local_error_out));
        goto out;
    }

out:
    if(rcxml_lock >= 0) close(rcxml_lock);
    return ok;
}

/* Publish the appearance to org.gnome.desktop.interface so libadwaita
 * applications follow it. The files above are the durable record; a backend
 * that refuses or lacks the keys is reported, not silently treated as
 * success, and never rolls the file writes back. */
static gboolean publish_interface_settings(GError** error)
{
    GSettingsSchemaSource* source = g_settings_schema_source_get_default();
    g_autoptr(GSettingsSchema) schema = source ? g_settings_schema_source_lookup(source, "org.gnome.desktop.interface", TRUE) : NULL;
    gboolean published = FALSE;
    if(!schema)
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                            "The org.gnome.desktop.interface schema is not installed");
        return FALSE;
    }
    {
        g_autoptr(GSettings) settings = g_settings_new_full(schema, NULL, NULL);
        const char* keys[] = {"gtk-theme", "icon-theme", "font-name", "cursor-theme", "color-scheme"};
        const char* values[] = {gtk_theme_name, icon_theme_name, font_name,
                                cursor_theme_name ? cursor_theme_name : "default",
                                appearance_is_dark() ? "prefer-dark" : "default"};
        guint i;
        g_settings_delay(settings);
        for(i = 0; i < G_N_ELEMENTS(keys); ++i)
        {
            if(!g_settings_schema_has_key(schema, keys[i]))
                continue;
            if(!g_settings_is_writable(settings, keys[i]))
            {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_READ_ONLY,
                            "The GSettings key '%s' is not writable", keys[i]);
                g_settings_revert(settings);
                return FALSE;
            }
            if(!g_settings_set_string(settings, keys[i], values[i]))
            {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "The GSettings backend rejected '%s'", keys[i]);
                g_settings_revert(settings);
                return FALSE;
            }
            published = TRUE;
        }
        g_settings_apply(settings);
        g_settings_sync();
    }
    if(!published)
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                            "The interface schema exposes none of the expected keys");
        return FALSE;
    }
    return TRUE;
}

static void on_list_sel_changed( GtkTreeSelection* sel, const char* prop )
{
    GtkTreeIter it;
    GtkTreeModel* model;
    if( gtk_tree_selection_get_selected( sel, &model, &it ) )
    {
        char* name;
        gtk_tree_model_get( model, &it, COL_NAME, &name, -1 );

        if( model == GTK_TREE_MODEL (gtk_theme_list) )   /* gtk+ theme */
        {
            if( name && gtk_theme_name && 0 == strcmp( name, gtk_theme_name ) )
                goto out;
            g_free( gtk_theme_name );
            gtk_theme_name = name;
            const LuxeTheme *theme = luxe_theme_find(name);
            if(theme) {
                g_free(icon_theme_name);
                icon_theme_name = g_strdup(theme->dark ? "Luxe-Dark" : "Luxe-Light");
                if(theme->dark) {
                    g_free(last_dark_theme);
                    last_dark_theme = g_strdup(name);
                }
                select_list_theme(icon_theme_view, icon_theme_list, icon_theme_name);
            }
        }
        else if( model == GTK_TREE_MODEL (icon_theme_list) )   /* icon theme */
        {
            if( name && icon_theme_name && 0 == strcmp( name, icon_theme_name ) )
                goto out;
            g_free( icon_theme_name );
            icon_theme_name = name;
        }
#if CURSOR_THEME
        else if( model == GTK_TREE_MODEL (cursor_theme_list) )   /* cursor theme */
        {
            if( name && cursor_theme_name && 0 == strcmp( name, cursor_theme_name ) )
                goto out;
            g_free( cursor_theme_name );
            cursor_theme_name = g_strdup(name);
        }
#endif
        else
        {
            g_free(name);
            return;
        }

        /* Live preview for both luxesession and standalone use; Apply
         * persists the choice to the config files. */
        sync_color_mode_selector();
        mark_dirty();
        return;
    out:
        g_free( name );
    }
    (void)prop;
}

static gint sort_func( GtkTreeModel* model, GtkTreeIter* it1, GtkTreeIter* it2, gpointer data )
{
    int column = GPOINTER_TO_INT(data);
    char* str1 = NULL;
    char* str2 = NULL;
	int ret;
	gtk_tree_model_get( model, it1, column, &str1, -1 );
	gtk_tree_model_get( model, it2, column, &str2, -1 );
	/* GTK/cursor rows leave the display-name column NULL, so an unguarded
	 * collation here would crash as soon as the sort column was corrected. */
	ret = g_utf8_collate( str1 ? str1 : "", str2 ? str2 : "" );
	g_free( str1 );
	g_free( str2 );
	return ret;
}

static GtkListStore* init_tree_view( GtkTreeView* view, GCallback on_sel_changed, const char* prop )
{
    GtkTreeViewColumn* col;
    GtkListStore* list;
    GtkTreeSelection* sel;
    int text_col = strcmp(prop, "gtk-theme-name") && strcmp(prop, "gtk-cursor-theme-name") ? COL_DISP_NAME : COL_NAME;

    col = gtk_tree_view_column_new_with_attributes( NULL, gtk_cell_renderer_text_new(),
                                                    "text", text_col, NULL );
    gtk_tree_view_append_column( view, col );

    sel = gtk_tree_view_get_selection(view);
    g_signal_connect( sel, "changed", on_sel_changed, (gpointer)prop );

    list = gtk_list_store_new( N_COLS, G_TYPE_STRING, G_TYPE_STRING );
    /* Sort on the column that is actually displayed, and let the comparator
     * read that same column. */
    gtk_tree_sortable_set_sort_func( (GtkTreeSortable*)list, text_col, sort_func, GINT_TO_POINTER(text_col), NULL );
    gtk_tree_view_set_model( view, (GtkTreeModel*)list );
    g_object_unref( list );
    return list;
}

static gint load_tb_style_from_luxesession(void)
{
    if(!session_name) session_name = default_session_name();
    g_autofree char *path = g_build_filename(g_get_user_config_dir(), "luxesession", session_name,
                                             "desktop.conf", NULL);
    g_autoptr(GKeyFile) key = g_key_file_new();
    if(!g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, NULL)) return 2;
    g_autoptr(GError) error = NULL;
    gint style = g_key_file_get_integer(key, "GTK", "iGtk/ToolbarStyle", &error);
    return !error && style >= 0 && style <= 3 ? style : 2;
}

typedef struct { char *name; char *display; } ThemeRow;
typedef struct { GPtrArray *gtk; GPtrArray *icons; GPtrArray *cursors; } ThemeScan;
typedef struct {
    guint generation;
    char *gtk_selected;
    char *icon_selected;
    char *cursor_selected;
} ThemeScanRequest;

static guint theme_generation = 0;
static gboolean theme_load_pending = FALSE;
static GCancellable *theme_scan_cancellable = NULL;

static void theme_row_free(gpointer data)
{
    ThemeRow *row = data;
    g_free(row->name); g_free(row->display); g_free(row);
}

static void theme_scan_free(gpointer data)
{
    ThemeScan *scan = data;
    if(!scan) return;
    g_ptr_array_unref(scan->gtk); g_ptr_array_unref(scan->icons); g_ptr_array_unref(scan->cursors);
    g_free(scan);
}

static void theme_scan_request_free(gpointer data)
{
    ThemeScanRequest *request = data;
    if(!request) return;
    g_free(request->gtk_selected); g_free(request->icon_selected); g_free(request->cursor_selected);
    g_free(request);
}

static gboolean scan_theme_root(const char *root, guint kind, const char *selected,
                                GPtrArray *rows, GHashTable *seen, GCancellable *cancel)
{
    g_autoptr(GDir) dir = g_dir_open(root, 0, NULL);
    if(!dir) return TRUE;
    const char *name;
    guint visited = 0;
    while((name = g_dir_read_name(dir)) != NULL) {
        if(g_cancellable_is_cancelled(cancel)) return FALSE;
        if(++visited > 4096 || rows->len >= 4096) break;
        if(!g_hash_table_contains(seen, name)) {
            g_autofree char *theme_dir = g_build_filename(root, name, NULL);
            g_autofree char *lookup = NULL;
            char *display = NULL;
            gboolean valid = FALSE;
            if(kind == 0) {
                lookup = g_build_filename(theme_dir, "gtk-4.0", "gtk.css", NULL);
                valid = g_file_test(lookup, G_FILE_TEST_IS_REGULAR);
            } else if(kind == 1) {
                lookup = g_build_filename(theme_dir, "index.theme", NULL);
                struct stat st;
                if(g_stat(lookup, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0 && st.st_size <= 65536) {
                    g_autofree char *contents = NULL;
                    gsize length = 0;
                    g_autoptr(GKeyFile) key = g_key_file_new();
                    if(lxsession_read_bounded_file(lookup, 65536, &contents, &length, NULL) &&
                       g_key_file_load_from_data(key, contents, length, G_KEY_FILE_NONE, NULL) &&
                       g_key_file_has_key(key, "Icon Theme", "Directories", NULL)) {
                        valid = TRUE;
                        if(g_key_file_has_key(key, "Icon Theme", "Hidden", NULL)) {
                            g_autoptr(GError) error = NULL;
                            gboolean hidden = g_key_file_get_boolean(key, "Icon Theme", "Hidden", &error);
                            valid = !error && (!hidden || g_str_equal(name, "Adwaita") || g_str_equal(name, selected));
                        }
                        if(valid) display = g_key_file_get_locale_string(key, "Icon Theme", "Name", NULL, NULL);
                    }
                }
                if(g_str_has_prefix(name, "default.")) valid = FALSE;
            } else {
                lookup = g_build_filename(theme_dir, "cursors", NULL);
                g_autoptr(GDir) cursor_dir = g_dir_open(lookup, 0, NULL);
                if(cursor_dir) {
                    const char *entry;
                    guint cursor_count = 0;
                    while((entry = g_dir_read_name(cursor_dir)) != NULL && cursor_count++ < 512) {
                        g_autofree char *cursor = g_build_filename(lookup, entry, NULL);
                        if(g_file_test(cursor, G_FILE_TEST_IS_REGULAR)) { valid = TRUE; break; }
                    }
                }
                if(valid) display = g_strdup(name);
            }
            if(valid) {
                ThemeRow *row = g_new0(ThemeRow, 1);
                row->name = g_strdup(name); row->display = display ? display : g_strdup(name);
                g_ptr_array_add(rows, row);
                g_hash_table_add(seen, g_strdup(name));
            } else g_free(display);
        }
    }
    return TRUE;
}

static gboolean scan_theme_kind(ThemeScan *scan, guint kind, const char *selected, GCancellable *cancel)
{
    GPtrArray *rows = kind == 0 ? scan->gtk : kind == 1 ? scan->icons : scan->cursors;
    g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    const char *relative = kind == 0 ? "themes" : "icons";
    const char *const *dirs = g_get_system_data_dirs();
    g_autofree char *user_root = g_build_filename(g_get_user_data_dir(), relative, NULL);
    if(!scan_theme_root(user_root, kind, selected, rows, seen, cancel)) return FALSE;
    for(const char *const *dir = dirs; *dir; ++dir) {
        g_autofree char *root = g_build_filename(*dir, relative, NULL);
        if(!scan_theme_root(root, kind, selected, rows, seen, cancel)) return FALSE;
    }
    g_autofree char *legacy = g_build_filename(g_get_home_dir(), kind == 0 ? ".themes" : ".icons", NULL);
    if(!scan_theme_root(legacy, kind, selected, rows, seen, cancel)) return FALSE;
    if(kind == 0) {
        const char *builtins[] = {"Adwaita", "Adwaita-dark", "HighContrast"};
        for(guint i = 0; i < G_N_ELEMENTS(builtins); i++) {
            if(g_hash_table_contains(seen, builtins[i])) continue;
            ThemeRow *row = g_new0(ThemeRow, 1);
            row->name = g_strdup(builtins[i]); row->display = g_strdup(builtins[i]);
            g_ptr_array_add(rows, row); g_hash_table_add(seen, g_strdup(builtins[i]));
        }
    }
    return !g_cancellable_is_cancelled(cancel);
}

static void theme_scan_worker(GTask *task, gpointer source, gpointer task_data, GCancellable *cancel)
{
    ThemeScanRequest *request = task_data;
    (void)source;
    if(g_task_return_error_if_cancelled(task)) return;
#ifdef LUXE_APPEARANCE_TESTING
    const char *test_delay = g_getenv("LUXE_THEME_SCAN_TEST_DELAY_MS");
    if(test_delay) {
        guint64 ms = g_ascii_strtoull(test_delay, NULL, 10);
        if(ms <= 1000) g_usleep(ms * 1000);
    }
#endif
    ThemeScan *scan = g_new0(ThemeScan, 1);
    scan->gtk = g_ptr_array_new_with_free_func(theme_row_free);
    scan->icons = g_ptr_array_new_with_free_func(theme_row_free);
    scan->cursors = g_ptr_array_new_with_free_func(theme_row_free);
    if(!scan_theme_kind(scan, 0, request->gtk_selected, cancel) ||
       !scan_theme_kind(scan, 1, request->icon_selected, cancel) ||
       !scan_theme_kind(scan, 2, request->cursor_selected, cancel)) {
        theme_scan_free(scan);
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Theme scan cancelled");
        return;
    }
    g_task_return_pointer(task, scan, theme_scan_free);
}

static void apply_theme_rows(GtkListStore *list, GtkTreeView *view, GPtrArray *rows,
                             const char *selected, gboolean has_display, gboolean sort_display)
{
    gtk_list_store_clear(list);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(view);
    for(guint i = 0; i < rows->len; i++) {
        ThemeRow *row = g_ptr_array_index(rows, i);
        GtkTreeIter iter;
        gtk_list_store_append(list, &iter);
        gtk_list_store_set(list, &iter, COL_NAME, row->name,
                           COL_DISP_NAME, has_display ? row->display : NULL, -1);
        if(g_strcmp0(selected, row->name) == 0)
            gtk_tree_selection_select_iter(selection, &iter);
    }
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list),
        sort_display ? COL_DISP_NAME : COL_NAME, GTK_SORT_ASCENDING);
}

static void theme_scan_finished(GObject *source, GAsyncResult *result, gpointer data)
{
    ThemeScanRequest *request = data;
    g_autoptr(GError) error = NULL;
    ThemeScan *scan = g_task_propagate_pointer(G_TASK(result), &error);
    (void)source;
    if(request->generation == theme_generation) {
        theme_load_pending = FALSE;
        g_clear_object(&theme_scan_cancellable);
        if(scan && main_dlg_widget) {
            if(gtk_theme_list) apply_theme_rows(gtk_theme_list, gtk_theme_view, scan->gtk, request->gtk_selected, FALSE, FALSE);
            if(icon_theme_list) apply_theme_rows(icon_theme_list, icon_theme_view, scan->icons, request->icon_selected, TRUE, TRUE);
#if CURSOR_THEME
            if(cursor_theme_list) apply_theme_rows(cursor_theme_list, cursor_theme_view, scan->cursors, request->cursor_selected, TRUE, FALSE);
#endif
        }
    }
    if(scan) theme_scan_free(scan);
    theme_scan_request_free(request);
}

static void load_all_themes_now(void)
{
    if(theme_scan_cancellable) g_cancellable_cancel(theme_scan_cancellable);
    g_clear_object(&theme_scan_cancellable);
    theme_scan_cancellable = g_cancellable_new();
    ThemeScanRequest *request = g_new0(ThemeScanRequest, 1);
    request->generation = ++theme_generation;
    request->gtk_selected = g_strdup(gtk_theme_name);
    request->icon_selected = g_strdup(icon_theme_name);
    request->cursor_selected = g_strdup(cursor_theme_name);
    theme_load_pending = TRUE;
    GTask *task = g_task_new(NULL, theme_scan_cancellable, theme_scan_finished, request);
    g_task_set_task_data(task, request, NULL);
    g_task_run_in_thread(task, theme_scan_worker);
    g_object_unref(task);
}

static void schedule_theme_load(void) { load_all_themes_now(); }

void main_dlg_ensure_themes_loaded(void)
{
    if(!theme_load_pending) schedule_theme_load();
    while(theme_load_pending) g_main_context_iteration(NULL, TRUE);
}

/* Late arrival of the async settings-daemon probe (see main.c): switch
 * from standalone gtkrc mode to luxesession mode and refresh the toolbar
 * combo. Returns FALSE when the dialog is already gone so the caller cannot
 * flip session state for a window that no longer exists. Safe to call when
 * already in luxesession mode. */
gboolean main_dlg_note_session_daemon(void)
{
    if(!main_dlg_widget)
        return FALSE;
    if(!session_name)
        session_name = default_session_name();
    if(rc_file)
    {
        g_free(rc_file);
        rc_file = NULL;
    }
    tb_style = load_tb_style_from_luxesession();
    /* A toolbar change made before the late probe would be discarded; treat
     * the session value as the startup value so closing without Apply
     * restores what the session actually uses, not the standalone default. */
    old_tb_style = tb_style;
    {
        GtkWidget* tb_combo = lookup_widget(main_dlg_widget, "tb_style");
        if(tb_combo)
        {
            int previous = tb_style;
            gtk_combo_box_set_active(GTK_COMBO_BOX(tb_combo), tb_style < 4 ? tb_style : 3);
            /* The combo is already at the loaded index, so on_tb_style_changed()
             * returns early; refresh the preview directly. */
            (void)previous;
            demo_refresh_toolbar();
        }
    }
    return TRUE;
}

void main_dlg_init( GtkWidget* dlg )
{
    main_dlg_widget = dlg;
    GtkWidget* demo;
    GtkWidget* tb_combo;

    /* NOTE: gtk_rc_get_default_files() is gone with GTK2; the gtkrc is
     * ~/.gtkrc-2.0 (still consumed by GTK2 apps, which is what this
     * file configures). */
	if( under_luxesession )
	{
        g_free(session_name);
		session_name = default_session_name();
	}
	else
	{
        /* no luxesession-settings daemon, use gtkrc-2.0 */
        g_free(rc_file);
        rc_file = g_build_filename( g_get_home_dir(), ".gtkrc-2.0", NULL );
	}

    g_object_get( gtk_settings_get_default(),
                        "gtk-theme-name", &gtk_theme_name,
                        "gtk-icon-theme-name", &icon_theme_name,
                        "gtk-font-name", &font_name,
#if CURSOR_THEME
                        "gtk-cursor-theme-name", &cursor_theme_name,
                        "gtk-cursor-theme-size", &cursor_theme_size,
#endif
                        NULL );

    if(  ! gtk_theme_name )
        gtk_theme_name = g_strdup( "Raleigh" );
    if(  ! icon_theme_name )
        icon_theme_name = g_strdup( "hicolor" );
#if CURSOR_THEME
    if(  ! cursor_theme_name )
        cursor_theme_name = g_strdup( "default" );
#endif
    if( ! font_name )
        font_name = g_strdup( "Sans 10" );
    if(  ! cursor_theme_size )
        cursor_theme_size = 16;

    /* gtk-toolbar-style is not a GtkSettings property in GTK4. */
    if( under_luxesession )
        tb_style = load_tb_style_from_luxesession();
    else
        tb_style = 2;

    init_presets(dlg);
    save_startup_settings();

    INIT_LIST_VIEW( gtk_theme, "gtk-theme-name" )
    INIT_LIST_VIEW( icon_theme, "gtk-icon-theme-name" )

    color_mode_light = GTK_TOGGLE_BUTTON(lookup_widget(dlg, "color_mode_light"));
    color_mode_dark = GTK_TOGGLE_BUTTON(lookup_widget(dlg, "color_mode_dark"));
    gtk_toggle_button_set_group(color_mode_dark, color_mode_light);
    g_signal_connect(color_mode_light, "toggled", G_CALLBACK(on_color_mode_toggled), GINT_TO_POINTER(FALSE));
    g_signal_connect(color_mode_dark, "toggled", G_CALLBACK(on_color_mode_toggled), GINT_TO_POINTER(TRUE));
    sync_color_mode_selector();

#if CURSOR_THEME
    INIT_LIST_VIEW( cursor_theme, "gtk-cursor-theme-name" )
#endif
    /* Enumerate themes after the dialog is up; never block startup. */
    schedule_theme_load();

    gtk_font_chooser_set_font( GTK_FONT_CHOOSER(lookup_widget(dlg, "font")), font_name );

#if CURSOR_THEME
    gtk_range_set_value( GTK_RANGE(lookup_widget(dlg, "cursor_theme_size")), cursor_theme_size );
#endif

    tb_combo = lookup_widget(dlg, "tb_style");
    gtk_combo_box_set_active( GTK_COMBO_BOX(tb_combo), tb_style < 4 ? tb_style : 3 );

    apply_button = lookup_widget(dlg, "apply");

    GET_WIDGET_WITH_TYPE( demo_box, GTK_WIDGET );

    /* In-process preview (the XEMBED socket/plug demo is gone in GTK4):
     * the demo content always lives in this process now. */
    demo = show_demo();
    if(demo)
        gtk_box_append(GTK_BOX(demo_box), demo);

    disable_apply();
    check_pending_apply();
}

static void reload_all_programs( gboolean icon_only )
{
    /* gdk_event_send_clientmessage_toall() is gone in GTK4; send the
     * client message directly when on X11, skip on Wayland. */
    if(!have_x11())
        return;
#ifdef GDK_WINDOWING_X11
    {
        Display* dpy = xdisplay();
        if(!dpy)
            return;
        Window root = DefaultRootWindow(dpy);
        XEvent event;
        memset(&event, 0, sizeof(event));
        event.xclient.type = ClientMessage;
        event.xclient.send_event = True;
        event.xclient.display = dpy;
        event.xclient.window = root;
        event.xclient.format = 8;
        /* These selection-owner messages must be trapped: a window manager
         * that dies between the atom lookup and the send would otherwise
         * abort the process. */
        gdk_x11_display_error_trap_push(gdk_display_get_default());
        if(under_luxesession)
        {
            event.xclient.message_type = XInternAtom(dpy, "_LUXESESSION", False);
            event.xclient.data.b[0] = 0; /* LXS_RELOAD */
        }
        else if(icon_only)
            event.xclient.message_type = XInternAtom(dpy, "_GTK_LOAD_ICONTHEMES", False);
        else
            event.xclient.message_type = XInternAtom(dpy, "_GTK_READ_RCFILES", False);
        XSendEvent(dpy, root, False,
                   SubstructureRedirectMask | SubstructureNotifyMask, &event);
        XFlush(dpy);
        gdk_x11_display_error_trap_pop_ignored(gdk_display_get_default());
    }
#else
    (void)icon_only;
#endif
}

static void reload_openbox_theme(void)
{
    char* argv[] = {"openbox", "--reconfigure", NULL};
    /* Openbox applies rc.xml on its standard reconfigure request. This is
     * best-effort: persistence has already succeeded and non-Openbox callers
     * can still use Luxe Appearance for their GTK settings. */
    g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                  NULL, NULL, NULL, NULL);
}

/* GTK4 applications do not all re-read their CSS when the legacy XSettings
 * notification arrives.  LuxePanel's "System theme" background deliberately
 * follows GTK, so restart an already session-managed panel after a successful
 * Apply.  Do not invoke luxepanelctl outside LuxeSession: its restart command
 * intentionally respawns a missing panel, which would be surprising from an
 * appearance utility used in another desktop. */
static void reload_luxepanel_theme(void)
{
    char* argv[] = {"luxepanelctl", "restart", NULL};

    if(!under_luxesession || !g_getenv("_LUXESESSION_PID"))
        return;
    g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                  NULL, NULL, NULL, NULL);
}

/* One persistence target inside the Apply transaction. Every file Apply can
 * write is snapshotted up front, so a failure in any later write rolls back
 * exactly the files that were actually touched instead of leaving a
 * half-applied desktop. */
typedef struct {
    char* path;
    char* contents;
    gsize len;
    gboolean existed;
    gboolean written;
} ApplyTarget;

static void apply_target_free(ApplyTarget* target)
{
    if(!target)
        return;
    g_free(target->path);
    g_free(target->contents);
    g_free(target);
}

static ApplyTarget* snapshot_file(const char* path, GError** error)
{
    ApplyTarget* target = g_new0(ApplyTarget, 1);
    target->path = g_strdup(path);
    struct stat st;
    if(g_lstat(path, &st) == 0 || errno != ENOENT)
    {
        target->existed = TRUE;
        if(!lxsession_read_bounded_file(path, 1024 * 1024, &target->contents, &target->len, error))
        {
            apply_target_free(target);
            return NULL;
        }
    }
    return target;
}

/* Restore one target and report failures instead of silently leaving the
 * desktop in a different state than the error message claims. */
static gboolean restore_snapshot(ApplyTarget* target, GError** error)
{
    if(!target || !target->written)
        return TRUE;
    gboolean ok;
    if(!target->existed) {
        ok = g_unlink(target->path) == 0 || errno == ENOENT;
        if(!ok)
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                        "Cannot remove %s: %s", target->path, g_strerror(errno));
    } else
        ok = lxsession_atomic_write_file(target->path, target->contents, (gssize)target->len, error);
    if(ok) target->written = FALSE;
    return ok;
}

static gboolean rollback_targets(GArray* targets, GError** error)
{
    guint i;
    g_autoptr(GString) failures = g_string_new(NULL);
    /* Undo in reverse write order. */
    for(i = targets->len; i > 0; --i)
    {
        ApplyTarget* target = &g_array_index(targets, ApplyTarget, i - 1);
        g_autoptr(GError) local = NULL;
        if(!restore_snapshot(target, &local))
        {
            g_warning("luxeappearance: could not restore %s: %s",
                      target->path, local ? local->message : "unknown error");
            if(failures->len) g_string_append(failures, "\n");
            g_string_append_printf(failures, "%s: %s", target->path,
                                   local ? local->message : "unknown error");
        }
    }
    if(failures->len) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Appearance rollback incomplete:\n%s", failures->str);
        return FALSE;
    }
    return TRUE;
}

static void free_apply_targets(GArray* targets)
{
    guint i;
    for(i = 0; i < targets->len; ++i)
    {
        ApplyTarget* target = &g_array_index(targets, ApplyTarget, i);
        g_free(target->path);
        g_free(target->contents);
        target->path = NULL;
        target->contents = NULL;
    }
    g_array_set_size(targets, 0);
}

static char *apply_journal_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "luxe", "appearance-pending.ini", NULL);
}

static gboolean write_apply_journal(GArray *targets, GError **error)
{
    g_autofree char *path = apply_journal_path();
    struct stat st;
    if(g_lstat(path, &st) == 0 || errno != ENOENT) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PENDING,
                            "Resolve the interrupted appearance Apply before saving again");
        return FALSE;
    }
    g_autoptr(GKeyFile) journal = g_key_file_new();
    g_key_file_set_integer(journal, "Apply", "count", targets->len);
    for(guint i = 0; i < targets->len; i++) {
        ApplyTarget *target = &g_array_index(targets, ApplyTarget, i);
        g_autofree char *group = g_strdup_printf("Target %u", i);
        g_autofree char *encoded = g_base64_encode((const guchar *)(target->contents ? target->contents : ""), target->len);
        g_key_file_set_string(journal, group, "path", target->path);
        g_key_file_set_boolean(journal, group, "existed", target->existed);
        g_key_file_set_string(journal, group, "contents", encoded);
    }
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    g_autoptr(GSettingsSchema) schema = source ? g_settings_schema_source_lookup(source, "org.gnome.desktop.interface", TRUE) : NULL;
    if(schema) {
        g_autoptr(GSettings) settings = g_settings_new_full(schema, NULL, NULL);
        const char *keys[] = {"gtk-theme", "icon-theme", "font-name", "cursor-theme", "color-scheme"};
        for(guint i = 0; i < G_N_ELEMENTS(keys); i++) {
            if(!g_settings_schema_has_key(schema, keys[i])) continue;
            g_autofree char *value = g_settings_get_string(settings, keys[i]);
            g_key_file_set_string(journal, "Interface", keys[i], value);
        }
    }
    gsize len;
    g_autofree char *data = g_key_file_to_data(journal, &len, error);
    return data && lxsession_atomic_write_file(path, data, len, error);
}

static gboolean journal_target_allowed(const char *path)
{
    const char *relative[] = {"gtk-4.0/settings.ini", "gtk-3.0/settings.ini",
        "gtk-4.0/gtk.css", "gtk-4.0/luxe-colors.css", "openbox/rc.xml", "luxe/appearance.ini"};
    for(guint i = 0; i < G_N_ELEMENTS(relative); i++) {
        g_autofree char *allowed = g_build_filename(g_get_user_config_dir(), relative[i], NULL);
        if(!g_strcmp0(path, allowed)) return TRUE;
    }
    if(rc_file && !g_strcmp0(path, rc_file)) return TRUE;
    g_autofree char *gtkrc = g_build_filename(g_get_home_dir(), ".gtkrc-2.0", NULL);
    if(!g_strcmp0(path, gtkrc)) return TRUE;
    g_autofree char *prefix = g_build_filename(g_get_user_config_dir(), "luxesession", NULL);
    g_autofree char *parent = path ? g_path_get_dirname(path) : NULL;
    g_autofree char *root = parent ? g_path_get_dirname(parent) : NULL;
    g_autofree char *profile = parent ? g_path_get_basename(parent) : NULL;
    g_autofree char *base = path ? g_path_get_basename(path) : NULL;
    return !g_strcmp0(root, prefix) && valid_session_name(profile) && !g_strcmp0(base, "desktop.conf");
}

/* Recovery is explicit: never overwrite possible edits made after a crash
 * without asking. Validate every target and snapshot before restoring any. */
static gboolean recover_apply_journal(GError **error)
{
    g_autofree char *path = apply_journal_path();
    g_autofree char *data = NULL;
    gsize len;
    g_autoptr(GKeyFile) journal = g_key_file_new();
    if(!lxsession_read_bounded_file(path, 12 * 1024 * 1024, &data, &len, error) ||
       !g_key_file_load_from_data(journal, data, len, 0, error)) return FALSE;
    int count = g_key_file_get_integer(journal, "Apply", "count", error);
    if(count < 1 || count > 7) {
        if(!error || !*error) g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid appearance journal");
        return FALSE;
    }
    GArray *targets = g_array_new(FALSE, TRUE, sizeof(ApplyTarget));
    gboolean ok = TRUE;
    for(int i = 0; i < count && ok; i++) {
        g_autofree char *group = g_strdup_printf("Target %d", i);
        g_autofree char *target_path = g_key_file_get_string(journal, group, "path", NULL);
        g_autofree char *encoded = g_key_file_get_string(journal, group, "contents", NULL);
        if(!journal_target_allowed(target_path) || !encoded || strlen(encoded) > 1400000) { ok = FALSE; break; }
        for(guint j = 0; j < targets->len; j++)
            if(!g_strcmp0(target_path, g_array_index(targets, ApplyTarget, j).path)) ok = FALSE;
        if(!ok) break;
        ApplyTarget *current = snapshot_file(target_path, error);
        if(!current) { ok = FALSE; break; }
        g_free(current->contents);
        current->contents = (char *)g_base64_decode(encoded, &current->len);
        g_autofree char *canonical = g_base64_encode((guchar *)current->contents, current->len);
        if(strcmp(canonical, encoded) || (current->len && memchr(current->contents, 0, current->len)) || current->len > 1024 * 1024) {
            apply_target_free(current); ok = FALSE; break;
        }
        /* Decode results need NUL termination for the shared text writer. */
        current->contents = g_realloc(current->contents, current->len + 1);
        current->contents[current->len] = 0;
        current->existed = g_key_file_get_boolean(journal, group, "existed", error);
        current->written = TRUE;
        if(error && *error) { apply_target_free(current); ok = FALSE; break; }
        g_array_append_val(targets, *current); g_free(current);
    }
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    g_autoptr(GSettingsSchema) schema = source ? g_settings_schema_source_lookup(source, "org.gnome.desktop.interface", TRUE) : NULL;
    g_autoptr(GSettings) settings = schema ? g_settings_new_full(schema, NULL, NULL) : NULL;
    const char *keys[] = {"gtk-theme", "icon-theme", "font-name", "cursor-theme", "color-scheme"};
    /* Treat the user-owned journal as data: invalid enum values must neither
     * raise GSettings criticals nor restore files before validation finishes. */
    for(guint i = 0; ok && settings && i < G_N_ELEMENTS(keys); i++) {
        if(!g_settings_schema_has_key(schema, keys[i])) continue;
        g_autofree char *value = g_key_file_get_string(journal, "Interface", keys[i], NULL);
        if(!value) continue;
        g_autoptr(GSettingsSchemaKey) key = g_settings_schema_get_key(schema, keys[i]);
        g_autoptr(GVariant) variant = g_variant_ref_sink(g_variant_new_string(value));
        if(strlen(value) > 16384 || !g_settings_schema_key_range_check(key, variant) ||
           !g_settings_is_writable(settings, keys[i])) ok = FALSE;
    }
    if(ok) {
        ok = rollback_targets(targets, error);
    } else if(!error || !*error)
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid or unsafe appearance journal data");
    free_apply_targets(targets); g_array_unref(targets);
    if(ok) {
        if(settings) {
            g_settings_delay(settings);
            for(guint i = 0; i < G_N_ELEMENTS(keys); i++) {
                if(!g_settings_schema_has_key(schema, keys[i])) continue;
                g_autofree char *value = g_key_file_get_string(journal, "Interface", keys[i], NULL);
                if(!value) continue;
                if(strlen(value) > 16384 || !g_settings_is_writable(settings, keys[i]) ||
                   !g_settings_set_string(settings, keys[i], value)) { ok = FALSE; break; }
            }
            if(ok) { g_settings_apply(settings); g_settings_sync(); }
            else {
                g_settings_revert(settings);
                g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Files restored, but global interface settings could not be restored");
            }
        }
    }
    if(ok && g_unlink(path) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Cannot clear appearance journal: %s", g_strerror(errno));
        ok = FALSE;
    }
    return ok;
}

static void pending_apply_choice(gint choice, gpointer data)
{
    (void)data;
    if(!main_dlg_widget || choice < 0) return;
    g_autofree char *path = apply_journal_path();
    g_autoptr(GError) error = NULL;
    if(choice == 1) {
        if(!recover_apply_journal(&error)) {
            luxe_show_error(GTK_WINDOW(main_dlg_widget), _("Could not restore appearance settings"), error->message);
            return;
        }
        settings_dirty = FALSE;
        /* The existing session reads its restored profile on this request.
         * Reopen Appearance to read the reconciled settings from GTK. */
        reload_all_programs(FALSE); reload_openbox_theme(); reload_luxepanel_theme();
        gtk_window_close(GTK_WINDOW(main_dlg_widget));
    } else if(choice == 0) {
        if(g_unlink(path) != 0 && errno != ENOENT)
            luxe_show_error(GTK_WINDOW(main_dlg_widget), _("Could not clear appearance journal"), g_strerror(errno));
    }
}

static void check_pending_apply(void)
{
    g_autofree char *path = apply_journal_path();
    if(!g_file_test(path, G_FILE_TEST_EXISTS) && !g_file_test(path, G_FILE_TEST_IS_SYMLINK)) return;
    const char *choices[] = {_("Keep current settings"), _("Restore previous settings"), NULL};
    luxe_ask(GTK_WINDOW(main_dlg_widget), _("An appearance Apply was interrupted"),
        _("Restore the files saved before Apply, or keep the current files. Restoring also replaces edits made after the interruption. Reopen Appearance after restoring."),
        choices, -1, 0, pending_apply_choice, NULL);
}

void
on_apply_clicked                       (GtkButton       *button,
                                        gpointer         user_data)
{
    (void)user_data;
    GError* error = NULL;
    GError* rollback_error = NULL;
    GArray* targets = g_array_new(FALSE, TRUE, sizeof(ApplyTarget));
    gboolean gtk_theme_changed = g_strcmp0(gtk_theme_name, old_gtk_theme_name) != 0;
    g_autofree char* path40 = g_build_filename(g_get_user_config_dir(), "gtk-4.0", "settings.ini", NULL);
    g_autofree char* path30 = g_build_filename(g_get_user_config_dir(), "gtk-3.0", "settings.ini", NULL);
    g_autofree char* pathob = openbox_config_path();
    g_autofree char* pathcss = g_build_filename(g_get_user_config_dir(), "gtk-4.0", "gtk.css", NULL);
    g_autofree char* pathbridge = g_build_filename(g_get_user_config_dir(), "gtk-4.0", "luxe-colors.css", NULL);
    g_autofree char* pathstate = appearance_state_path();
    gboolean ok = TRUE;
    guint i;

    /* Snapshot every file this Apply may write, including the LuxeSession
     * config and the standalone gtkrc. Previously only the two GTK files and
     * rc.xml were restored, so a late failure left the session config and
     * gtkrc permanently changed while the UI reported "not saved". */
    {
        g_autofree char* session_path = NULL;
        const char* paths[7];
        guint n = 0;
        if(under_luxesession)
        {
            /* Snapshot the exact profile used by write_luxesession_config(),
             * not a fresh environment-derived name. */
            ensure_session_name();
            session_path = g_build_filename(g_get_user_config_dir(), "luxesession", session_name, "desktop.conf", NULL);
        }
        paths[n++] = path40;
        paths[n++] = path30;
        paths[n++] = pathob;
        /* Under LuxeSession the gtkrc is not written; standalone it is. A
         * duplicate snapshot of path40 in the impossible "neither" case keeps
         * the index arithmetic below simple. */
        paths[n++] = session_path ? session_path : (rc_file ? rc_file : path40);
        paths[n++] = pathcss;
        paths[n++] = pathbridge;
        paths[n++] = pathstate;
        for(i = 0; i < n; ++i)
        {
            ApplyTarget* target = snapshot_file(paths[i], &error);
            if(!target)
            {
                ok = FALSE;
                break;
            }
            g_array_append_val(targets, *target);
            g_free(target);
        }
    }

    gboolean journal_written = FALSE;
    if(ok) {
        ok = write_apply_journal(targets, &error);
        journal_written = ok;
    }
    if(ok)
    {
        /* Each write records that it ran, so rollback only touches files
         * this Apply actually modified. */
        if(!write_settings_file("gtk-4.0", &error))
        {
            g_array_index(targets, ApplyTarget, 0).written = TRUE;
            ok = FALSE;
        }
        else
        {
            g_array_index(targets, ApplyTarget, 0).written = TRUE;
            if(!write_settings_file("gtk-3.0", &error))
            {
                g_array_index(targets, ApplyTarget, 1).written = TRUE;
                ok = FALSE;
            }
            else
            {
                g_array_index(targets, ApplyTarget, 1).written = TRUE;
                if(under_luxesession)
                {
                    if(!write_luxesession_config(&error))
                    {
                        g_array_index(targets, ApplyTarget, 3).written = TRUE;
                        ok = FALSE;
                    }
                    else
                        g_array_index(targets, ApplyTarget, 3).written = TRUE;
                }
                else if(rc_file)
                {
                    if(!write_rc_file(rc_file, &error))
                    {
                        g_array_index(targets, ApplyTarget, 3).written = TRUE;
                        ok = FALSE;
                    }
                    else
                        g_array_index(targets, ApplyTarget, 3).written = TRUE;
                }
                gboolean openbox_may_have_written = FALSE;
                ApplyTarget *openbox_target = &g_array_index(targets, ApplyTarget, 2);
                if(ok && !write_openbox_theme_config(openbox_target->contents, openbox_target->len,
                                                      openbox_target->existed, TRUE,
                                                      &openbox_may_have_written, &error))
                {
                    g_array_index(targets, ApplyTarget, 2).written = openbox_may_have_written;
                    ok = FALSE;
                }
                else if(ok)
                    g_array_index(targets, ApplyTarget, 2).written = openbox_may_have_written;
            }
        }
    }

    if(ok) {
        g_array_index(targets, ApplyTarget, 4).written = TRUE;
        g_array_index(targets, ApplyTarget, 5).written = TRUE;
        ok = luxe_theme_write_bridge(luxe_theme_find(gtk_theme_name), &error);
        if(ok && luxe_style_get_high_contrast(luxe_style_settings()))
            ok = luxe_theme_refresh_bridge(NULL, &error);
        if(ok) {
            g_array_index(targets, ApplyTarget, 6).written = TRUE;
            ok = write_appearance_state(&error);
        }
    }
    if(ok && journal_written) {
        g_autofree char *journal = apply_journal_path();
        if(g_unlink(journal) != 0) {
            g_set_error(&error, G_FILE_ERROR, g_file_error_from_errno(errno), "Cannot finish appearance transaction: %s", g_strerror(errno));
            ok = FALSE;
        }
    }
    if(ok)
        free_apply_targets(targets);
    else
    {
        gboolean restored = rollback_targets(targets, &rollback_error);
        if(journal_written && restored) {
            g_autofree char *journal = apply_journal_path();
            if(g_unlink(journal) != 0 && errno != ENOENT)
                g_set_error(&rollback_error, G_FILE_ERROR, g_file_error_from_errno(errno), "Cannot clear appearance journal: %s", g_strerror(errno));
        }
        free_apply_targets(targets);
    }
    g_array_unref(targets);

    if(!ok)
    {
        g_autofree char *detail = error && rollback_error
            ? g_strdup_printf("%s\n\n%s", error->message, rollback_error->message)
            : g_strdup(error ? error->message : rollback_error ? rollback_error->message : "Unknown error");

        GtkAlertDialog* dialog = gtk_alert_dialog_new("Could not save appearance settings");
        gtk_alert_dialog_set_detail(dialog, detail);
        gtk_alert_dialog_show(dialog, GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(button))));
        g_object_unref(dialog);
        g_clear_error(&error);
        g_clear_error(&rollback_error);
        return;
    }
    {
        /* The files are already durable. A GSettings failure is reported but
         * does not undo them, and Apply is still considered complete. */
        g_autoptr(GError) publish_error = NULL;
        if(!publish_interface_settings(&publish_error))
            luxe_show_error(GTK_WINDOW(main_dlg_widget), _("Theme files saved, but global interface settings could not be published"),
                publish_error ? publish_error->message : _("The interface settings backend is unavailable"));
    }
    save_startup_settings();
    reload_all_programs(FALSE);
    reload_openbox_theme();
    if(gtk_theme_changed)
        reload_luxepanel_theme();
    apply_preset_wallpaper();
    settings_dirty = FALSE;
    disable_apply();
}


void
on_font_changed                        (GtkFontButton   *fontbutton,
                                        gpointer         user_data)
{
    char* name = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(fontbutton));
    (void)user_data;
    if( name && font_name && 0 == strcmp( name, font_name ) )
    {
        g_free(name);
        return;
    }
    g_free( font_name );
    font_name = name;

    /* Live preview; Apply persists the choice. */
    mark_dirty();
}

/* Keep one cancellable install job per window; archive work stays off GTK. */
typedef struct {
    char* archive;
    char* script;
} InstallJob;

static void install_job_free(gpointer data)
{
    InstallJob* job = data;
    if(!job)
        return;
    g_free(job->archive);
    g_free(job->script);
    g_free(job);
}

static void refresh_icon_and_cursor_lists(void)
{
    if(!icon_theme_list)
        return;
    schedule_theme_load();
}

static void report_install_result(const char* title, const char* detail)
{
    GtkWindow* window = main_dlg_widget ? GTK_WINDOW(main_dlg_widget) : NULL;
    GtkAlertDialog* dialog;
    if(!window)
        return;
    dialog = gtk_alert_dialog_new("%s", title);
    if(detail && *detail)
        gtk_alert_dialog_set_detail(dialog, detail);
    gtk_alert_dialog_show(dialog, window);
    g_object_unref(dialog);
}

static void on_install_finished(GObject* source, GAsyncResult* result, gpointer user_data)
{
    InstallJob* job = user_data;
    GError* error = NULL;
    g_autofree char* output = NULL;
    g_autofree char* stderr_output = NULL;
    gboolean ok;

    ok = g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source), result, &output, &stderr_output, &error);
    if(ok && !g_subprocess_get_successful(G_SUBPROCESS(source)))
    {
        gint status = g_subprocess_get_status(G_SUBPROCESS(source));
        g_autofree char *diagnostic = stderr_output ? g_utf8_make_valid(stderr_output, -1) : NULL;
        if(diagnostic && strlen(diagnostic) > 4096)
            diagnostic[4096] = '\0';
        g_set_error(&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "The icon-theme installer exited with status %d.%s%s%s",
                    status,
                    (diagnostic && *diagnostic) ? "\n\n" : "",
                    (diagnostic && *diagnostic) ? diagnostic : "",
                    stderr_output && strlen(stderr_output) > 4096 ? "\n[diagnostic truncated]" : "");
        ok = FALSE;
    }
    g_clear_object(&active_install_process);

    /* The window may be gone while the helper was running; every global it
     * touched is NULL after main_dlg_shutdown(). */
    if(!main_dlg_widget)
    {
        g_clear_error(&error);
        install_job_free(job);
        return;
    }

    if(ok)
    {
        /* Refresh and select the theme the installer reported, if any. */
        g_autofree char* installed = g_strdup(output ? output : "");
        char* newline = strchr(installed, '\n');
        if(newline)
            *newline = '\0';
        refresh_icon_and_cursor_lists();
        if(*installed)
        {
            select_list_theme(icon_theme_view, icon_theme_list, installed);
#if CURSOR_THEME
            select_list_theme(cursor_theme_view, cursor_theme_list, installed);
#endif
            mark_dirty();
        }
        if(stderr_output && *stderr_output) {
            g_autofree char *diagnostic = g_utf8_make_valid(stderr_output, -1);
            if(strlen(diagnostic) > 4096) diagnostic[4096] = '\0';
            report_install_result(_("Icon theme installed with warnings"), diagnostic);
        } else {
            report_install_result(_("Icon theme installed"),
                                  _("Apply to save the new theme."));
        }
    }
    else
        report_install_result(_("Could not install the icon theme"),
                              error ? error->message : _("Unknown error"));
    g_clear_error(&error);
    GtkWidget *install_button = lookup_widget(main_dlg_widget, "install_theme");
    if(install_button) gtk_widget_set_sensitive(install_button, TRUE);
    install_job_free(job);
}

static void on_install_response( GtkNativeDialog* dialog,
                                 int response,
                                 gpointer user_data )
{
    GError* error = NULL;
    GListModel* files = NULL;
    GObject* file_object = NULL;
    g_autofree char* file = NULL;
    g_autofree char* script = NULL;
    InstallJob* job = NULL;
    const char* argv[3] = { NULL, NULL, NULL };
    GSubprocess* subprocess = NULL;
    GtkWidget* window = user_data;

    if(active_install_process) {
        report_install_result(_("Icon theme installation is already running"),
                              _("Wait for the current archive to finish before starting another."));
        g_object_unref(dialog);
        return;
    }

    if( response == GTK_RESPONSE_ACCEPT )
    {
        files = gtk_file_chooser_get_files( GTK_FILE_CHOOSER(dialog) );
        if(files && g_list_model_get_n_items(files) > 0)
            file_object = g_list_model_get_object(files, 0);
        if(file_object)
        {
            g_autoptr(GFile) chosen = G_FILE(g_steal_pointer(&file_object));
            file = g_file_get_path(chosen);
        }
    }
    /* get_files() returns a full reference; it was leaked on every install. */
    g_clear_object(&files);
    g_clear_object(&file_object);

    if(!file)
    {
        if(response == GTK_RESPONSE_ACCEPT)
            report_install_result(_("Could not install the icon theme"),
                                  _("The selected archive could not be read as a local file."));
        g_object_unref( dialog );
        return;
    }

    /* The script is shipped under the package data directory. Never fall back
     * to a relative path: a direct build launched from an untrusted working
     * directory would execute that directory's script. */
    script = g_build_filename(PACKAGE_DATA_DIR, "luxeappearance", "install-icon-theme.sh", NULL);
    if(!g_file_test(script, G_FILE_TEST_IS_EXECUTABLE))
    {
        report_install_result(_("Could not install the icon theme"),
                              _("The icon-theme installer is missing from this installation."));
        g_object_unref( dialog );
        return;
    }

    argv[0] = script;
    argv[1] = file;
    argv[2] = NULL;
    subprocess = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE, &error);
    if(!subprocess)
    {
        report_install_result(_("Could not install the icon theme"),
                              error ? error->message : _("Unknown error"));
        g_clear_error(&error);
        g_object_unref( dialog );
        return;
    }

    job = g_new0(InstallJob, 1);
    job->archive = g_steal_pointer(&file);
    job->script = g_steal_pointer(&script);
    active_install_process = g_object_ref(subprocess);
    GtkWidget *install_button = lookup_widget(main_dlg_widget, "install_theme");
    if(install_button) gtk_widget_set_sensitive(install_button, FALSE);
    g_subprocess_communicate_utf8_async(subprocess, NULL, NULL, on_install_finished, job);
    /* The job outlives this callback through the async call. */
    g_object_unref( subprocess );
    (void)window;
    g_object_unref( dialog );
}

void
on_install_theme_clicked               (GtkButton       *button,
                                        gpointer         user_data)
{
    GtkFileFilter* filter = gtk_file_filter_new();
    GtkFileChooserNative* fc;
    GFile* initial_folder = NULL;
    GtkTreeSelection* sel;
    GtkTreeModel* model = NULL;
    GtkTreeIter it;
    char* selected_theme = NULL;
    (void)button; (void)user_data;

    /* Adw activates the portal FileChooser backend; without an explicit
     * folder that backend opens in the process cwd and the icon-theme
     * installer misbehaves. Start in the current theme's directory when
     * it can be located, falling back to ~/.local/share/themes. */
    sel = gtk_tree_view_get_selection(icon_theme_view);
    if(sel && gtk_tree_selection_get_selected(sel, &model, &it))
        gtk_tree_model_get(model, &it, COL_NAME, &selected_theme, -1);
    if(selected_theme && *selected_theme)
    {
        const char* const* dirs = g_get_system_data_dirs();
        const char* const* dir;
        char* path;
        for(dir = dirs; *dir && !initial_folder; ++dir)
        {
            path = g_build_filename(*dir, "icons", selected_theme, NULL);
            if(g_file_test(path, G_FILE_TEST_IS_DIR))
                initial_folder = g_file_new_for_path(path);
            g_free(path);
        }
        if(!initial_folder)
        {
            path = g_build_filename(g_get_user_data_dir(), "icons", selected_theme, NULL);
            if(g_file_test(path, G_FILE_TEST_IS_DIR))
                initial_folder = g_file_new_for_path(path);
            g_free(path);
        }
        if(!initial_folder)
        {
            path = g_build_filename(g_get_home_dir(), ".icons", selected_theme, NULL);
            if(g_file_test(path, G_FILE_TEST_IS_DIR))
                initial_folder = g_file_new_for_path(path);
            g_free(path);
        }
    }
    g_free(selected_theme);
    if(!initial_folder)
    {
        /* An icon archive belongs with icon themes, not window themes. */
        char* path = g_build_filename(g_get_user_data_dir(), "icons", NULL);
        initial_folder = g_file_new_for_path(path);
        g_free(path);
    }
    fc = gtk_file_chooser_native_new( _("Select an icon theme"),
                                      GTK_WINDOW(main_dlg_widget),
                                      GTK_FILE_CHOOSER_ACTION_OPEN,
                                      _("_Open"), _("_Cancel") );

    gtk_file_filter_add_pattern( filter, "*.tar.gz" );
    gtk_file_filter_add_pattern( filter, "*.tar.bz2" );
    gtk_file_filter_set_name( filter, _("*.tar.gz, *.tar.bz2 (Icon Theme)") );

    gtk_file_chooser_add_filter( GTK_FILE_CHOOSER(fc), filter );
    gtk_file_chooser_set_filter( GTK_FILE_CHOOSER(fc), filter );

    /* A rejected initial folder would silently leave the chooser in the
     * process working directory. */
    if(!gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(fc), initial_folder, NULL))
    {
        g_autoptr(GFile) home = g_file_new_for_path(g_get_home_dir());
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(fc), home, NULL);
    }
    g_object_unref(initial_folder);

    g_signal_connect( fc, "response", G_CALLBACK(on_install_response), main_dlg_widget );
    gtk_native_dialog_show( GTK_NATIVE_DIALOG(fc) );
    /* NOTE: the dialog unrefs itself in on_install_response. */
}


/* Remove the selected user icon theme to the trash. System themes and
 * non-icon selections are refused. */
typedef struct { char *name; } RemoveThemeJob;

static void on_theme_trashed(GObject *source, GAsyncResult *result, gpointer data)
{
    RemoveThemeJob *job = data;
    g_autoptr(GError) error = NULL;
    gboolean ok = g_file_trash_finish(G_FILE(source), result, &error);
    if(!main_dlg_widget) goto done;
    if(!ok) {
        g_autofree char *message = g_strdup_printf(_("Could not remove theme: %s"),
            error ? error->message : _("unknown error"));
        luxe_show_error(GTK_WINDOW(main_dlg_widget), message, NULL);
        goto done;
    }
    if(g_strcmp0(icon_theme_name, job->name) == 0 ||
       g_strcmp0(cursor_theme_name, job->name) == 0) {
        gboolean removed_cursor = g_strcmp0(cursor_theme_name, job->name) == 0;
        g_free(icon_theme_name); icon_theme_name = g_strdup("Adwaita");
        if(removed_cursor) { g_free(cursor_theme_name); cursor_theme_name = g_strdup("default"); }
        apply_to_settings();
        settings_dirty = TRUE;
        enable_apply();
    }
    refresh_icon_and_cursor_lists();
done:
    g_free(job->name); g_free(job);
}

static void on_remove_theme_confirmed(gint button_index, gpointer data)
{
    g_autofree char *path = data;
    g_autoptr(GFile) dir = NULL;
    g_autofree char *name = NULL;

    if (button_index != 1 || !path)
        return;
    if (!main_dlg_widget)
        return; /* the dialog closed while the confirmation was pending */

    name = g_path_get_basename(path);
    dir = g_file_new_for_path(path);
    RemoveThemeJob *job = g_new0(RemoveThemeJob, 1);
    job->name = g_steal_pointer(&name);
    g_file_trash_async(dir, G_PRIORITY_DEFAULT, NULL, on_theme_trashed, job);
}

/* Locate a user icon theme in the roots this application may manage. The
 * installer writes to $XDG_DATA_HOME/icons, so checking only ~/.icons made
 * every freshly installed theme look non-removable. */
static char* find_user_icon_theme(const char* name)
{
    g_autofree char *xdg_icons = g_build_filename(g_get_user_data_dir(), "icons", NULL);
    g_autofree char *legacy_icons = g_build_filename(g_get_home_dir(), ".icons", NULL);
    const char *roots[] = { xdg_icons, legacy_icons };

    if (!name || !*name || strchr(name, '/'))
        return NULL;
    for (gsize i = 0; i < G_N_ELEMENTS(roots); i++)
    {
        g_autofree char *candidate = g_build_filename(roots[i], name, NULL);
        if (g_file_test(candidate, G_FILE_TEST_IS_DIR) &&
            !g_file_test(candidate, G_FILE_TEST_IS_SYMLINK))
            return g_steal_pointer(&candidate);
    }
    return NULL;
}

void
on_remove_theme_clicked                (GtkButton       *button,
                                        gpointer         user_data)
{
    (void)button; (void)user_data;
    GtkTreeSelection *sel;
    GtkTreeModel *model = NULL;
    GtkTreeIter it;
    g_autofree char *name = NULL;
    g_autofree char *path = NULL;

    if (!main_dlg_widget)
        return;
    sel = gtk_tree_view_get_selection(icon_theme_view);
    if (!gtk_tree_selection_get_selected(sel, &model, &it))
        return;
    gtk_tree_model_get(model, &it, COL_NAME, &name, -1);
    if (!name || !*name)
        return;

    path = find_user_icon_theme(name);
    if (!path)
    {
        gchar *msg = g_strdup_printf(
            _("Theme '%s' is not a user theme and cannot be removed here."), name);
        luxe_show_error(GTK_WINDOW(main_dlg_widget), msg, NULL);
        g_free(msg);
        return;
    }

    {
        gchar *msg = g_strdup_printf(_("Remove theme '%s'? It will be moved to the trash."), name);
        luxe_confirm(GTK_WINDOW(main_dlg_widget), msg, NULL, TRUE,
                     on_remove_theme_confirmed, g_steal_pointer(&path));
        g_free(msg);
    }
}

#if CURSOR_THEME
void
on_cursor_size_changed                 (GtkRange       *range,
                                        gpointer         user_data)
{
    (void)user_data;
    cursor_theme_size = (gint)gtk_range_get_value(range);
    mark_dirty();
}
#endif

void
on_tb_style_changed                    (GtkComboBox     *combobox,
                                        gpointer         user_data)
{
    int sel = gtk_combo_box_get_active( combobox );
    (void)user_data;
    if( sel == tb_style || sel < 0 )
        return;
    tb_style = sel;

    /* No GtkSettings property to update in GTK4; the demo toolbar
     * refreshes directly. Apply persists the choice. */
    demo_refresh_toolbar();
    settings_dirty = TRUE;
    enable_apply();
}

/* Close handling (wired in main.c): restore the startup settings when
 * the session is dirty, i.e. closing without Apply reverts the live
 * preview, mirroring the original tmp-gtkrc behavior. */
void main_dlg_handle_close( GtkWidget* dlg )
{
    (void)dlg;
    if(settings_dirty)
        restore_startup_settings();
    settings_dirty = FALSE;
}

/* Teardown for a window that is being destroyed. Everything a late callback
 * (deferred enumeration, D-Bus probe, file chooser response, icon install)
 * could still reach is cancelled and cleared here, so those callbacks see
 * NULL instead of a destroyed widget or a stale model. */
void main_dlg_shutdown( GtkWidget* dlg )
{
    if(active_install_process)
        g_subprocess_send_signal(active_install_process, SIGTERM);
    theme_generation++;
    if(theme_scan_cancellable) g_cancellable_cancel(theme_scan_cancellable);
    g_clear_object(&theme_scan_cancellable);
    theme_load_pending = FALSE;
    /* Restore while the live widgets and demo are still reachable; only then
     * clear the global widget pointer. */
    main_dlg_handle_close(dlg);
    main_dlg_widget = NULL;
    apply_button = NULL;
    gtk_theme_view = NULL;
    gtk_theme_list = NULL;
    icon_theme_view = NULL;
    icon_theme_list = NULL;
    color_mode_light = NULL;
    color_mode_dark = NULL;
    color_mode_syncing = FALSE;
    theme_wallpaper = NULL;
    g_clear_pointer(&last_dark_theme, g_free);
    g_clear_pointer(&old_last_dark_theme, g_free);
    if(preset_preview) {
        gtk_style_context_remove_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(preset_preview));
        g_clear_object(&preset_preview);
    }
    g_clear_pointer(&preset_preview_name, g_free);
    preset_preview_initialized = FALSE;
#if CURSOR_THEME
    cursor_theme_view = NULL;
    cursor_theme_list = NULL;
#endif
    g_clear_pointer(&gtk_theme_name, g_free);
    g_clear_pointer(&icon_theme_name, g_free);
    g_clear_pointer(&font_name, g_free);
    g_clear_pointer(&cursor_theme_name, g_free);
    g_clear_pointer(&old_gtk_theme_name, g_free);
    g_clear_pointer(&old_icon_theme_name, g_free);
    g_clear_pointer(&old_font_name, g_free);
#if CURSOR_THEME
    g_clear_pointer(&old_cursor_theme_name, g_free);
#endif
    g_clear_pointer(&rc_file, g_free);
    g_clear_pointer(&session_name, g_free);
}
