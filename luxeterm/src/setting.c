/**
 *      Copyright (C) 2026 Jerry Howell
 *      Copyright 2008 Fred Chien <cfsghost@gmail.com>
 *      Copyright (c) 2010 LxDE Developers, see the file AUTHORS for details.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#include <glib.h>
#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <vte/vte.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "setting.h"


/* Single copy setting*/
Setting * setting;

ColorPreset color_presets[] = {
    {
        .name = "VGA",
        .background_color = "#000000",
        .foreground_color = "#aaaaaa",
        .palette = {
            "#000000", "#aa0000", "#00aa00", "#aa5500",
            "#0000aa", "#aa00aa", "#00aaaa", "#aaaaaa",
            "#555555", "#ff5555", "#55ff55", "#ffff55",
            "#5555ff", "#ff55ff", "#55ffff", "#ffffff"
        }
    },
    {
        .name = "xterm",
        .background_color = "#000000",
        .foreground_color = "#e5e5e5",
        .palette = {
            "#000000", "#cd0000", "#00cd00", "#cdcd00",
            "#0000ee", "#cd00cd", "#00cdcd", "#e5e5e5",
            "#7f7f7f", "#ff0000", "#00ff00", "#ffff00",
            "#5c5cff", "#ff00ff", "#00ffff", "#ffffff"
        }
    },
    {
        .name = "Tango",
        .background_color = "#000000",
        .foreground_color = "#d3d7cf",
        .palette = {
            "#000000", "#cd0000", "#4e9a06", "#c4a000",
            "#3465a4", "#75507b", "#06989a", "#d3d7cf",
            "#555753", "#ef2929", "#8ae234", "#fce94f",
            "#729fcf", "#ad7fa8", "#34e2e2", "#eeeeec"
        }
    },
    {
        .name = "Solarized Dark",
        .background_color = "#002b36",
        .foreground_color = "#839496",
        .palette = {
            "#073642", "#dc322f", "#859900", "#b58900",
            "#268bd2", "#d33682", "#2aa198", "#eee8d5",
            "#002b36", "#cb4b16", "#586e75", "#657b83",
            "#839496", "#6c71c4", "#93a1a1", "#fdf6e3"
        }
    },
    {
        .name = "Solarized Light",
        .background_color = "#fdf6e3",
        .foreground_color = "#657b83",
        .palette = {
            "#073642", "#dc322f", "#859900", "#b58900",
            "#268bd2", "#d33682", "#2aa198", "#eee8d5",
            "#002b36", "#cb4b16", "#586e75", "#657b83",
            "#839496", "#6c71c4", "#93a1a1", "#fdf6e3"
        }
    },
    {
        .name = "Custom"
    }
};
const guint color_preset_count = G_N_ELEMENTS(color_presets);

guint setting_custom_preset_index(void)
{
    for (guint i = 0; i < color_preset_count; i++)
        if (!color_presets[i].background_color) return i;
    g_assert_not_reached();
}

