/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#include "../screenshot-app.c"
#include <slop.hpp>
#include <math.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cairo-xlib.h>
static gboolean inject_slop;
static gboolean inject_oversized_screen;
static struct slop_selection fake_selection;
static struct slop_selection test_slop_select(struct slop_options *options)
{
    return inject_slop ? fake_selection : slop_select(options);
}
static Status test_XGetGeometry(Display *dpy, Drawable drawable, Window *root,
                                int *x, int *y, unsigned int *width, unsigned int *height,
                                unsigned int *border, unsigned int *depth)
{
    Status result = XGetGeometry(dpy, drawable, root, x, y, width, height, border, depth);
    if(result && inject_oversized_screen) { *width = 8192; *height = 8192; }
    return result;
}
static cairo_surface_t *test_cairo_xlib_surface_create(Display *dpy, Drawable drawable,
                                                        Visual *visual, int width, int height)
{
    g_assert_false(inject_oversized_screen); /* No backend allocation on rejection. */
    return cairo_xlib_surface_create(dpy, drawable, visual, width, height);
}
#define slop_select test_slop_select
#define XGetGeometry test_XGetGeometry
#define cairo_xlib_surface_create test_cairo_xlib_surface_create
#include "../screenshot-logic.c"
#undef slop_select
#undef XGetGeometry
#undef cairo_xlib_surface_create
static GdkPixbuf *capture_result;
static gboolean capture_failure;
static gboolean capture_oversize;
static GdkPixbuf *test_capture(gint area, gboolean cursor, gboolean decorations, GError **error)
{
    (void)area; (void)cursor; (void)decorations;
    if(capture_oversize) g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Capture is too large");
    if(capture_failure) g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Fixture capture failed");
    return capture_result ? g_object_ref(capture_result) : NULL;
}
#define screenshot test_capture
#include "../screenshot-menu.c"
#undef screenshot
static gboolean simulated_store;
static GTask *pending_store;
static void test_store_async(GdkClipboard *clipboard, int priority, GCancellable *cancel,
                             GAsyncReadyCallback callback, gpointer data)
{
    if (!simulated_store) {
        gdk_clipboard_store_async(clipboard, priority, cancel, callback, data);
        return;
    }
    /* Deliberately ignore cancellation, like a delayed/broken backend. */
    g_assert_null(pending_store);
    pending_store = g_task_new(clipboard, NULL, callback, data);
    g_task_set_source_tag(pending_store, test_store_async);
}
static gboolean test_store_finish(GdkClipboard *clipboard, GAsyncResult *result, GError **error)
{
    if (g_async_result_is_tagged(result, test_store_async))
        return g_task_propagate_boolean(G_TASK(result), error);
    return gdk_clipboard_store_finish(clipboard, result, error);
}
#define SHOTTY_CLIPBOARD_TIMEOUT_MS 150
#define gdk_clipboard_store_async test_store_async
#define gdk_clipboard_store_finish test_store_finish
static gboolean fail_png_write;
static gboolean fail_directory_fsync;
static const char *change_destination;
static GMutex write_lock;
static GCond write_cond;
static gboolean block_write, write_entered, release_write;
static ssize_t test_png_write(int fd, const void *buffer, size_t count)
{
	 g_mutex_lock(&write_lock);
    if (block_write) {
        write_entered = TRUE; g_cond_broadcast(&write_cond);
        while (!release_write) g_cond_wait(&write_cond, &write_lock);
    }
    g_mutex_unlock(&write_lock);
    if (fail_png_write) { errno = ENOSPC; return -1; }
    if (change_destination) {
        const char *path = change_destination; change_destination = NULL;
        g_assert_true(g_file_set_contents(path, "other writer", -1, NULL));
    }
    return write(fd, buffer, count);
}
static int test_fsync(int fd)
{
	struct stat st;
	if (fail_directory_fsync && fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
		errno = EIO;
		return -1;
	}
	return fsync(fd);
}
#define write test_png_write
#define fsync test_fsync
#include "../actions-menu.c"
#undef write
#undef fsync
#undef gdk_clipboard_store_async
#undef gdk_clipboard_store_finish
#include <sys/wait.h>

static ScreenshotApp *app;

static void drain(void)
{
    while (g_main_context_iteration(NULL, FALSE));
}

