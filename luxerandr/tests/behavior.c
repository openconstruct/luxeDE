/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <gtk/gtk.h>
#include <glib/gstdio.h>
static char *fixture_dir, *fixture_log, *fixture_fail, *fixture_query, *fixture_slow, *old_path;
static const char fixture[];
#define main randr_main
#include "../src/luxerandr.c"
#undef main

static const char fixture[] =
    "Screen 0: minimum 8 x 8, current 3200 x 1080, maximum 32767 x 32767\n"
    "eDP-1 connected primary 1920x1080+0+0 (normal left inverted right x axis y axis)\n"
    "   1920x1080 60.00*+ 59.94\n"
    "   1280x720 60.00 +\n"
    "HDMI-1 connected 1280x720+1920+0 (normal left inverted right x axis y axis)\n"
    "   1280x720 60.00*+\n";

static void drain(void) { while (g_main_context_iteration(NULL, FALSE)); }
static gboolean heartbeat(gpointer data) { ++*(guint *)data; return G_SOURCE_REMOVE; }
static gboolean received_move(Display *display, Window window)
{
    XEvent event;
    Atom move = XInternAtom(display, "_NET_MOVERESIZE_WINDOW", False);
    XSync(display, False);
    while (XCheckTypedEvent(display, ClientMessage, &event))
        if (event.xclient.window == window && event.xclient.message_type == move) return TRUE;
    return FALSE;
}
static guint command_count(void)
{
    g_autofree char *contents = NULL;
    if (!g_file_get_contents(fixture_log, &contents, NULL, NULL)) return 0;
    guint count = 0; for (char *p = contents; *p; p++) if (*p == '\n') count++;
    return count;
}
static void wait_confirmation(void)
{
    gint64 deadline = g_get_monotonic_time() + 3 * G_TIME_SPAN_SECOND;
    while (!confirmation && (transaction || query_pending) && g_get_monotonic_time() < deadline)
        g_main_context_iteration(NULL, TRUE);
    g_assert_nonnull(confirmation);
}
static void wait_transaction(void)
{
    gint64 deadline = g_get_monotonic_time() + 3 * G_TIME_SPAN_SECOND;
    while ((transaction || query_pending) && g_get_monotonic_time() < deadline) g_main_context_iteration(NULL, TRUE);
    g_assert_null(transaction);
    g_assert_false(query_pending);
}

static GtkWidget *find_button(GtkWidget *widget, const char *label)
{
    if (GTK_IS_BUTTON(widget) && g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)), label) == 0)
        return widget;
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *found = find_button(child, label);
        if (found) return found;
    }
    return NULL;
}

