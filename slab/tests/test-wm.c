/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#define main slab_main
#include "../src/main.c"
#undef main
#include <glib/gstdio.h>
#include <time.h>

static void settle(int milliseconds)
{
    gint64 until = g_get_monotonic_time() + milliseconds * 1000;
    do { while (g_main_context_iteration(NULL, FALSE)); g_usleep(1000); }
    while (g_get_monotonic_time() < until);
}
static Window active_window(Dock *d)
{
    unsigned long n; Window *value = (Window *)property(d, d->root, "_NET_ACTIVE_WINDOW", XA_WINDOW, 32, &n);
    Window result = n ? value[0] : None; if (value) XFree(value); return result;
}
static void command_done(GObject *object, GAsyncResult *result, gpointer data)
{
    g_assert_true(g_subprocess_wait_check_finish(G_SUBPROCESS(object), result, NULL));
    *(gboolean *)data = TRUE;
}
static gboolean stop_loop(gpointer data) { g_main_loop_quit(data); return G_SOURCE_REMOVE; }
int main(void)
{
    g_autofree char *apps = g_build_filename(g_get_user_data_dir(), "applications", NULL);
    g_assert_cmpint(g_mkdir_with_parents(apps, 0700), ==, 0);
    g_autofree char *first = g_build_filename(apps, "slab-integration.desktop", NULL);
    g_autofree char *second = g_build_filename(apps, "slab-second.desktop", NULL);
    g_assert_true(g_file_set_contents(first, "[Desktop Entry]\nType=Application\nName=Integration\nExec=/bin/true\nStartupWMClass=SlabIntegration\nIcon=utilities-terminal\n", -1, NULL));
    g_assert_true(g_file_set_contents(second, "[Desktop Entry]\nType=Application\nName=Second\nExec=/bin/true\nIcon=system-file-manager\n", -1, NULL));
    g_autofree char *pins_dir = g_build_filename(g_get_user_config_dir(), "slab", NULL);
    g_assert_cmpint(g_mkdir_with_parents(pins_dir, 0700), ==, 0);
    g_autofree char *pins_path = g_build_filename(pins_dir, "pins.txt", NULL);
    g_assert_true(g_file_set_contents(pins_path, "", 0, NULL));
    g_autoptr(GSubprocess) wm = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
        NULL, "openbox", "--sm-disable", NULL);
    g_assert_nonnull(wm); settle(400);
    luxe_gtk_init(); Dock d = {0};
    d.app = gtk_application_new("org.luxe.Slab", G_APPLICATION_DEFAULT_FLAGS);
    g_assert_true(g_application_register(G_APPLICATION(d.app), NULL, NULL)); activate(d.app, &d);
    d.autohide = FALSE; queue_refresh(&d);
    Window windows[2];
    for (int i = 0; i < 2; ++i) {
        windows[i] = XCreateSimpleWindow(d.display, d.root, 40 + 200*i, 40, 180, 120, 0, 0, 0x333333);
        const char klass[] = "slab-fixture\0SlabIntegration\0";
        XChangeProperty(d.display, windows[i], atom(&d, "WM_CLASS"), XA_STRING, 8,
            PropModeReplace, (const unsigned char *)klass, sizeof klass - 1);
        XStoreName(d.display, windows[i], "slab integration window"); XMapWindow(d.display, windows[i]);
    }
    XFlush(d.display); settle(300);
    g_assert_cmpuint(d.groups->len, ==, 1);
    Group *g = g_ptr_array_index(d.groups, 0); g_assert_cmpuint(g->windows->len, ==, 2);
    request_focus(&d, windows[0]); settle(150); g_assert_cmpuint(active_window(&d), ==, windows[0]);
    g_signal_emit_by_name(g->button, "clicked"); settle(150); g_assert_cmpuint(active_window(&d), ==, windows[1]);
    g_signal_emit_by_name(g->button, "clicked"); settle(150); g_assert_cmpuint(active_window(&d), ==, windows[0]);
    /* Real GTK drag gestures in the private X server, not only callback calls. */
    g_assert_true(pin_at(&d, "slab-second.desktop", 0));
    g_assert_true(pin_at(&d, "slab-integration.desktop", 1)); settle(150);
    Group *from = g_ptr_array_index(d.groups, 0), *to = g_ptr_array_index(d.groups, 1);
    graphene_rect_t a, b;
    g_assert_true(gtk_widget_compute_bounds(from->button, GTK_WIDGET(d.window), &a));
    g_assert_true(gtk_widget_compute_bounds(to->button, GTK_WIDGET(d.window), &b));
    int scale = gtk_widget_get_scale_factor(GTK_WIDGET(d.window));
    g_autofree char *x1 = g_strdup_printf("%d", d.last_x + (int)((a.origin.x + a.size.width/2)*scale));
    g_autofree char *y1 = g_strdup_printf("%d", d.last_y + (int)((a.origin.y + a.size.height/2)*scale));
    g_autofree char *x2 = g_strdup_printf("%d", d.last_x + (int)((b.origin.x + b.size.width - 3)*scale));
    g_autofree char *mid = g_strdup_printf("%d", d.last_x + (int)((a.origin.x + a.size.width + 4)*scale));
    g_autoptr(GSubprocess) drag = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, NULL,
        "xdotool", "mousemove", x1, y1, "mousedown", "1", "sleep", "0.2",
        "mousemove", mid, y1, "sleep", "0.3", "mousemove", x2, y1, "sleep", "0.2", "mouseup", "1", NULL);
    g_assert_nonnull(drag); gboolean done = FALSE;
    g_subprocess_wait_check_async(drag, NULL, command_done, &done);
    for (int i = 0; !done && i < 30; ++i) settle(100);
    g_assert_true(done); settle(200);
    g_assert_cmpstr(g_ptr_array_index(d.pins, 0), ==, "slab-integration.desktop");
    g_assert_cmpstr(g_ptr_array_index(d.pins, 1), ==, "slab-second.desktop");
    request_focus(&d, windows[0]); settle(100);
    /* Actual delayed hide and pointer-edge reveal, with WM focus policy active. */
    XWarpPointer(d.display, None, d.root, 0, 0, 0, 0, 0, 0); XFlush(d.display); settle(100);
    d.autohide = TRUE; d.hovered = FALSE; schedule_hide(&d); settle(850);
    g_assert_true(d.hidden);
    g_autofree char *test_exe = g_file_read_link("/proc/self/exe", NULL);
    g_autofree char *test_dir = g_path_get_dirname(test_exe);
    g_autofree char *slab_exe = g_build_filename(test_dir, "slab", NULL);
    g_autoptr(GSubprocess) reveal = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, NULL, slab_exe, "--reveal", NULL);
    gboolean revealed = FALSE;
    g_subprocess_wait_check_async(reveal, NULL, command_done, &revealed);
    for (int i = 0; !revealed && i < 20; ++i) settle(25);
    g_assert_true(revealed); g_assert_false(d.hidden);
    g_assert_cmpuint(active_window(&d), ==, windows[0]);
    request_focus(&d, windows[0]); settle(100);
    XWarpPointer(d.display, None, d.root, 0, 0, 0, 0, 0, 0); XFlush(d.display);
    settle(1200); g_assert_true(d.hidden);
    /* Measure this test process while hidden; no polling loop in the fixture. */
    struct timespec before, after;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &before);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_timeout_add(1000, stop_loop, loop); g_main_loop_run(loop); g_main_loop_unref(loop);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &after);
    g_print("Hidden idle CPU over 1 second: %.3f ms\n",
        (after.tv_sec - before.tv_sec)*1000.0 + (after.tv_nsec - before.tv_nsec)/1000000.0);
    XWindowAttributes edge; g_assert_true(XGetWindowAttributes(d.display, d.edge, &edge));
    int saved_width = edge.width, saved_x = edge.x;
    /* Hidden GTK allocation can be zero; model/property refresh must not
     * shrink or recenter the activation strip. */
    queue_refresh(&d); settle(150); position(&d); XSync(d.display, False);
    g_assert_true(XGetWindowAttributes(d.display, d.edge, &edge));
    g_assert_cmpint(edge.width, ==, saved_width);
    g_assert_cmpint(edge.x, ==, saved_x);
    XWindowAttributes native; XGetWindowAttributes(d.display, d.xid, &native);
    g_assert_cmpint(edge.width, >=, native.width);
    XWarpPointer(d.display, None, d.root, 0, 0, 0, 0, edge.x + edge.width / 2, edge.y + 1);
    XFlush(d.display); settle(150);
    g_assert_false(d.hidden); g_assert_cmpuint(active_window(&d), ==, windows[0]);
    /* Parking at the very bottom must not hide/reveal in a loop. */
    XWarpPointer(d.display, None, d.root, 0, 0, 0, 0, edge.x + edge.width / 2, edge.y + edge.height - 1);
    XFlush(d.display); settle(1600); g_assert_false(d.hidden);
    XWarpPointer(d.display, None, d.root, 0, 0, 0, 0, d.last_x + 20, d.last_y + 20);
    XFlush(d.display); settle(100); g_assert_true(d.hovered);
    settle(850); g_assert_false(d.hidden);
    /* A screenshot stays in the out-of-tree build directory. */
    const char *capture = g_getenv("SLAB_TEST_CAPTURE");
    if (capture) {
        g_autoptr(GSubprocess) shot = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, NULL,
            "import", "-window", "root", capture, NULL);
        g_assert_nonnull(shot); g_assert_true(g_subprocess_wait_check(shot, NULL, NULL));
    }
    XWarpPointer(d.display, None, d.root, 0, 0, 0, 0, 0, 0); XFlush(d.display); settle(850);
    g_assert_true(d.hidden);
    for (int i = 0; i < 2; ++i) XDestroyWindow(d.display, windows[i]);
    for (int i = 0; i < 3; ++i) {
        XGetWindowAttributes(d.display, d.edge, &edge);
        XWarpPointer(d.display, None, d.root, 0, 0, 0, 0, edge.x + edge.width / 2, edge.y + edge.height - 1);
        XFlush(d.display); settle(150); g_assert_false(d.hidden);
        XWarpPointer(d.display, None, d.root, 0, 0, 0, 0, 0, 0);
        XFlush(d.display); settle(850); g_assert_true(d.hidden);
    }
    XFlush(d.display); settle(150); g_assert_cmpuint(d.groups->len, ==, 2);
    for (guint i = 0; i < d.groups->len; ++i)
        g_assert_cmpuint(((Group *)g_ptr_array_index(d.groups, i))->windows->len, ==, 0);
    cleanup(&d); g_object_unref(d.app);
    g_subprocess_force_exit(wm); g_subprocess_wait(wm, NULL, NULL); return 0;
}
