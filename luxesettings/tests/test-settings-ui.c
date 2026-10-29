/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <glib/gstdio.h>
#include <locale.h>
#include <libintl.h>
#define main settings_program_main
#include "../src/main.c"
#undef main

static void collect_rows(GtkWidget *widget, GPtrArray *rows)
{
    if (GTK_IS_LIST_BOX_ROW(widget) && g_object_get_data(G_OBJECT(widget), "luxe-action"))
        g_ptr_array_add(rows, widget);
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
         child = gtk_widget_get_next_sibling(child))
        collect_rows(child, rows);
}

static GtkWidget *find_refresh_button(GtkWidget *widget)
{
    if (g_object_get_data(G_OBJECT(widget), "luxe-refresh"))
        return widget;
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
         child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *found = find_refresh_button(child);
        if (found) return found;
    }
    return NULL;
}

static void collect_labels(GtkWidget *widget, GPtrArray *labels)
{
    if (GTK_IS_LABEL(widget))
        g_ptr_array_add(labels, widget);
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
         child = gtk_widget_get_next_sibling(child))
        collect_labels(child, labels);
}

static void settle(void)
{
    while (g_main_context_iteration(NULL, FALSE));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_setenv("LANGUAGE", "de", TRUE);
    g_setenv("LC_ALL", "de_DE.UTF-8", TRUE);
    g_assert_nonnull(setlocale(LC_ALL, "de_DE.UTF-8"));
    bindtextdomain(GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
    g_assert_cmpstr(dgettext(GETTEXT_PACKAGE, "Wallpaper"), ==, "Hintergrund");
    GError *error = NULL;
    g_autofree char *directory = g_dir_make_tmp("luxe-settings-launch-XXXXXX", &error);
    g_assert_no_error(error);
    g_autofree char *marker = g_build_filename(directory, "launch.txt", NULL);
    g_autofree char *original_path = g_strdup(g_getenv("PATH"));
    const char fixture[] = "#!/bin/sh\nprintf '%s\\n' \"$0\" \"$@\" > \"$LUXE_SETTINGS_TEST_MARKER\"\n";
    for (guint i = 0; i < luxe_settings_actions_count; i++) {
        g_autofree char *command = g_build_filename(directory, luxe_settings_actions[i].argv[0], NULL);
        g_assert_true(g_file_set_contents(command, fixture, -1, &error));
        g_assert_no_error(error);
        g_assert_cmpint(g_chmod(command, 0700), ==, 0);
    }
    g_setenv("PATH", directory, TRUE);
    g_setenv("LUXE_SETTINGS_TEST_MARKER", marker, TRUE);
    g_unsetenv("GSK_RENDERER");
    luxe_gtk_init();
    g_assert_null(g_getenv("GSK_RENDERER"));
    g_autoptr(GtkApplication) application = gtk_application_new(
        "org.luxe.Settings.Test", G_APPLICATION_NON_UNIQUE);
    g_signal_connect_after(application, "startup", G_CALLBACK(startup), NULL);
    g_assert_true(g_application_register(G_APPLICATION(application), NULL, &error));
    g_assert_no_error(error);
    activate(G_APPLICATION(application));
    activate(G_APPLICATION(application));
    settle();
    GList *windows = gtk_application_get_windows(application);
    g_assert_cmpuint(g_list_length(windows), ==, 1);
    GtkWindow *window = windows->data;
    g_assert_true(GSK_IS_CAIRO_RENDERER(gtk_native_get_renderer(GTK_NATIVE(window))));
    g_assert_true(GTK_IS_APPLICATION_WINDOW(window));
    g_assert_true(g_action_group_has_action(G_ACTION_GROUP(application), "about"));
    g_autoptr(GPtrArray) rows = g_ptr_array_new();
    collect_rows(GTK_WIDGET(window), rows);
    g_assert_cmpuint(rows->len, ==, luxe_settings_actions_count);
    const LuxeSettingsAction *wallpaper = &luxe_settings_actions[2];
    GtkWidget *wallpaper_row = NULL;
    for (guint i = 0; i < rows->len; i++)
        if (g_object_get_data(G_OBJECT(g_ptr_array_index(rows, i)), "luxe-action") == wallpaper)
            wallpaper_row = g_ptr_array_index(rows, i);
    g_assert_nonnull(wallpaper_row);
    g_autoptr(GPtrArray) labels = g_ptr_array_new();
    collect_labels(wallpaper_row, labels);
    gboolean translated_title = FALSE, translated_subtitle = FALSE;
    for (guint i = 0; i < labels->len; i++) {
        const char *text = gtk_label_get_text(GTK_LABEL(g_ptr_array_index(labels, i)));
        translated_title |= g_str_equal(text, "Hintergrund");
        translated_subtitle |= g_str_equal(text,
            "Wähle einen Wallpaper-Ordner; täglicher Wechsel oder beim nächsten Login");
    }
    g_assert_true(translated_title && translated_subtitle);
    g_ptr_array_set_size(labels, 0);
    collect_labels(GTK_WIDGET(window), labels);
    gboolean translated_section = FALSE;
    for (guint i = 0; i < labels->len; i++)
        translated_section |= g_str_equal(
            gtk_label_get_text(GTK_LABEL(g_ptr_array_index(labels, i))), "Arbeitsfläche");
    g_assert_true(translated_section);
    g_assert_cmpstr(wallpaper->argv[0], ==, "frame");
    g_assert_cmpstr(wallpaper->argv[1], ==, "--wallpaper-settings");

    /* Resolve rows against private fixtures, then change PATH. Every launch
     * must still use the executable resolved when the row was built. */
    for (guint i = 0; i < rows->len; i++) {
        const LuxeSettingsAction *action = &luxe_settings_actions[i];
        g_autofree char *command = g_build_filename(directory, action->argv[0], NULL);
        g_setenv("PATH", "/nonexistent", TRUE);
        g_autoptr(GString) expected = g_string_new(command);
        g_string_append_c(expected, '\n');
        for (guint j = 1; action->argv[j]; j++)
            g_string_append_printf(expected, "%s\n", action->argv[j]);
        g_assert_true(gtk_widget_activate(g_ptr_array_index(rows, i)));
        gint64 deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
        g_autofree char *contents = NULL;
        /* Creation precedes printf's write. Wait for completed delivery, not
         * merely an empty path; wrong argv still fails at the deadline. */
        do {
            settle();
            g_clear_pointer(&contents, g_free);
            g_file_get_contents(marker, &contents, NULL, NULL);
            if (g_strcmp0(contents, expected->str) == 0) break;
            g_usleep(1000);
        } while (g_get_monotonic_time() < deadline);
        g_assert_cmpstr(contents, ==, expected->str);
        g_unlink(marker);
        g_assert_cmpint(g_unlink(command), ==, 0);
    }
    /* Missing launcher must display an error rather than disappear/crash. */
    g_assert_true(gtk_widget_activate(g_ptr_array_index(rows, 0)));
    settle();
    GListModel *toplevels = gtk_window_get_toplevels();
    gboolean found_error = FALSE;
    for (guint i = 0; i < g_list_model_get_n_items(toplevels); i++) {
        g_autoptr(GtkWindow) candidate = g_list_model_get_item(toplevels, i);
        if (candidate != window && gtk_widget_get_visible(GTK_WIDGET(candidate))) {
            g_assert_true(gtk_window_get_modal(candidate));
            found_error = TRUE;
            gtk_window_destroy(candidate);
            break;
        }
    }
    g_assert_true(found_error);

    g_autofree char *panel_program = g_build_filename(directory, "luxepanelctl", NULL);
    const char failing_panel[] = "#!/bin/sh\nexit 7\n";
    g_assert_true(g_file_set_contents(panel_program, failing_panel, -1, &error));
    g_assert_no_error(error);
    g_assert_cmpint(g_chmod(panel_program, 0700), ==, 0);
    GtkWidget *panel_row = NULL;
    for (guint i = 0; i < rows->len; i++)
        if (g_object_get_data(G_OBJECT(g_ptr_array_index(rows, i)), "luxe-action") == &luxe_settings_actions[3])
            panel_row = g_ptr_array_index(rows, i);
    g_assert_nonnull(panel_row);
    g_assert_true(gtk_widget_activate(panel_row));
    gint64 panel_deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
    gboolean panel_error = FALSE;
    while (!panel_error && g_get_monotonic_time() < panel_deadline) {
        settle();
        GListModel *windows_now = gtk_window_get_toplevels();
        for (guint i = 0; i < g_list_model_get_n_items(windows_now); i++) {
            g_autoptr(GtkWindow) candidate = g_list_model_get_item(windows_now, i);
            if (candidate != window && gtk_widget_get_visible(GTK_WIDGET(candidate))) {
                panel_error = TRUE;
                gtk_window_destroy(candidate);
                break;
            }
        }
        if (!panel_error) g_usleep(1000);
    }
    g_assert_true(panel_error);
    g_unlink(panel_program);

    GtkWidget *refresh = find_refresh_button(GTK_WIDGET(window));
    g_assert_nonnull(refresh);
    g_setenv("PATH", directory, TRUE);
    g_assert_null(g_find_program_in_path("frame"));
    g_signal_emit_by_name(refresh, "clicked");
    settle();
    g_ptr_array_set_size(rows, 0);
    collect_rows(GTK_WIDGET(window), rows);
    wallpaper_row = NULL;
    for (guint i = 0; i < rows->len; i++)
        if (g_object_get_data(G_OBJECT(g_ptr_array_index(rows, i)), "luxe-action") == wallpaper)
            wallpaper_row = g_ptr_array_index(rows, i);
    g_assert_nonnull(wallpaper_row);
    g_assert_false(gtk_widget_get_sensitive(wallpaper_row));

    g_autofree char *restored_program = g_build_filename(directory, "luxeappearance", NULL);
    g_assert_true(g_file_set_contents(restored_program, fixture, -1, &error));
    g_assert_no_error(error);
    g_assert_cmpint(g_chmod(restored_program, 0700), ==, 0);
    g_signal_emit_by_name(refresh, "clicked");
    settle();
    g_ptr_array_set_size(rows, 0);
    collect_rows(GTK_WIDGET(window), rows);
    GtkWidget *appearance_row = NULL;
    for (guint i = 0; i < rows->len; i++)
        if (g_object_get_data(G_OBJECT(g_ptr_array_index(rows, i)), "luxe-action") == &luxe_settings_actions[0])
            appearance_row = g_ptr_array_index(rows, i);
    g_assert_nonnull(appearance_row);
    g_assert_true(gtk_widget_get_sensitive(appearance_row));
    g_assert_cmpstr(g_object_get_data(G_OBJECT(appearance_row), "luxe-program"), ==, restored_program);
    g_unlink(restored_program);
    if (original_path) g_setenv("PATH", original_path, TRUE);
    else g_unsetenv("PATH");
    g_unsetenv("LUXE_SETTINGS_TEST_MARKER");
    g_rmdir(directory);

    GtkWidget *about = create_about_dialog(window);
    g_assert_cmpstr(gtk_about_dialog_get_version(GTK_ABOUT_DIALOG(about)), ==, VERSION);
    g_assert_cmpint(gtk_about_dialog_get_license_type(GTK_ABOUT_DIALOG(about)), ==, GTK_LICENSE_GPL_2_0);
    gtk_window_present(GTK_WINDOW(about));
    settle();
    gtk_window_destroy(GTK_WINDOW(about));
    GSettings *settings = g_object_get_data(G_OBJECT(application), "luxe-interface-settings");
    if (settings) {
        for (guint i = 0; i < 12; i++) {
            gboolean dark = i % 2 == 0, actual;
            g_assert_true(g_settings_set_string(settings, "color-scheme", dark ? "prefer-dark" : "prefer-light"));
            settle();
            g_object_get(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", &actual, NULL);
            g_assert_cmpint(actual, ==, dark);
        }
    }
    gtk_window_destroy(window);
    return 0;
}
