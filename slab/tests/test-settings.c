/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#define main settings_main
#include "../src/settings.c"
#undef main
#include <glib/gstdio.h>
static void drain(void) {
    gint64 end = g_get_monotonic_time() + 200000;
    do { while (g_main_context_iteration(NULL, FALSE)); g_usleep(1000); } while (g_get_monotonic_time() < end);
}
static GtkWidget *find(GtkWidget *w, GType type) {
    if (G_TYPE_CHECK_INSTANCE_TYPE(w, type)) return w;
    for (GtkWidget *c = gtk_widget_get_first_child(w); c; c = gtk_widget_get_next_sibling(c)) {
        GtkWidget *result = find(c, type); if (result) return result;
    }
    return NULL;
}
static void called(GSimpleAction *a, GVariant *v, gpointer data) { (void)a; (void)v; ++*(int *)data; }
int main(void) {
    g_autofree char *config_home = g_dir_make_tmp("slab-settings-config-XXXXXX", NULL);
    g_assert_nonnull(config_home); g_setenv("XDG_CONFIG_HOME", config_home, TRUE);
    luxe_gtk_init(); int calls = 0;
    g_autoptr(GtkApplication) dock = gtk_application_new("org.luxe.Slab", G_APPLICATION_DEFAULT_FLAGS);
    g_autoptr(GSimpleAction) action = g_simple_action_new("quit", NULL);
    g_signal_connect(action, "activate", G_CALLBACK(called), &calls);
    g_action_map_add_action(G_ACTION_MAP(dock), G_ACTION(action));
    g_assert_true(g_application_register(G_APPLICATION(dock), NULL, NULL));
    g_autoptr(GtkApplication) app = gtk_application_new("org.luxe.SlabSettingsTest", G_APPLICATION_NON_UNIQUE);
    g_assert_true(g_application_register(G_APPLICATION(app), NULL, NULL));
    Settings s = {0}; activate(app, &s); drain();
    g_assert_nonnull(s.settings);
    GtkWidget *hide = find(GTK_WIDGET(s.window), GTK_TYPE_CHECK_BUTTON);
    GtkWidget *size = find(GTK_WIDGET(s.window), GTK_TYPE_SPIN_BUTTON);
    g_assert_nonnull(hide); g_assert_nonnull(size);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(hide), FALSE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(size), 16); drain();
    g_assert_false(g_settings_get_boolean(s.settings, "autohide"));
    g_assert_cmpint(g_settings_get_int(s.settings, "icon-size"), ==, 16);
    GSettings *theme = g_object_get_data(G_OBJECT(gtk_settings_get_default()), "slab-theme");
    g_assert_nonnull(theme); g_settings_set_string(theme, "gtk-theme", "Luxe-mocha"); drain();
    char *name = NULL; g_object_get(gtk_settings_get_default(), "gtk-theme-name", &name, NULL);
    g_assert_cmpstr(name, ==, "Luxe-mocha"); g_free(name);
    g_settings_set_string(s.settings, "theme-name", "Luxe-nord"); drain();
    g_object_get(gtk_settings_get_default(), "gtk-theme-name", &name, NULL);
    g_assert_cmpstr(name, ==, "Luxe-nord"); g_free(name);
    g_settings_set_string(s.settings, "theme-name", ""); drain();
    g_object_get(gtk_settings_get_default(), "gtk-theme-name", &name, NULL);
    g_assert_cmpstr(name, ==, "Luxe-mocha"); g_free(name);
    gtk_list_box_select_row(s.list, gtk_list_box_get_row_at_index(s.list, 1));
    g_assert_true(g_file_set_contents(s.path, "pcmanager.desktop\nluxeterm.desktop\n", -1, NULL));
    gint64 monitor_deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    while (s.pins->len != 2 && g_get_monotonic_time() < monitor_deadline) drain();
    g_assert_cmpuint(s.pins->len, ==, 2);
    GtkListBoxRow *selected = gtk_list_box_get_selected_row(s.list);
    g_assert_nonnull(selected);
    g_assert_cmpint(gtk_list_box_row_get_index(selected), ==, 0);
    send_action("quit"); drain(); g_assert_cmpint(calls, ==, 1);
    gtk_window_destroy(s.window); g_object_unref(s.window);
    g_clear_object(&s.pins_monitor);
    g_object_unref(s.settings); g_ptr_array_unref(s.pins);
    g_autofree char *pins_dir = g_path_get_dirname(s.path);
    g_autofree char *lock_path = g_strconcat(s.path, ".lock", NULL);
    g_unlink(s.path); g_unlink(lock_path); g_rmdir(pins_dir);
    g_free(s.path); g_assert_cmpint(g_rmdir(config_home), ==, 0); return 0;
}
