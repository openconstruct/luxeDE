/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#define main notification_main
#include "../src/luxenotify.c"
#undef main
#include "../src/notification-window.c"

static GVariant *image_hints(gboolean alpha, gint channels, gint stride, gsize length)
{
    guint8 pixels[32] = { 0 };
    GVariantBuilder hints;
    g_variant_builder_init(&hints, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&hints, "{sv}", "image-data",
        g_variant_new("(iiibii@ay)", 2, 2, stride, alpha, 8, channels,
            g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, pixels, length, 1)));
    return g_variant_ref_sink(g_variant_builder_end(&hints));
}

static void images(void)
{
    struct { gboolean alpha; gint channels, stride; gsize length; gboolean valid; } cases[] = {
        { TRUE, 4, 8, 16, TRUE }, { FALSE, 3, 8, 14, TRUE },
        { TRUE, 3, 6, 12, FALSE }, { FALSE, 4, 8, 16, FALSE },
        { TRUE, 4, 8, 15, FALSE }, { TRUE, 4, 7, 16, FALSE },
        { TRUE, 4, G_MAXINT, 16, FALSE }
    };
    for (guint i = 0; i < G_N_ELEMENTS(cases); i++) {
        GVariant *hints = image_hints(cases[i].alpha, cases[i].channels,
                                    cases[i].stride, cases[i].length);
        GdkPaintable *image = paintable_from_image_data(hints);
        g_assert_cmpint(image != NULL, ==, cases[i].valid);
        g_clear_object(&image);
        g_variant_unref(hints);
    }
    guint8 tiny[32] = { 0 };
    GVariantBuilder oversized_dimensions;
    g_variant_builder_init(&oversized_dimensions, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&oversized_dimensions, "{sv}", "image-data",
        g_variant_new("(iiibii@ay)", 2049, 1, 2049 * 4, TRUE, 8, 4,
            g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, tiny, sizeof tiny, 1)));
    g_autoptr(GVariant) invalid_dimensions =
        g_variant_ref_sink(g_variant_builder_end(&oversized_dimensions));
    g_assert_null(paintable_from_image_data(invalid_dimensions));
    g_autofree guint8 *large_pixels = g_malloc0(256 * 128 * 4);
    GVariantBuilder large_hints;
    g_variant_builder_init(&large_hints, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&large_hints, "{sv}", "image-data",
        g_variant_new("(iiibii@ay)", 256, 128, 256 * 4, TRUE, 8, 4,
            g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, large_pixels, 256 * 128 * 4, 1)));
    g_autoptr(GVariant) large = g_variant_ref_sink(g_variant_builder_end(&large_hints));
    g_autoptr(GdkPaintable) reduced = paintable_from_image_data(large);
    g_assert_nonnull(reduced);
    g_assert_cmpint(gdk_paintable_get_intrinsic_width(reduced), ==, 40);
    g_assert_cmpint(gdk_paintable_get_intrinsic_height(reduced), ==, 20);
    /* The texture must remain valid after the D-Bus hint itself is released. */
    GVariant *small_hint = image_hints(TRUE, 4, 8, 16);
    g_autoptr(GdkPaintable) small = paintable_from_image_data(small_hint);
    g_variant_unref(small_hint);
    g_assert_true(GDK_IS_TEXTURE(small));
    guint8 downloaded[16];
    gdk_texture_download(GDK_TEXTURE(small), downloaded, 8);
    for (guint i = 0; i < sizeof downloaded; i++) g_assert_cmpuint(downloaded[i], ==, 0);
    gchar *path = NULL;
    gint fd = g_file_open_tmp("luxe-notify-image-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    GdkPixbuf *source = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 128, 128);
    gdk_pixbuf_fill(source, 0xff0000ff);
    g_assert_true(gdk_pixbuf_save(source, path, "png", NULL, NULL));
    g_object_unref(source);
    GtkWidget *picture = g_object_ref_sink(gtk_picture_new());
    g_autoptr(GCancellable) cancel = g_cancellable_new();
    g_assert_true(start_icon_load(GTK_PICTURE(picture), path, cancel));
    gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (!gtk_picture_get_paintable(GTK_PICTURE(picture)) &&
           g_get_monotonic_time() < deadline) {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(1000);
    }
    GdkPaintable *scaled = gtk_picture_get_paintable(GTK_PICTURE(picture));
    g_assert_nonnull(scaled);
    g_assert_cmpint(gdk_paintable_get_intrinsic_width(scaled), ==, 40);
    /* A dismissed card may release its picture before its I/O completes. */
    g_assert_true(start_icon_load(GTK_PICTURE(picture), path, cancel));
    g_cancellable_cancel(cancel);
    g_object_add_weak_pointer(G_OBJECT(picture), (gpointer *)&picture);
    g_object_unref(picture);
    while (picture && g_get_monotonic_time() < deadline) {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(1000);
    }
    g_assert_null(picture);
    g_assert_cmpuint(active_icon_loads, ==, 0);
    g_autoptr(GCancellable) burst_cancel = g_cancellable_new();
    GtkWidget *burst = g_object_ref_sink(gtk_picture_new());
    for (guint i = 0; i < MAX_ICON_LOADS; i++)
        g_assert_true(start_icon_load(GTK_PICTURE(burst), path, burst_cancel));
    g_assert_false(start_icon_load(GTK_PICTURE(burst), path, burst_cancel));
    g_assert_cmpuint(active_icon_loads, ==, MAX_ICON_LOADS);
    g_cancellable_cancel(burst_cancel);
    deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (active_icon_loads && g_get_monotonic_time() < deadline) {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(1000);
    }
    g_assert_cmpuint(active_icon_loads, ==, 0);
    g_object_unref(burst);

    /* Exercise actual bytes read, independent of stale metadata: a stream
     * that grows past the limit must stop before attempting image decode. */
    GtkWidget *over_picture = g_object_ref_sink(gtk_picture_new());
    IconLoad *over = g_new0(IconLoad, 1);
    over->picture = g_object_ref(GTK_PICTURE(over_picture));
    over->cancel = g_cancellable_new();
    over->bytes = g_byte_array_new();
    g_autoptr(GBytes) oversized = g_bytes_new_take(g_malloc0(MAX_ICON_BYTES + 1), MAX_ICON_BYTES + 1);
    over->stream = g_memory_input_stream_new_from_bytes(oversized);
    active_icon_loads++;
    icon_read_next(over);
    deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (active_icon_loads && g_get_monotonic_time() < deadline) {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(1000);
    }
    g_assert_cmpuint(active_icon_loads, ==, 0);
    g_assert_null(gtk_picture_get_paintable(GTK_PICTURE(over_picture)));
    g_object_unref(over_picture);
    unlink(path);
    g_free(path);
}