/* One field inventory drives load/save/copy/free; all strings are owned. */
typedef struct { const char *group, *key, *fallback; gsize offset; } StringField;
#define STR(group, key, member, fallback) {group, key, fallback, G_STRUCT_OFFSET(Setting, member)}
static const StringField strings[] = {
    STR(GENERAL_GROUP, FONT_NAME, font_name, "Monospace 10"),
    STR(GENERAL_GROUP, TAB_POS, tab_position, "top"),
    STR(GENERAL_GROUP, SEL_CHARS, word_selection_characters, "-A-Za-z0-9,./?%&#:_"),
    STR(GENERAL_GROUP, COLOR_PRESET, color_preset, NULL),
#define ACCEL(key, member) STR(SHORTCUT_GROUP, key, member, key##_DEF)
    ACCEL(NEW_WINDOW_ACCEL, new_window_accel), ACCEL(NEW_TAB_ACCEL, new_tab_accel),
    ACCEL(CLOSE_TAB_ACCEL, close_tab_accel), ACCEL(CLOSE_WINDOW_ACCEL, close_window_accel),
    ACCEL(COPY_ACCEL, copy_accel), ACCEL(PASTE_ACCEL, paste_accel),
    ACCEL(NAME_TAB_ACCEL, name_tab_accel), ACCEL(PREVIOUS_TAB_ACCEL, previous_tab_accel),
    ACCEL(NEXT_TAB_ACCEL, next_tab_accel), ACCEL(MOVE_TAB_LEFT_ACCEL, move_tab_left_accel),
    ACCEL(MOVE_TAB_RIGHT_ACCEL, move_tab_right_accel), ACCEL(ZOOM_IN_ACCEL, zoom_in_accel),
    ACCEL(ZOOM_OUT_ACCEL, zoom_out_accel), ACCEL(ZOOM_RESET_ACCEL, zoom_reset_accel)
#undef ACCEL
};
typedef struct { const char *key; gsize offset; } BoolField;
#define BOOL(key, member) {key, G_STRUCT_OFFSET(Setting, member)}
static const BoolField booleans[] = {
    BOOL(FOLLOW_DESKTOP_THEME, follow_desktop_theme),
    BOOL(DISALLOW_BOLD, disallow_bold), BOOL(CURSOR_BLINKS, cursor_blink),
    BOOL(CURSOR_UNDERLINE, cursor_underline), BOOL(AUDIBLE_BELL, audible_bell),
    BOOL(HIDE_SCROLLBAR, hide_scroll_bar), BOOL(HIDE_MENUBAR, hide_menu_bar),
    BOOL(HIDE_CLOSE_BUTTON, hide_close_button), BOOL(HIDE_POINTER, hide_pointer),
    BOOL(DISABLE_F10, disable_f10), BOOL(DISABLE_ALT, disable_alt),
    BOOL(DISABLE_CONFIRM, disable_confirm)
};
typedef struct { const char *key; gsize offset; int fallback, min, max; } IntField;
#define INT(key, member, fallback, min, max) {key, G_STRUCT_OFFSET(Setting, member), fallback, min, max}
static const IntField integers[] = {
    INT(SCROLLBACK, scrollback, 1000, -1, 10000000),
    INT(GEOMETRY_COLUMNS, geometry_columns, 80, 2, 1000),
    INT(GEOMETRY_ROWS, geometry_rows, 24, 2, 1000)
};
gboolean setting_integer_range(const char *key, int *minimum, int *maximum)
{
    for (guint i = 0; i < G_N_ELEMENTS(integers); i++) {
        if (g_str_equal(key, integers[i].key)) {
            if (minimum) *minimum = integers[i].min;
            if (maximum) *maximum = integers[i].max;
            return TRUE;
        }
    }
    return FALSE;
}
#define FIELD(s, type, offset) (*(type *)((char *)(s) + (offset)))

Setting *get_setting(void) { return setting; }

void free_setting(Setting **value)
{
    if (!value || !*value) return;
    for (guint i = 0; i < G_N_ELEMENTS(strings); i++)
        g_free(FIELD(*value, char *, strings[i].offset));
    g_clear_pointer(&(*value)->keyfile, g_key_file_unref);
    g_slice_free(Setting, *value);
    *value = NULL;
}

void set_setting(Setting *value)
{
    if (value == setting) return;
    free_setting(&setting);
    setting = value;
}

Setting *copy_setting(Setting *value)
{
    g_return_val_if_fail(value, NULL);
    Setting *copy = g_slice_new(Setting);
    *copy = *value;
    for (guint i = 0; i < G_N_ELEMENTS(strings); i++)
        FIELD(copy, char *, strings[i].offset) = g_strdup(FIELD(value, char *, strings[i].offset));
    copy->keyfile = g_key_file_new();
    gsize length;
    g_autofree char *text = g_key_file_to_data(value->keyfile, &length, NULL);
    g_key_file_load_from_data(copy->keyfile, text, length, G_KEY_FILE_KEEP_COMMENTS, NULL);
    return copy;
}

static void load_color(GKeyFile *file, const char *key, GdkRGBA *color, const char *fallback)
{
    gdk_rgba_parse(color, fallback);
    g_autofree char *text = g_key_file_get_string(file, GENERAL_GROUP, key, NULL);
    GdkRGBA parsed;
    if (text && gdk_rgba_parse(&parsed, text)) *color = parsed;
}

