/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#define main input_program_main
#include "../src/luxeinput.c"
#undef main

static int reject_pointer_mapping(Display *display, const unsigned char *map, int count)
{
    (void)display; (void)map; (void)count;
    return MappingBusy;
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_setenv("DESKTOP_SESSION", "LXDE", TRUE);
    luxe_ui_init();
    g_autoptr(GtkApplication) app = luxe_application_new("org.luxe.Input.Test", G_APPLICATION_NON_UNIQUE);
    g_assert_true(g_application_register(G_APPLICATION(app), NULL, NULL));
    Display *display = xdisplay();
    XChangePointerControl(display, True, True, 7, 3, 9);
    XkbSetAutoRepeatRate(display, XkbUseCoreKbd, 432, 37);
    XChangeKeyboardControl(display, KBBellPercent | KBBellPitch | KBBellDuration,
        &(XKeyboardControl){ .bell_percent = 57, .bell_pitch = 123, .bell_duration = 234 });
    XSync(display, False);
    g_assert_cmpstr(settings_relative_path(), ==, "luxesession/LXDE/desktop.conf");
    g_setenv("DESKTOP_SESSION", "luxe", TRUE);
    g_assert_cmpstr(settings_relative_path(), ==, "luxesession/LXDE/desktop.conf");
    g_setenv("DESKTOP_SESSION", "../../tmp", TRUE);
    g_assert_cmpstr(settings_relative_path(), ==, "luxesession/LXDE/desktop.conf");
    g_setenv("DESKTOP_SESSION", "Custom-Profile_1", TRUE);
    g_assert_cmpstr(settings_relative_path(), ==, "luxesession/Custom-Profile_1/desktop.conf");
    g_setenv("DESKTOP_SESSION", "LXDE", TRUE);
    on_activate(GTK_APPLICATION(app), NULL);
    GtkWidget *first = dlg;
    g_assert_cmpfloat_with_epsilon(gtk_range_get_value(mouse_accel), (double)accel / 10.0, 0.01);
    g_assert_cmpfloat(gtk_range_get_value(mouse_threshold), ==, threshold);
    g_assert_cmpfloat(gtk_range_get_value(kb_delay), ==, delay);
    g_assert_cmpfloat(gtk_range_get_value(kb_interval), ==, interval);
    on_activate(GTK_APPLICATION(app), NULL);
    g_assert_true(dlg == first);
    g_assert_cmpuint(g_list_length(gtk_application_get_windows(GTK_APPLICATION(app))), ==, 1);
    gtk_range_set_value(mouse_accel, 4.0);
    gtk_range_set_value(kb_delay, 800);
    on_cancel_clicked(NULL, NULL);
    int num, den, thresh;
    unsigned int repeat_delay, repeat_interval;
    XGetPointerControl(display, &num, &den, &thresh);
    XkbGetAutoRepeatRate(display, XkbUseCoreKbd, &repeat_delay, &repeat_interval);
    g_assert_cmpint(num, ==, 7); g_assert_cmpint(den, ==, 3); g_assert_cmpint(thresh, ==, 9);
    g_assert_cmpuint(repeat_delay, ==, 432); g_assert_cmpuint(repeat_interval, ==, 37);
    XKeyboardState bell;
    XGetKeyboardControl(display, &bell);
    g_assert_cmpint(bell.bell_percent, ==, 57);
    g_assert_cmpint(bell.bell_pitch, ==, 123);
    g_assert_cmpint(bell.bell_duration, ==, 234);

    test_force_no_x11 = TRUE;
    on_activate(GTK_APPLICATION(app), NULL);
    g_assert_false(gtk_widget_get_sensitive(GTK_WIDGET(mouse_accel)));
    g_assert_false(gtk_widget_get_sensitive(GTK_WIDGET(kb_delay)));
    test_force_no_x11 = FALSE;
    on_cancel_clicked(NULL, NULL);
    test_force_no_xkb = TRUE;
    on_activate(GTK_APPLICATION(app), NULL);
    g_assert_true(gtk_widget_get_sensitive(GTK_WIDGET(mouse_accel)));
    g_assert_false(gtk_widget_get_sensitive(GTK_WIDGET(kb_delay)));
    test_force_no_xkb = FALSE;
    on_cancel_clicked(NULL, NULL);

    g_autofree char *path = NULL;
    g_autoptr(GKeyFile) kf = read_settings_file(&path, NULL);
    g_autofree char *directory = g_path_get_dirname(path);
    g_mkdir_with_parents(directory, 0700);
    g_key_file_set_string(kf, "Unrelated", "keep", "user value");
    g_assert_true(g_key_file_save_to_file(kf, path, NULL));
    accel = 45;
    threshold = 0;
    GError *error = NULL;
    g_assert_true(save_settings_to_files(&error));
    g_assert_no_error(error);
    g_assert_true(g_key_file_load_from_file(kf, path, 0, NULL));
    g_autofree char *kept = g_key_file_get_string(kf, "Unrelated", "keep", NULL);
    g_assert_cmpstr(kept, ==, "user value");
    g_assert_cmpint(g_key_file_get_integer(kf, "Mouse", "AccFactor", NULL), ==, 45);
    g_assert_cmpint(g_key_file_get_integer(kf, "Mouse", "AccThreshold", NULL), ==, 0);
    on_activate(GTK_APPLICATION(app), NULL);
    first = dlg;
    gboolean initial_left_handed = gtk_check_button_get_active(mouse_left_handed);
    test_pointer_mapping_setter = reject_pointer_mapping;
    gtk_check_button_set_active(mouse_left_handed, !initial_left_handed);
    test_pointer_mapping_setter = XSetPointerMapping;
    g_assert_cmpint(gtk_check_button_get_active(mouse_left_handed), ==, initial_left_handed);
    g_assert_true(gtk_widget_get_visible(GTK_WIDGET(status_label)));
    g_assert_true(g_file_set_contents(path, "[broken", -1, NULL));
    g_assert_false(save_settings_to_files(&error));
    g_clear_error(&error);
    g_autofree char *contents = NULL;
    g_assert_true(g_file_get_contents(path, &contents, NULL, NULL));
    g_assert_cmpstr(contents, ==, "[broken");
    finish_and_quit(TRUE);
    g_assert_true(dlg == first);
    g_assert_false(handled);
    finish_and_quit(FALSE);
    return 0;
}
