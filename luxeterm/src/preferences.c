/**
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

/* Copyright (C) 2026 Jerry Howell. */
#include "luxe-gtk-ui.h"
#include <glib/gi18n.h>
#include "luxeterm.h"
#include "preferences.h"

typedef struct {
    GtkWindow *dialog;
    Setting *settings;
    GWeakRef window;
    GtkWidget *preset, *follow, *colors[18];
    GPtrArray *shortcuts;
    gboolean updating;
} Preferences;

/* Settings are process-global, so two editable snapshots must not coexist. */
static GtkWindow *active_preferences;

static void preferences_destroyed(GtkWidget *dialog, gpointer unused)
{
    (void)unused;
    if (active_preferences == GTK_WINDOW(dialog)) {
        g_object_remove_weak_pointer(G_OBJECT(dialog), (gpointer *)&active_preferences);
        active_preferences = NULL;
    }
}

static void preferences_free(gpointer data)
{
    Preferences *prefs = data;
    free_setting(&prefs->settings);
    g_weak_ref_clear(&prefs->window);
    g_ptr_array_unref(prefs->shortcuts);
    g_free(prefs);
}
gint terminal_tab_get_position_id(gchar *position)
{
    const char *names[] = {"top", "bottom", "left", "right"};
    for (int i = 0; i < 4; i++) if (g_strcmp0(position, names[i]) == 0) return i;
    return 0;
}
static void set_string(char **target, const char *value)
{
    g_free(*target);
    *target = g_strdup(value);
}
static GdkRGBA *color_at(Setting *settings, int i)
{
    return i == 0 ? &settings->background_color : i == 1 ?
        &settings->foreground_color : &settings->palette_color[i - 2];
}
static void color_changed(GtkColorDialogButton *button, GParamSpec *spec, Preferences *prefs)
{
    (void)spec;
    if (prefs->updating) return;
    for (int i = 0; i < 18; i++)
        if (GTK_WIDGET(button) == prefs->colors[i])
            *color_at(prefs->settings, i) = *gtk_color_dialog_button_get_rgba(button);
    guint custom = setting_custom_preset_index();
    set_string(&prefs->settings->color_preset, color_presets[custom].name);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(prefs->preset), custom);
}
static void preset_changed(GtkDropDown *row, GParamSpec *spec, Preferences *prefs)
{
    (void)spec;
    guint selected = gtk_drop_down_get_selected(row);
    if (prefs->updating || selected >= color_preset_count) return;
    set_string(&prefs->settings->color_preset, color_presets[selected].name);
    if (!color_presets[selected].background_color) return;
    prefs->updating = TRUE;
    for (int i = 0; i < 18; i++) {
        const char *text = i == 0 ? color_presets[selected].background_color :
            i == 1 ? color_presets[selected].foreground_color : color_presets[selected].palette[i - 2];
        GdkRGBA *color = color_at(prefs->settings, i);
        gdk_rgba_parse(color, text);
        gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(prefs->colors[i]), color);
    }
    prefs->updating = FALSE;
}
static void follow_theme_changed(GtkSwitch *row, GParamSpec *spec, Preferences *prefs)
{
    (void)spec;
    prefs->settings->follow_desktop_theme = gtk_switch_get_active(row);
    gtk_widget_set_sensitive(prefs->preset, !prefs->settings->follow_desktop_theme);
    for (int i = 0; i < 18; i++)
        gtk_widget_set_sensitive(prefs->colors[i], !prefs->settings->follow_desktop_theme);
}
static void font_changed(GtkFontDialogButton *button, GParamSpec *spec, Setting *settings)
{
    (void)spec;
    const PangoFontDescription *font = gtk_font_dialog_button_get_font_desc(button);
    if (!font) return;
    g_free(settings->font_name);
    settings->font_name = pango_font_description_to_string(font);
    settings->geometry_change = TRUE;
}
static void bool_changed(GtkSwitch *row, GParamSpec *spec, gboolean *value)
{
    (void)spec;
    *value = gtk_switch_get_active(row);
}
static void int_changed(GtkSpinButton *row, GParamSpec *spec, int *value)
{
    (void)spec;
    *value = (int)gtk_spin_button_get_value(row);
}
static void text_changed(GtkEditable *editable, char **value)
{
    set_string(value, gtk_editable_get_text(editable));
}
static void position_changed(GtkDropDown *row, GParamSpec *spec, Setting *settings)
{
    (void)spec;
    const char *positions[] = {"top", "bottom", "left", "right"};
    guint selected = gtk_drop_down_get_selected(row);
    if (selected < 4) set_string(&settings->tab_position, positions[selected]);
}
static gboolean shortcut_pressed(GtkEventControllerKey *controller, guint key,
                                  guint keycode, GdkModifierType state, Preferences *prefs)
{
    (void)keycode;
    if (key == GDK_KEY_Tab || key == GDK_KEY_ISO_Left_Tab) return FALSE;
    guint mods = state & gtk_accelerator_get_default_mod_mask();
    if (!gtk_accelerator_valid(key, mods)) return TRUE;
    GtkWidget *entry = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
    g_autofree char *name = gtk_accelerator_name(key, mods);
    for (guint i = 0; i < prefs->shortcuts->len; i++) {
        GtkWidget *other = g_ptr_array_index(prefs->shortcuts, i);
        char **value = g_object_get_data(G_OBJECT(other), "shortcut-value");
        if (other != entry && g_str_equal(*value, name)) return TRUE;
    }
    char **value = g_object_get_data(G_OBJECT(entry), "shortcut-value");
    set_string(value, name);
    g_autofree char *label = gtk_accelerator_get_label(key, mods);
    gtk_editable_set_text(GTK_EDITABLE(entry), label);
    return TRUE;
}
static void apply_preferences(GtkButton *button, Preferences *prefs)
{
    (void)button;
    GError *error = NULL;
    if (!save_setting(prefs->settings, &error)) {
        GtkWindow *alert = luxe_choice_dialog_new(_("Could not save preferences"), error->message);
        luxe_choice_add_response(GTK_DIALOG(alert), "close", _("Close"));
        luxe_dialog_present(alert, GTK_WIDGET(prefs->dialog));
        g_error_free(error);
        return;
    }
    set_setting(copy_setting(prefs->settings));
    GtkWidget *window = g_weak_ref_get(&prefs->window);
    if (window && gtk_widget_get_realized(window)) {
        LuxeTerm *terminal = g_object_get_data(G_OBJECT(window), "luxe-terminal");
        if (terminal) terminal_settings_apply_to_all(terminal);
    }
    g_clear_object(&window);
    luxe_dialog_close(prefs->dialog);
}
static GtkWidget *group_new(GtkStack *stack, const char *name, const char *title)
{
    GtkWidget *page = luxe_preferences_page_new();
    GtkWidget *group = luxe_preferences_group_new();
    luxe_preferences_page_add(GTK_SCROLLED_WINDOW(page), GTK_BOX(group));
    gtk_stack_add_titled(stack, page, name, title);
    return group;
}
static GtkWidget *row_new(GtkWidget *group, const char *title, GtkWidget *control)
{
    GtkWidget *row = luxe_action_row_new();
    luxe_row_set_title(GTK_WIDGET(row), title);
    luxe_row_set_use_markup(GTK_WIDGET(row), FALSE);
    gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
    luxe_action_row_add_suffix(GTK_LIST_BOX_ROW(row), control);
    luxe_action_row_set_activatable_widget(GTK_LIST_BOX_ROW(row), control);
    luxe_preferences_group_add(GTK_BOX(group), row);
    return row;
}
void terminal_preferences_dialog(GSimpleAction *action, GVariant *param, gpointer data)
{
    (void)action; (void)param;
    LuxeTerm *terminal = data;
    if (active_preferences) {
        gtk_window_present(active_preferences);
        return;
    }
    Preferences *prefs = g_new0(Preferences, 1);
    prefs->settings = copy_setting(get_setting());
    prefs->shortcuts = g_ptr_array_new();
    g_weak_ref_init(&prefs->window, terminal->window);
    prefs->dialog = luxe_dialog_new();
    active_preferences = prefs->dialog;
    g_object_add_weak_pointer(G_OBJECT(active_preferences), (gpointer *)&active_preferences);
    g_signal_connect(active_preferences, "destroy", G_CALLBACK(preferences_destroyed), NULL);
    g_object_set_data_full(G_OBJECT(prefs->dialog), "preferences", prefs, preferences_free);
    gtk_window_set_title(prefs->dialog, _("LuxeTerm Preferences"));
    luxe_dialog_set_width(prefs->dialog, 620);
    luxe_dialog_set_height(prefs->dialog, 560);
    GtkWidget *toolbar = luxe_toolbar_new(), *header = gtk_header_bar_new();
    GtkWidget *apply = gtk_button_new_with_label(_("Apply"));
    gtk_widget_add_css_class(apply, "suggested-action");
    g_signal_connect(apply, "clicked", G_CALLBACK(apply_preferences), prefs);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), apply);
    luxe_toolbar_add_top_bar(GTK_BOX(toolbar), header);
    GtkStack *stack = GTK_STACK(gtk_stack_new());
    GtkWidget *switcher = luxe_switcher_bar_new();
    luxe_switcher_bar_set_stack(GTK_REVEALER(switcher), stack);
    gtk_revealer_set_reveal_child(GTK_REVEALER(switcher), TRUE);
    luxe_toolbar_add_bottom_bar(GTK_BOX(toolbar), switcher);
    luxe_toolbar_set_content(GTK_BOX(toolbar), GTK_WIDGET(stack));
    luxe_window_set_content(prefs->dialog, toolbar);
    GtkWidget *style = group_new(stack, "style", _("Style"));
    GtkFontDialog *font_dialog = gtk_font_dialog_new();
    GtkWidget *font = gtk_font_dialog_button_new(font_dialog);
    PangoFontDescription *desc = pango_font_description_from_string(prefs->settings->font_name);
    gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(font), desc);
    pango_font_description_free(desc);
    row_new(style, _("Terminal font"), font);
    g_signal_connect(font, "notify::font-desc", G_CALLBACK(font_changed), prefs->settings);
    prefs->follow = luxe_switch_row_new();
    luxe_row_set_title(GTK_WIDGET(prefs->follow), _("Follow desktop theme"));
    luxe_row_set_subtitle(GTK_WIDGET(prefs->follow),
        _("Use the active desktop palette; keep custom colors for when this is off."));
    luxe_preferences_group_add(GTK_BOX(style), prefs->follow);
    GtkStringList *model = gtk_string_list_new(NULL);
    for (guint i = 0; i < color_preset_count; i++) gtk_string_list_append(model, color_presets[i].name);
    prefs->preset = luxe_combo_row_new();
    luxe_row_set_title(GTK_WIDGET(prefs->preset), _("Color preset"));
    gtk_drop_down_set_model(GTK_DROP_DOWN(prefs->preset), G_LIST_MODEL(model));
    g_object_unref(model);
    guint selected = setting_custom_preset_index();
    for (guint i = 0; i < color_preset_count; i++)
        if (g_str_equal(prefs->settings->color_preset, color_presets[i].name)) selected = i;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(prefs->preset), selected);
    luxe_preferences_group_add(GTK_BOX(style), prefs->preset);
    for (int i = 0; i < 18; i++) {
        GtkColorDialog *chooser = gtk_color_dialog_new();
        gtk_color_dialog_set_with_alpha(chooser, i == 0);
        GtkWidget *button = gtk_color_dialog_button_new(chooser);
        prefs->colors[i] = button;
        gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(button), color_at(prefs->settings, i));
        g_autofree char *title = i == 0 ? g_strdup(_("Background")) : i == 1 ?
            g_strdup(_("Foreground")) : g_strdup_printf(_("Palette color %d"), i - 2);
        row_new(style, title, button);
        g_signal_connect(button, "notify::rgba", G_CALLBACK(color_changed), prefs);
    }
    g_signal_connect(prefs->preset, "notify::selected", G_CALLBACK(preset_changed), prefs);
    gtk_switch_set_active(GTK_SWITCH(prefs->follow), prefs->settings->follow_desktop_theme);
    g_signal_connect(prefs->follow, "notify::active", G_CALLBACK(follow_theme_changed), prefs);
    follow_theme_changed(GTK_SWITCH(prefs->follow), NULL, prefs);
    GtkWidget *general = group_new(stack, "general", _("Display"));
    struct {const char *title; gboolean *value;} toggles[] = {
        {N_("Cursor blinks"), &prefs->settings->cursor_blink},
        {N_("Underline cursor"), &prefs->settings->cursor_underline},
        {N_("Audible bell"), &prefs->settings->audible_bell},
        {N_("Hide scrollbar"), &prefs->settings->hide_scroll_bar},
        {N_("Hide menu"), &prefs->settings->hide_menu_bar},
        {N_("Hide close buttons"), &prefs->settings->hide_close_button},
        {N_("Hide mouse pointer"), &prefs->settings->hide_pointer},
        {N_("Disable Alt+n shortcuts"), &prefs->settings->disable_alt},
        {N_("Disable close confirmation"), &prefs->settings->disable_confirm},
    };
    for (guint i = 0; i < G_N_ELEMENTS(toggles); i++) {
        GtkWidget *row = luxe_switch_row_new();
        luxe_row_set_title(GTK_WIDGET(row), _(toggles[i].title));
        gtk_switch_set_active(GTK_SWITCH(row), *toggles[i].value);
        g_signal_connect(row, "notify::active", G_CALLBACK(bool_changed), toggles[i].value);
        luxe_preferences_group_add(GTK_BOX(general), row);
    }
    /* These stored compatibility flags have no GTK4/VTE implementation. */
    GtkWidget *legacy = gtk_label_new(_("Legacy bold and F10 preferences are preserved; this VTE version does not support them."));
    gtk_label_set_wrap(GTK_LABEL(legacy), TRUE);
    luxe_preferences_group_add(GTK_BOX(general), legacy);
    struct {const char *title, *key; int *value;} numbers[] = {
        {N_("Scrollback lines (-1 is unlimited)"), SCROLLBACK, &prefs->settings->scrollback},
        {N_("Columns"), GEOMETRY_COLUMNS, &prefs->settings->geometry_columns},
        {N_("Rows"), GEOMETRY_ROWS, &prefs->settings->geometry_rows},
    };
    for (guint i = 0; i < G_N_ELEMENTS(numbers); i++) {
        int minimum, maximum;
        if (!setting_integer_range(numbers[i].key, &minimum, &maximum)) {
            g_warning("Missing terminal setting range: %s", numbers[i].key);
            continue;
        }
        GtkWidget *row = luxe_spin_row_new_with_range(minimum, maximum, 1);
        g_object_set_data(G_OBJECT(prefs->dialog), numbers[i].key, row);
        luxe_row_set_title(GTK_WIDGET(row), _(numbers[i].title));
        if (g_str_equal(numbers[i].key, SCROLLBACK))
            luxe_row_set_subtitle(GTK_WIDGET(row), _("Unlimited scrollback can use unbounded storage."));
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(row), *numbers[i].value);
        g_signal_connect(row, "notify::value", G_CALLBACK(int_changed), numbers[i].value);
        luxe_preferences_group_add(GTK_BOX(general), row);
    }
    GtkWidget *position = luxe_combo_row_new();
    model = gtk_string_list_new((const char *[]) {_("Top"), _("Bottom"), _("Left"), _("Right"), NULL});
    gtk_drop_down_set_model(GTK_DROP_DOWN(position), G_LIST_MODEL(model));
    g_object_unref(model);
    luxe_row_set_title(GTK_WIDGET(position), _("Tab position"));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(position), terminal_tab_get_position_id(prefs->settings->tab_position));
    g_signal_connect(position, "notify::selected", G_CALLBACK(position_changed), prefs->settings);
    luxe_preferences_group_add(GTK_BOX(general), position);
    GtkWidget *words = luxe_entry_row_new();
    luxe_row_set_title(GTK_WIDGET(words), _("Word-selection characters"));
    gtk_editable_set_text(GTK_EDITABLE(words), prefs->settings->word_selection_characters);
    g_signal_connect(words, "changed", G_CALLBACK(text_changed), &prefs->settings->word_selection_characters);
    luxe_preferences_group_add(GTK_BOX(general), words);
    GtkWidget *shortcuts = group_new(stack, "shortcuts", _("Shortcuts"));
    struct {const char *title; char **value;} keys[] = {
#define KEY(title, field) {N_(title), &prefs->settings->field}
        KEY("New Window", new_window_accel), KEY("New Tab", new_tab_accel),
        KEY("Close Tab", close_tab_accel), KEY("Close Window", close_window_accel),
        KEY("Copy", copy_accel), KEY("Paste", paste_accel), KEY("Name Tab", name_tab_accel),
        KEY("Previous Tab", previous_tab_accel), KEY("Next Tab", next_tab_accel),
        KEY("Move Tab Left", move_tab_left_accel), KEY("Move Tab Right", move_tab_right_accel),
        KEY("Zoom In", zoom_in_accel), KEY("Zoom Out", zoom_out_accel), KEY("Zoom Reset", zoom_reset_accel)
#undef KEY
    };
    for (guint i = 0; i < G_N_ELEMENTS(keys); i++) {
        GtkWidget *entry = gtk_entry_new();
        gtk_editable_set_editable(GTK_EDITABLE(entry), FALSE);
        guint key = 0; GdkModifierType mods = 0;
        gtk_accelerator_parse(*keys[i].value, &key, &mods);
        g_autofree char *label = gtk_accelerator_get_label(key, mods);
        gtk_editable_set_text(GTK_EDITABLE(entry), label);
        row_new(shortcuts, _(keys[i].title), entry);
        g_object_set_data(G_OBJECT(entry), "shortcut-value", keys[i].value);
        g_ptr_array_add(prefs->shortcuts, entry);
        GtkEventController *controller = gtk_event_controller_key_new();
        g_signal_connect(controller, "key-pressed", G_CALLBACK(shortcut_pressed), prefs);
        gtk_widget_add_controller(entry, controller);
    }
    luxe_dialog_present(prefs->dialog, terminal->window);
}