static void windows(void)
{
    g_application_activate(G_APPLICATION(app));
    g_application_activate(G_APPLICATION(app));
    GList *windows = gtk_application_get_windows(GTK_APPLICATION(app));
    g_assert_cmpuint(g_list_length(windows), ==, 1);
    ScreenshotMenu *window = windows->data;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(window->priv->area_list), GTK_INVALID_LIST_POSITION);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(window->priv->area_list), 0);
    screenshot_button_clicked(NULL, window);
    guint source = window->priv->screenshot_source;
    screenshot_button_clicked(NULL, window);
    g_assert_cmpuint(window->priv->screenshot_source, ==, source);
    g_application_activate(G_APPLICATION(app));
    g_assert_cmpuint(g_list_length(gtk_application_get_windows(GTK_APPLICATION(app))), ==, 1);
    /* Capture cancellation/failure must not leak a timer reference. */
    g_source_remove(source);
    start_screenshot(window);
    g_assert_true(gtk_widget_get_visible(GTK_WIDGET(window)));
    g_assert_null(luxe_window_get_visible_dialog(GTK_WINDOW(window))); /* Cancellation is quiet. */
    capture_failure = TRUE;
    start_screenshot(window);
    GtkWindow *error_dialog = luxe_window_get_visible_dialog(GTK_WINDOW(window));
    g_assert_nonnull(error_dialog);
    luxe_dialog_close(error_dialog);
    capture_failure = FALSE;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(window->priv->area_list), AREA_SCREEN);
    capture_oversize = TRUE;
    start_screenshot(window);
    g_assert_cmpint(gtk_drop_down_get_selected(GTK_DROP_DOWN(window->priv->area_list)), ==, AREA_REGION);
    error_dialog = luxe_window_get_visible_dialog(GTK_WINDOW(window));
    g_assert_nonnull(error_dialog);
    luxe_dialog_close(error_dialog);
    capture_oversize = FALSE;
    g_object_add_weak_pointer(G_OBJECT(window), (gpointer *)&window);
    gtk_window_destroy(GTK_WINDOW(window));
    drain();
    g_assert_null(window);
    window = screenshot_menu_new(app);
    screenshot_button_clicked(NULL, window);
    source = window->priv->screenshot_source;
    gtk_window_destroy(GTK_WINDOW(window));
    g_assert_null(g_main_context_find_source_by_id(NULL, source));

    capture_result = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 64, 48);
    gdk_pixbuf_fill(capture_result, 0xff0000ff);
    window = screenshot_menu_new(app);
    start_screenshot(window);
    ActionsMenu *preview = gtk_application_get_windows(GTK_APPLICATION(app))->data;
    g_assert_true(ACTIONS_IS_MENU(preview));
    g_object_add_weak_pointer(G_OBJECT(capture_result), (gpointer *)&capture_result);
    g_object_unref(capture_result);
    gtk_window_destroy(GTK_WINDOW(preview));
    drain();
    g_assert_null(capture_result);
}

static void saving(void)
{
    GdkPixbuf *pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 64, 48);
    gdk_pixbuf_fill(pixels, 0xff0000ff);
    ActionsMenu *window = actions_menu_new(pixels, app);
    gtk_window_present(GTK_WINDOW(window));
    save_async(window, "/nonexistent/shotty-audit/output.png");
    gint64 deadline=g_get_monotonic_time()+3*G_TIME_SPAN_SECOND;
    while (window->priv->save_cancellable && g_get_monotonic_time()<deadline) { drain(); g_usleep(1000); }
    g_assert_null(window->priv->save_cancellable);
    g_assert_nonnull(gtk_window_get_application(GTK_WINDOW(window)));
    GtkWindow *dialog = luxe_window_get_visible_dialog(GTK_WINDOW(window));
    g_assert_nonnull(dialog);
    luxe_dialog_close(dialog);
    gchar *path = NULL;
    gint fd = g_file_open_tmp("shotty-audit-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_object_add_weak_pointer(G_OBJECT(window),(gpointer *)&window);
    save_async(window, path);
    deadline=g_get_monotonic_time()+3*G_TIME_SPAN_SECOND;
    while (window && g_get_monotonic_time()<deadline) { drain(); g_usleep(1000); }
    g_assert_null(window);
    GdkPixbuf *saved = gdk_pixbuf_new_from_file(path, NULL);
    g_assert_nonnull(saved);
    g_assert_cmpint(gdk_pixbuf_get_width(saved), ==, 64);
    g_object_unref(saved);
    /* Closing Shotty must not invalidate a path a delayed viewer has yet to open. */
    window = actions_menu_new(pixels, app);
    window->priv->handoff_file = path;
    window->priv->handoff_source = g_timeout_add_seconds(30, finish_open_handoff, window);
    gchar *check_path = g_strdup(path);
    gtk_window_destroy(GTK_WINDOW(window));
    g_assert_true(g_file_test(check_path, G_FILE_TEST_EXISTS));
    g_assert_cmpint(g_unlink(check_path), ==, 0);
    g_free(check_path);
    g_object_unref(pixels);
    drain();
}