Setting *load_setting(void)
{
    Setting *value = g_slice_new0(Setting);
    value->keyfile = g_key_file_new();
    g_autofree char *path = g_build_filename(g_get_user_config_dir(), "luxeterm", "luxeterm.conf", NULL);
    GError *error = NULL;
    if (!g_key_file_load_from_file(value->keyfile, path, G_KEY_FILE_KEEP_COMMENTS, &error)) {
        if (g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
            g_key_file_load_from_file(value->keyfile, PACKAGE_DATA_DIR "/luxeterm/luxeterm.conf",
                                     G_KEY_FILE_KEEP_COMMENTS, NULL);
        else g_printerr("LuxeTerm: keeping unreadable configuration unchanged: %s\n", error->message);
        g_clear_error(&error);
    }
    for (guint i = 0; i < G_N_ELEMENTS(strings); i++) {
        const StringField *field = &strings[i];
        char *text = g_key_file_get_string(value->keyfile, field->group, field->key, NULL);
        if (!text) text = g_strdup(field->fallback ? field->fallback : color_presets[0].name);
        FIELD(value, char *, field->offset) = text;
    }
    for (guint i = 0; i < G_N_ELEMENTS(booleans); i++)
        FIELD(value, gboolean, booleans[i].offset) =
            g_key_file_get_boolean(value->keyfile, GENERAL_GROUP, booleans[i].key, NULL);
    GError *follow_error = NULL;
    value->follow_desktop_theme = g_key_file_get_boolean(value->keyfile,
        GENERAL_GROUP, FOLLOW_DESKTOP_THEME, &follow_error);
    if (follow_error) {
        value->follow_desktop_theme = TRUE; /* existing configs opt in by default */
        g_clear_error(&follow_error);
    }
    for (guint i = 0; i < G_N_ELEMENTS(integers); i++) {
        const IntField *field = &integers[i];
        int number = g_key_file_get_integer(value->keyfile, GENERAL_GROUP, field->key, &error);
        FIELD(value, int, field->offset) = error ? field->fallback : CLAMP(number, field->min, field->max);
        g_clear_error(&error);
    }
    load_color(value->keyfile, BG_COLOR, &value->background_color, color_presets[0].background_color);
    load_color(value->keyfile, FG_COLOR, &value->foreground_color, color_presets[0].foreground_color);
    for (int i = 0; i < 16; i++) {
        g_autofree char *key = g_strdup_printf(PALETTE_COLOR_PREFIX "%d", i);
        load_color(value->keyfile, key, &value->palette_color[i], color_presets[0].palette[i]);
    }
    set_setting(value);
    return value;
}

static GdkRGBA mix_color(GdkRGBA a, GdkRGBA b, double amount)
{
    return (GdkRGBA){a.red*(1-amount)+b.red*amount,
                     a.green*(1-amount)+b.green*amount,
                     a.blue*(1-amount)+b.blue*amount, 1};
}

static GdkRGBA terminal_color(GdkRGBA color)
{
    /* GTK may resolve semantic colors in a wide gamut. VTE requires every
     * component inside sRGB's [0,1] range and an opaque ANSI palette. */
    color.red = CLAMP(color.red, 0, 1);
    color.green = CLAMP(color.green, 0, 1);
    color.blue = CLAMP(color.blue, 0, 1);
    color.alpha = 1;
    return color;
}

/* The installed GTK theme is the color authority; do not rewrite the user's
 * private preset. The derived ANSI colors keep semantic red/green/yellow and
 * the theme's accent while retaining readable neutral slots. */
gboolean setting_desktop_palette(GdkRGBA *foreground, GdkRGBA *background,
                                 GdkRGBA palette[16])
{
    GtkWidget *probe = g_object_ref_sink(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    GtkStyleContext *style = gtk_widget_get_style_context(probe);
    gboolean have_bg = gtk_style_context_lookup_color(style, "theme_bg_color", background) ||
                       gtk_style_context_lookup_color(style, "window_bg_color", background);
    gboolean have_fg = gtk_style_context_lookup_color(style, "theme_fg_color", foreground) ||
                       gtk_style_context_lookup_color(style, "window_fg_color", foreground);
    if (!have_bg || !have_fg) { g_object_unref(probe); return FALSE; }
    GdkRGBA error, success, warning, accent;
    gdk_rgba_parse(&error, "#d4474b");
    gdk_rgba_parse(&success, "#4caa6c");
    gdk_rgba_parse(&warning, "#c89c3c");
    accent = *foreground;
    gtk_style_context_lookup_color(style, "error_color", &error);
    gtk_style_context_lookup_color(style, "success_color", &success);
    gtk_style_context_lookup_color(style, "warning_color", &warning);
    if (!gtk_style_context_lookup_color(style, "accent_bg_color", &accent))
        gtk_style_context_lookup_color(style, "theme_selected_bg_color", &accent);
    g_object_unref(probe);
    *background = terminal_color(*background);
    *foreground = terminal_color(*foreground);
    error = terminal_color(error);
    success = terminal_color(success);
    warning = terminal_color(warning);
    accent = terminal_color(accent);
    palette[0] = *background;
    palette[1] = error;
    palette[2] = success;
    palette[3] = warning;
    palette[4] = accent;
    palette[5] = mix_color(accent, error, .45);
    palette[6] = mix_color(accent, success, .5);
    palette[7] = *foreground;
    palette[8] = mix_color(*background, *foreground, .45);
    for (int i = 1; i < 7; i++) palette[8+i] = mix_color(palette[i], *foreground, .24);
    palette[15] = *foreground;
    return TRUE;
}

gboolean save_setting(Setting *value, GError **error)
{
    g_autofree char *directory = g_build_filename(g_get_user_config_dir(), "luxeterm", NULL);
    g_autofree char *path = g_build_filename(directory, "luxeterm.conf", NULL);
    g_autoptr(GKeyFile) file = g_key_file_new();
    struct stat info;
    if (lstat(path, &info) == 0 && !S_ISREG(info.st_mode)) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Refusing a non-regular settings file");
        return FALSE;
    }
    GError *read_error = NULL;
    if (!g_key_file_load_from_file(file, path, G_KEY_FILE_KEEP_COMMENTS, &read_error) &&
        !g_error_matches(read_error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
        g_propagate_error(error, read_error);
        return FALSE;
    }
    g_clear_error(&read_error);
    for (guint i = 0; i < G_N_ELEMENTS(strings); i++)
        g_key_file_set_string(file, strings[i].group, strings[i].key, FIELD(value, char *, strings[i].offset));
    for (guint i = 0; i < G_N_ELEMENTS(booleans); i++)
        g_key_file_set_boolean(file, GENERAL_GROUP, booleans[i].key, FIELD(value, gboolean, booleans[i].offset));
    for (guint i = 0; i < G_N_ELEMENTS(integers); i++)
        g_key_file_set_integer(file, GENERAL_GROUP, integers[i].key, FIELD(value, int, integers[i].offset));
    for (int i = -2; i < 16; i++) {
        g_autofree char *key = i == -2 ? g_strdup(BG_COLOR) : i == -1 ? g_strdup(FG_COLOR) :
            g_strdup_printf(PALETTE_COLOR_PREFIX "%d", i);
        GdkRGBA *color = i == -2 ? &value->background_color : i == -1 ? &value->foreground_color : &value->palette_color[i];
        g_autofree char *text = gdk_rgba_to_string(color);
        g_key_file_set_string(file, GENERAL_GROUP, key, text);
    }
    if (g_mkdir_with_parents(directory, 0700) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "%s", g_strerror(errno));
        return FALSE;
    }
    gsize length;
    g_autofree char *text = g_key_file_to_data(file, &length, error);
    if (!text || !g_file_set_contents_full(path, text, length,
            G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, error)) return FALSE;
    g_key_file_unref(value->keyfile);
    value->keyfile = g_steal_pointer(&file);
    return TRUE;
}
