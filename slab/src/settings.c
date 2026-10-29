/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <gtk/gtk.h>
#include <gio/gdesktopappinfo.h>
#include <errno.h>
#include "luxe-renderer.h"
#include "preferences.h"
#include "pins.h"
#ifndef SLAB_VERSION
#define SLAB_VERSION "0.2.9~testing1"
#endif
typedef struct {
    GtkWindow *window;
    GtkListBox *list;
    GSettings *settings;
    GPtrArray *pins;
    char *path;
    GFileMonitor *pins_monitor;
    guint pins_refresh;
} Settings;
static void report(Settings *s, const char *message)
{
    GtkAlertDialog *dialog = gtk_alert_dialog_new("slab Settings");
    gtk_alert_dialog_set_detail(dialog, message); gtk_alert_dialog_show(dialog, s->window); g_object_unref(dialog);
}
static void send_action(const char *name)
{
    g_autoptr(GDBusConnection) bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    if (!bus) return;
    g_autoptr(GDBusActionGroup) actions = g_dbus_action_group_get(bus, "org.luxe.Slab", "/org/luxe/Slab");
    g_action_group_activate_action(G_ACTION_GROUP(actions), name, NULL);
}
static void start(GtkButton *button, gpointer data)
{
    (void)button; Settings *s = data; GError *error = NULL;
    g_autoptr(GSubprocess) child = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, &error, "slab", NULL);
    if (!child) { report(s, error->message); g_clear_error(&error); }
}
static void stop(GtkButton *button, gpointer data) { (void)button; (void)data; send_action("quit"); }
static void populate(Settings *s)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(s->list)))) gtk_list_box_remove(s->list, child);
    for (guint i = 0; i < s->pins->len; ++i) {
        const char *id = g_ptr_array_index(s->pins, i);
        g_autoptr(GDesktopAppInfo) app = g_desktop_app_info_new(id);
        GtkWidget *label = gtk_label_new(app ? g_app_info_get_display_name(G_APP_INFO(app)) : id);
        gtk_label_set_xalign(GTK_LABEL(label), 0); gtk_widget_set_margin_top(label, 6); gtk_widget_set_margin_bottom(label, 6);
        gtk_widget_set_tooltip_text(label, id); gtk_list_box_append(s->list, label);
    }
}
static gboolean refresh_pins(gpointer data)
{
    Settings *s = data; s->pins_refresh = 0;
    g_autofree char *selected_id = NULL;
    GtkListBoxRow *row = gtk_list_box_get_selected_row(s->list);
    if (row && (guint)gtk_list_box_row_get_index(row) < s->pins->len)
        selected_id = g_strdup(g_ptr_array_index(s->pins, gtk_list_box_row_get_index(row)));
    GError *error = NULL; GPtrArray *fresh = slab_pins_load(s->path, &error);
    if (!fresh) { report(s, error->message); g_clear_error(&error); return G_SOURCE_REMOVE; }
    g_ptr_array_unref(s->pins); s->pins = fresh; populate(s);
    if (selected_id) {
        int index = slab_pin_index(s->pins, selected_id);
        if (index >= 0) gtk_list_box_select_row(s->list, gtk_list_box_get_row_at_index(s->list, index));
    }
    return G_SOURCE_REMOVE;
}
static void pins_changed(GFileMonitor *monitor, GFile *file, GFile *other,
                         GFileMonitorEvent event, gpointer data)
{
    (void)monitor; (void)event; Settings *s = data;
    g_autoptr(GFile) target = g_file_new_for_path(s->path);
    if (!g_file_equal(file, target) && (!other || !g_file_equal(other, target))) return;
    if (s->pins_refresh) g_source_remove(s->pins_refresh);
    s->pins_refresh = g_timeout_add(100, refresh_pins, s);
}
static void edit_pin(GtkButton *button, gpointer data)
{
    Settings *s = data; GtkListBoxRow *row = gtk_list_box_get_selected_row(s->list);
    if (!row) return;
    int index = gtk_list_box_row_get_index(row);
    /* Reload before applying, so edits made in the dock are not overwritten. */
    const char *selected = g_ptr_array_index(s->pins, index);
    GError *error = NULL; g_autoptr(GPtrArray) current = slab_pins_load(s->path, &error);
    if (!current) { report(s, error->message); g_clear_error(&error); return; }
    index = slab_pin_index(current, selected); if (index < 0) return;
    int direction = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "direction"));
    GPtrArray *next;
    if (direction) next = slab_pins_move(current, selected, direction < 0 ? MAX(0, index - 1) : MIN(current->len, (guint)index + 2));
    else {
        next = g_ptr_array_new_with_free_func(g_free);
        for (guint i = 0; i < current->len; ++i)
            if ((int)i != index) g_ptr_array_add(next, g_strdup(g_ptr_array_index(current, i)));
    }
    if (!slab_pins_save_if_unchanged(s->path, current, next, &error)) {
        report(s, error->message); g_clear_error(&error); g_ptr_array_unref(next); return;
    }
    g_ptr_array_unref(s->pins); s->pins = next; populate(s); send_action("reload");
    gtk_list_box_select_row(s->list, gtk_list_box_get_row_at_index(s->list, MAX(0, index + direction)));
}
static void about(GtkButton *button, gpointer data)
{
    (void)button; Settings *s = data; const char *authors[] = {"Jerry Howell", NULL};
    gtk_show_about_dialog(s->window, "program-name", "slab", "version", SLAB_VERSION,
        "website", "https://github.com/openconstruct/luxeDE", "authors", authors,
        "copyright", "Copyright © 2026 Jerry Howell", "license-type", GTK_LICENSE_GPL_2_0,
        "comments", "Experimental GTK4 dock for luxe. Original implementation inspired by Plank.", NULL);
}
static void number_setting(Settings *s, GtkBox *box, const char *label, const char *key, int min, int max)
{
    if (s->settings) (void)slab_setting_integer_range(key, &min, &max);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *name = gtk_label_new(label); gtk_widget_set_hexpand(name, TRUE);
    gtk_label_set_xalign(GTK_LABEL(name), 0); gtk_box_append(GTK_BOX(row), name);
    GtkWidget *spin = gtk_spin_button_new_with_range(min, max, 1);
    gtk_box_append(GTK_BOX(row), spin); gtk_box_append(box, row);
    if (s->settings) g_settings_bind(s->settings, key, gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(spin)), "value", G_SETTINGS_BIND_DEFAULT);
    else gtk_widget_set_sensitive(spin, FALSE);
}
static void activate(GtkApplication *app, gpointer data)
{
    Settings *s = data;
    if (s->window) { gtk_window_present(s->window); return; }
    slab_follow_theme(); s->settings = slab_preferences();
    s->window = GTK_WINDOW(gtk_application_window_new(app)); g_object_ref(s->window);
    gtk_window_set_title(s->window, "slab Settings"); gtk_window_set_default_size(s->window, 420, 620);
    GtkWidget *header = gtk_header_bar_new(); gtk_window_set_titlebar(s->window, header);
    GtkWidget *info = gtk_button_new_with_label("About"); gtk_header_bar_pack_end(GTK_HEADER_BAR(header), info);
    g_signal_connect(info, "clicked", G_CALLBACK(about), s);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(box, 10); gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 10); gtk_widget_set_margin_bottom(box, 10);
    GtkWidget *page = gtk_scrolled_window_new(); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(page), box);
    gtk_window_set_child(s->window, page);
    GtkWidget *theme = gtk_label_new("Follows your desktop theme and icon theme automatically.");
    gtk_label_set_wrap(GTK_LABEL(theme), TRUE); gtk_box_append(GTK_BOX(box), theme);
    GtkWidget *hide = gtk_check_button_new_with_label("Automatically hide the dock"); gtk_box_append(GTK_BOX(box), hide);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(row), gtk_label_new("Icon size"));
    int size_min = 0, size_max = 100;
    if (s->settings) (void)slab_setting_integer_range("icon-size", &size_min, &size_max);
    GtkWidget *size = gtk_spin_button_new_with_range(size_min, size_max, 2); gtk_box_append(GTK_BOX(row), size); gtk_box_append(GTK_BOX(box), row);
    if (s->settings) {
        g_settings_bind(s->settings, "autohide", hide, "active", G_SETTINGS_BIND_DEFAULT);
        g_settings_bind(s->settings, "icon-size", gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(size)), "value", G_SETTINGS_BIND_DEFAULT);
    } else {
        gtk_widget_set_sensitive(hide, FALSE); gtk_widget_set_sensitive(size, FALSE);
    }
    number_setting(s, GTK_BOX(box), "Space between icons (px)", "gap", 0, 24);
    number_setting(s, GTK_BOX(box), "Icon padding (px)", "padding", 0, 20);
    number_setting(s, GTK_BOX(box), "Dock inner margin (px)", "margin", 0, 24);
    number_setting(s, GTK_BOX(box), "Background opacity (%) — requires compositor", "opacity", 25, 100);
    GtkWidget *animation = gtk_check_button_new_with_label("Short hover transitions (no zoom or bounce)");
    gtk_box_append(GTK_BOX(box), animation);
    GtkWidget *override = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(override), "GTK theme name — blank follows desktop");
    gtk_widget_set_tooltip_text(override, "Optional installed GTK theme name, for example Luxe-nord. Clear to follow the desktop again.");
    gtk_box_append(GTK_BOX(box), override);
    if (s->settings) {
        g_settings_bind(s->settings, "animations", animation, "active", G_SETTINGS_BIND_DEFAULT);
        g_settings_bind(s->settings, "theme-name", override, "text", G_SETTINGS_BIND_DEFAULT);
    } else { gtk_widget_set_sensitive(animation, FALSE); gtk_widget_set_sensitive(override, FALSE); }
    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *run = gtk_button_new_with_label("Start / show dock"), *end = gtk_button_new_with_label("Stop dock");
    g_signal_connect(run, "clicked", G_CALLBACK(start), s); g_signal_connect(end, "clicked", G_CALLBACK(stop), s);
    gtk_box_append(GTK_BOX(controls), run); gtk_box_append(GTK_BOX(controls), end); gtk_box_append(GTK_BOX(box), controls);
    GtkWidget *title = gtk_label_new("Pinned applications"); gtk_widget_set_margin_top(title, 14); gtk_box_append(GTK_BOX(box), title);
    s->list = GTK_LIST_BOX(gtk_list_box_new());
    GtkWidget *scroll = gtk_scrolled_window_new(); gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 100);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(s->list)); gtk_box_append(GTK_BOX(box), scroll);
    s->path = g_build_filename(g_get_user_config_dir(), "slab", "pins.txt", NULL);
    g_autofree char *pins_dir = g_path_get_dirname(s->path);
    g_autoptr(GFile) dir = g_file_new_for_path(pins_dir);
    GError *error = NULL;
    if (g_mkdir_with_parents(pins_dir, 0700) != 0) {
        g_set_error(&error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Cannot create pins directory: %s", g_strerror(errno));
        report(s, error->message); g_clear_error(&error);
    } else {
        s->pins_monitor = g_file_monitor_directory(dir, G_FILE_MONITOR_WATCH_MOVES, NULL, &error);
        if (s->pins_monitor) g_signal_connect(s->pins_monitor, "changed", G_CALLBACK(pins_changed), s);
        else if (error) { report(s, error->message); g_clear_error(&error); }
    }
    s->pins = slab_pins_load(s->path, &error);
    if (!s->pins) s->pins = g_ptr_array_new_with_free_func(g_free);
    populate(s);
    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    const char *labels[] = {"Move up", "Move down", "Unpin"}; int directions[] = {-1, 1, 0};
    for (int i = 0; i < 3; ++i) {
        GtkWidget *button = gtk_button_new_with_label(labels[i]);
        g_object_set_data(G_OBJECT(button), "direction", GINT_TO_POINTER(directions[i]));
        g_signal_connect(button, "clicked", G_CALLBACK(edit_pin), s); gtk_box_append(GTK_BOX(buttons), button);
    }
    gtk_box_append(GTK_BOX(box), buttons); gtk_window_present(s->window);
    if (error) { report(s, error->message); g_clear_error(&error); }
    if (!s->settings) report(s, "Install the slab package to enable persistent dock preferences.");
}
int main(int argc, char **argv)
{
    luxe_gtk_init(); Settings s = {0};
    GtkApplication *app = gtk_application_new("org.luxe.SlabSettings", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &s);
    int result = g_application_run(G_APPLICATION(app), argc, argv);
    if (s.pins_refresh) g_source_remove(s.pins_refresh);
    g_clear_object(&s.pins_monitor);
    if (s.window) { gtk_window_destroy(s.window); g_object_unref(s.window); }
    g_clear_object(&s.settings); g_clear_pointer(&s.pins, g_ptr_array_unref); g_free(s.path); g_object_unref(app); return result;
}