static void geometry(void)
{
    g_autoptr(GError) error = NULL;
    g_assert_true(capture_size_allowed(7680, 4320, &error));
    g_assert_no_error(error);
    g_assert_false(capture_size_allowed(8192, 8192, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE);
    g_clear_error(&error);
    g_assert_false(capture_size_allowed(G_MAXINT, G_MAXINT, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE);
    g_clear_error(&error);
    g_assert_false(capture_size_allowed(0, 1, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_FAILED);
    g_clear_error(&error);
    inject_oversized_screen = TRUE;
    g_assert_null(screenshot(AREA_SCREEN, FALSE, FALSE, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE);
    g_clear_error(&error);
    inject_oversized_screen = FALSE;
    inject_slop = TRUE;
    fake_selection = (struct slop_selection){ .cancelled = 1, .x = NAN };
    g_assert_null(screenshot(AREA_REGION, FALSE, FALSE, &error));
    g_assert_no_error(error); /* Cancelled selections remain quiet. */
    fake_selection = (struct slop_selection){ .x = INFINITY, .w = 2, .h = 2 };
    g_assert_null(screenshot(AREA_REGION, FALSE, FALSE, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_FAILED);
    g_clear_error(&error);
    inject_slop = FALSE;
    GdkPixbuf *pixel_image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 1, 1);
    gdk_pixbuf_fill(pixel_image, 0xffffffff);
    unsigned long argb = 0x80800000;
    XFixesCursorImage translucent = { .width = 1, .height = 1, .pixels = &argb };
    composite_cursor(pixel_image, &translucent, (GdkRectangle){ 0, 0, 1, 1 });
    g_assert_cmpuint(gdk_pixbuf_get_pixels(pixel_image)[0], ==, 255);
    g_assert_cmpuint(gdk_pixbuf_get_pixels(pixel_image)[1], ==, 127);
    g_object_unref(pixel_image);
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    Display *display = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    G_GNUC_END_IGNORE_DEPRECATIONS
    Window root = DefaultRootWindow(display);
    Window frame = XCreateSimpleWindow(display, root, 100, 100, 220, 180, 0, 0, 0);
    Window client = XCreateSimpleWindow(display, frame, 10, 30, 200, 140, 0, 0, 0xffffff);
    XMapWindow(display, frame);
    XMapWindow(display, client);
    Atom active = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    XChangeProperty(display, root, active, XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&client, 1);
    unsigned long extents[] = { 10, 10, 30, 10 };
    Atom frame_extents = XInternAtom(display, "_NET_FRAME_EXTENTS", False);
    XChangeProperty(display, client, frame_extents, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)extents, 4);
    XSync(display, False);
    GdkPixbuf *image = screenshot(AREA_WINDOW, FALSE, FALSE, NULL);
    g_assert_nonnull(image);
    g_assert_cmpint(gdk_pixbuf_get_width(image), ==, 200);
    g_assert_cmpint(gdk_pixbuf_get_height(image), ==, 140);
    g_object_unref(image);
    const char cursor_bits[] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    Pixmap bitmap = XCreateBitmapFromData(display, root, cursor_bits, 8, 8);
    XColor black = { 0 }, white = { .red = 65535, .green = 65535, .blue = 65535 };
    Cursor cursor = XCreatePixmapCursor(display, bitmap, bitmap, &black, &white, 0, 0);
    XDefineCursor(display, client, cursor);
    XWarpPointer(display, None, client, 0, 0, 0, 0, 50, 40);
    XSync(display, False);
    image = screenshot(AREA_WINDOW, TRUE, FALSE, NULL);
    guchar *pixel = gdk_pixbuf_get_pixels(image) + 40 * gdk_pixbuf_get_rowstride(image)
                    + 50 * gdk_pixbuf_get_n_channels(image);
    g_assert_cmpuint(pixel[0], ==, 0);
    g_assert_cmpuint(pixel[1], ==, 0);
    g_assert_cmpuint(pixel[2], ==, 0);
    g_object_unref(image);
    XFreeCursor(display, cursor);
    XFreePixmap(display, bitmap);
    /* Without EWMH activation, resolve the client beneath a WM frame. */
    Atom state = XInternAtom(display, "WM_STATE", False);
    unsigned long normal[] = { 1, None };
    XChangeProperty(display, client, state, state, 32, PropModeReplace,
                    (unsigned char *)normal, 2);
    XDeleteProperty(display, root, active);
    image = screenshot(AREA_WINDOW, FALSE, FALSE, NULL);
    g_assert_cmpint(gdk_pixbuf_get_width(image), ==, 200);
    g_object_unref(image);
    XChangeProperty(display, root, active, XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&client, 1);
    image = screenshot(AREA_WINDOW, FALSE, TRUE, NULL);
    g_assert_cmpint(gdk_pixbuf_get_width(image), ==, 220);
    g_assert_cmpint(gdk_pixbuf_get_height(image), ==, 180);
    g_object_unref(image);
    unsigned long hostile[] = { G_MAXUINT, G_MAXUINT, G_MAXUINT, G_MAXUINT };
    XChangeProperty(display, client, frame_extents, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)hostile, 4);
    image = screenshot(AREA_WINDOW, FALSE, TRUE, NULL);
    g_assert_cmpint(gdk_pixbuf_get_width(image), ==, 200);
    g_object_unref(image);
    g_assert_null(screenshot(999, FALSE, FALSE, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error(&error);
    XDestroyWindow(display, frame);
    g_assert_null(screenshot(AREA_WINDOW, FALSE, FALSE, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_FAILED);
    g_clear_error(&error);
    XDeleteProperty(display, root, active);
    GdkRectangle area = { G_MAXINT, G_MAXINT, G_MAXINT, G_MAXINT };
    clamp_to_screen(&area, (GdkRectangle){ 0, 0, 1024, 768 });
    g_assert_cmpint(area.width, ==, 0);
    g_assert_cmpint(area.height, ==, 0);
}

static gboolean pasted;
static void clipboard_read(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GdkTexture) texture = gdk_clipboard_read_texture_finish(GDK_CLIPBOARD(source), result, &error);
    g_assert_no_error(error);
    g_assert_nonnull(texture);
    g_assert_cmpint(gdk_texture_get_width(texture), ==, 64);
    g_assert_cmpint(gdk_texture_get_height(texture), ==, 48);
    pasted = TRUE;
}

static void clipboard_copy(void)
{
    /* A private-X manager advertises persistence, but never answers it. */
    Display *manager = XOpenDisplay(NULL);
    g_assert_nonnull(manager);
    Window owner = XCreateSimpleWindow(manager, DefaultRootWindow(manager), 0, 0, 1, 1, 0, 0, 0);
    XSetSelectionOwner(manager, XInternAtom(manager, "CLIPBOARD_MANAGER", False), owner, CurrentTime);
    XSync(manager, False);
    GdkPixbuf *pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 64, 48);
    gdk_pixbuf_fill(pixels, 0x2080ffff);
    ActionsMenu *window = actions_menu_new(pixels, app);
    g_object_unref(pixels);
    gtk_window_present(GTK_WINDOW(window));
    drain();
    action_copy(window);
    GdkContentFormats *formats = gdk_clipboard_get_formats(gtk_widget_get_clipboard(GTK_WIDGET(window)));
    g_assert_true(gdk_content_formats_contain_mime_type(formats, "image/png"));
    gsize format_count = 0;
    gdk_content_formats_get_mime_types(formats, &format_count);
    g_assert_cmpuint(format_count, ==, 1);
    /* Copy must never disable the client-side close/back controls. */
    g_assert_true(gtk_widget_get_sensitive(GTK_WIDGET(window)));
    g_assert_false(gtk_widget_get_sensitive(window->priv->ok_button));
    CopyRequest *first = window->priv->copy_request;
    action_copy(window);
    g_assert_true(first == window->priv->copy_request);
    g_object_add_weak_pointer(G_OBJECT(window), (gpointer *)&window);
    gtk_window_close(GTK_WINDOW(window));
    drain();
    g_assert_null(window);
    /* A second copy times out, remains pasteable, and can still be closed. */
    pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 64, 48);
    gdk_pixbuf_fill(pixels, 0x2080ffff);
    window = actions_menu_new(pixels, app);
    g_object_unref(pixels);
    gtk_window_present(GTK_WINDOW(window));
    action_copy(window);
    g_autoptr(GCancellable) cancel = g_object_ref(window->priv->copy_request->cancellable);
    gint64 deadline = g_get_monotonic_time() + 3 * G_TIME_SPAN_SECOND;
    while (window->priv->copy_request && g_get_monotonic_time() < deadline) {
        drain(); g_usleep(1000);
    }
    g_assert_null(window->priv->copy_request);
    g_assert_true(g_cancellable_is_cancelled(cancel));
    g_assert_true(gtk_widget_get_sensitive(GTK_WIDGET(window)));
    g_assert_true(gtk_widget_get_sensitive(window->priv->ok_button));
    GtkWindow *notice = luxe_window_get_visible_dialog(GTK_WINDOW(window));
    g_assert_nonnull(notice);
    pasted = FALSE;
    gdk_clipboard_read_texture_async(gtk_widget_get_clipboard(GTK_WIDGET(window)), NULL, clipboard_read, NULL);
    deadline = g_get_monotonic_time() + 3 * G_TIME_SPAN_SECOND;
    while (!pasted && g_get_monotonic_time() < deadline) { drain(); g_usleep(1000); }
    g_assert_true(pasted);
    luxe_dialog_close(notice);
    g_object_add_weak_pointer(G_OBJECT(window), (gpointer *)&window);
    gtk_window_close(GTK_WINDOW(window));
    drain();
    g_assert_null(window);
    XDestroyWindow(manager, owner);
    XCloseDisplay(manager);
    drain();
}

static void clipboard_completion(void)
{
    simulated_store = TRUE;
    g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 64, 48);
    gdk_pixbuf_fill(pixels, 0x2080ffff);
    for (guint pass = 0; pass < 3; pass++) {
        ActionsMenu *window = actions_menu_new(pixels, app);
        g_object_add_weak_pointer(G_OBJECT(window), (gpointer *)&window);
        gtk_window_present(GTK_WINDOW(window));
        action_copy(window);
        guint timeout = window->priv->copy_request->timeout;
        if (pass == 1) {
            gint64 deadline = g_get_monotonic_time() + 3 * G_TIME_SPAN_SECOND;
            while (window->priv->copy_request && g_get_monotonic_time() < deadline) {
                drain(); g_usleep(1000);
            }
            g_assert_null(window->priv->copy_request);
        } else if (pass == 2) {
            gtk_window_close(GTK_WINDOW(window));
            drain();
            g_assert_null(window);
        }
        g_task_return_boolean(pending_store, TRUE);
        g_clear_object(&pending_store);
        drain();
        if (pass == 0 || pass == 2) g_assert_null(window);
        else g_assert_nonnull(window);
        g_assert_null(g_main_context_find_source_by_id(NULL, timeout));
        if (window) gtk_window_destroy(GTK_WINDOW(window));
        drain();
    }
    simulated_store = FALSE;
}

static void region(void)
{
    gchar *xdotool = g_find_program_in_path("xdotool");
    if (!xdotool) {
        g_test_skip("xdotool required for real slop region selection");
        return;
    }
    g_free(xdotool);
    g_assert_true(g_spawn_command_line_sync("xdotool mousemove 200 150", NULL, NULL, NULL, NULL));
    gchar *argv[] = { "sh", "-c",
        "sleep 1; xdotool mousedown 1; sleep 0.3; "
        "xdotool mousemove 520 430; sleep 0.3; xdotool mouseup 1", NULL };
    GPid child;
    g_assert_true(g_spawn_async(NULL, argv, NULL,
        G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &child, NULL));
    GdkPixbuf *image = screenshot(AREA_REGION, FALSE, FALSE, NULL);
    int status;
    waitpid(child, &status, 0);
    g_spawn_close_pid(child);
    g_assert_true(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    g_assert_nonnull(image);
    g_assert_cmpint(gdk_pixbuf_get_width(image), >=, 300);
    g_assert_cmpint(gdk_pixbuf_get_height(image), >=, 260);
    g_object_unref(image);
}

static void handoff_fd(void)
{
    g_autoptr(GdkPixbuf) image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 3, 2);
    gdk_pixbuf_fill(image, 0x123456ff);
    g_autofree char *dir = g_dir_make_tmp("shotty-fd-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(dir, "image", NULL);
    g_autofree char *renamed = g_build_filename(dir, "original", NULL);
    int fd = g_open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(g_rename(path, renamed), ==, 0);
    g_assert_true(g_file_set_contents(path, "sentinel", -1, NULL));
    g_autoptr(GError) error = NULL;
    g_assert_true(save_pixbuf_to_fd(image, fd, &error));
    g_assert_cmpint(close(fd), ==, 0);
    g_autofree char *contents = NULL;
    g_assert_true(g_file_get_contents(path, &contents, NULL, NULL));
    g_assert_cmpstr(contents, ==, "sentinel");
    g_autoptr(GdkPixbuf) loaded = gdk_pixbuf_new_from_file(renamed, &error);
    g_assert_no_error(error);
    g_assert_cmpint(gdk_pixbuf_get_width(loaded), ==, 3);
    g_assert_cmpint(gdk_pixbuf_get_height(loaded), ==, 2);
    fd = g_open("/dev/full", O_WRONLY, 0);
    g_assert_cmpint(fd, >=, 0);
    g_assert_false(save_pixbuf_to_fd(image, fd, &error));
    g_assert_nonnull(error);
    close(fd);
    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(g_unlink(renamed), ==, 0);
    g_assert_cmpint(g_rmdir(dir), ==, 0);
}

static void atomic_save(void)
{
    g_autoptr(GdkPixbuf) image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 3, 2);
    gdk_pixbuf_fill(image, 0x123456ff);
    g_autofree char *dir = g_dir_make_tmp("shotty-save-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(dir, "image.png", NULL);
    g_autofree char *link = g_build_filename(dir, "link.png", NULL);
    g_assert_true(g_file_set_contents(path, "original", -1, NULL));
    g_assert_cmpint(g_chmod(path, 0640), ==, 0);
    g_autoptr(GError) error = NULL;
    fail_png_write = TRUE;
    g_assert_false(save_pixbuf_atomically(image, path, NULL, &error));
    fail_png_write = FALSE;
    g_assert_nonnull(error); g_clear_error(&error);
    g_autofree char *contents = NULL;
    g_assert_true(g_file_get_contents(path, &contents, NULL, NULL));
    g_assert_cmpstr(contents, ==, "original");
    fail_directory_fsync = TRUE;
    g_assert_false(save_pixbuf_atomically(image, path, NULL, &error));
    fail_directory_fsync = FALSE;
    g_assert_nonnull(error); g_clear_error(&error);
    g_clear_pointer(&contents, g_free);
    g_assert_true(g_file_get_contents(path, &contents, NULL, NULL));
    g_assert_cmpstr(contents, ==, "original");
    change_destination = path;
    g_assert_false(save_pixbuf_atomically(image, path, NULL, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_BUSY); g_clear_error(&error);
    g_clear_pointer(&contents, g_free);
    g_assert_true(g_file_get_contents(path, &contents, NULL, NULL));
    g_assert_cmpstr(contents, ==, "other writer");
    g_assert_cmpint(symlink(path, link), ==, 0);
    g_assert_false(save_pixbuf_atomically(image, link, NULL, &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_ACCES); g_clear_error(&error);
    g_assert_cmpint(g_chmod(path, 0640), ==, 0);
    g_assert_true(save_pixbuf_atomically(image, path, NULL, &error));
    g_assert_no_error(error);
    struct stat st; g_assert_cmpint(g_stat(path, &st), ==, 0);
    g_assert_cmpint(st.st_mode & 0777, ==, 0640);
    g_autoptr(GdkPixbuf) loaded = gdk_pixbuf_new_from_file(path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(gdk_pixbuf_get_width(loaded), ==, 3);
    g_assert_cmpint(gdk_pixbuf_get_height(loaded), ==, 2);
    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(g_unlink(link), ==, 0);
    g_assert_cmpint(g_rmdir(dir), ==, 0); /* No staging files remain. */
}

static void handoff_reactivate(void)
{
    g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 8, 8);
    ActionsMenu *handoff = actions_menu_new(pixels, app);
    gchar *path = NULL;
    int fd = g_file_open_tmp("shotty-handoff-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0); close(fd);
    handoff->priv->handoff_file = path;
    handoff->priv->handoff_source = g_timeout_add_seconds(30, finish_open_handoff, handoff);
    g_assert_false(gtk_widget_get_visible(GTK_WIDGET(handoff)));
    g_application_activate(G_APPLICATION(app));
    GList *windows = gtk_application_get_windows(GTK_APPLICATION(app));
    g_assert_cmpuint(g_list_length(windows), ==, 2);
    ScreenshotMenu *capture = SCREENSHOT_MENU(windows->data);
    g_assert_true(SCREENSHOT_IS_MENU(capture));
    g_assert_true(gtk_widget_get_visible(GTK_WIDGET(capture)));
    g_application_activate(G_APPLICATION(app));
    g_assert_cmpuint(g_list_length(gtk_application_get_windows(GTK_APPLICATION(app))), ==, 2);
    /* Finishing another screenshot must not cancel this handoff or capture. */
    simulated_store = TRUE;
    ActionsMenu *other = actions_menu_new(pixels, app);
    gtk_window_present(GTK_WINDOW(other));
    action_copy(other);
    g_task_return_boolean(pending_store, TRUE);
    g_clear_object(&pending_store);
    drain();
    simulated_store = FALSE;
    g_assert_cmpuint(g_list_length(gtk_application_get_windows(GTK_APPLICATION(app))), ==, 2);
    g_assert_true(g_file_test(path, G_FILE_TEST_EXISTS));
    g_assert_true(gtk_widget_get_visible(GTK_WIDGET(capture)));
    gchar *check_path = g_strdup(path);
    g_source_remove(handoff->priv->handoff_source);
    finish_open_handoff(handoff);
    g_assert_false(g_file_test(check_path, G_FILE_TEST_EXISTS));
    g_free(check_path);
    g_assert_cmpuint(g_list_length(gtk_application_get_windows(GTK_APPLICATION(app))), ==, 1);
    g_assert_true(gtk_widget_get_visible(GTK_WIDGET(capture)));
    gtk_window_destroy(GTK_WINDOW(capture));
    drain();
}

static gboolean heartbeat_tick(gpointer data)
{
    ++*(guint *)data; return G_SOURCE_CONTINUE;
}
static void async_save_cancel(void)
{
    g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 64, 48);
    gdk_pixbuf_fill(pixels, 0x123456ff);
    g_autofree char *dir = g_dir_make_tmp("shotty-async-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(dir, "image.png", NULL);
    g_assert_true(g_file_set_contents(path, "original", -1, NULL));
    ActionsMenu *window = actions_menu_new(pixels, app);
    gtk_window_present(GTK_WINDOW(window));
    g_mutex_lock(&write_lock);
    block_write = TRUE; write_entered = FALSE; release_write = FALSE;
    g_mutex_unlock(&write_lock);
    save_async(window, path);
    gint64 deadline = g_get_monotonic_time()+3*G_TIME_SPAN_SECOND;
    gboolean entered = FALSE;
    while (!entered && g_get_monotonic_time()<deadline) {
        drain(); g_usleep(1000);
        g_mutex_lock(&write_lock); entered = write_entered; g_mutex_unlock(&write_lock);
    }
    g_assert_true(entered);
    guint ticks=0; guint timer=g_timeout_add(10,heartbeat_tick,&ticks);
    gint64 until=g_get_monotonic_time()+150000;
    while (g_get_monotonic_time()<until) { drain(); g_usleep(1000); }
    g_source_remove(timer); g_assert_cmpuint(ticks,>,3);
    g_object_add_weak_pointer(G_OBJECT(window),(gpointer *)&window);
    gtk_window_destroy(GTK_WINDOW(window)); drain(); g_assert_null(window);
    g_mutex_lock(&write_lock); release_write=TRUE; g_cond_broadcast(&write_cond); g_mutex_unlock(&write_lock);
    gboolean cleaned=FALSE;
    deadline=g_get_monotonic_time()+3*G_TIME_SPAN_SECOND;
    while (!cleaned && g_get_monotonic_time()<deadline) {
        drain(); g_usleep(1000);
        g_autoptr(GDir) listing=g_dir_open(dir,0,NULL);
        guint count=0; while(g_dir_read_name(listing)) count++;
        cleaned=(count==1);
    }
    g_assert_true(cleaned);
    g_mutex_lock(&write_lock); block_write=FALSE; g_mutex_unlock(&write_lock);
    g_autofree char *content=NULL;
    g_assert_true(g_file_get_contents(path,&content,NULL,NULL));
    g_assert_cmpstr(content,==,"original");
    g_assert_cmpint(g_unlink(path),==,0); g_assert_cmpint(g_rmdir(dir),==,0);
    drain();
}

static void handoff_cache_permissions(void)
{
    g_autofree char *base = g_dir_make_tmp("shotty-cache-XXXXXX", NULL);
    g_autoptr(GError) error = NULL;
    g_autofree char *dir = handoff_cache_dir(base, &error);
    g_assert_no_error(error);
    g_assert_nonnull(dir);
    struct stat st;
    g_assert_cmpint(g_lstat(dir, &st), ==, 0);
    g_assert_cmpint(st.st_mode & 077, ==, 0);
    g_assert_cmpint(g_chmod(dir, 0755), ==, 0);
    g_assert_null(handoff_cache_dir(base, &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_ACCES);
    g_clear_error(&error);
    g_assert_cmpint(g_rmdir(dir), ==, 0);
    g_assert_cmpint(symlink(base, dir), ==, 0);
    g_assert_null(handoff_cache_dir(base, &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_ACCES);
    g_clear_error(&error);
    g_assert_cmpint(g_unlink(dir), ==, 0);
    g_assert_true(g_file_set_contents(dir, "not a directory", -1, NULL));
    g_assert_null(handoff_cache_dir(base, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_assert_cmpint(g_unlink(dir), ==, 0);
    g_assert_cmpint(g_rmdir(base), ==, 0);
}

static void handoff_cache_expiry(void)
{
	 g_autofree char *base = g_dir_make_tmp("shotty-cache-expiry-XXXXXX", NULL);
	 g_autoptr(GError) error = NULL;
	 g_autofree char *dir = handoff_cache_dir(base, &error);
	 g_assert_no_error(error);
	 g_autofree char *stale = g_build_filename(dir, "shotty-ABC123.png", NULL);
	 g_autofree char *recent = g_build_filename(dir, "shotty-DEF456.png", NULL);
	 g_autofree char *link = g_build_filename(dir, "shotty-GHI789.png", NULL);
	 g_assert_true(g_file_set_contents(stale, "stale", -1, NULL));
	 g_assert_true(g_file_set_contents(recent, "recent", -1, NULL));
	 struct timespec old_times[2] = {{ .tv_sec = time(NULL) - SHOTTY_HANDOFF_RETENTION_SECONDS - 2 },
	                                { .tv_sec = time(NULL) - SHOTTY_HANDOFF_RETENTION_SECONDS - 2 }};
	 g_assert_cmpint(utimensat(AT_FDCWD, stale, old_times, 0), ==, 0);
	 g_assert_cmpint(symlink("/etc/passwd", link), ==, 0);
	 g_assert_true(handoff_cache_prepare(dir, 0, &error));
	 g_assert_no_error(error);
	 g_assert_false(g_file_test(stale, G_FILE_TEST_EXISTS));
	 g_assert_true(g_file_test(recent, G_FILE_TEST_EXISTS));
	 g_assert_true(g_file_test(link, G_FILE_TEST_IS_SYMLINK));
	 g_assert_cmpint(truncate(recent, SHOTTY_HANDOFF_MAX_BYTES), ==, 0);
	 g_assert_false(handoff_cache_prepare(dir, 1, &error));
	 g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE);
	 g_clear_error(&error);
	 g_assert_cmpint(g_unlink(recent), ==, 0);
	 g_assert_cmpint(g_unlink(link), ==, 0);
	 g_assert_cmpint(g_rmdir(dir), ==, 0);
	 g_assert_cmpint(g_rmdir(base), ==, 0);
}

static void action_settings(void)
{
    g_autoptr(GSettings) settings = g_settings_new("org.luxe.Shotty");
    const char *keys[] = {"action", "action-save", "action-copy", "action-open"};
    for (guint i = 0; i < G_N_ELEMENTS(keys); i++) g_settings_reset(settings, keys[i]);
    g_settings_set_boolean(settings, "action-save", TRUE);
    g_settings_set_boolean(settings, "action-copy", TRUE);
    g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 4, 4);
    ActionsMenu *one = actions_menu_new(pixels, app);
    g_autofree char *choice = g_settings_get_string(settings, "action");
    g_assert_cmpstr(choice, ==, "copy");
    g_assert_true(gtk_check_button_get_active(GTK_CHECK_BUTTON(one->priv->action_copy)));
    ActionsMenu *two = actions_menu_new(pixels, app);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(one->priv->action_open), TRUE);
    drain();
    g_clear_pointer(&choice, g_free);
    choice = g_settings_get_string(settings, "action");
    g_assert_cmpstr(choice, ==, "open");
    g_assert_true(gtk_check_button_get_active(GTK_CHECK_BUTTON(two->priv->action_open)));
    gtk_window_destroy(GTK_WINDOW(one));
    gtk_window_destroy(GTK_WINDOW(two));
    drain();
    /* Once migrated, stale legacy values must never override the new key. */
    one = actions_menu_new(pixels, app);
    g_assert_true(gtk_check_button_get_active(GTK_CHECK_BUTTON(one->priv->action_open)));
    gtk_window_destroy(GTK_WINDOW(one));
    drain();
    for (guint i = 0; i < G_N_ELEMENTS(keys); i++) g_settings_reset(settings, keys[i]);
    one = actions_menu_new(pixels, app);
    g_assert_true(gtk_check_button_get_active(GTK_CHECK_BUTTON(one->priv->action_save)));
    gtk_window_destroy(GTK_WINDOW(one));
    drain();
    for (guint i = 0; i < G_N_ELEMENTS(keys); i++) g_settings_reset(settings, keys[i]);
}

/* Only call this for a private test-owned temporary directory. Libraries may
 * create their own caches alongside Shotty's. Never follow symlinks. */
static void remove_test_tree(GFile *directory)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GFileEnumerator) entries = g_file_enumerate_children(directory,
        G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, &error);
    g_assert_no_error(error);
    g_assert_nonnull(entries);
    GFileInfo *info;
    while ((info = g_file_enumerator_next_file(entries, NULL, &error)) != NULL) {
        g_autoptr(GFile) child = g_file_get_child(directory, g_file_info_get_name(info));
        GFileType type = g_file_info_get_file_type(info);
        g_object_unref(info);
        if (type == G_FILE_TYPE_DIRECTORY) remove_test_tree(child);
        else {
            g_assert_true(g_file_delete(child, NULL, &error));
            g_assert_no_error(error);
        }
    }
    g_assert_no_error(error);
    g_assert_true(g_file_enumerator_close(entries, NULL, &error));
    g_assert_no_error(error);
    g_assert_true(g_file_delete(directory, NULL, &error));
    g_assert_no_error(error);
}

static void test_cache_cleanup(void)
{
    g_autofree char *root = g_dir_make_tmp("shotty-cleanup-XXXXXX", NULL);
    g_assert_nonnull(root);
    g_autofree char *nested = g_build_filename(root, "driver-cache", "nested", NULL);
    g_assert_cmpint(g_mkdir_with_parents(nested, 0700), ==, 0);
    g_autofree char *entry = g_build_filename(nested, "cache.bin", NULL);
    g_assert_true(g_file_set_contents(entry, "cache", -1, NULL));
    g_autofree char *outside = g_dir_make_tmp("shotty-cleanup-outside-XXXXXX", NULL);
    g_assert_nonnull(outside);
    g_autofree char *sentinel = g_build_filename(outside, "sentinel", NULL);
    g_assert_true(g_file_set_contents(sentinel, "keep", -1, NULL));
    g_autofree char *link = g_build_filename(root, "outside-link", NULL);
    g_assert_cmpint(symlink(outside, link), ==, 0);
    g_autoptr(GFile) directory = g_file_new_for_path(root);
    remove_test_tree(directory);
    g_assert_false(g_file_test(root, G_FILE_TEST_EXISTS));
    g_assert_true(g_file_test(sentinel, G_FILE_TEST_EXISTS));
    g_assert_cmpint(g_unlink(sentinel), ==, 0);
    g_assert_cmpint(g_rmdir(outside), ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_autofree char *test_cache = g_dir_make_tmp("shotty-test-cache-XXXXXX", NULL);
    g_assert_nonnull(test_cache);
    g_setenv("XDG_CACHE_HOME", test_cache, TRUE);
    luxe_ui_init();
    app = screenshot_app_new();
    g_application_set_flags(G_APPLICATION(app), G_APPLICATION_NON_UNIQUE);
    g_assert_true(g_application_register(G_APPLICATION(app), NULL, NULL));
    g_test_add_func("/shotty/windows", windows);
    g_test_add_func("/shotty/saving", saving);
    g_test_add_func("/shotty/handoff-fd", handoff_fd);
    g_test_add_func("/shotty/atomic-save", atomic_save);
    g_test_add_func("/shotty/handoff-reactivate", handoff_reactivate);
    g_test_add_func("/shotty/async-save-cancel", async_save_cancel);
    g_test_add_func("/shotty/handoff-cache", handoff_cache_permissions);
    g_test_add_func("/shotty/handoff-cache-expiry", handoff_cache_expiry);
    g_test_add_func("/shotty/action-settings", action_settings);
    g_test_add_func("/shotty/cache-cleanup", test_cache_cleanup);
    g_test_add_func("/shotty/clipboard-copy", clipboard_copy);
    g_test_add_func("/shotty/clipboard-completion", clipboard_completion);
    g_test_add_func("/shotty/geometry", geometry);
    g_test_add_func("/shotty/region", region);
    int status = g_test_run();
    g_object_unref(app);
    g_autofree char *shotty_cache = g_build_filename(test_cache, "shotty", NULL);
    g_assert_cmpint(g_rmdir(shotty_cache), ==, 0);
    g_autoptr(GFile) cache_root = g_file_new_for_path(test_cache);
    remove_test_tree(cache_root);
    return status;
}