static GtkWidget *find_body(GtkWidget *widget)
{
    if (gtk_widget_has_css_class(widget, "body"))
        return widget;
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
         child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *found = find_body(child);
        if (found) return found;
    }
    return NULL;
}

static void lifecycle(void)
{
    LuxeNotifyServer server = { 0 };
    GError *error = NULL;
    server.connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    g_assert_no_error(error);
    GVariant *actions = g_variant_ref_sink(g_variant_new_strv(
        (const gchar *[]){ "default", "Open" }, 2));
    GVariant *hints = g_variant_ref_sink(g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0));
    const gchar *body = "<b>Bold</b> and <a href=\"https://example.com\">link</a>";
    LuxeNotification *notification = luxe_notification_window_new(&server, 1,
        "test", "missing-test-icon", "test", body, actions, hints, 0);
    server.notifications = g_list_append(NULL, notification);
    g_assert_true(notification->has_default_action);
    GtkWidget *label = find_body(GTK_WIDGET(notification->window));
    g_assert_nonnull(label);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(label)), ==, "Bold and link");
    luxe_notify_server_active_count_changed(&server);
    g_assert_cmpuint(server.published_count, ==, 1);
    luxe_notification_reposition_all(&server);
    guint idle = server.reposition_id;
    luxe_notification_reposition_all(&server);
    g_assert_cmpuint(server.reposition_id, ==, idle);
    while (server.reposition_id) g_main_context_iteration(NULL, TRUE);
    g_assert_nonnull(server.workarea_display);
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(notification->window));
    Display *display = native_x11_display(gdk_display_get_default());
    Window xid = native_x11_window(surface);
    XMoveWindow(display, xid, 20, 20);
    XSync(display, False);
    workarea_monitor_notified(NULL, NULL, &server);
    g_assert_false(server.workarea_valid);
    idle = server.reposition_id;
    g_assert_cmpuint(idle, !=, 0);
    workarea_monitor_notified(NULL, NULL, &server);
    g_assert_cmpuint(server.reposition_id, ==, idle);
    while (server.reposition_id) g_main_context_iteration(NULL, TRUE);
    XSync(display, False);
    XWindowAttributes attributes;
    g_assert_true(XGetWindowAttributes(display, xid, &attributes));
    g_assert_cmpint(attributes.x, !=, 20);
    g_assert_cmpuint(server.workarea_x11_source, !=, 0);
    XMoveWindow(display, xid, 20, 20);
    Atom workarea = XInternAtom(display, "_NET_WORKAREA", False);
    unsigned long area[] = { 0, 0, 800, 600 };
    XChangeProperty(display, DefaultRootWindow(display), workarea, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)area, 4);
    XSync(display, False);
    gint64 move_deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
    do {
        while (g_main_context_iteration(NULL, FALSE));
        XSync(display, False);
        g_assert_true(XGetWindowAttributes(display, xid, &attributes));
        if (attributes.x != 20) break;
        g_usleep(1000);
    } while (g_get_monotonic_time() < move_deadline);
    g_assert_cmpint(attributes.x, !=, 20);
    XMoveWindow(display, xid, 20, 20);
    Atom workspace = XInternAtom(display, "_NET_CURRENT_DESKTOP", False);
    unsigned long desktop = 0;
    XChangeProperty(display, DefaultRootWindow(display), workspace, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&desktop, 1);
    XSync(display, False);
    move_deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
    do {
        while (g_main_context_iteration(NULL, FALSE));
        XSync(display, False);
        g_assert_true(XGetWindowAttributes(display, xid, &attributes));
        if (attributes.x != 20) break;
        g_usleep(1000);
    } while (g_get_monotonic_time() < move_deadline);
    g_assert_cmpint(attributes.x, !=, 20);
    XDeleteProperty(display, DefaultRootWindow(display), workarea);
    XDeleteProperty(display, DefaultRootWindow(display), workspace);
    XFlush(display);
    notification->resident = TRUE;
    run_action(notification, "default");
    g_assert_cmpuint(g_list_length(server.notifications), ==, 1);
    notification->resident = FALSE;
    g_autoptr(GCancellable) icon_cancel = g_object_ref(notification->icon_cancel);
    run_action(notification, "default");
    g_assert_true(g_cancellable_is_cancelled(icon_cancel));
    g_assert_null(server.notifications);
    g_assert_cmpuint(server.published_count, ==, 0);
    notification = luxe_notification_window_new(&server, 2, "test", "", "test",
        "<b>unclosed", actions, hints, 1);
    server.notifications = g_list_append(NULL, notification);
    label = find_body(GTK_WIDGET(notification->window));
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(label)), ==, "<b>unclosed");
    while (server.notifications || server.reposition_id)
        g_main_context_iteration(NULL, TRUE);
    server.next_id = G_MAXUINT;
    g_assert_cmpuint(allocate_notification_id(&server), ==, 1);
    gchar *safe = sanitize_body_markup("<a href=\"file:///etc/passwd\">x</a><script>y</script>");
    g_assert_null(strstr(safe, "<a href"));
    g_assert_null(strstr(safe, "<script>"));
    g_free(safe);
    GString *large = g_string_new(NULL);
    for (int i = 0; i < 10000; i++) g_string_append(large, "é");
    g_autofree gchar *split = bounded_text(large->str, 3);
    g_assert_true(g_utf8_validate(split, -1, NULL));
    g_assert_cmpstr(split, ==, "é…");
    notification = luxe_notification_window_new(&server, 3, large->str, "", large->str,
                                                   large->str, actions, hints, 0);
    server.notifications = g_list_append(NULL, notification);
    label = find_body(GTK_WIDGET(notification->window));
    const char *displayed = gtk_label_get_text(GTK_LABEL(label));
    g_assert_true(g_utf8_validate(displayed, -1, NULL));
    g_assert_cmpuint(strlen(displayed), <=, MAX_BODY_BYTES + strlen("…"));
    g_assert_true(g_str_has_suffix(displayed, "…"));
    g_assert_cmpint(gtk_label_get_lines(GTK_LABEL(label)), ==, 4);
    g_assert_cmpuint(strlen(gtk_window_get_title(notification->window)), <=, MAX_SUMMARY_BYTES + strlen("…"));
    g_assert_cmpuint(strlen(notification->app_name), <=, MAX_APP_TEXT_BYTES + strlen("…"));
    luxe_notify_server_close(&server, notification, 3, FALSE);
    g_string_free(large, TRUE);
    g_variant_unref(actions);
    g_variant_unref(hints);
    if (server.reposition_id) { g_source_remove(server.reposition_id); server.reposition_id = 0; }
    g_autoptr(GObject) watched = server.workarea_monitors && g_list_model_get_n_items(server.workarea_monitors)
        ? g_list_model_get_item(server.workarea_monitors, 0) : NULL;
    luxe_notification_stop_monitoring(&server);
    g_assert_null(server.workarea_display);
    if (watched && g_object_class_find_property(G_OBJECT_GET_CLASS(watched), "geometry")) {
        server.workarea_valid = TRUE;
        g_object_notify(watched, "geometry");
        g_assert_true(server.workarea_valid);
    }
    g_object_unref(server.connection);
}