static void behavior(void)
{
    Display *observer = XOpenDisplay(NULL);
    g_assert_nonnull(observer);
    Window root = DefaultRootWindow(observer);
    Window outside = XCreateSimpleWindow(observer, root, WidthOfScreen(DefaultScreenOfDisplay(observer)) + 100, 50, 100, 100, 0, 0, 0);
    XMapWindow(observer, outside);
    XSelectInput(observer, root, SubstructureNotifyMask);
    Atom clients = XInternAtom(observer, "_NET_CLIENT_LIST", False);
    XChangeProperty(observer, root, clients, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&outside, 1);
    XSync(observer, False);
    g_assert_true(parse_xrandr_output("DP-1 connected (normal left inverted right x axis y axis)\n"));
    g_assert_cmpuint(g_slist_length(monitors), ==, 1);
    g_assert_null(((Monitor *)monitors->data)->mode_lines);
    g_assert_true(parse_xrandr_output(fixture));
    g_assert_cmpuint(g_slist_length(monitors), ==, 2);
    g_assert_true(monitors->data == LVDS);
    g_assert_cmpint(LVDS->active_mode, ==, 0);
    g_assert_cmpint(LVDS->pref_mode, ==, 1);
    g_assert_cmpint(LVDS->pref_rate, ==, 0);
    Monitor *external = monitors->next->data;
    g_assert_cmpint(external->placement, ==, PLACEMENT_RIGHT);
    build_main_window(app);
    on_activate(app, NULL);
    g_assert_cmpuint(g_list_length(gtk_application_get_windows(GTK_APPLICATION(app))), ==, 1);
    g_autofree char *moved_output = g_strdup(fixture);
    char *moved_position = strstr(moved_output, "+1920+0");
    g_assert_nonnull(moved_position);
    memcpy(moved_position, "+2000+0", strlen("+2000+0"));
    GSList *saved_monitors = monitors;
    Monitor *saved_lvds = LVDS;
    monitors = NULL; LVDS = NULL;
    g_assert_true(parse_xrandr_output(moved_output));
    g_assert_false(same_topology(saved_monitors, monitors));
    free_monitors(); monitors = NULL; LVDS = NULL;
    g_assert_true(parse_xrandr_output(fixture));
    g_assert_true(same_topology(saved_monitors, monitors));
    free_monitors(); monitors = saved_monitors; LVDS = saved_lvds;
    GtkCheckButton *quick = GTK_CHECK_BUTTON(gtk_check_button_new());
    g_object_ref_sink(quick);
    g_signal_connect(quick, "toggled", G_CALLBACK(on_quick_option), GINT_TO_POINTER(2));
    gtk_check_button_set_active(quick, TRUE);
    g_assert_false(gtk_check_button_get_active(LVDS->enable));
    g_assert_true(gtk_check_button_get_active(external->enable));
    g_assert_false(gtk_widget_get_sensitive(GTK_WIDGET(external->pos_combo)));
    g_object_unref(quick);

    /* A slow snapshot query must not stall the GTK main loop. */
    g_assert_true(g_file_set_contents(fixture_slow, "slow", -1, NULL));
    gint64 query_started = g_get_monotonic_time();
    set_xrandr_info();
    g_assert_cmpint(g_get_monotonic_time() - query_started, <, 250000);
    g_assert_true(query_pending);
    guint beats = 0;
    g_timeout_add(50, heartbeat, &beats);
    while (!beats && query_pending) g_main_context_iteration(NULL, TRUE);
    g_assert_cmpuint(beats, ==, 1);
    g_assert_true(query_pending);
    wait_confirmation();
    g_assert_false(received_move(observer, outside));
    reject_confirmation();
    wait_transaction();
    g_assert_false(received_move(observer, outside));
    g_assert_cmpint(g_unlink(fixture_slow), ==, 0);
    g_assert_cmpint(g_unlink(fixture_log), ==, 0);

    set_xrandr_info();
    wait_confirmation();
    g_assert_cmpuint(command_count(), ==, 1);
    g_assert_nonnull(confirmation);
    guint timer = confirmation_source;
    g_assert_nonnull(g_main_context_find_source_by_id(NULL, timer));
    set_xrandr_info();
    on_save_clicked(NULL, dlg);
    g_assert_cmpuint(command_count(), ==, 1);
    g_source_remove(timer);
    confirmation_source = g_timeout_add(1, apply_confirmation_timeout, NULL);
    while (confirmation) g_main_context_iteration(NULL, TRUE);
    wait_transaction();
    g_assert_cmpuint(command_count(), ==, 2);
    g_assert_cmpuint(confirmation_source, ==, 0);
    g_assert_true(gtk_check_button_get_active(LVDS->enable));
    g_autofree char *log = NULL;
    g_assert_true(g_file_get_contents(fixture_log, &log, NULL, NULL));
    g_assert_nonnull(strstr(log, "--output eDP-1 --mode 1920x1080"));
    g_assert_nonnull(strstr(log, "--pos 1920x0"));
    drain();

    set_xrandr_info();
    wait_confirmation();
    GtkWidget *keep = find_button(GTK_WIDGET(confirmation), "Keep");
    g_assert_nonnull(keep);
    g_signal_emit_by_name(keep, "clicked");
    wait_transaction(); drain();
    g_assert_true(received_move(observer, outside));
    g_assert_null(confirmation);
    g_assert_cmpuint(confirmation_source, ==, 0);
    g_assert_cmpuint(command_count(), ==, 3);

    gtk_check_button_set_active(LVDS->enable, FALSE);
    gtk_check_button_set_active(external->enable, FALSE);
    GError *error = NULL;
    g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*Refusing unsafe*");
    g_assert_false(save_configuration(&error));
    g_test_assert_expected_messages();
    g_assert_nonnull(error);
    g_clear_error(&error);
    gtk_check_button_set_active(LVDS->enable, TRUE);
    g_autofree char *autostart_dir=g_build_filename(g_get_user_config_dir(),"autostart",NULL);
    g_assert_cmpint(g_mkdir_with_parents(autostart_dir,0700),==,0);
    g_autofree char *prior_path=g_build_filename(autostart_dir,"luxerandr-autostart.desktop",NULL);
    g_assert_true(g_file_set_contents(prior_path,"existing settings",-1,NULL));
    g_assert_false(save_configuration(&error));
    g_assert_error(error,G_IO_ERROR,G_IO_ERROR_BUSY);g_clear_error(&error);
    g_autofree char *prior=NULL;g_assert_true(g_file_get_contents(prior_path,&prior,NULL,NULL));
    g_assert_cmpstr(prior,==,"existing settings");
    set_xrandr_info();
    wait_confirmation();
    keep=find_button(GTK_WIDGET(confirmation),"Keep");g_assert_nonnull(keep);
    g_signal_emit_by_name(keep,"clicked");wait_transaction();drain();g_assert_null(confirmation);
    g_assert_true(save_configuration(&error));
    g_assert_no_error(error);
    gchar *path = g_build_filename(g_get_user_config_dir(), "autostart", "luxerandr-autostart.desktop", NULL);
    GKeyFile *file = g_key_file_new();
    g_assert_true(g_key_file_load_from_file(file, path, 0, NULL));
    gchar *exec = g_key_file_get_string(file, "Desktop Entry", "Exec", NULL);
    g_assert_nonnull(strstr(exec, "--output eDP-1 --mode"));
    g_free(exec); g_free(path); g_key_file_unref(file);
    guint before_failure = command_count();
    g_assert_true(g_file_set_contents(fixture_fail, "fail", -1, NULL));
    set_xrandr_info();
    wait_transaction();
    g_assert_cmpuint(command_count(), ==, before_failure + 2);
    g_assert_null(confirmation);
    g_assert_null(rollback_command);
    GtkWindow *failure = luxe_window_get_visible_dialog(GTK_WINDOW(dlg));
    g_assert_nonnull(failure);
    luxe_dialog_close(failure);
    g_assert_true(g_file_set_contents(fixture_slow, "slow", -1, NULL));
    set_xrandr_info();
    g_assert_true(query_pending);
    GtkWindow *closing = GTK_WINDOW(dlg);
    on_window_close(closing, NULL);
    g_assert_null(confirmation);
    gtk_window_destroy(closing);
    wait_transaction(); drain();
    g_assert_cmpint(g_unlink(fixture_slow), ==, 0);
    free_monitors();
    XDeleteProperty(observer, root, clients);
    XDestroyWindow(observer, outside);
    XCloseDisplay(observer);
}