static void workarea(void)
{
    Display *display = native_x11_display(gdk_display_get_default());
    Window root = DefaultRootWindow(display);
    Atom area = XInternAtom(display, "_NET_WORKAREA", False);
    unsigned long values[] = { G_MAXINT, G_MAXINT, G_MAXUINT, G_MAXUINT };
    XChangeProperty(display, root, area, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)values, 4);
    gint x = 0, y = 0, width = 1024, height = 768;
    apply_desktop_workarea(display, &x, &y, &width, &height);
    g_assert_cmpint(width, ==, 1024);
    unsigned long valid[] = { 0, 30, 1024, 738 };
    XChangeProperty(display, root, area, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)valid, 4);
    apply_desktop_workarea(display, &x, &y, &width, &height);
    g_assert_cmpint(y, ==, 30);
    g_assert_cmpint(height, ==, 738);
    XDeleteProperty(display, root, area);
}

static void crowded_workarea(void)
{
    LuxeNotifyServer server = {0};
    g_autoptr(GError) error = NULL;
    server.connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    g_assert_no_error(error);
    g_autoptr(GVariant) actions = g_variant_ref_sink(g_variant_new_strv(NULL, 0));
    g_autoptr(GVariant) hints = g_variant_ref_sink(g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0));
    for (guint i = 1; i <= 10; i++) {
        LuxeNotification *n = luxe_notification_window_new(&server, i, "test", "", "Notice",
                                                             "First line\nSecond line", actions, hints, 0);
        server.notifications = g_list_append(server.notifications, n);
    }
    while (g_main_context_iteration(NULL, FALSE));
    GdkDisplay *gdk_display = gdk_display_get_default();
    workarea_ensure_subscriptions(&server, gdk_display);
    server.workarea_x=0; server.workarea_y=0; server.workarea_width=800; server.workarea_height=300;
    server.workarea_valid=TRUE;
    luxe_notification_reposition_all(&server);
    while (server.reposition_id) g_main_context_iteration(NULL, TRUE);
    Display *display = native_x11_display(gdk_display);
    XSync(display, False);
    g_assert_cmpuint(g_list_length(server.notifications), >, 0);
    g_assert_cmpuint(g_list_length(server.notifications), <, 10);
    g_assert_cmpuint(((LuxeNotification *)g_list_last(server.notifications)->data)->id, ==, 10);
    for (GList *item = server.notifications; item; item = item->next) {
        LuxeNotification *n = item->data;
        GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(n->window));
        XWindowAttributes a;
        g_assert_true(XGetWindowAttributes(display, native_x11_window(surface), &a));
        g_assert_cmpint(a.x, >=, 0);
        g_assert_cmpint(a.x + a.width, <=, 800);
        g_assert_cmpint(a.y, >=, 0);
        g_assert_cmpint(a.y + a.height, <=, 300);
    }
    while (server.notifications) luxe_notify_server_close(&server, server.notifications->data, 3, FALSE);
    if (server.reposition_id) g_source_remove(server.reposition_id);
    luxe_notification_stop_monitoring(&server);
    g_object_unref(server.connection);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    gtk_init();
    g_test_add_func("/notify/images", images);
    g_test_add_func("/notify/lifecycle", lifecycle);
    g_test_add_func("/notify/workarea", workarea);
    g_test_add_func("/notify/crowded-workarea", crowded_workarea);
    return g_test_run();
}