static void malformed_x(void)
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    Display *display = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    G_GNUC_END_IGNORE_DEPRECATIONS
    Window root = DefaultRootWindow(display);
    Atom list = XInternAtom(display, "_NET_CLIENT_LIST", False);
    const unsigned char bytes[] = { 1, 2, 3 };
    XChangeProperty(display, root, list, XA_WINDOW, 8, PropModeReplace, bytes, 3);
    clamp_windows_to_screen();
    Window stale = XCreateSimpleWindow(display, root, 0, 0, 10, 10, 0, 0, 0);
    XDestroyWindow(display, stale);
    XChangeProperty(display, root, list, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&stale, 1);
    clamp_windows_to_screen();
    XDeleteProperty(display, root, list);
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    g_test_init(&argc, &argv, NULL);
    fixture_dir = g_dir_make_tmp("luxerandr-ui-XXXXXX", NULL);
    fixture_log = g_build_filename(fixture_dir, "log", NULL);
    fixture_fail = g_build_filename(fixture_dir, "fail", NULL);
    fixture_query = g_build_filename(fixture_dir, "query", NULL);
    fixture_slow = g_build_filename(fixture_dir, "slow", NULL);
    g_assert_true(g_file_set_contents(fixture_query, fixture, -1, NULL));
    g_autofree char *script = g_build_filename(fixture_dir, "xrandr", NULL);
    g_assert_true(g_file_set_contents(script,
        "#!/bin/sh\nif [ \"$1\" = --query ]; then if [ -f \"$RANDR_SLOW\" ]; then /bin/sleep 1; fi; /bin/cat \"$RANDR_QUERY\"; exit 0; fi\n"
        "printf 'xrandr %s\\n' \"$*\" >> \"$RANDR_LOG\"\n"
        "if [ -f \"$RANDR_FAIL\" ]; then /bin/rm \"$RANDR_FAIL\"; exit 1; fi\n", -1, NULL));
    g_assert_cmpint(g_chmod(script, 0700), ==, 0);
    old_path = g_strdup(g_getenv("PATH"));
    g_setenv("PATH", fixture_dir, TRUE);
    g_setenv("RANDR_LOG", fixture_log, TRUE);
    g_setenv("RANDR_FAIL", fixture_fail, TRUE);
    g_setenv("RANDR_QUERY", fixture_query, TRUE);
    g_setenv("RANDR_SLOW", fixture_slow, TRUE);
    luxe_ui_init();
    app = luxe_application_new("org.luxe.Randr.Audit", G_APPLICATION_NON_UNIQUE);
    g_assert_true(g_application_register(G_APPLICATION(app), NULL, NULL));
    g_test_add_func("/randr/behavior", behavior);
    g_test_add_func("/randr/malformed-x", malformed_x);
    int result = g_test_run();
    g_object_unref(app);
    if (old_path) g_setenv("PATH", old_path, TRUE); else g_unsetenv("PATH");
    g_unlink(fixture_log); g_unlink(fixture_fail); g_unlink(fixture_query); g_unlink(fixture_slow); g_unlink(script); g_rmdir(fixture_dir);
    g_free(old_path); g_free(fixture_log); g_free(fixture_fail); g_free(fixture_query); g_free(fixture_slow); g_free(fixture_dir);
    return result;
}
